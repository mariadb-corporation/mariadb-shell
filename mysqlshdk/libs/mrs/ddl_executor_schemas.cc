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

#include "mysqlshdk/libs/mrs/ddl_executor.h"
#include "mysqlshdk/libs/mrs/metadata_json.h"

namespace mrs {

using namespace ast;

void Ddl_executor::do_execute(const Create_rest_schema &s, Statement_result *r) {
  const std::string request_path =
      s.schema_path ? *s.schema_path : "/" + s.schema_name;
  const auto full_path = full_service_path(s.service, request_path);
  set_failure_context("Failed to create the REST SCHEMA `" + full_path + "`.");

  Db_transaction transaction(m_session);

  const auto service_id = require_service(s.service);

  // OR REPLACE writes an existing schema anew in place: it keeps its id and
  // its REST objects
  const auto existing = find_existing(s.flags, [&] {
    return metadata::find_schema(m_session, service_id, request_path);
  });
  if (existing && s.flags.if_not_exists) {
    r->message = "REST SCHEMA `" + full_path + "` created successfully.";
    transaction.commit();
    return;
  }
  // The REST schema of an MRS module belongs to the content set its scripts
  // are registered from; it cannot become a database schema in place
  if (existing && existing->schema_type != "DATABASE_SCHEMA") {
    throw std::runtime_error("The request path " + request_path +
                             " is used by the REST schema of an MRS module.");
  }

  metadata::Schema_definition definition;
  definition.service_id = service_id;
  definition.name = s.schema_name;
  definition.request_path = request_path;
  definition.requires_auth = s.options.requires_auth;
  if (s.options.enabled) definition.enabled = static_cast<int>(*s.options.enabled);
  definition.items_per_page = s.options.items_per_page;
  definition.comments = s.options.comments;
  if (s.options.options) definition.options = s.options.options->value;
  definition.metadata = s.options.metadata;

  // The first schema of the current service becomes the current schema
  bool becomes_current = false;
  Id id;
  if (existing) {
    metadata::replace_schema(m_session, existing->id, definition);
  } else {
    id = metadata::add_schema(m_session, definition);
    becomes_current = m_state->current_service_id == service_id &&
                      metadata::get_schemas(m_session, service_id).size() == 1;
  }

  transaction.commit();

  // Only after the commit: a failing statement leaves the state as it was
  if (becomes_current) {
    m_state->current_schema_id = id;
    m_state->current_schema = request_path;
  }

  r->message = "REST SCHEMA `" + full_path + "` created successfully.";
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
}

void Ddl_executor::do_execute(const Drop_rest_schema &s, Statement_result *r) {
  const auto full_path = full_service_path(s.service, s.schema_path);
  set_failure_context("Failed to drop the REST SCHEMA `" + full_path + "`.");

  Db_transaction transaction(m_session);

  // IF EXISTS: a missing service is nothing to do, as a missing schema
  std::optional<metadata::Schema> schema;
  if (const auto service_id = s.if_exists ? find_service_if_exists(s.service)
                                          : require_service(s.service)) {
    schema = metadata::find_schema(m_session, *service_id, s.schema_path);
  }
  // FOR DATABASE: only a REST schema still on that database schema, so that
  // a deployment script can drop a schema that moved to another database
  // on every deployment, without dropping it again once it has moved
  if (schema && s.for_database &&
      !metadata::same_database_schema(m_session, schema->name, *s.for_database)) {
    schema.reset();
  }
  if (!schema && !s.if_exists) {
    throw std::runtime_error("The given REST SCHEMA `" + full_path +
                             "` could not be found.");
  }
  if (schema) metadata::delete_schema(m_session, schema->id);

  transaction.commit();
  if (schema && m_state->current_schema_id == schema->id) m_state->clear_schema();
  r->message = "REST SCHEMA `" + full_path + "` dropped successfully.";
}

void Ddl_executor::do_execute(const Show_rest_schemas &s, Statement_result *r) {
  set_failure_context("Cannot SHOW the REST schemas.");

  const auto service_id = require_service(s.service);

  if (s.format == Output_format::json) {
    json::Value::Array docs;
    for (const auto &schema : metadata::get_schemas(m_session, service_id)) {
      docs.push_back(metadata::schema_json(schema));
    }
    r->set_json("REST SCHEMAS", json::Value(std::move(docs)).dump(true));
    return;
  }

  r->columns = {"REST schema path", "enabled"};
  for (const auto &schema : metadata::get_schemas(m_session, service_id)) {
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
}

}  // namespace mrs
