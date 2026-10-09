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
#include "modules/mrs/mrs_schema_deployers.h"
#include "modules/mrs/mrs_shell_session.h"
#include "mysqlshdk/include/scripting/types/cpp.h"
#include "mysqlshdk/include/shellcore/base_session.h"
#include "mysqlshdk/include/shellcore/custom_result.h"
#include "mysqlshdk/include/shellcore/shell_core.h"
#include "mysqlshdk/include/shellcore/sql_handler_registry.h"
#include "mysqlshdk/include/shellcore/utils_help.h"

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
              "session.runSql(). A file of REST SQL statements is run like "
              "any SQL script, e.g. with \\source in SQL mode or with "
              "--sql -f <file>.");
REGISTER_HELP(MRS_DETAIL1,
              "Plugins can add functions to this object with the "
              "plugin_function decorator, e.g. @plugin_function(\"mrs.<name>\"); "
              "the mrs_plugin adds the generation of the MRS SDK files and the "
              "dumping and loading of MRS projects this way.");

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
    : Extensible_object("mrs", "mrs", true), m_shell_core(*owner) {}

namespace {

// The handler is registered once per process, but the shell (and its mrs
// object) can be recreated, e.g. by the tests. The callback therefore runs
// on the mrs object that registered last, and never on a destroyed one.
std::weak_ptr<Mrs> &active_mrs() {
  static std::weak_ptr<Mrs> mrs;
  return mrs;
}

}  // namespace

void Mrs::register_sql_handler() {
  auto registry = shcore::current_sql_handler_registry(true);
  if (!registry) return;

  active_mrs() = std::static_pointer_cast<Mrs>(shared_from_this());

  // Already registered, e.g. by another shell instance of the same process
  if (registry->get_sql_handlers().count(k_handler_name) > 0) return;

  auto callback = shcore::Cpp_function::create(
      "mrsSqlHandler",
      [](const shcore::Argument_list &args) -> shcore::Value {
        args.ensure_count(2, "MRS SQL handler");
        const auto mrs = active_mrs().lock();
        if (!mrs) {
          throw shcore::Exception::runtime_error(
              "The MRS module is not available.");
        }
        return mrs->run_rest_sql(args.object_at<ShellBaseSession>(0),
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
  std::erase_if(m_states, [](const auto &entry) { return entry.first.expired(); });
  auto &state = m_states[session];

  // One query gives the sql_mode the statements are parsed with and what
  // the executor's metadata checks depend on
  auto fingerprint = ::mrs::metadata::read_fingerprint(
      &db, state.metadata_check.version_view);

  ::mrs::ast::Script script;
  try {
    script = ::mrs::parse_script(
        sql, ::mrs::Sql_mode::from_string(fingerprint.sql_mode));
  } catch (const ::mrs::Parse_error &e) {
    throw shcore::Exception::runtime_error(e.what());
  }

  const auto deployer = make_schema_deployer(&m_shell_core, session);
  ::mrs::Ddl_executor executor(&db, &state);
  executor.set_schema_deployer(deployer.get());
  executor.set_fingerprint(std::move(fingerprint));
  const auto results = executor.run(script);

  auto shell_results = shcore::make_array();
  for (const auto &result : results) {
    shell_results->emplace_back(to_shell_result(result));
  }

  return shcore::Value(std::make_shared<ShellResult>(
      std::make_shared<mysqlshdk::Custom_result_set>(shell_results)));
}

}  // namespace mrs
}  // namespace mysqlsh
