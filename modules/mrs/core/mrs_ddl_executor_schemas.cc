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

// The REST SCHEMA statements: CREATE, ALTER, DROP, SHOW and SHOW CREATE.

#include <stdexcept>

#include "modules/mrs/core/mrs_ddl_executor.h"
#include "modules/mrs/core/mrs_metadata_json.h"

namespace mrs {

using namespace ast;

void Ddl_executor::do_execute(const Create_rest_schema &s, Statement_result *r) {
  const std::string request_path =
      s.schema_path ? *s.schema_path : "/" + s.schema_name;
  const auto full_path = full_service_path(s.service, request_path);
  set_failure_context("Failed to create the REST SCHEMA `" + full_path + "`.");

  Db_transaction transaction(m_session);

  const auto service = require_service(s.service);

  if (s.flags.or_replace || s.flags.if_not_exists) {
    const auto existing = metadata::find_schema(m_session, service.id, request_path);
    if (existing) {
      if (s.flags.if_not_exists) {
        r->message = "REST SCHEMA `" + full_path + "` created successfully.";
        r->id = existing->id;
        transaction.commit();
        return;
      }
      metadata::delete_schema(m_session, existing->id);
      if (m_state->current_schema_id == existing->id) m_state->clear_schema();
    }
  }

  metadata::Schema_definition definition;
  definition.service_id = service.id;
  definition.name = s.schema_name;
  definition.request_path = request_path;
  definition.requires_auth = s.options.requires_auth;
  if (s.options.enabled) definition.enabled = static_cast<int>(*s.options.enabled);
  definition.items_per_page = s.options.items_per_page;
  definition.comments = s.options.comments;
  if (s.options.options) definition.options = s.options.options->value;
  definition.metadata = s.options.metadata;

  const Id id = metadata::add_schema(m_session, definition);

  // The first schema of the current service becomes the current schema
  if (m_state->current_service_id == service.id &&
      metadata::get_schemas(m_session, service.id).size() == 1) {
    m_state->current_schema_id = id;
    m_state->current_schema = request_path;
  }

  transaction.commit();

  r->message = "REST SCHEMA `" + full_path + "` created successfully.";
  r->id = id;
}

void Ddl_executor::do_execute(const Alter_rest_schema &s, Statement_result *r) {
  std::optional<Schema_selector> selector;
  if (s.schema_path) selector = Schema_selector{s.service, *s.schema_path};
  const auto full_path = full_schema_path(selector);
  set_failure_context("Failed to update the REST SCHEMA `" + full_path + "`.");

  const auto schema = require_schema(selector);

  metadata::Schema_changes changes;
  changes.name = s.schema_name;
  changes.request_path = s.new_path;
  changes.requires_auth = s.options.requires_auth;
  if (s.options.enabled) changes.enabled = static_cast<int>(*s.options.enabled);
  changes.items_per_page = s.options.items_per_page;
  changes.comments = s.options.comments;
  if (s.options.options) {
    changes.options = s.options.options->value;
    changes.merge_options = s.options.options->merge;
  }
  changes.metadata = s.options.metadata;

  Db_transaction transaction(m_session);
  metadata::update_schema(m_session, schema.id, changes);
  transaction.commit();

  if (m_state->current_schema_id == schema.id && s.new_path) {
    m_state->current_schema = *s.new_path;
  }

  r->affected_items_count = 1;
  r->id = schema.id;
}

void Ddl_executor::do_execute(const Drop_rest_schema &s, Statement_result *r) {
  const auto full_path = full_service_path(s.service, s.schema_path);
  set_failure_context("Failed to drop the REST SCHEMA `" + full_path + "`.");

  Db_transaction transaction(m_session);

  const auto service = require_service(s.service);
  const auto schema = metadata::find_schema(m_session, service.id, s.schema_path);
  if (!schema && !s.if_exists) {
    throw std::runtime_error("The given REST SCHEMA `" + full_path +
                             "` could not be found.");
  }
  if (schema) {
    metadata::delete_schema(m_session, schema->id);
    if (m_state->current_schema_id == schema->id) m_state->clear_schema();
    r->id = schema->id;
  }

  transaction.commit();
  r->message = "REST SCHEMA `" + full_path + "` dropped successfully.";
}

void Ddl_executor::do_execute(const Show_rest_schemas &s, Statement_result *r) {
  set_failure_context("Cannot SHOW the REST schemas.");

  const auto service = require_service(s.service);

  r->columns = {"REST schema path", "enabled"};
  for (const auto &schema : metadata::get_schemas(m_session, service.id)) {
    auto &row = r->add_row();
    row.emplace_back(schema.request_path);
    row.emplace_back(metadata::enabled_caption(schema.enabled));
  }
}

void Ddl_executor::do_execute(const Show_create_rest_schema &s,
                              Statement_result *r) {
  std::optional<Schema_selector> selector;
  if (s.schema_path) selector = Schema_selector{s.service, *s.schema_path};
  const auto full_path = full_schema_path(selector);
  set_failure_context("Failed to get the REST SCHEMA `" + full_path + "`.");

  const auto schema = require_schema(selector);

  r->columns = {"CREATE REST SCHEMA"};
  r->add_row().emplace_back(
      s.format == Output_format::json
          ? metadata::schema_json(schema).dump(true)
          : metadata::schema_create_statement(m_session, schema, false));
  r->id = schema.id;
}

}  // namespace mrs
