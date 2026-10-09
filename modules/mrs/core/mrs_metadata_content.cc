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

#include "modules/mrs/core/mrs_metadata_content.h"
#include "modules/mrs/core/mrs_scripts.h"
#include "modules/mrs/core/mrs_strings.h"

#include <cstdint>
#include <stdexcept>

namespace mrs {
namespace metadata {

using sql::Value;

namespace {

// -- Content set rows -----------------------------------------------------

const char *k_content_set_select = R"(
SELECT cs.id, cs.service_id, cs.request_path, cs.requires_auth,
    cs.enabled, cs.internal, cs.comments, cs.options,
    CONCAT(h.name, se.url_context_root) AS host_ctx,
    cs.content_type
FROM `mysql_rest_service_metadata`.`content_set` cs
    LEFT OUTER JOIN `mysql_rest_service_metadata`.`service` se
        ON se.id = cs.service_id
    LEFT JOIN `mysql_rest_service_metadata`.`url_host` h
        ON se.url_host_id = h.id
)";

Content_set content_set_from_row(const Db_row &row) {
  Content_set cs;
  cs.id = row["id"].as_string();
  cs.service_id = row["service_id"].as_string();
  cs.content_type = row["content_type"].as_string();
  cs.request_path = row["request_path"].as_string();
  cs.requires_auth = row["requires_auth"].as_bool();
  cs.enabled = static_cast<int>(row["enabled"].as_int());
  cs.internal = row["internal"].as_bool();
  cs.comments = row["comments"].as_optional_string();
  cs.options = row["options"].as_optional_string();
  cs.host_ctx = row["host_ctx"].as_string();
  return cs;
}

std::vector<Content_set> query_content_sets(Db_session *session,
                                            const std::string &where,
                                            std::vector<Value> params) {
  std::string sql = k_content_set_select;
  if (!where.empty()) sql += " WHERE " + where;
  sql += " ORDER BY cs.request_path";

  std::vector<Content_set> content_sets;
  for (const auto &row : session->query(sql, std::move(params)).rows) {
    content_sets.push_back(content_set_from_row(row));
  }
  return content_sets;
}

// -- Content file rows ----------------------------------------------------

std::string content_file_select(bool include_content) {
  return std::string(R"(
SELECT f.id, f.content_set_id, f.request_path, f.requires_auth, f.enabled,
    f.size, f.options,
    cs.request_path AS content_set_request_path,
    CONCAT(h.name, se.url_context_root) AS host_ctx)") +
         (include_content ? ", f.content" : "") + R"(
FROM `mysql_rest_service_metadata`.`content_file` f
    LEFT OUTER JOIN `mysql_rest_service_metadata`.`content_set` cs
        ON cs.id = f.content_set_id
    LEFT OUTER JOIN `mysql_rest_service_metadata`.`service` se
        ON se.id = cs.service_id
    LEFT JOIN `mysql_rest_service_metadata`.`url_host` h
        ON se.url_host_id = h.id
)";
}

Content_file content_file_from_row(const Db_row &row) {
  Content_file f;
  f.id = row["id"].as_string();
  f.content_set_id = row["content_set_id"].as_string();
  f.request_path = row["request_path"].as_string();
  f.requires_auth = row["requires_auth"].as_bool();
  f.enabled = static_cast<int>(row["enabled"].as_int());
  f.size = row["size"].as_int();
  f.options = row["options"].as_optional_string();
  f.content_set_request_path = row["content_set_request_path"].as_string();
  f.host_ctx = row["host_ctx"].as_string();
  if (row.has("content")) f.content = row["content"].as_string();
  return f;
}

std::vector<Content_file> query_content_files(Db_session *session,
                                              const std::string &where,
                                              std::vector<Value> params,
                                              bool include_content) {
  std::string sql = content_file_select(include_content);
  if (!where.empty()) sql += " WHERE " + where;
  sql += " ORDER BY f.request_path";

  std::vector<Content_file> files;
  for (const auto &row : session->query(sql, std::move(params)).rows) {
    files.push_back(content_file_from_row(row));
  }
  return files;
}

// A LONGBLOB value: a hex literal keeps arbitrary bytes out of the
// connection character set.
Value blob_value(const std::string &content) {
  return content.empty() ? Value("") : Value::raw(sql::hex(content));
}

// Whether a file's content can be written as CONTENT '...': text without
// the characters that only a backslash escape can write, so the literal
// reads the same with and without NO_BACKSLASH_ESCAPES (sql::quote then
// only doubles quotes). Everything else is written as base64.
bool is_plain_text(std::string_view content) {
  return is_text(content) &&
         content.find_first_of(std::string_view("\\\0\032", 3)) == std::string_view::npos;
}

// -- SHOW CREATE pieces ---------------------------------------------------

std::string enabled_clause(int enabled) {
  if (enabled == 2) return "\n    PRIVATE";
  if (enabled == 0) return "\n    DISABLED";
  return {};
}

std::string authentication_clause(bool requires_auth) {
  return requires_auth ? "\n    AUTHENTICATION REQUIRED"
                       : "\n    AUTHENTICATION NOT REQUIRED";
}

// The options document of a script set without what the script
// registration generates (it is created again by LOAD SCRIPTS).
std::optional<std::string> written_options(const Content_set &content_set) {
  if (!content_set.options || content_set.content_type != "SCRIPTS") {
    return content_set.options;
  }
  auto doc = json::try_parse(*content_set.options);
  if (!doc || !doc->is_object()) return content_set.options;
  for (const auto key : k_generated_script_options) doc->remove(key);
  if (doc->as_object().empty()) return std::nullopt;
  return doc->dump();
}

}  // namespace

// -- Content sets ---------------------------------------------------------

std::optional<Content_set> get_content_set(Db_session *session, const Id &id) {
  auto sets = query_content_sets(session, "cs.id = ?", {Value::id(id)});
  if (sets.empty()) return std::nullopt;
  return std::move(sets.front());
}

std::optional<Content_set> find_content_set(Db_session *session,
                                            const Id &service_id,
                                            std::string_view request_path) {
  if (request_path.empty() || request_path[0] != '/') {
    throw std::runtime_error("The request_path has to start with '/'.");
  }
  auto sets = query_content_sets(session,
                                 "cs.service_id = ? AND cs.request_path = ?",
                                 {Value::id(service_id), request_path});
  if (sets.empty()) return std::nullopt;
  return std::move(sets.front());
}

std::vector<Content_set> get_content_sets(Db_session *session,
                                          const Id &service_id) {
  return query_content_sets(session, "cs.service_id = ?",
                            {Value::id(service_id)});
}

Id add_content_set(Db_session *session, const Content_set_definition &definition) {
  if (definition.request_path.empty() || definition.request_path[0] != '/') {
    throw std::runtime_error("The request_path has to start with '/'.");
  }

  const bool requires_auth = definition.requires_auth.value_or(false);

  const Id id = definition.id ? *definition.id : new_id(session);
  sql::Insert insert("content_set");
  insert.set("id", Value::id(id));
  insert.set("service_id", Value::id(definition.service_id));
  insert.set("request_path", definition.request_path);
  insert.set("requires_auth", requires_auth);
  insert.set("enabled", definition.enabled.value_or(1));
  insert.set("comments", definition.comments.value_or(""));
  insert.set("options", definition.options);
  insert.set("content_type", definition.content_type);
  session->execute(insert);

  return id;
}

void update_content_set(Db_session *session, const Id &id,
                        const Content_set_changes &changes) {
  sql::Update update("content_set");
  update.set_if("request_path", changes.request_path);
  update.set_if("requires_auth", changes.requires_auth);
  update.set_if("enabled", changes.enabled);
  update.set_if("comments", changes.comments);
  update.set_if("content_type", changes.content_type);
  if (changes.options) {
    set_json_options(session, &update, "content_set", id, *changes.options,
                     changes.merge_options);
  }
  if (update.empty()) return;
  update.where("id", Value::id(id));
  session->execute(update);
}

void delete_registered_scripts(Db_session *session, const Content_set &content_set) {
  // Deleting the links deletes their SCRIPT objects (AFTER DELETE trigger)
  session->execute(sql::Delete("content_set_has_obj_def")
                       .where("content_set_id", Value::id(content_set.id)));

  // The script modules left without objects go as well. They are looked up
  // first: a DELETE on db_schema whose subquery reads db_object fails with
  // 1442, as the db_schema trigger deletes from db_object.
  std::string placeholders;
  std::vector<Value> empty_modules;
  for (const auto &row :
       session
           ->query("SELECT id FROM " + sql::metadata_table("db_schema") +
                       " WHERE service_id = ? AND schema_type = 'SCRIPT_MODULE' "
                       "AND id NOT IN (SELECT db_schema_id FROM " +
                       sql::metadata_table("db_object") + ")",
                   {Value::id(content_set.service_id)})
           .rows) {
    placeholders += placeholders.empty() ? "?" : ", ?";
    empty_modules.push_back(Value::id(row["id"].as_string()));
  }
  if (!empty_modules.empty()) {
    session->execute(sql::Delete("db_schema")
                         .where_raw("id IN (" + placeholders + ")",
                                    std::move(empty_modules)));
  }
}

void delete_content_set(Db_session *session, const Id &id) {
  const auto content_set = get_content_set(session, id);
  if (content_set) delete_registered_scripts(session, *content_set);

  if (session->execute(sql::Delete("content_set").where("id", Value::id(id))) ==
      0) {
    throw std::runtime_error("The specified content_set with id " +
                             id + " was not found.");
  }
}

// -- Content files --------------------------------------------------------

std::optional<Content_file> get_content_file(Db_session *session, const Id &id,
                                             bool include_content) {
  auto files = query_content_files(session, "f.id = ?", {Value::id(id)},
                                   include_content);
  if (files.empty()) return std::nullopt;
  return std::move(files.front());
}

std::optional<Content_file> find_content_file(Db_session *session,
                                              const Id &content_set_id,
                                              std::string_view request_path,
                                              bool include_content) {
  auto files = query_content_files(
      session,
      "f.content_set_id = ? AND f.request_path = ?",
      {Value::id(content_set_id), request_path}, include_content);
  if (files.empty()) return std::nullopt;
  return std::move(files.front());
}

std::vector<Content_file> get_content_files(Db_session *session,
                                            const Id &content_set_id,
                                            bool include_content) {
  return query_content_files(
      session, "f.content_set_id = ?", {Value::id(content_set_id)},
      include_content);
}

Id add_content_file(Db_session *session,
                    const Content_file_definition &definition) {
  const Id id = new_id(session);
  sql::Insert insert("content_file");
  insert.set("id", Value::id(id));
  insert.set("content_set_id", Value::id(definition.content_set_id));
  insert.set("request_path", definition.request_path);
  insert.set("requires_auth", definition.requires_auth.value_or(false));
  insert.set("enabled", definition.enabled.value_or(1));
  insert.set("content", blob_value(definition.content));
  insert.set("options", definition.options);
  session->execute(insert);
  return id;
}

void delete_content_file(Db_session *session, const Id &id) {
  if (session->execute(sql::Delete("content_file").where("id", Value::id(id))) ==
      0) {
    throw std::runtime_error("The specified REST content file with id " +
                             id + " was not found.");
  }
}

std::string content_set_create_statement(const Content_set &content_set,
                                         bool on_current_service) {
  std::string output = "CREATE OR REPLACE REST CONTENT SET " +
                       quote_request_path(content_set.request_path);
  if (!on_current_service) output += "\n    ON SERVICE " + content_set.host_ctx;
  output += enabled_clause(content_set.enabled);
  if (content_set.comments && !content_set.comments->empty()) {
    output += "\n    COMMENT " + sql::quote(*content_set.comments);
  }
  const auto options = format_json_entry("OPTIONS", written_options(content_set));
  if (!options.empty()) output += "\n" + options;
  output += authentication_clause(content_set.requires_auth);
  return output + ";";
}

std::string content_file_create_statement(Db_session *session,
                                          const Content_file &content_file,
                                          bool on_current_service) {
  // The content is loaded only when the given file has none; either way it
  // is not copied (files can be large).
  std::optional<Content_file> with_content;
  if (!content_file.content) {
    with_content = get_content_file(session, content_file.id, true);
    if (!with_content) {
      throw std::runtime_error("The REST content file " +
                               content_file.request_path + " was not found.");
    }
  }
  static const std::string k_no_content;
  const std::string &content =
      content_file.content
          ? *content_file.content
          : (with_content->content ? *with_content->content : k_no_content);

  std::string output = "CREATE OR REPLACE REST CONTENT FILE " +
                       quote_request_path(content_file.request_path) +
                       "\n    ON " +
                       (on_current_service
                            ? ""
                            : "SERVICE " + quote_request_path(content_file.host_ctx) + " ") +
                       "CONTENT SET " +
                       quote_request_path(content_file.content_set_request_path);
  if (is_plain_text(content)) {
    output += "\n    CONTENT " + sql::quote(content);
  } else {
    output += "\n    BINARY CONTENT '" + base64_encode(content) + "'";
  }
  output += enabled_clause(content_file.enabled);
  const auto options = format_json_entry("OPTIONS", content_file.options);
  if (!options.empty()) output += "\n" + options;
  output += authentication_clause(content_file.requires_auth);
  return output + ";";
}

std::vector<std::string> content_set_statements(Db_session *session,
                                                const Content_set &content_set,
                                                bool on_current_service) {
  std::vector<std::string> statements{
      content_set_create_statement(content_set, on_current_service)};
  for (const auto &file : get_content_files(session, content_set.id, true)) {
    statements.push_back(content_file_create_statement(session, file, on_current_service));
  }
  if (content_set.content_type == "SCRIPTS") {
    statements.push_back(
        "ALTER REST CONTENT SET " + quote_request_path(content_set.request_path) +
        (on_current_service ? "" : "\n    ON SERVICE " + content_set.host_ctx) +
        "\n    LOAD TYPESCRIPT SCRIPTS;");
  }
  return statements;
}

std::vector<std::string> content_set_create_statements(Db_session *session,
                                                       const Id &service_id,
                                                       bool include_dynamic) {
  std::vector<std::string> statements;
  for (const auto &content_set : get_content_sets(session, service_id)) {
    if (content_set.content_type == "SCRIPTS" && !include_dynamic) continue;
    for (auto &statement : content_set_statements(session, content_set, true)) {
      statements.push_back(std::move(statement));
    }
  }
  return statements;
}

Id clone_content_set(Db_session *session, const Content_set &content_set,
                     const Id &new_service_id) {
  Content_set_definition definition;
  definition.service_id = new_service_id;
  definition.request_path = content_set.request_path;
  definition.requires_auth = content_set.requires_auth;
  definition.enabled = content_set.enabled;
  definition.comments = content_set.comments;
  definition.options = content_set.options;
  definition.content_type = content_set.content_type;

  const Id new_set_id = add_content_set(session, definition);

  // The files are copied on the server: their content never travels to the
  // client and back. The ids come from the column default (UUID_v7()).
  const auto table = sql::metadata_table("content_file");
  session->execute(
      "INSERT INTO " + table +
          " (content_set_id, request_path, requires_auth, enabled, content, options)"
          " SELECT ?, request_path, requires_auth, enabled, content, options FROM " +
          table + " WHERE content_set_id = ?",
      {Value::id(new_set_id), Value::id(content_set.id)});
  return new_set_id;
}

// -- Helpers --------------------------------------------------------------

bool is_text(std::string_view data) {
  size_t other = 0;
  for (const unsigned char c : data) {
    if ((c >= 32 && c < 127) || c == '\n' || c == '\r' || c == '\t' ||
        c == '\b') {
      continue;
    }
    if (c == 0) return false;
    ++other;
  }
  // 30% or more bytes outside the text range make it binary; so does an
  // empty content, which keeps the Python plugin's behaviour.
  return other * 10 < data.size() * 3;
}

}  // namespace metadata
}  // namespace mrs
