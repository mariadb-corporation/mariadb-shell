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

// The REST CONTENT SET and CONTENT FILE statements: CREATE, ALTER, DROP,
// SHOW and SHOW CREATE. The content arrives inline (CONTENT, BINARY
// CONTENT); ALTER ... LOAD SCRIPTS registers the MRS scripts of the stored
// files (mrs_scripts).

#include <stdexcept>

#include "modules/mrs/core/mrs_ddl_executor.h"
#include "modules/mrs/core/mrs_metadata_content.h"
#include "modules/mrs/core/mrs_metadata_json.h"
#include "modules/mrs/core/mrs_scripts.h"
#include "modules/mrs/core/mrs_strings.h"

namespace mrs {

using namespace ast;

namespace {

// The OPTIONS of the statement, if any.
std::optional<std::string> content_set_options(const Content_set_options &options) {
  if (!options.options) return std::nullopt;
  return options.options->value;
}

std::string join_statements(const std::vector<std::string> &statements) {
  std::string result;
  for (const auto &statement : statements) {
    if (!result.empty()) result += "\n\n";
    result += statement;
  }
  return result;
}

}  // namespace

// -- CONTENT SET ----------------------------------------------------------

void Ddl_executor::do_execute(const Create_rest_content_set &s,
                              Statement_result *r) {
  const auto full_path = full_service_path(s.service, s.path);
  set_failure_context("Failed to create the REST CONTENT SET `" + full_path +
                      "`.");

  Db_transaction transaction(m_session);

  const auto service_id = require_service(s.service);

  if (s.flags.or_replace || s.flags.if_not_exists) {
    const auto existing = metadata::find_content_set(m_session, service_id, s.path);
    if (existing) {
      if (s.flags.if_not_exists) {
        r->message = "REST content set `" + full_path + "` created successfully.";
        r->id = existing->id;
        transaction.commit();
        return;
      }
      metadata::delete_content_set(m_session, existing->id);
    }
  }

  metadata::Content_set_definition definition;
  definition.service_id = service_id;
  definition.request_path = s.path;
  definition.requires_auth = s.options.requires_auth.value_or(true);
  if (s.options.enabled) definition.enabled = static_cast<int>(*s.options.enabled);
  definition.comments = s.options.comments;
  definition.options = content_set_options(s.options);

  const Id id = metadata::add_content_set(m_session, definition);

  transaction.commit();

  r->message = "REST content set `" + full_path + "` created successfully.";
  r->id = id;
}

void Ddl_executor::do_execute(const Alter_rest_content_set &s,
                              Statement_result *r) {
  const auto full_path = full_service_path(s.service, s.path);
  set_failure_context("Failed to update the REST content set `" + full_path +
                      "`.");

  Db_transaction transaction(m_session);

  const auto service_id = require_service(s.service);
  const auto content_set = metadata::find_content_set(m_session, service_id, s.path);
  if (!content_set) {
    throw std::runtime_error("The given REST content set `" + full_path +
                             "` could not be found.");
  }

  metadata::Content_set_changes changes;
  changes.request_path = s.new_path;
  changes.requires_auth = s.options.requires_auth;
  if (s.options.enabled) changes.enabled = static_cast<int>(*s.options.enabled);
  changes.comments = s.options.comments;
  changes.options = content_set_options(s.options);
  if (s.options.options) changes.merge_options = s.options.options->merge;

  metadata::update_content_set(m_session, content_set->id, changes);

  // LOAD SCRIPTS: analyse the stored files and register their MRS scripts
  if (s.options.load_scripts) {
    const auto updated = metadata::get_content_set(m_session, content_set->id);
    const auto registered = metadata::register_scripts(m_session, *updated);
    r->message = "REST content set `" + full_path + "` updated successfully. " +
                 std::to_string(registered.scripts) + " MRS script(s) of " +
                 std::to_string(registered.modules) + " module(s) registered.";
  }

  transaction.commit();

  r->affected_items_count = 1;
  r->id = content_set->id;
}

void Ddl_executor::do_execute(const Drop_rest_content_set &s,
                              Statement_result *r) {
  const auto full_path = full_service_path(s.service, s.path);
  set_failure_context("Failed to drop the REST CONTENT SET `" + full_path +
                      "`.");

  Db_transaction transaction(m_session);

  const auto service_id = require_service(s.service);
  const auto content_set = metadata::find_content_set(m_session, service_id, s.path);
  if (!content_set && !s.if_exists) {
    throw std::runtime_error("The given REST CONTENT SET `" + full_path +
                             "` could not be found.");
  }
  if (content_set) {
    metadata::delete_content_set(m_session, content_set->id);
    r->id = content_set->id;
  }

  transaction.commit();
  r->message = "REST CONTENT SET `" + full_path + "` dropped successfully.";
}

void Ddl_executor::do_execute(const Show_rest_content_sets &s,
                              Statement_result *r) {
  set_failure_context("Cannot SHOW the REST CONTENT SETs.");

  const auto service_id = require_service(s.service);

  r->columns = {"REST CONTENT SET path", "enabled"};
  for (const auto &content_set : metadata::get_content_sets(m_session, service_id)) {
    auto &row = r->add_row();
    row.emplace_back(content_set.request_path);
    row.emplace_back(metadata::enabled_caption(content_set.enabled));
  }
}

void Ddl_executor::do_execute(const Show_create_rest_content_set &s,
                              Statement_result *r) {
  const auto full_path = full_service_path(s.service, s.path);
  set_failure_context("Failed to get the REST CONTENT SET `" + full_path + "`.");

  const auto service_id = require_service(s.service);
  const auto content_set = metadata::find_content_set(m_session, service_id, s.path);
  if (!content_set) {
    throw std::runtime_error("The given REST content set `" + full_path +
                             "` could not be found.");
  }

  r->columns = {"CREATE REST CONTENT SET"};
  r->add_row().emplace_back(
      s.format == Output_format::json
          ? metadata::content_set_json(*content_set).dump(true)
          : join_statements(
                metadata::content_set_statements(m_session, *content_set)));
  r->id = content_set->id;
}

// -- CONTENT FILE ---------------------------------------------------------

void Ddl_executor::do_execute(const Create_rest_content_file &s,
                              Statement_result *r) {
  const auto full_path =
      full_service_path(s.service, s.content_set_path + s.path);
  set_failure_context("Failed to create the REST CONTENT FILE `" + full_path +
                      "`.");

  Db_transaction transaction(m_session);

  const auto service_id = require_service(s.service);
  const auto content_set =
      metadata::find_content_set(m_session, service_id, s.content_set_path);
  if (!content_set) {
    throw std::runtime_error("CONTENT SET " + s.content_set_path + " not found.");
  }

  // The metadata schema has no unique index on the file paths of a set
  if (const auto existing =
          metadata::find_content_file(m_session, content_set->id, s.path, false)) {
    if (!s.flags.or_replace && !s.flags.if_not_exists) {
      throw std::runtime_error(
          "The request_path is already used by another entity.");
    }
    if (s.flags.if_not_exists) {
      r->message = "REST CONTENT FILE `" + full_path + "` created successfully.";
      r->id = existing->id;
      transaction.commit();
      return;
    }
    metadata::delete_content_file(m_session, existing->id);
  }

  metadata::Content_file_definition definition;
  definition.content_set_id = content_set->id;
  definition.request_path = s.path;
  definition.requires_auth = s.options.requires_auth.value_or(true);
  if (s.options.enabled) definition.enabled = static_cast<int>(*s.options.enabled);
  if (s.options.options) definition.options = s.options.options->value;

  if (s.binary) {
    definition.content = base64_decode(s.content.value_or(""));
  } else {
    definition.content = s.content.value_or("");
  }

  const Id id = metadata::add_content_file(m_session, definition);

  transaction.commit();

  r->message = "REST CONTENT FILE `" + full_path + "` created successfully.";
  r->id = id;
}

void Ddl_executor::do_execute(const Drop_rest_content_file &s,
                              Statement_result *r) {
  const auto full_path =
      full_service_path(s.service, s.content_set_path + s.path);
  set_failure_context("Failed to drop the REST CONTENT FILE `" + full_path +
                      "`.");

  Db_transaction transaction(m_session);

  const auto service_id = require_service(s.service);
  const auto content_set =
      metadata::find_content_set(m_session, service_id, s.content_set_path);
  if (!content_set && !s.if_exists) {
    throw std::runtime_error("The REST content set " + s.content_set_path +
                             " was not found.");
  }

  std::optional<metadata::Content_file> content_file;
  if (content_set) {
    content_file =
        metadata::find_content_file(m_session, content_set->id, s.path, false);
    if (!content_file && !s.if_exists) {
      throw std::runtime_error("The REST content file " + full_path +
                               " was not found.");
    }
  }
  if (content_file) {
    metadata::delete_content_file(m_session, content_file->id);
    r->id = content_file->id;
  }

  transaction.commit();
  r->message = "REST CONTENT FILE `" + full_path + "` dropped successfully.";
}

void Ddl_executor::do_execute(const Show_rest_content_files &s,
                              Statement_result *r) {
  const auto full_path = full_service_path(s.service, s.content_set_path);
  set_failure_context("Cannot SHOW the REST CONTENT FILEs.");

  const auto service_id = require_service(s.service);
  const auto content_set =
      metadata::find_content_set(m_session, service_id, s.content_set_path);
  if (!content_set) {
    throw std::runtime_error("The given REST content set `" + full_path +
                             "` could not be found.");
  }

  r->columns = {"REST CONTENT FILE path", "size", "enabled"};
  for (const auto &file :
       metadata::get_content_files(m_session, content_set->id, false)) {
    auto &row = r->add_row();
    row.emplace_back(file.request_path);
    row.emplace_back(file.size);
    row.emplace_back(metadata::enabled_caption(file.enabled));
  }
}

void Ddl_executor::do_execute(const Show_create_rest_content_file &s,
                              Statement_result *r) {
  const auto full_path =
      full_service_path(s.service, s.content_set_path + s.path);
  set_failure_context("Failed to get the REST CONTENT FILE `" + full_path +
                      "`.");

  const auto service_id = require_service(s.service);
  const auto content_set =
      metadata::find_content_set(m_session, service_id, s.content_set_path);
  if (!content_set) {
    throw std::runtime_error("The given REST content set `" +
                             full_service_path(s.service, s.content_set_path) +
                             "` could not be found.");
  }
  const auto content_file =
      metadata::find_content_file(m_session, content_set->id, s.path, true);
  if (!content_file) {
    throw std::runtime_error("The given REST content file `" + full_path +
                             "` could not be found.");
  }

  r->columns = {"CREATE REST CONTENT FILE"};
  r->add_row().emplace_back(
      s.format == Output_format::json
          ? metadata::content_file_json(*content_file).dump(true)
          : metadata::content_file_create_statement(m_session, *content_file));
  r->id = content_file->id;
}

}  // namespace mrs
