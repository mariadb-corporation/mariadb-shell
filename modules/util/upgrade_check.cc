/*
 * Copyright (c) 2017, 2026, Oracle and/or its affiliates.
 * Copyright (c) 2026, MariaDB plc.
 *
 * SPDX-License-Identifier: GPL-2.0-only
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

#include "modules/util/upgrade_check.h"

#include <algorithm>
#include <cinttypes>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "modules/util/upgrade_checker/common.h"
#include "modules/util/upgrade_checker/manual_check.h"
#include "modules/util/upgrade_checker/upgrade_check_registry.h"
#include "mysqlshdk/include/shellcore/scoped_contexts.h"
#include "mysqlshdk/include/shellcore/shell_init.h"
#include "mysqlshdk/libs/db/mysql/session.h"
#include "mysqlshdk/libs/db/mysqlx/session.h"
#include "mysqlshdk/libs/db/utils/utils.h"
#include "mysqlshdk/libs/utils/utils_string.h"
#include "mysqlshdk/libs/utils/version.h"

namespace mysqlsh {
namespace upgrade_checker {
namespace {
struct Stats_info {
  void update(const Upgrade_issue::Level level) {
    switch (level) {
      case Upgrade_issue::ERROR:
        errors++;
        break;
      case Upgrade_issue::WARNING:
        warnings++;
        break;
      case Upgrade_issue::NOTICE:
        notices++;
        break;
    }
  }

  int errors = 0;
  int warnings = 0;
  int notices = 0;
};

}  // namespace

using mysqlshdk::utils::Version;
namespace suggested_version {
/*
 * RULE: Do NOT skip bugfix or LTS versions.
 *
 * bugfix:     5.7.whatever    want-to: 9.0   suggestion=8.0
 * bugfix:     8.0.whatever    want-to: 9.0   suggestion=8.4
 * Innovation: 8.1..8.3        want-to: 9.0   suggestion=8.4
 * LTS:        8.4.whatever    want-to: 26.7   suggestion=9.7
 * Innovation: 9.0..9.6        want-to: 26.7   suggestion=9.7
 * LTS:        9.7.whatever    want-to: 28.4   suggestion=28.4
 * Innovation: 26.7..28.1      want-to: 28.7   suggestion=28.4
 * LTS:        28.4.whatever   want-to: 30.4   suggestion=30.4
 */
std::optional<Version> get_next_suggested_lts_version(
    const Version &version, std::string *out_key = nullptr) {
  std::optional<Version> next_lts;
  decltype(k_latest_versions)::const_iterator item = k_latest_versions.end();

  // 8.0 has reached EOL, but the upgrade checker keeps a fixed 8.0.46
  // intermediate suggestion for pre-8.0 sources.
  if (version < Version(8, 0, 0)) {
    item = k_latest_versions.find("8.0");
  } else if (mysqlshdk::utils::version::is_lts(version) &&
             !(version.get_major() == 8 && version.get_minor() == 0)) {
    // We might be already on the LTS release of the series, so it jumps to
    // the LTS in the next series (If they exist)
    item = k_latest_versions.find(
        mysqlshdk::utils::version::next_lts(version).get_short());
  } else {
    // in other cases lets find the latest LTS version in the series
    item = k_latest_versions.find(
        mysqlshdk::utils::version::first_lts(version).get_short());
  }

  if (item != k_latest_versions.end()) {
    if (out_key) {
      *out_key = item->first;
      return item->second;
    }
  }

  return {};
}

std::string format_suggested_version_message(
    const Version &server_version, const Version &target_version,
    const std::string &suggested_target_version) {
  return shcore::str_format(
      "Upgrading MySQL Server from version %s to %s is not supported. Please "
      "consider running the check using the following option: targetVersion=%s",
      server_version.get_base().c_str(), target_version.get_base().c_str(),
      suggested_target_version.c_str());
}
}  // namespace suggested_version

std::string check_for_version_suggestion(const Version &server_version,
                                         const Version &target_version) {
  std::string suggested_target_version;
  const auto suggested = suggested_version::get_next_suggested_lts_version(
      server_version, &suggested_target_version);
  if (suggested.value_or(target_version).numeric_version_series() >=
      target_version.numeric_version_series()) {
    return {};
  }

  return suggested_version::format_suggested_version_message(
      server_version, target_version, suggested_target_version);
}

void run_checks(const Upgrade_check_config &config,
                Upgrade_check_output_formatter *print,
                const std::vector<std::unique_ptr<Upgrade_check>> *checklist,
                Stats_info *stats) {
  Checker_cache cache{*config.db_filters()};
  auto session_pool = std::make_shared<mysqlshdk::db::Session_pool>(
      config.session()->get_connection_options(), config.worker_threads());

  shcore::atomic_flag interrupt_flag;

  shcore::Interrupt_handler intr_handler(
      [&interrupt_flag]() {
        interrupt_flag.test_and_set();
        return false;
      },
      []() {
        mysqlsh::current_console()->print_note(
            "Interrupted by user. Cancelling...");
      });

  Check_context check_context(
      config.session(), config.upgrade_info(), session_pool, &cache,
      config.has_check_timeout() ? config.check_timeout() : 0, &interrupt_flag);

  const auto checklist_size = checklist->size();
  int current_check = -1;
  for (const auto &check : *checklist) {
    if (interrupt_flag.test()) {
      break;
    }

    check->set_timedout(false);

    ++current_check;
    if (config.show_progress()) {
      print->update_progress(check.get(), current_check, checklist_size);

      check_context.on_progress = [print, check = check.get(), current_check,
                                   total_checks = checklist_size](
                                      const std::string &detail, int current,
                                      int total) {
        print->update_progress(check, current_check, total_checks, detail,
                               current, total);
      };
    }
    // running check
    if (check->is_runnable()) {
      try {
        print->check_title(*check);

        const auto issues = config.filter_issues(check->run(check_context));
        for (const auto &issue : issues) {
          stats->update(issue.level);
        }
        if (check->is_timedout()) {
          print->check_error(
              *check, "Warning: Check timed out and was interrupted.", false);
          stats->warnings++;
        } else {
          print->check_results(*check, issues);
        }
      } catch (const Check_configuration_error &e) {
        print->check_error(*check, e.what(), false);
      } catch (const mysqlshdk::db::Error &e) {
        if (check->is_timedout()) {
          print->check_error(
              *check, "Warning: Check timed out and was interrupted.", false);
          stats->warnings++;
        } else {
          print->check_error(*check, e.what());
        }
      } catch (const std::exception &e) {
        print->check_error(*check, e.what());
      }
    } else {
      stats->update(dynamic_cast<Manual_check *>(check.get())->get_level());
      print->manual_check(*check);
    }
  }

  if (config.show_progress()) {
    print->update_progress(nullptr, checklist_size, checklist_size);
  }
}

bool run_checks_for_upgrade(const Upgrade_check_config &config,
                            Upgrade_check_output_formatter &print) {
  assert(config.session());

  const auto version_suggestion =
      config.upgrade_info().skip_target_version_check
          ? ""
          : check_for_version_suggestion(config.upgrade_info().server_version,
                                         config.upgrade_info().target_version);

  print.check_info(config.session()->get_connection_options().uri_endpoint(),
                   config.upgrade_info().server_version_long,
                   config.upgrade_info().target_version.get_base(),
                   config.upgrade_info().explicit_target_version,
                   version_suggestion);
  config.upgrade_info().validate();

  Upgrade_check_registry::Upgrade_check_vec rejected;
  const auto checklist = Upgrade_check_registry::create_checklist(
      config, false, config.warn_on_excludes() ? &rejected : nullptr);

  Stats_info stats;
  run_checks(config, &print, &checklist, &stats);

  std::string summary;
  if (stats.errors > 0) {
    summary = shcore::str_format(
        "%i errors were found. Please correct these issues before upgrading "
        "to avoid compatibility issues.",
        stats.errors);
  } else if ((stats.warnings > 0) || (stats.notices > 0)) {
    summary =
        "No fatal errors were found that would prevent an upgrade, "
        "but some potential issues were detected. Please ensure that the "
        "reported issues are not significant before upgrading.";
  } else {
    summary = "No known compatibility errors or issues were found.";
  }

  std::map<std::string, std::string> excluded;
  if (config.warn_on_excludes() && !config.exclude_list().empty()) {
    for (const auto &check : rejected) {
      if (!config.exclude_list().contains(check->get_name())) continue;

      excluded[check->get_name()] = check->get_title();
    }
  }

  print.summarize(stats.errors, stats.warnings, stats.notices, summary,
                  excluded);

  return 0 == stats.errors;
}

bool list_checks_for_upgrade(const Upgrade_check_config &config,
                             Upgrade_check_output_formatter &print) {
  if (config.session()) {
    print.list_info(config.session()->get_connection_options().uri_endpoint(),
                    config.upgrade_info().server_version_long,
                    config.upgrade_info().target_version.get_base(),
                    config.upgrade_info().explicit_target_version);
  } else {
    print.list_info();
  }

  config.upgrade_info().validate(true);

  Upgrade_check_registry::Upgrade_check_vec rejected;

  const auto accepted = Upgrade_check_registry::create_checklist(
      config, config.session() == nullptr, &rejected);

  print.list_item_infos("Included", accepted);
  print.list_item_infos("Excluded", rejected);

  print.list_summarize(accepted.size(), rejected.size());

  return true;
}

/*
 * Upgrade Checker entry point
 */
bool check_for_upgrade(const Upgrade_check_config &config) {
  if (!config.list_checks()) {
    if (config.user_privileges()) {
      if (config.user_privileges()
              ->validate({"PROCESS", "SELECT"})
              .has_missing_privileges()) {
        throw std::runtime_error(
            "The upgrade check needs to be performed by user with PROCESS, and "
            "SELECT privileges.");
      }
    } else {
      log_warning(
          "User privileges were not validated, upgrade check may fail.");
    }
  }

  const auto print = config.formatter();
  assert(print);

  if (config.list_checks()) {
    return list_checks_for_upgrade(config, *print);
  }
  return run_checks_for_upgrade(config, *print);
}

}  // namespace upgrade_checker
}  // namespace mysqlsh
