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

#ifndef MODULES_MRS_CORE_MRS_DDL_EXECUTOR_H_
#define MODULES_MRS_CORE_MRS_DDL_EXECUTOR_H_

// Executes parsed REST SQL statements (mrs_ast.h) against the MRS metadata
// schema through a Db_session. This is the counterpart of the Python
// plugin's MrsDdlListener + MrsDdlExecutor pair, with the statement data
// coming from the parser's tree instead of ANTLR callbacks.
//
// The handlers are split over several source files by object type:
//   mrs_ddl_executor.cc            dispatch, shared helpers, metadata, USE
//   mrs_ddl_executor_services.cc   REST SERVICE, CLONE, DUMP, LOAD
//   mrs_ddl_executor_schemas.cc    REST SCHEMA
//   mrs_ddl_executor_db_objects.cc REST VIEW, PROCEDURE, FUNCTION
//   mrs_ddl_executor_auth.cc       AUTH APP, USER, ROLE, GRANT, REVOKE
//   mrs_ddl_executor_content.cc    CONTENT SET, CONTENT FILE

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "modules/mrs/core/mrs_ast.h"
#include "modules/mrs/core/mrs_db_session.h"
#include "modules/mrs/core/mrs_schema_deployment.h"
#include "modules/mrs/core/mrs_metadata.h"

namespace mrs {

// The outcome of one statement.
struct Statement_result {
  struct Warning {
    std::string level = "warning";
    int code = 0;
    std::string message;
  };

  int statement_index = 0;
  int line = 0;
  bool success = true;
  std::string operation;  // e.g. "CREATE REST SERVICE"
  std::string message;    // the success or error message
  std::optional<std::string> id;  // 0x... id of the object concerned
  std::optional<uint64_t> affected_items_count;
  double execution_time = 0.0;  // seconds

  // The result set of a SHOW statement.
  std::vector<std::string> columns;
  std::vector<std::vector<Db_value>> rows;

  std::vector<Warning> warnings;

  // Error details.
  int error_code = 0;
  std::string sqlstate;

  bool has_result_set() const { return !columns.empty(); }
  void add_column(std::string name) { columns.push_back(std::move(name)); }
  std::vector<Db_value> &add_row() { return rows.emplace_back(); }
};

// The state USE REST SERVICE / SCHEMA leaves behind. One instance per
// connection; it outlives the executor.
struct Executor_state {
  std::optional<Id> current_service_id;
  std::string current_service;  // url_context_root
  std::string current_service_host;
  std::vector<std::string> current_developers;
  std::optional<Id> current_schema_id;
  std::string current_schema;  // request path

  void clear_service() {
    current_service_id.reset();
    current_service.clear();
    current_service_host.clear();
    current_developers.clear();
    clear_schema();
  }
  void clear_schema() {
    current_schema_id.reset();
    current_schema.clear();
  }
};

class Ddl_executor {
 public:
  Ddl_executor(Db_session *session, Executor_state *state);

  // Runs the statements in order and stops at the first failure. The
  // returned results include the failed statement's error.
  std::vector<Statement_result> run(const ast::Script &script);

  // Runs one statement; errors are reported in the result, not thrown.
  Statement_result execute(const ast::Statement &statement);

  Db_session *session() const { return m_session; }
  Executor_state *state() const { return m_state; }

  // How CONFIGURE REST METADATA deploys the metadata schema (the msm
  // plugin, or a metadata::Script_deployer). Without one it fails.
  void set_schema_deployer(metadata::Schema_deployer *deployer) {
    m_schema_deployer = deployer;
  }
  metadata::Schema_deployer *schema_deployer() const {
    return m_schema_deployer;
  }

 private:
  friend struct Executor_access;

  // One handler per statement type. A handler fills the result, and throws
  // on failure; the failure message is prefixed by the context set with
  // set_failure_context().
  void do_execute(const ast::Configure_rest_metadata &s, Statement_result *r);
  void do_execute(const ast::Create_rest_service &s, Statement_result *r);
  void do_execute(const ast::Create_rest_schema &s, Statement_result *r);
  void do_execute(const ast::Create_rest_view &s, Statement_result *r);
  void do_execute(const ast::Create_rest_routine &s, Statement_result *r);
  void do_execute(const ast::Create_rest_content_set &s, Statement_result *r);
  void do_execute(const ast::Create_rest_content_file &s, Statement_result *r);
  void do_execute(const ast::Create_rest_auth_app &s, Statement_result *r);
  void do_execute(const ast::Create_rest_user &s, Statement_result *r);
  void do_execute(const ast::Create_rest_role &s, Statement_result *r);
  void do_execute(const ast::Clone_rest_service &s, Statement_result *r);
  void do_execute(const ast::Alter_rest_service &s, Statement_result *r);
  void do_execute(const ast::Alter_rest_schema &s, Statement_result *r);
  void do_execute(const ast::Alter_rest_view &s, Statement_result *r);
  void do_execute(const ast::Alter_rest_routine &s, Statement_result *r);
  void do_execute(const ast::Alter_rest_content_set &s, Statement_result *r);
  void do_execute(const ast::Alter_rest_auth_app &s, Statement_result *r);
  void do_execute(const ast::Alter_rest_user &s, Statement_result *r);
  void do_execute(const ast::Drop_rest_service &s, Statement_result *r);
  void do_execute(const ast::Drop_rest_schema &s, Statement_result *r);
  void do_execute(const ast::Drop_rest_db_object &s, Statement_result *r);
  void do_execute(const ast::Drop_rest_content_set &s, Statement_result *r);
  void do_execute(const ast::Drop_rest_content_file &s, Statement_result *r);
  void do_execute(const ast::Drop_rest_auth_app &s, Statement_result *r);
  void do_execute(const ast::Drop_rest_user &s, Statement_result *r);
  void do_execute(const ast::Drop_rest_role &s, Statement_result *r);
  void do_execute(const ast::Rest_privilege_statement &s, Statement_result *r);
  void do_execute(const ast::Rest_role_statement &s, Statement_result *r);
  void do_execute(const ast::Use_rest &s, Statement_result *r);
  void do_execute(const ast::Show_rest_metadata_status &s, Statement_result *r);
  void do_execute(const ast::Show_rest_services &s, Statement_result *r);
  void do_execute(const ast::Show_rest_schemas &s, Statement_result *r);
  void do_execute(const ast::Show_rest_db_objects &s, Statement_result *r);
  void do_execute(const ast::Show_rest_content_sets &s, Statement_result *r);
  void do_execute(const ast::Show_rest_content_files &s, Statement_result *r);
  void do_execute(const ast::Show_rest_auth_apps &s, Statement_result *r);
  void do_execute(const ast::Show_rest_auth_vendors &s, Statement_result *r);
  void do_execute(const ast::Show_rest_users &s, Statement_result *r);
  void do_execute(const ast::Show_rest_columns &s, Statement_result *r);
  void do_execute(const ast::Show_rest_daemons &s, Statement_result *r);
  void do_execute(const ast::Drop_rest_daemon &s, Statement_result *r);
  void do_execute(const ast::Show_rest_roles &s, Statement_result *r);
  void do_execute(const ast::Show_rest_grants &s, Statement_result *r);
  void do_execute(const ast::Show_create_rest_service &s, Statement_result *r);
  void do_execute(const ast::Show_create_rest_schema &s, Statement_result *r);
  void do_execute(const ast::Show_create_rest_db_object &s, Statement_result *r);
  void do_execute(const ast::Show_create_rest_content_set &s, Statement_result *r);
  void do_execute(const ast::Show_create_rest_content_file &s, Statement_result *r);
  void do_execute(const ast::Show_create_rest_auth_app &s, Statement_result *r);
  void do_execute(const ast::Show_create_rest_role &s, Statement_result *r);
  void do_execute(const ast::Show_create_rest_user &s, Statement_result *r);

  // -- Helpers shared by the handlers ------------------------------------

  // The message an error is prefixed with, e.g.
  // "Failed to create the REST SERVICE `/svc`."
  void set_failure_context(std::string context) {
    m_failure_context = std::move(context);
  }

  // A service resolved from a statement or from USE REST SERVICE.
  struct Resolved_service {
    Id id;
    std::string url_context_root;
    std::string url_host_name;
    std::vector<std::string> developers;
  };

  // The service named in the statement, or the current one. Throws
  // "No REST SERVICE specified." when neither is available, or when the
  // named one does not exist.
  Resolved_service require_service(
      const std::optional<ast::Service_path> &given);
  // Same, but returns nullopt when no service is given nor current.
  std::optional<Resolved_service> resolve_service(
      const std::optional<ast::Service_path> &given);
  // The service of a role statement: `ON ANY SERVICE` yields nullopt.
  std::optional<Id> resolve_role_service(
      const std::optional<ast::Role_service> &given);

  // The schema named in the statement, or the current one. Throws when
  // neither is available or the named one does not exist.
  metadata::Schema require_schema(
      const std::optional<ast::Schema_selector> &given);
  // The service part of a schema selector, or the current service.
  Resolved_service require_service(
      const std::optional<ast::Schema_selector> &given);

  // Paths for messages: the service's full path (developers@host/path)
  // and the schema's path below it, with an optional request path appended.
  std::string full_service_path(const std::optional<ast::Service_path> &given,
                                std::string_view request_path = {});
  std::string full_schema_path(const std::optional<ast::Schema_selector> &given,
                               std::string_view request_path = {});
  std::string current_service_path() const;

  // The request path a service is created with: the statement's path or
  // the LOAD REST SERVICE AS override.
  std::string service_path(const ast::Service_path &path) const;

  // Drops the current service / schema if they no longer exist.
  void validate_state();

  // ADD AUTH APP / REMOVE AUTH APP of the service statements.
  void link_auth_apps(const Id &service_id,
                      const std::vector<ast::Auth_app_reference> &add,
                      const std::vector<ast::Auth_app_reference> &remove);

  // Makes the current service the given one.
  void set_current_service(const metadata::Service &service);
  void set_current_schema(const metadata::Schema &schema);

  // Converts a Statement_result's rows from a set of key/value pairs.
  void set_message_rows(Statement_result *result,
                        const std::vector<std::pair<std::string, std::string>>
                            &key_values);

  Db_session *m_session;
  Executor_state *m_state;
  std::string m_failure_context;
  bool m_state_validated = false;
  metadata::Schema_deployer *m_schema_deployer = nullptr;
};

}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_DDL_EXECUTOR_H_
