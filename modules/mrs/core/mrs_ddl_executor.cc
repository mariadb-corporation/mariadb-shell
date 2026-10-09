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

Ddl_executor::Ddl_executor(Db_session *session, Executor_state *state)
    : m_session(session), m_state(state) {
  m_session->set_metadata_schema(m_state->assumed_metadata_schema());
}

std::vector<Statement_result> Ddl_executor::run(const Script &script) {
  std::vector<Statement_result> results;
  for (const auto &statement : script) {
    results.push_back(execute(statement));
    if (!results.back().success) break;
  }
  return results;
}

Statement_result Ddl_executor::execute(const Statement &statement) {
  const auto start = std::chrono::steady_clock::now();

  Statement_result result;
  m_failure_context.clear();

  try {
    // The statements on the metadata schema itself work without one, and
    // find it themselves
    if (!statement.is<Configure_rest_metadata>() &&
        !statement.is<Show_rest_metadata_status>() &&
        !statement.is<Use_rest_metadata_schema>() &&
        !statement.is<Show_rest_metadata_schemas>()) {
      check_metadata();
    }

    std::visit([this, &result](const auto &s) { do_execute(s, &result); },
               statement.value);
  } catch (const std::exception &e) {
    if (const auto db_error = dynamic_cast<const Db_error *>(&e)) {
      result.error_code = db_error->code();
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

void Ddl_executor::check_metadata() {
  if (m_metadata_checked) return;

  auto &check = m_state->metadata_check;
  if (!m_fingerprint) {
    m_fingerprint = metadata::read_fingerprint(m_session, check.version_view);
  }

  if (!m_fingerprint->valid || m_fingerprint->version != check.version ||
      check.schema != m_session->metadata_schema()) {
    // A first run, another schema or version, or no fingerprint: the
    // metadata schema is looked up again and checked in full, which gives
    // the error messages and finds the version view
    select_metadata_schema();
    switch_metadata_schema(m_session->metadata_schema());
    check.version = metadata::check_schema(m_session, &check.version_view);
    m_fingerprint = metadata::read_fingerprint(m_session, check.version_view);
  }

  if (!m_fingerprint->valid || !check.state_checked ||
      m_fingerprint->audit_id != check.audit_id) {
    validate_state();
    check.state_checked = m_fingerprint->valid;
    check.audit_id = m_fingerprint->audit_id;
  }
  m_metadata_checked = true;
}

void Ddl_executor::select_metadata_schema() {
  m_session->set_metadata_schema(m_state->metadata_schema
                                     ? *m_state->metadata_schema
                                     : metadata::resolve_metadata_schema(m_session));
}

void Ddl_executor::switch_metadata_schema(const std::string &name) {
  auto &check = m_state->metadata_check;
  if (check.schema != name) {
    // The current service and schema belong to another metadata schema
    if (!check.schema.empty()) m_state->clear_service();
    check = {};
    check.schema = name;
  }
  m_session->set_metadata_schema(name);
}

void Ddl_executor::validate_state() {
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

  // The named schema, else the one in use
  if (s.schema) {
    metadata::check_metadata_schema_name(*s.schema);
    switch_metadata_schema(*s.schema);
  } else {
    select_metadata_schema();
    switch_metadata_schema(m_session->metadata_schema());
  }

  // The schema may be redeployed: the next statement checks it in full
  m_state->metadata_check = {};
  m_state->metadata_check.schema = m_session->metadata_schema();
  m_metadata_checked = false;
  m_fingerprint.reset();

  const auto result = metadata::configure(m_session, options, m_schema_deployer);

  // A named schema is the one the session goes on with
  if (s.schema) m_state->metadata_schema = *s.schema;
  r->message = result.schema_changed ? "REST metadata configured successfully."
                                     : "REST Metadata updated successfully.";
}

void Ddl_executor::do_execute(const Show_rest_metadata_status &s,
                              Statement_result *r) {
  set_failure_context("Cannot SHOW the REST metadata status.");

  select_metadata_schema();
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
                "metadata_version", "metadata_schema"};
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
  row.push_back(Db_value(status.metadata_schema));
}

void Ddl_executor::do_execute(const Use_rest_metadata_schema &s,
                              Statement_result *r) {
  set_failure_context("Cannot USE the REST metadata schema `" + s.schema + "`.");

  metadata::check_metadata_schema_name(s.schema);
  const auto names = metadata::find_metadata_schemas(m_session);
  if (std::find(names.begin(), names.end(), s.schema) == names.end()) {
    throw std::runtime_error(
        "It is not a REST metadata schema, or not visible to the current "
        "account.");
  }

  switch_metadata_schema(s.schema);
  m_state->metadata_schema = s.schema;
  r->message = "Now using REST METADATA SCHEMA `" + s.schema + "`.";
}

void Ddl_executor::do_execute(const Show_rest_metadata_schemas &,
                              Statement_result *r) {
  set_failure_context("Cannot SHOW the REST metadata schemas.");

  // The schema in use: the chosen one, else the one a statement would use
  // (none when several are visible)
  std::string current;
  if (m_state->metadata_schema) {
    current = *m_state->metadata_schema;
  } else {
    try {
      current = metadata::resolve_metadata_schema(m_session);
    } catch (const std::runtime_error &) {
    }
  }

  r->columns = {"schema_name", "version", "current"};
  for (const auto &name : metadata::find_metadata_schemas(m_session)) {
    std::optional<std::string> version;
    try {
      const auto result = m_session->query(
          "SELECT CONCAT(major, '.', minor, '.', patch) AS version FROM " +
          sql::quote_qualified(name, "msm_schema_version"));
      if (!result.empty()) version = result.first()["version"].as_string();
    } catch (const Db_error &) {
      // The view may not be readable for the account
    }
    auto &row = r->add_row();
    row.emplace_back(name);
    row.push_back(version ? Db_value(*version) : Db_value(nullptr));
    row.emplace_back(name == current ? "YES" : "NO");
  }
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
  } else {
    r->message = "Now using REST SERVICE `" + current_service_path() + "`.";
  }
}

}  // namespace mrs
