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

#include "modules/mrs/core/mrs_ddl_executor.h"

#include <chrono>
#include <stdexcept>

#include "modules/mrs/core/mrs_metadata_json.h"
#include "modules/mrs/core/mrs_metadata_schema.h"

namespace mrs {

using namespace ast;

namespace {

std::string create_prefix(const Create_flags &flags) {
  if (flags.or_replace) return "CREATE OR REPLACE";
  if (flags.if_not_exists) return "CREATE IF NOT EXISTS";
  return "CREATE";
}

std::string db_object_caption(Db_object_kind kind) {
  switch (kind) {
    case Db_object_kind::view:
      return "VIEW";
    case Db_object_kind::procedure:
      return "PROCEDURE";
    case Db_object_kind::function:
      return "FUNCTION";
  }
  return "VIEW";
}

std::string routine_caption(Create_rest_routine::Kind kind) {
  return kind == Create_rest_routine::Kind::function ? "FUNCTION" : "PROCEDURE";
}

// The name of the operation, as reported in the results.
struct Operation_name {
  std::string operator()(const Configure_rest_metadata &) const {
    return "CONFIGURE REST METADATA";
  }
  std::string operator()(const Create_rest_service &s) const {
    return create_prefix(s.flags) + " REST SERVICE";
  }
  std::string operator()(const Create_rest_schema &s) const {
    return create_prefix(s.flags) + " REST SCHEMA";
  }
  std::string operator()(const Create_rest_view &s) const {
    return create_prefix(s.flags) + " REST VIEW";
  }
  std::string operator()(const Create_rest_routine &s) const {
    return create_prefix(s.flags) + " REST " + routine_caption(s.kind);
  }
  std::string operator()(const Create_rest_content_set &s) const {
    return create_prefix(s.flags) + " REST CONTENT SET";
  }
  std::string operator()(const Create_rest_content_file &s) const {
    return create_prefix(s.flags) + " REST CONTENT FILE";
  }
  std::string operator()(const Create_rest_auth_app &s) const {
    return create_prefix(s.flags) + " REST AUTH APP";
  }
  std::string operator()(const Create_rest_user &s) const {
    return create_prefix(s.flags) + " REST USER";
  }
  std::string operator()(const Create_rest_role &s) const {
    return create_prefix(s.flags) + " REST ROLE";
  }
  std::string operator()(const Clone_rest_service &) const {
    return "CLONE REST SERVICE";
  }
  std::string operator()(const Alter_rest_service &) const {
    return "ALTER REST SERVICE";
  }
  std::string operator()(const Alter_rest_schema &) const {
    return "ALTER REST SCHEMA";
  }
  std::string operator()(const Alter_rest_view &) const {
    return "ALTER REST VIEW";
  }
  std::string operator()(const Alter_rest_routine &s) const {
    return "ALTER REST " + routine_caption(s.kind);
  }
  std::string operator()(const Alter_rest_content_set &) const {
    return "ALTER REST CONTENT SET";
  }
  std::string operator()(const Alter_rest_auth_app &) const {
    return "ALTER REST AUTH APP";
  }
  std::string operator()(const Alter_rest_user &) const {
    return "ALTER REST USER";
  }
  std::string operator()(const Drop_rest_service &) const {
    return "DROP REST SERVICE";
  }
  std::string operator()(const Drop_rest_schema &) const {
    return "DROP REST SCHEMA";
  }
  std::string operator()(const Drop_rest_db_object &s) const {
    return "DROP REST " + db_object_caption(s.kind);
  }
  std::string operator()(const Drop_rest_content_set &) const {
    return "DROP REST CONTENT SET";
  }
  std::string operator()(const Drop_rest_content_file &) const {
    return "DROP REST CONTENT FILE";
  }
  std::string operator()(const Drop_rest_auth_app &) const {
    return "DROP REST AUTH APP";
  }
  std::string operator()(const Drop_rest_user &) const {
    return "DROP REST USER";
  }
  std::string operator()(const Drop_rest_role &) const {
    return "DROP REST ROLE";
  }
  std::string operator()(const Rest_privilege_statement &s) const {
    return s.revoke ? "REVOKE REST PRIVILEGE" : "GRANT REST PRIVILEGE";
  }
  std::string operator()(const Rest_role_statement &s) const {
    return s.revoke ? "REVOKE REST ROLE" : "GRANT REST ROLE";
  }
  std::string operator()(const Use_rest &s) const {
    return s.schema_path ? "USE REST SCHEMA" : "USE REST SERVICE";
  }
  std::string operator()(const Show_rest_metadata_status &) const {
    return "SHOW REST METADATA STATUS";
  }
  std::string operator()(const Show_rest_services &) const {
    return "SHOW REST SERVICES";
  }
  std::string operator()(const Show_rest_schemas &) const {
    return "SHOW REST SCHEMAS";
  }
  std::string operator()(const Show_rest_db_objects &s) const {
    return "SHOW REST " + db_object_caption(s.kind) + "S";
  }
  std::string operator()(const Show_rest_content_sets &) const {
    return "SHOW REST CONTENT SETS";
  }
  std::string operator()(const Show_rest_content_files &) const {
    return "SHOW REST CONTENT FILES";
  }
  std::string operator()(const Show_rest_auth_apps &) const {
    return "SHOW REST AUTH APPS";
  }
  std::string operator()(const Show_rest_auth_vendors &) const {
    return "SHOW REST AUTH VENDORS";
  }
  std::string operator()(const Show_rest_users &) const {
    return "SHOW REST USERS";
  }
  std::string operator()(const Show_rest_columns &) const {
    return "SHOW REST COLUMNS";
  }
  std::string operator()(const Show_rest_daemons &) const {
    return "SHOW REST DAEMONS";
  }
  std::string operator()(const Drop_rest_daemon &) const {
    return "DROP REST DAEMON";
  }
  std::string operator()(const Show_rest_roles &) const {
    return "SHOW REST ROLES";
  }
  std::string operator()(const Show_rest_grants &) const {
    return "SHOW REST GRANTS";
  }
  std::string operator()(const Show_create_rest_service &) const {
    return "SHOW CREATE REST SERVICE";
  }
  std::string operator()(const Show_create_rest_schema &) const {
    return "SHOW CREATE REST SCHEMA";
  }
  std::string operator()(const Show_create_rest_db_object &s) const {
    return "SHOW CREATE REST " + db_object_caption(s.kind);
  }
  std::string operator()(const Show_create_rest_content_set &) const {
    return "SHOW CREATE REST CONTENT SET";
  }
  std::string operator()(const Show_create_rest_content_file &) const {
    return "SHOW CREATE REST CONTENT FILE";
  }
  std::string operator()(const Show_create_rest_auth_app &) const {
    return "SHOW CREATE REST AUTH APP";
  }
  std::string operator()(const Show_create_rest_role &) const {
    return "SHOW CREATE REST ROLE";
  }
  std::string operator()(const Show_create_rest_user &) const {
    return "SHOW CREATE REST USER";
  }
};

}  // namespace

Ddl_executor::Ddl_executor(Db_session *session, Executor_state *state)
    : m_session(session), m_state(state) {}

std::vector<Statement_result> Ddl_executor::run(const Script &script) {
  std::vector<Statement_result> results;
  for (const auto &statement : script) {
    results.push_back(execute(statement));
    results.back().statement_index = static_cast<int>(results.size());
    if (!results.back().success) break;
  }
  return results;
}

Statement_result Ddl_executor::execute(const Statement &statement) {
  const auto start = std::chrono::steady_clock::now();

  Statement_result result;
  result.statement_index = 1;
  result.line = statement.line;
  result.operation = std::visit(Operation_name{}, statement.value);
  m_failure_context.clear();

  try {
    // Only CONFIGURE REST METADATA and SHOW REST METADATA STATUS work
    // without a metadata schema.
    if (!statement.is<Configure_rest_metadata>() &&
        !statement.is<Show_rest_metadata_status>()) {
      metadata::check_schema(m_session);
      validate_state();
    }

    std::visit([this, &result](const auto &s) { do_execute(s, &result); },
               statement.value);
  } catch (const std::exception &e) {
    if (const auto db_error = dynamic_cast<const Db_error *>(&e)) {
      result.error_code = db_error->code();
      result.sqlstate = db_error->sqlstate();
    }
    result.success = false;
    result.message = m_failure_context.empty()
                         ? std::string(e.what())
                         : m_failure_context + " " + e.what();
    result.columns.clear();
    result.rows.clear();
  }

  result.execution_time =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  return result;
}

// -- Shared helpers -------------------------------------------------------

void Ddl_executor::validate_state() {
  if (m_state_validated) return;
  m_state_validated = true;

  if (m_state->current_service_id &&
      !metadata::row_exists(m_session, "service", *m_state->current_service_id)) {
    m_state->clear_service();
  }
  if (m_state->current_schema_id &&
      !metadata::row_exists(m_session, "db_schema", *m_state->current_schema_id)) {
    m_state->clear_schema();
  }
}

std::optional<Id> Ddl_executor::resolve_service(
    const std::optional<Service_path> &given) {
  if (given) {
    const auto service = metadata::find_service(m_session, given->path,
                                                given->developers);
    if (!service) {
      throw std::runtime_error("Could not find the REST SERVICE " +
                               metadata::format_developers(given->developers) +
                               given->path + ".");
    }
    return service->id;
  }
  return m_state->current_service_id;
}

Id Ddl_executor::require_service(const std::optional<Service_path> &given) {
  auto service = resolve_service(given);
  if (!service) throw std::runtime_error("No REST SERVICE specified.");
  return *service;
}

Id Ddl_executor::require_service(const std::optional<Schema_selector> &given) {
  return require_service(given ? given->service : std::nullopt);
}

std::optional<Id> Ddl_executor::resolve_role_service(
    const std::optional<Role_service> &given) {
  if (given && given->any_service) return std::nullopt;
  return require_service(given ? given->service : std::nullopt);
}

metadata::Schema Ddl_executor::require_schema(
    const std::optional<Schema_selector> &given) {
  if (given) {
    const auto schema = metadata::find_schema(
        m_session, require_service(given->service), given->schema_path);
    if (!schema) {
      throw std::runtime_error("Could not find the REST SCHEMA " +
                               full_schema_path(given) + ".");
    }
    return *schema;
  }

  if (m_state->current_schema_id) {
    const auto schema = metadata::get_schema(m_session, *m_state->current_schema_id);
    if (schema) return *schema;
  }
  throw std::runtime_error("No REST SCHEMA specified.");
}

std::string Ddl_executor::current_service_path() const {
  if (!m_state->current_service_id) return {};
  return metadata::format_developers(m_state->current_developers) +
         m_state->current_service_host + m_state->current_service;
}

std::string Ddl_executor::full_service_path(
    const std::optional<Service_path> &given, std::string_view request_path) {
  std::string path;
  if (given) {
    path = metadata::format_developers(given->developers) + given->path;
  } else {
    path = current_service_path();
  }
  return path + std::string(request_path);
}

std::string Ddl_executor::full_schema_path(
    const std::optional<Schema_selector> &given, std::string_view request_path) {
  std::string path;
  if (given) {
    path = full_service_path(given->service) + given->schema_path;
  } else {
    path = current_service_path() + m_state->current_schema;
  }
  return path + std::string(request_path);
}

void Ddl_executor::set_current_service(const metadata::Service &service) {
  m_state->current_service_id = service.id;
  m_state->current_service = service.url_context_root;
  m_state->current_service_host = service.url_host_name;
  m_state->current_developers = service.developers;
  m_state->clear_schema();
}

void Ddl_executor::set_current_schema(const metadata::Schema &schema) {
  m_state->current_schema_id = schema.id;
  m_state->current_schema = schema.request_path;
}

// -- CONFIGURE REST METADATA, SHOW REST METADATA STATUS, USE REST ---------

void Ddl_executor::do_execute(const Configure_rest_metadata &s,
                              Statement_result *r) {
  set_failure_context("Failed to configure the REST metadata.");

  metadata::Configure_options options;
  options.enabled = s.enabled;
  if (s.options) {
    options.options = s.options->value;
    options.merge_options = s.options->merge;
  }
  options.update_if_available = s.update_if_available;

  const auto result = metadata::configure(m_session, options, m_schema_deployer);
  r->message = result.schema_changed ? "REST metadata configured successfully."
                                     : "REST Metadata updated successfully.";
}

void Ddl_executor::do_execute(const Show_rest_metadata_status &s,
                              Statement_result *r) {
  set_failure_context("Cannot SHOW the REST metadata status.");

  const auto status = metadata::get_status(m_session);
  if (s.format == Output_format::json) {
    r->columns = {"REST METADATA STATUS"};
    r->add_row().emplace_back(
        metadata::status_json(
            status, m_schema_deployer ? m_schema_deployer->available_versions()
                                      : std::vector<metadata::Version>{})
            .dump(true));
    return;
  }

  const auto flag = [](bool b) { return Db_value(b ? "true" : "false"); };
  const auto text = [](const std::optional<std::string> &s) {
    return s ? Db_value(*s) : Db_value(nullptr);
  };

  r->columns = {"service_configured",      "service_enabled",
                "service_upgradeable",     "service_upgrade_ignored",
                "service_count",           "service_being_upgraded",
                "major_upgrade_required",  "current_metadata_version",
                "available_metadata_version", "required_router_version",
                "metadata_version"};
  auto &row = r->add_row();
  row.push_back(flag(status.service_configured));
  row.push_back(flag(status.service_enabled));
  row.push_back(flag(status.service_upgradeable));
  row.push_back(flag(status.service_upgrade_ignored));
  row.push_back(Db_value(static_cast<int64_t>(status.service_count)));
  row.push_back(flag(status.service_being_upgraded));
  row.push_back(flag(status.major_upgrade_required));
  row.push_back(text(status.current_metadata_version));
  row.push_back(text(status.available_metadata_version));
  row.push_back(text(status.required_router_version));
  row.push_back(status.metadata_version
                    ? Db_value(*status.metadata_version)
                    : Db_value(nullptr));
}

void Ddl_executor::do_execute(const Use_rest &s, Statement_result *r) {
  set_failure_context("Cannot USE the specified REST object.");

  if (s.service) {
    const auto service = metadata::find_service(m_session, s.service->path,
                                                s.service->developers);
    if (!service) {
      throw std::runtime_error("A REST SERVICE with the request path " +
                               metadata::format_developers(s.service->developers) +
                               s.service->path + " could not be found.");
    }
    set_current_service(*service);
  }

  if (s.schema_path) {
    if (!m_state->current_service_id) {
      throw std::runtime_error("No current REST SERVICE specified.");
    }
    const auto schema = metadata::find_schema(
        m_session, *m_state->current_service_id, *s.schema_path);
    if (!schema) {
      throw std::runtime_error("A REST SCHEMA with the request path " +
                               *s.schema_path + " could not be found.");
    }
    set_current_schema(*schema);
    r->message = "Now using REST SCHEMA `" + schema->request_path +
                 "` on REST SERVICE `" + current_service_path() + "`.";
    r->id = schema->id;
  } else {
    r->message = "Now using REST SERVICE `" + current_service_path() + "`.";
    r->id = *m_state->current_service_id;
  }
}

}  // namespace mrs
