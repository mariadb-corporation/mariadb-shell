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

#ifndef MODULES_UTIL_UPGRADE_CHECKER_UPGRADE_CHECK_H_
#define MODULES_UTIL_UPGRADE_CHECKER_UPGRADE_CHECK_H_

#include <atomic>
#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "modules/util/upgrade_checker/common.h"
#include "mysqlshdk/include/shellcore/shell_init.h"
#include "mysqlshdk/libs/db/mysql/session.h"
#include "mysqlshdk/libs/db/session.h"
#include "mysqlshdk/libs/db/session_pool.h"
#include "mysqlshdk/libs/utils/thread_pool.h"
#include "mysqlshdk/libs/utils/version.h"

namespace mysqlsh {
namespace upgrade_checker {

class Condition;

enum class Category {
  ACCOUNTS,
  CONFIG,
  PARSING,
  SCHEMA,
};

class Upgrade_check;

class Worker_pool {
 public:
  Worker_pool(std::shared_ptr<mysqlshdk::db::Session_pool> session_pool,
              uint64_t num_threads, uint64_t timeout_seconds = 0,
              shcore::atomic_flag *interrupt_flag = nullptr,
              std::function<void(int, int)> &&progress_callback = {})
      : m_thread_pool(num_threads, 0, mysqlsh::thread_init, mysqlsh::thread_end,
                      interrupt_flag),
        m_interrupt_flag(interrupt_flag),
        m_session_pool(std::move(session_pool)),
        m_timeout_seconds(timeout_seconds),
        m_progress_callback(std::move(progress_callback)) {}

  void init();

  void execute(Upgrade_check *check,
               std::function<std::vector<Upgrade_issue>(
                   const std::shared_ptr<mysqlshdk::db::ISession> &)> &&task,
               std::shared_ptr<mysqlshdk::db::ISession> session = {});

  std::vector<Upgrade_issue> wait();

 private:
  struct Active_check {
    Upgrade_check *check;
    uint64_t connection_id;
    std::chrono::steady_clock::time_point start_time;
  };

  void kill_check(const Active_check &check);
  void on_work_start(const std::shared_ptr<mysqlshdk::db::ISession> &session,
                     Upgrade_check *check);
  void on_work_end(const std::shared_ptr<mysqlshdk::db::ISession> &session,
                   Upgrade_check *check);
  void check_timeouts();

 private:
  using Thread_pool = shcore::Thread_pool_base<std::vector<Upgrade_issue>>;
  Thread_pool m_thread_pool;
  shcore::atomic_flag *m_interrupt_flag;
  std::shared_ptr<mysqlshdk::db::Session_pool> m_session_pool;
  std::vector<Upgrade_issue> m_issues;
  uint64_t m_timeout_seconds;

  std::mutex m_active_checks_mutex;
  std::list<Active_check> m_active_checks;

  const std::atomic<Thread_pool::Async_state> *m_async_process_state = nullptr;
  std::function<void(int, int)> m_progress_callback;
  std::atomic<int> m_total_progress_units{0};
  std::atomic<int> m_finished_progress_units{0};
};

class Check_context {
 public:
  Check_context() = delete;
  Check_context(const Check_context &) = delete;
  Check_context &operator=(const Check_context &) = delete;
  Check_context(const std::shared_ptr<mysqlshdk::db::ISession> &session,
                const Upgrade_info &server_info,
                std::shared_ptr<mysqlshdk::db::Session_pool> session_pool,
                Checker_cache *cache, uint64_t timeout_seconds = 0,
                shcore::atomic_flag *interrupt_flag = nullptr);

  ~Check_context() = default;

  inline std::shared_ptr<mysqlshdk::db::ISession> session() const noexcept {
    return m_session;
  }

  inline const Upgrade_info &server_info() const { return m_server_info; }

  inline std::shared_ptr<mysqlshdk::db::Session_pool> session_pool() const {
    return m_session_pool;
  }

  std::unique_ptr<Worker_pool> make_worker_pool(
      std::function<void(int, int)> &&progress_callback = {},
      bool skip_pool = false) const;

  inline Checker_cache *cache() const { return m_cache; }

  inline bool interrupted() const {
    if (!m_interrupt_flag) return false;

    return m_interrupt_flag->test();
  }

  std::vector<Upgrade_issue> execute_simple(
      Upgrade_check *check,
      std::function<std::vector<Upgrade_issue>(
          const std::shared_ptr<mysqlshdk::db::ISession> &)> &&task) const;

  std::function<void(const std::string &, int, int)> on_progress;

 private:
  std::shared_ptr<mysqlshdk::db::ISession> m_session;
  std::shared_ptr<mysqlshdk::db::Session_pool> m_session_pool;
  Checker_cache *m_cache;
  const Upgrade_info &m_server_info;
  uint64_t m_timeout_seconds;
  shcore::atomic_flag *m_interrupt_flag;
};

class Upgrade_check {
 public:
  explicit Upgrade_check(const std::string_view name, Category category)
      : m_name(name), m_category(category) {}
  virtual ~Upgrade_check() {}

  const std::string &get_name() const { return m_name; }
  const std::string &get_category() const { return to_string(m_category); }
  virtual const std::string &get_title() const;
  virtual std::string get_description(
      const std::string &group = "",
      const Token_definitions &tokens = {}) const;
  virtual const std::string &get_doc_link(const std::string &group = "") const;
  virtual bool is_runnable() const { return true; }
  virtual bool is_multi_lvl_check() const { return false; }
  std::vector<std::string> get_solutions(const std::string &group = "") const;

  virtual std::vector<Upgrade_issue> run(const Check_context &) {
    throw std::logic_error("not implemented");
  }

  const std::string &get_text(const char *field) const;
  virtual bool enabled() const { return true; }

  void set_condition(Condition *condition) { m_condition = condition; }
  Condition *get_condition() const noexcept { return m_condition; }

  void set_groups(std::vector<std::string> groups) {
    m_groups = std::move(groups);
  }
  const std::vector<std::string> &groups() const { return m_groups; }
  Upgrade_issue create_issue() const;

  virtual bool is_custom_session_required() const { return false; }

  bool is_timedout() const noexcept { return m_timedout.load(); }
  void set_timedout(bool timedout) { m_timedout.store(timedout); }

 protected:
  virtual Token_definitions base_tokens() const { return {}; }

 private:
  const std::string &to_string(Category category) const;

  std::string m_name;
  Category m_category;
  Condition *m_condition = nullptr;
  std::vector<std::string> m_groups;
  std::atomic_bool m_timedout{false};
};

class Invalid_privileges_check : public Upgrade_check {
 public:
  explicit Invalid_privileges_check(const Upgrade_info &server_info);

  void add_privileges(Version version, const std::set<std::string> &privileges);

  bool has_privilege(const std::string &privilege);

  bool enabled() const override;

  std::vector<Upgrade_issue> run(const Check_context &context) override;

 private:
  const Upgrade_info &m_upgrade_info;
  std::map<Version, std::set<std::string>> m_privileges;
};

}  // namespace upgrade_checker
}  // namespace mysqlsh

#endif  // MODULES_UTIL_UPGRADE_CHECKER_UPGRADE_CHECK_H_
