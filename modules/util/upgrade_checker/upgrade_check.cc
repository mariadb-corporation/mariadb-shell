/*
 * Copyright (c) 2017, 2026, Oracle and/or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is designed to work with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms,
 * as designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have either included with
 * the program or referenced in the documentation.
 *
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "modules/util/upgrade_checker/upgrade_check.h"

#include "modules/util/upgrade_checker/upgrade_check_condition.h"
#include "modules/util/upgrade_checker/upgrade_check_creators.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "modules/util/upgrade_checker/upgrade_check_condition.h"
#include "modules/util/upgrade_checker/upgrade_check_creators.h"

#include "mysqlshdk/libs/mysql/instance.h"
#include "mysqlshdk/libs/utils/utils_general.h"

namespace mysqlsh {
namespace upgrade_checker {

void Worker_pool::init() {
  m_thread_pool.start_threads();
  m_async_process_state = &m_thread_pool.process_async();
}

void Worker_pool::execute(
    Upgrade_check *check,
    std::function<std::vector<Upgrade_issue>(
        const std::shared_ptr<mysqlshdk::db::ISession> &session)> &&task,
    std::shared_ptr<mysqlshdk::db::ISession> use_session) {
  m_thread_pool.add_task(
      [check, task = std::move(task), use_session = std::move(use_session),
       this]() -> std::vector<Upgrade_issue> {
        auto session = use_session ? use_session : m_session_pool->acquire();

        on_work_start(session, check);
        try {
          auto result = task(session);

          on_work_end(session, check);
          if (!use_session) {
            m_session_pool->release(std::move(session));
          }

          return result;
        } catch (const mysqlshdk::db::Error &err) {
          on_work_end(session, check);

          if (mysqlshdk::db::is_mysql_client_error(err.code())) {
            session->close();
          }
          if (!use_session) {
            m_session_pool->release(std::move(session));
          }
          throw;
        } catch (...) {
          on_work_end(session, check);

          if (!use_session) {
            m_session_pool->release(std::move(session));
          }
          throw;
        }
      },
      [this](std::vector<Upgrade_issue> &&issues) {
        m_issues.insert(m_issues.end(), std::make_move_iterator(issues.begin()),
                        std::make_move_iterator(issues.end()));
      });
}

std::vector<Upgrade_issue> Worker_pool::wait() {
  using State =
      shcore::Thread_pool_base<std::vector<Upgrade_issue>>::Async_state;

  m_thread_pool.tasks_done();

  for (;;) {
    auto state = m_async_process_state->load();

    if (state == State::DONE || state == State::TERMINATED ||
        (m_interrupt_flag && m_interrupt_flag->test())) {
      break;
    }
    check_timeouts();
    if (state == State::IDLE) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  m_thread_pool.wait_for_process();

  return std::move(m_issues);
}

void Worker_pool::on_work_start(
    const std::shared_ptr<mysqlshdk::db::ISession> &session,
    Upgrade_check *check) {
  std::lock_guard<std::mutex> lock(m_active_checks_mutex);

  m_active_checks.emplace_back(Active_check{check, session->get_connection_id(),
                                            std::chrono::steady_clock::now()});
}

void Worker_pool::on_work_end(
    const std::shared_ptr<mysqlshdk::db::ISession> &session, Upgrade_check *) {
  std::lock_guard<std::mutex> lock(m_active_checks_mutex);

  auto it = std::find_if(
      m_active_checks.begin(), m_active_checks.end(),
      [id = session->get_connection_id()](const Active_check &active_check) {
        return active_check.connection_id == id;
      });
  if (it != m_active_checks.end()) {
    m_active_checks.erase(it);
  } else {
    throw std::logic_error("Attempt to end a check which was not started");
  }
}

void Worker_pool::check_timeouts() {
  if (m_timeout_seconds == 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(m_active_checks_mutex);

  auto now = std::chrono::steady_clock::now();

  for (const auto &check : m_active_checks) {
    auto elapsed_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(now - check.start_time)
            .count();

    if (elapsed_seconds > static_cast<int64_t>(m_timeout_seconds)) {
      m_interrupt_flag->test_and_set();
      kill_check(check);
    }
  }
}

void Worker_pool::kill_check(const Active_check &check) {
  if (check.check->is_timedout()) {
    return;
  }
  check.check->set_timedout(true);

  try {
    std::shared_ptr<mysqlshdk::db::ISession> kill_session;
    shcore::on_leave_scope release_session([this, &kill_session]() {
      if (kill_session) {
        m_session_pool->release(std::move(kill_session));
      }
    });
    kill_session = m_session_pool->acquire(true);
    if (!check.check->is_custom_session_required()) {
      kill_session->execute("KILL QUERY " +
                            std::to_string(check.connection_id));
    } else {
      kill_session->execute("KILL CONNECTION " +
                            std::to_string(check.connection_id));
    }
  } catch (const std::exception &ex) {
    current_console()->print_error(
        shcore::str_format("Error terminating check %.*s: %s",
                           static_cast<int>(check.check->get_name().length()),
                           check.check->get_name().data(), ex.what()));
    throw;
  }
}

Check_context::Check_context(
    const std::shared_ptr<mysqlshdk::db::ISession> &session,
    const Upgrade_info &server_info,
    std::shared_ptr<mysqlshdk::db::Session_pool> session_pool,
    Checker_cache *cache, uint64_t timeout_seconds,
    shcore::atomic_flag *interrupt_flag)
    : m_session(session),
      m_session_pool(session_pool),
      m_cache(cache),
      m_server_info(server_info),
      m_timeout_seconds(timeout_seconds),
      m_interrupt_flag(interrupt_flag) {}

std::unique_ptr<Worker_pool> Check_context::make_worker_pool(
    bool skip_pool) const {
  auto pool = std::make_unique<Worker_pool>(
      m_session_pool, skip_pool ? 1 : m_session_pool->max_size(),
      m_timeout_seconds, m_interrupt_flag);
  pool->init();
  return pool;
}

std::vector<Upgrade_issue> Check_context::execute_simple(
    Upgrade_check *check,
    std::function<std::vector<Upgrade_issue>(
        const std::shared_ptr<mysqlshdk::db::ISession> &)> &&task) const {
  auto wpool = make_worker_pool(true);
  wpool->execute(check, std::move(task), m_session);
  return wpool->wait();
}

const std::string &Upgrade_check::get_text(const char *field) const {
  std::string tag{m_name};
  return get_translation(tag.append(".").append(field).c_str());
}

const std::string &Upgrade_check::get_title() const {
  return get_text("title");
}

std::string Upgrade_check::get_description(
    const std::string &group, const Token_definitions &tokens) const {
  std::string description;

  std::string tag{"description"};
  if (!group.empty()) {
    tag.append(".").append(group);
  }

  try {
    return resolve_tokens(get_text(tag.c_str()), tokens);
  } catch (const std::logic_error &) {
    Token_definitions all_tokens = base_tokens();
    Token_definitions tokens_copy = tokens;
    all_tokens.merge(std::move(tokens_copy));
    return resolve_tokens(get_text(tag.c_str()), all_tokens);
  }
}

std::vector<std::string> Upgrade_check::get_solutions(
    const std::string &group) const {
  std::vector<std::string> solutions;

  std::string tag{"solution"};
  if (!group.empty()) {
    tag += "." + group;
  }

  auto solution = get_text(tag.c_str());
  int index = 0;
  while (!solution.empty()) {
    solutions.push_back(std::move(solution));
    auto indexed_tag{tag};
    indexed_tag += std::to_string(++index);
    solution = get_text(indexed_tag.c_str());
  }

  return solutions;
}

const std::string &Upgrade_check::get_doc_link(const std::string &group) const {
  if (!group.empty()) {
    std::string tag{"docLink."};
    tag += group;
    return get_text(tag.c_str());
  }
  return get_text("docLink");
}

Upgrade_issue Upgrade_check::create_issue() const {
  Upgrade_issue issue;
  issue.check_name = get_name();
  return issue;
}

const std::string &Upgrade_check::to_string(Category category) const {
  static const std::string k_accounts = "accounts";
  static const std::string k_config = "config";
  static const std::string k_parsing = "parsing";
  static const std::string k_schema = "schema";
  switch (category) {
    case Category::ACCOUNTS:
      return k_accounts;
    case Category::CONFIG:
      return k_config;
    case Category::PARSING:
      return k_parsing;
    case Category::SCHEMA:
      return k_schema;
  }

  throw std::logic_error("Unknown category");
}

Invalid_privileges_check::Invalid_privileges_check(
    const Upgrade_info &server_info)
    : Upgrade_check(ids::k_invalid_privileges_check, Category::ACCOUNTS),
      m_upgrade_info(server_info) {
  set_groups({k_dynamic_group});
  if (m_upgrade_info.server_version > Version(8, 0, 0)) {
    add_privileges(Version(8, 4, 0), {"SET_USER_ID"});
  }

  add_privileges(Version(9, 0, 0), {"SUPER"});
}

void Invalid_privileges_check::add_privileges(
    Version version, const std::set<std::string> &privileges) {
  if (Version_condition(version).evaluate(m_upgrade_info)) {
    m_privileges[version] = privileges;
  }
}

bool Invalid_privileges_check::has_privilege(const std::string &privilege) {
  for (const auto &it : m_privileges) {
    if (it.second.find(privilege) != it.second.end()) {
      return true;
    }
  }

  return false;
}

bool Invalid_privileges_check::enabled() const { return !m_privileges.empty(); }

std::vector<Upgrade_issue> Invalid_privileges_check::run(
    const Check_context &context) {
  return context.execute_simple(
      this, [this, &context](
                const std::shared_ptr<mysqlshdk::db::ISession> &session) {
        session->execute("set @@session.sql_mode='TRADITIONAL'");
        shcore::on_leave_scope restore_sql_mode([&session]() {
          session->execute("set @@session.sql_mode=DEFAULT");
        });

        auto result = session->query("select user, host from mysql.user");
        std::vector<std::pair<std::string, std::string>> all_users;
        if (result) {
          while (auto *row = result->fetch_one()) {
            if (context.interrupted()) {
              break;
            }

            all_users.emplace_back(row->get_string(0), row->get_string(1));
          }
        }

        std::vector<Upgrade_issue> issues;

        mysqlshdk::mysql::Instance instance(session);
        for (const auto &user : all_users) {
          if (context.interrupted()) {
            break;
          }

          const auto privileges =
              instance.get_user_privileges(user.first, user.second, true);
          for (const auto &invalid : m_privileges) {
            const auto presult = privileges->validate(invalid.second);
            const auto &missing = presult.missing_privileges();

            // If all privileges are missing, we are good, it means no invalid
            // privileges are found
            if (missing.size() != invalid.second.size()) {
              std::vector<std::string> invalid_list;
              std::set_difference(invalid.second.begin(), invalid.second.end(),
                                  missing.begin(), missing.end(),
                                  std::back_inserter(invalid_list));

              auto issue = create_issue();
              std::string raw_description = get_text("issue");
              issue.schema = shcore::make_account(user.first, user.second);
              issue.level = Upgrade_issue::NOTICE;
              issue.group = shcore::str_join(invalid_list, ", ");

              issue.description = resolve_tokens(
                  raw_description,
                  {{"account", issue.schema},
                   {"privileges", shcore::str_join(invalid_list, ", ")}});
              issue.object_type = Upgrade_issue::Object_type::USER;
              issues.push_back(issue);
            }
          }
        }

        return issues;
      });
}

}  // namespace upgrade_checker
}  // namespace mysqlsh