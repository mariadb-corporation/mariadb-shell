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

// The REST SERVICE statements: CREATE, ALTER, DROP, CLONE, SHOW, SHOW
// CREATE, DUMP and LOAD.

#include <fstream>
#include <sstream>
#include <stdexcept>

#include "modules/mrs/core/mrs_ddl_executor.h"
#include "modules/mrs/core/mrs_metadata_auth.h"
#include "modules/mrs/core/mrs_parser.h"

namespace mrs {

using namespace ast;

namespace {

std::optional<std::string> text_of(const std::optional<Text_or_default> &value) {
  if (!value || value->is_default) return std::nullopt;
  return value->text;
}

// For ALTER: DEFAULT resets the column (nullopt inside), a text sets it.
std::optional<std::optional<std::string>> change_of(
    const std::optional<Text_or_default> &value) {
  if (!value) return std::nullopt;
  if (value->is_default) return std::optional<std::string>{};
  return std::optional<std::string>{value->text};
}

std::string expand_user_path(const std::string &path) {
  if (path.size() >= 2 && path[0] == '~' && (path[1] == '/' || path[1] == '\\')) {
    const char *home = std::getenv("HOME");
    if (!home) home = std::getenv("USERPROFILE");
    if (home) return std::string(home) + path.substr(1);
  }
  return path;
}

}  // namespace

void Ddl_executor::link_auth_apps(const Id &service_id,
                                  const std::vector<Auth_app_reference> &add,
                                  const std::vector<Auth_app_reference> &remove) {
  for (const auto &entry : add) {
    const auto auth_app = metadata::find_auth_app(m_session, entry.name);
    if (!auth_app && !entry.if_exists) {
      throw std::runtime_error("The given REST authentication app `" +
                               entry.name + "` was not found.");
    }
    if (auth_app) metadata::link_auth_app(m_session, auth_app->id, service_id);
  }
  for (const auto &entry : remove) {
    const auto auth_app = metadata::find_auth_app(m_session, entry.name);
    if (!auth_app && !entry.if_exists) {
      throw std::runtime_error("The given REST authentication app `" +
                               entry.name + "` was not found.");
    }
    if (auth_app) metadata::unlink_auth_app(m_session, auth_app->id, service_id);
  }
}

void Ddl_executor::do_execute(const Create_rest_service &s, Statement_result *r) {
  const auto path = service_path(s.path);
  const auto full_path = metadata::format_developers(s.path.developers) + path;
  set_failure_context("Failed to create the REST SERVICE `" + full_path + "`.");

  Db_transaction transaction(m_session);

  if (s.flags.or_replace || s.flags.if_not_exists) {
    const auto existing =
        metadata::find_service(m_session, path, s.path.developers);
    if (existing) {
      if (s.flags.if_not_exists) {
        r->message = "REST SERVICE `" + full_path + "` created successfully.";
        r->id = sql::hex(existing->id);
        transaction.commit();
        return;
      }
      metadata::delete_service(m_session, existing->id);
    }
  }

  metadata::Service_definition definition;
  definition.url_context_root = path;
  definition.developers = s.path.developers;
  definition.enabled = s.options.enabled;
  definition.published = s.options.published;
  definition.url_protocol = s.options.protocol;
  definition.comments = s.options.comments;
  if (s.options.options) definition.options = s.options.options->value;
  definition.metadata = s.options.metadata;
  definition.auth_path = text_of(s.options.auth_path);
  definition.auth_completed_url = text_of(s.options.auth_redirection);
  definition.auth_completed_url_validation = text_of(s.options.auth_validation);
  definition.auth_completed_page_content = text_of(s.options.auth_page_content);

  const Id id = metadata::add_service(m_session, definition);

  // Removing auth apps from a service being created makes no sense
  link_auth_apps(id, s.options.add_auth_apps, {});

  transaction.commit();

  r->message = "REST SERVICE `" + full_path + "` created successfully.";
  r->id = sql::hex(id);
}

void Ddl_executor::do_execute(const Alter_rest_service &s, Statement_result *r) {
  const auto full_path = full_service_path(s.path);
  set_failure_context("Failed to update the REST SERVICE `" + full_path + "`.");

  const auto service = require_service(s.path);

  metadata::Service_changes changes;
  if (s.new_path) {
    changes.url_context_root = s.new_path->path;
    changes.developers = s.new_path->developers;
  }
  changes.enabled = s.options.enabled;
  changes.published = s.options.published;
  changes.url_protocol = s.options.protocol;
  changes.comments = s.options.comments;
  if (s.options.options) {
    changes.options = s.options.options->value;
    changes.merge_options = s.options.options->merge;
  }
  changes.metadata = s.options.metadata;
  changes.auth_path = change_of(s.options.auth_path);
  changes.auth_completed_url = change_of(s.options.auth_redirection);
  changes.auth_completed_url_validation = change_of(s.options.auth_validation);
  changes.auth_completed_page_content = change_of(s.options.auth_page_content);

  Db_transaction transaction(m_session);
  metadata::update_service(m_session, service.id, changes);
  link_auth_apps(service.id, s.options.add_auth_apps, s.options.remove_auth_apps);
  transaction.commit();

  // Keep the current service in step with a renamed one
  if (m_state->current_service_id == service.id) {
    if (const auto updated = metadata::get_service(m_session, service.id)) {
      const auto schema_id = m_state->current_schema_id;
      const auto schema = m_state->current_schema;
      set_current_service(*updated);
      m_state->current_schema_id = schema_id;
      m_state->current_schema = schema;
    }
  }

  r->affected_items_count = 1;
  r->id = sql::hex(service.id);
}

void Ddl_executor::do_execute(const Drop_rest_service &s, Statement_result *r) {
  const auto full_path = full_service_path(s.path);
  set_failure_context("Failed to drop the REST SERVICE `" + full_path + "`.");

  Db_transaction transaction(m_session);

  const auto service =
      metadata::find_service(m_session, s.path.path, s.path.developers);
  if (!service && !s.if_exists) {
    throw std::runtime_error("The given REST SERVICE `" + full_path +
                             "` could not be found.");
  }
  if (service) {
    metadata::delete_service(m_session, service->id);
    if (m_state->current_service_id == service->id) m_state->clear_service();
    r->id = sql::hex(service->id);
  }

  transaction.commit();
  r->message = "REST SERVICE `" + full_path + "` dropped successfully.";
}

void Ddl_executor::do_execute(const Clone_rest_service &s, Statement_result *r) {
  const auto full_path = full_service_path(s.path);
  set_failure_context("Failed to clone the REST SERVICE `" + full_path + "`.");

  const auto resolved = require_service(s.path);
  const auto service = metadata::get_service(m_session, resolved.id);
  if (!service) throw std::runtime_error("The given REST SERVICE was not found.");

  Db_transaction transaction(m_session);
  metadata::clone_service(m_session, *service, s.new_path.path,
                          s.new_path.developers);
  transaction.commit();

  r->affected_items_count = 1;
  r->id = sql::hex(service->id);
}

void Ddl_executor::do_execute(const Show_rest_services &, Statement_result *r) {
  set_failure_context("Cannot SHOW the REST services.");

  r->columns = {"REST SERVICE Path", "enabled", "current", "auth_apps"};
  for (const auto &service : metadata::get_services(m_session)) {
    std::string auth_apps;
    for (const auto &name : service.auth_apps) {
      if (!auth_apps.empty()) auth_apps += ", ";
      auth_apps += name;
    }
    auto &row = r->add_row();
    row.emplace_back(service.full_service_path);
    row.emplace_back(metadata::enabled_caption(service.enabled));
    row.emplace_back(m_state->current_service_id == service.id ? "YES" : "NO");
    row.emplace_back(auth_apps);
  }
}

void Ddl_executor::do_execute(const Show_create_rest_service &s,
                              Statement_result *r) {
  const auto full_path = full_service_path(s.path);
  set_failure_context("Failed to get the REST SERVICE `" + full_path + "`.");

  const auto resolved = require_service(s.path);
  const auto service = metadata::get_service(m_session, resolved.id);
  if (!service) throw std::runtime_error("The given REST SERVICE was not found.");

  r->columns = {"CREATE REST SERVICE"};
  r->add_row().emplace_back(metadata::service_create_statement(
      m_session, *service, s.include_database_endpoints, false, false));
  r->id = sql::hex(service->id);
}

void Ddl_executor::do_execute(const Dump_rest_service &s, Statement_result *r) {
  const auto full_path = full_service_path(s.path);
  set_failure_context("Failed to execute DUMP REST SERVICE `" + full_path + "`.");

  if (s.zip) {
    throw std::runtime_error(
        "Dumping to a ZIP file is not supported by this version of MariaDB "
        "Shell yet.");
  }

  const auto resolved = require_service(s.path);
  const auto service = metadata::get_service(m_session, resolved.id);
  if (!service) throw std::runtime_error("The given REST SERVICE was not found.");

  const auto script = metadata::service_create_statement(
      m_session, *service, s.endpoints.database, s.endpoints.static_,
      s.endpoints.dynamic);

  const auto file_path = expand_user_path(s.directory);
  std::ofstream file(file_path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("The file '" + file_path + "' could not be written.");
  }
  file << script;
  file.close();

  r->columns = {"DUMP REST SERVICE"};
  r->add_row().emplace_back("Result stored in '" + file_path + "'");
  r->id = sql::hex(service->id);
}

void Ddl_executor::do_execute(const Load_rest_service &s, Statement_result *r) {
  const auto file_path = expand_user_path(s.directory);
  set_failure_context("Failed to execute LOAD REST SERVICE from `" + file_path +
                      "`.");

  std::ifstream file(file_path, std::ios::binary);
  if (!file) throw std::runtime_error("The specified file was not found.");
  std::stringstream content;
  content << file.rdbuf();

  const auto script = parse_script(content.str(), Sql_mode::from_string(m_session->sql_mode()));

  // The statements of the file run with their own executor and state; a
  // failure stops the load.
  Executor_state state;
  Ddl_executor loader(m_session, &state);
  if (s.as_path) loader.set_service_path_override(s.as_path->path);
  for (const auto &result : loader.run(script)) {
    if (!result.success) {
      throw std::runtime_error("Statement at line " +
                               std::to_string(result.line) + " failed: " +
                               result.message);
    }
  }

  // The loaded service is the AS path or the first service of the file.
  std::string loaded_path;
  if (s.as_path) {
    loaded_path = s.as_path->path;
  } else {
    for (const auto &statement : script) {
      if (const auto *create = statement.as<Create_rest_service>()) {
        loaded_path = create->path.path;
        break;
      }
    }
  }

  r->columns = {"LOAD REST SERVICE"};
  r->add_row().emplace_back("Service '" + loaded_path + "' loaded from '" +
                            file_path + "'");
}

void Ddl_executor::do_execute(const Dump_rest_project &s, Statement_result *) {
  set_failure_context("Failed to execute DUMP REST PROJECT `" + s.name + "`.");
  throw std::runtime_error(
      "DUMP REST PROJECT is not supported by this version of MariaDB Shell yet.");
}

void Ddl_executor::do_execute(const Load_rest_project &s, Statement_result *) {
  set_failure_context("Failed to execute LOAD REST PROJECT from `" + s.directory +
                      "`.");
  throw std::runtime_error(
      "LOAD REST PROJECT is not supported by this version of MariaDB Shell yet.");
}

}  // namespace mrs
