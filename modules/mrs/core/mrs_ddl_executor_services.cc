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

// The REST SERVICE statements: CREATE, ALTER, DROP, CLONE, SHOW and SHOW
// CREATE.

#include <stdexcept>

#include "modules/mrs/core/mrs_ddl_executor.h"
#include "modules/mrs/core/mrs_metadata_auth.h"
#include "modules/mrs/core/mrs_metadata_json.h"

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

}  // namespace

void Ddl_executor::link_auth_apps(const Id &service_id,
                                  const std::vector<Auth_app_reference> &add,
                                  const std::vector<Auth_app_reference> &remove) {
  const auto apply = [&](const std::vector<Auth_app_reference> &entries,
                          auto action) {
    for (const auto &entry : entries) {
      const auto auth_app = metadata::find_auth_app(m_session, entry.name);
      if (!auth_app && !entry.if_exists) {
        throw std::runtime_error("The given REST authentication app `" +
                                 entry.name + "` was not found.");
      }
      if (auth_app) action(m_session, auth_app->id, service_id);
    }
  };
  apply(add, metadata::link_auth_app);
  apply(remove, metadata::unlink_auth_app);
}

void Ddl_executor::do_execute(const Create_rest_service &s, Statement_result *r) {
  const auto path = s.path.path;
  const auto full_path = metadata::format_developers(s.path.developers) + path;
  set_failure_context("Failed to create the REST SERVICE `" + full_path + "`.");

  Db_transaction transaction(m_session);

  if (keep_existing(
          s.flags,
          [&] { return metadata::find_service(m_session, path, s.path.developers); },
          [&](const auto &existing) {
            metadata::delete_service(m_session, existing.id);
          })) {
    r->message = "REST SERVICE `" + full_path + "` created successfully.";
    transaction.commit();
    return;
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
}

void Ddl_executor::do_execute(const Alter_rest_service &s, Statement_result *r) {
  const auto full_path = full_service_path(s.path);
  set_failure_context("Failed to update the REST SERVICE `" + full_path + "`.");

  const auto service_id = require_service(s.path);

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
  metadata::update_service(m_session, service_id, changes);
  link_auth_apps(service_id, s.options.add_auth_apps, s.options.remove_auth_apps);
  transaction.commit();

  // Keep the current service in step with a renamed one
  if (m_state->current_service_id == service_id) {
    if (const auto updated = metadata::get_service(m_session, service_id)) {
      const auto schema_id = m_state->current_schema_id;
      const auto schema = m_state->current_schema;
      set_current_service(*updated);
      m_state->current_schema_id = schema_id;
      m_state->current_schema = schema;
    }
  }

  r->affected_items_count = 1;
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
  }

  transaction.commit();
  r->message = "REST SERVICE `" + full_path + "` dropped successfully.";
}

void Ddl_executor::do_execute(const Clone_rest_service &s, Statement_result *r) {
  const auto full_path = full_service_path(s.path);
  set_failure_context("Failed to clone the REST SERVICE `" + full_path + "`.");

  const auto service = metadata::get_service(m_session, require_service(s.path));
  if (!service) throw std::runtime_error("The given REST SERVICE was not found.");

  Db_transaction transaction(m_session);
  metadata::clone_service(m_session, *service, s.new_path.path,
                          s.new_path.developers);
  transaction.commit();

  r->affected_items_count = 1;
}

void Ddl_executor::do_execute(const Show_rest_services &s, Statement_result *r) {
  set_failure_context("Cannot SHOW the REST services.");

  // FOR AUTH APP limits the list to the services the auth app is linked to,
  // FOR DAEMON to the ones the daemon serves
  std::vector<metadata::Service> services;
  if (s.daemon) {
    if (!metadata::get_daemon(m_session, *s.daemon)) {
      throw std::runtime_error("The given REST DAEMON `" + *s.daemon +
                               "` could not be found.");
    }
    services = metadata::get_services_of_daemon(m_session, *s.daemon);
  } else if (s.auth_app) {
    const auto auth_app = metadata::find_auth_app(m_session, *s.auth_app);
    if (!auth_app) {
      throw std::runtime_error("The given REST AUTH APP `" + *s.auth_app +
                               "` could not be found.");
    }
    services = metadata::get_services_of_auth_app(m_session, auth_app->id);
  } else {
    services = metadata::get_services(m_session);
  }

  r->columns = {"REST SERVICE Path", "enabled", "current", "auth_apps"};
  for (const auto &service : services) {
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

void Ddl_executor::do_execute(const Show_rest_daemons &s, Statement_result *r) {
  set_failure_context("Cannot SHOW the REST daemons.");

  const auto daemons = metadata::get_daemons(m_session);
  if (s.format == Output_format::json) {
    json::Value::Array docs;
    for (const auto &daemon : daemons) docs.push_back(metadata::daemon_json(daemon));
    r->columns = {"REST DAEMONS"};
    r->add_row().emplace_back(json::Value(std::move(docs)).dump(true));
    return;
  }

  r->columns = {"id",      "name",          "address", "product_name",
                "version", "last_check_in", "active",  "developer"};
  const auto text = [](const std::optional<std::string> &value) {
    return value ? Db_value(*value) : Db_value(nullptr);
  };
  for (const auto &daemon : daemons) {
    auto &row = r->add_row();
    row.emplace_back(daemon.id);
    row.emplace_back(daemon.name);
    row.emplace_back(daemon.address);
    row.emplace_back(daemon.product_name);
    row.push_back(text(daemon.version));
    row.push_back(text(daemon.last_check_in));
    row.emplace_back(daemon.active ? "YES" : "NO");
    row.push_back(text(daemon.developer));
  }
}

void Ddl_executor::do_execute(const Drop_rest_daemon &s, Statement_result *r) {
  const auto &id = s.id;
  set_failure_context("Failed to drop the REST DAEMON `" + id + "`.");

  Db_transaction transaction(m_session);
  if (!metadata::get_daemon(m_session, s.id)) {
    if (!s.if_exists) {
      throw std::runtime_error("The given REST DAEMON `" + id +
                               "` could not be found.");
    }
  } else {
    metadata::delete_daemon(m_session, s.id);
    r->affected_items_count = 1;
  }
  transaction.commit();
  r->message = "REST DAEMON `" + id + "` dropped successfully.";
}

void Ddl_executor::do_execute(const Show_create_rest_service &s,
                              Statement_result *r) {
  const auto full_path = full_service_path(s.path);
  set_failure_context("Failed to get the REST SERVICE `" + full_path + "`.");

  const auto service = metadata::get_service(m_session, require_service(s.path));
  if (!service) throw std::runtime_error("The given REST SERVICE was not found.");

  r->columns = {"CREATE REST SERVICE"};
  r->add_row().emplace_back(
      s.format == Output_format::json
          ? metadata::service_json(m_session, *service, s.endpoints.database)
                .dump(true)
          : metadata::service_create_statement(m_session, *service,
                                               s.endpoints.database,
                                               s.endpoints.static_,
                                               s.endpoints.dynamic));
}

}  // namespace mrs
