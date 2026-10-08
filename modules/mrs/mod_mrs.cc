/*
 * Copyright (c) 2026, MariaDB plc.
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

#include "modules/mrs/mod_mrs.h"

#include <memory>
#include <string>
#include <utility>

#include "modules/mod_shell_result.h"
#include "modules/mrs/core/mrs_parser.h"
#include "modules/mrs/mrs_shell_session.h"
#include "mysqlshdk/include/scripting/types/cpp.h"
#include "mysqlshdk/include/shellcore/base_session.h"
#include "mysqlshdk/include/shellcore/console.h"
#include "mysqlshdk/include/shellcore/custom_result.h"
#include "mysqlshdk/include/shellcore/shell_core.h"
#include "mysqlshdk/include/shellcore/sql_handler_registry.h"
#include "mysqlshdk/include/shellcore/utils_help.h"
#include "mysqlshdk/libs/utils/utils_file.h"
#include "mysqlshdk/libs/utils/utils_path.h"

namespace mysqlsh {
namespace mrs {

REGISTER_HELP_GLOBAL_OBJECT(mrs, shellapi);
REGISTER_HELP(MRS_GLOBAL_BRIEF,
              "Global object for the MariaDB REST Service (MRS).");
REGISTER_HELP(MRS_BRIEF, "Global object for the MariaDB REST Service (MRS).");
REGISTER_HELP(MRS_DETAIL,
              "The MariaDB REST Service is managed with REST SQL statements, "
              "e.g. CONFIGURE REST METADATA, CREATE REST SERVICE or SHOW REST "
              "SERVICES, which the shell runs in SQL mode and through "
              "session.runSql(). This object groups the functions that work "
              "with local files, like running a REST SQL script.");

namespace {

constexpr const char *k_handler_name = "MRS";
constexpr const char *k_handler_description =
    "MariaDB REST Service SQL Extension";

shcore::Dictionary_t to_shell_result(const ::mrs::Statement_result &result) {
  auto dict = shcore::make_dict();

  if (!result.success) {
    // The sqlstate is left out: the shell would append it to the message
    dict->set("error", shcore::Value(result.message));
    dict->set("code", shcore::Value(static_cast<int64_t>(result.error_code)));
    return dict;
  }

  if (!result.message.empty()) dict->set("info", shcore::Value(result.message));
  if (result.affected_items_count) {
    dict->set("affectedItemsCount",
              shcore::Value(static_cast<int64_t>(*result.affected_items_count)));
  }
  dict->set("executionTime", shcore::Value(result.execution_time));

  if (result.has_result_set()) {
    auto columns = shcore::make_array();
    for (const auto &column : result.columns) {
      columns->emplace_back(column);
    }
    dict->set("columns", shcore::Value(columns));

    auto data = shcore::make_array();
    for (const auto &row : result.rows) {
      auto record = shcore::make_dict();
      for (size_t i = 0; i < row.size() && i < result.columns.size(); ++i) {
        const auto &value = row[i];
        shcore::Value cell;
        if (value.is_null()) {
          cell = shcore::Value::Null();
        } else if (value.is_bool()) {
          cell = shcore::Value(value.as_bool());
        } else if (value.is_integer()) {
          cell = shcore::Value(value.as_int());
        } else if (value.is_double()) {
          cell = shcore::Value(value.as_double());
        } else {
          cell = shcore::Value(value.as_string());
        }
        record->set(result.columns[i], std::move(cell));
      }
      data->emplace_back(std::move(record));
    }
    dict->set("data", shcore::Value(data));
  }

  if (!result.warnings.empty()) {
    auto warnings = shcore::make_array();
    for (const auto &warning : result.warnings) {
      warnings->emplace_back(shcore::make_dict(
          "level", shcore::Value(warning.level), "code",
          shcore::Value(static_cast<int64_t>(warning.code)), "message",
          shcore::Value(warning.message)));
    }
    dict->set("warnings", shcore::Value(warnings));
  }

  return dict;
}

}  // namespace

Mrs::Mrs(shcore::IShell_core *owner)
    : Extensible_object("mrs", "mrs", true), m_shell_core(*owner) {
  expose("runScript", &Mrs::run_script, "path")->cli();
}

namespace {

void remove_help_topic(shcore::Help_registry *help, shcore::Help_topic *topic) {
  // Removing a child changes the parent's set of children
  const auto children = topic->m_childs;
  for (auto *child : children) remove_help_topic(help, child);
  help->remove_topic(topic, shcore::Keyword_location::GLOBAL_CTX);
}

}  // namespace

void Mrs::unregister_help() {
  auto help = shcore::Help_registry::get();
  if (auto topic =
          help->get_topic("mrs", true, shcore::Topic_mask::any(), true)) {
    remove_help_topic(help, topic);
  }
}

void Mrs::register_sql_handler() {
  auto registry = shcore::current_sql_handler_registry(true);
  if (!registry) return;

  // Already registered, e.g. by another shell instance of the same process
  if (registry->get_sql_handlers().count(k_handler_name) > 0) return;

  auto callback = shcore::Cpp_function::create(
      "mrsSqlHandler",
      [this](const shcore::Argument_list &args) -> shcore::Value {
        args.ensure_count(2, "MRS SQL handler");
        return run_rest_sql(args.object_at<ShellBaseSession>(0),
                            args.string_at(1));
      },
      {{"session", shcore::Object}, {"sql", shcore::String}});

  registry->register_sql_handler(k_handler_name, k_handler_description,
                                 ::mrs::rest_sql_prefixes(), callback);
}

shcore::Value Mrs::run_rest_sql(
    const std::shared_ptr<ShellBaseSession> &session, const std::string &sql) {
  if (!session || !session->is_open()) {
    throw shcore::Exception::runtime_error(
        "The REST SQL statement needs an open session.");
  }

  Shell_db_session db(session->get_core_session());
  auto &state = m_states[db.connection_id()];

  ::mrs::ast::Script script;
  try {
    script = ::mrs::parse_script(sql, ::mrs::Sql_mode::from_string(db.sql_mode()));
  } catch (const ::mrs::Parse_error &e) {
    throw shcore::Exception::runtime_error(e.what());
  }

  ::mrs::Ddl_executor executor(&db, &state);
  const auto results = executor.run(script);

  auto shell_results = shcore::make_array();
  for (const auto &result : results) {
    shell_results->emplace_back(to_shell_result(result));
  }

  return shcore::Value(std::make_shared<ShellResult>(
      std::make_shared<mysqlshdk::Custom_result_set>(shell_results)));
}

REGISTER_HELP_FUNCTION(runScript, mrs);
REGISTER_HELP_FUNCTION_TEXT(MRS_RUNSCRIPT, R"*(
Runs the REST SQL statements of a script file.

@param path The path of the script file.

The file holds REST SQL statements separated by semicolons, as written by
DUMP REST SERVICE. They are run against the current session, one after the
other, and the run stops at the first failing statement.
)*");
/**
 * $(MRS_RUNSCRIPT_BRIEF)
 *
 * $(MRS_RUNSCRIPT)
 */
#if DOXYGEN_JS
Undefined Mrs::runScript(String path) {}
#elif DOXYGEN_PY
None Mrs::run_script(str path) {}
#endif
void Mrs::run_script(const std::string &path) {
  const auto session = m_shell_core.get_dev_session();
  if (!session || !session->is_open()) {
    throw shcore::Exception::runtime_error(
        "An open session is required to run a REST SQL script.");
  }

  const auto file = shcore::path::expand_user(path);
  if (!shcore::is_file(file)) {
    throw shcore::Exception::runtime_error("The file '" + file +
                                           "' does not exist.");
  }

  const auto result = run_rest_sql(session, shcore::get_text_file(file));
  const auto console = current_console();

  // Report every statement's outcome, as the shell does for SQL
  auto shell_result = result.as_object<ShellResult>();
  do {
    if (shell_result->has_data()) {
      for (auto row = shell_result->fetch_one(); row;
           row = shell_result->fetch_one()) {
        for (const auto &column : *shell_result->get_column_names()) {
          console->println(row->get_member(column).descr());
        }
      }
    } else {
      const auto info = shell_result->get_member("info").descr();
      if (!info.empty()) console->println(info);
    }
  } while (shell_result->next_result());
}

}  // namespace mrs
}  // namespace mysqlsh
