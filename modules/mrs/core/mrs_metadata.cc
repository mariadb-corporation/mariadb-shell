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

#include "modules/mrs/core/mrs_metadata.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "modules/mrs/core/mrs_metadata_auth.h"
#include "modules/mrs/core/mrs_metadata_content.h"
#include "modules/mrs/core/mrs_metadata_db_objects.h"
#include "modules/mrs/core/mrs_strings.h"

namespace mrs {
namespace metadata {

using sql::Value;

namespace {

// The options a new service gets when none are given.
constexpr std::string_view k_default_service_options = R"({
    "headers": {
        "Access-Control-Allow-Credentials": "true",
        "Access-Control-Allow-Headers": "Content-Type, Authorization, X-Requested-With, Origin, X-Auth-Token",
        "Access-Control-Allow-Methods": "GET, POST, PUT, DELETE, OPTIONS"
    },
    "http": {"allowedOrigin": "auto"},
    "logging": {
        "exceptions": true,
        "request": {"body": true, "headers": true},
        "response": {"body": true, "headers": true}
    },
    "returnInternalErrorDetails": true,
    "includeLinksInResults": false
})";

std::vector<std::string> developers_of(const std::optional<std::string> &in_development) {
  std::vector<std::string> developers;
  if (!in_development) return developers;
  const auto doc = json::try_parse(*in_development);
  if (!doc) return developers;
  const auto *list = doc->get("developers");
  if (!list || !list->is_array()) return developers;
  for (const auto &item : list->as_array()) {
    if (item.is_string()) developers.push_back(item.as_string());
  }
  return developers;
}

std::string in_development_json(const std::vector<std::string> &developers) {
  json::Value::Array items;
  for (const auto &developer : developers) items.emplace_back(developer);
  json::Value doc = json::Value::object();
  doc.set("developers", json::Value(std::move(items)));
  return doc.dump();
}

std::vector<std::string> names_of(const std::optional<std::string> &json_array) {
  std::vector<std::string> names;
  if (!json_array) return names;
  const auto doc = json::try_parse(*json_array);
  if (!doc || !doc->is_array()) return names;
  for (const auto &item : doc->as_array()) {
    if (item.is_string()) names.push_back(item.as_string());
  }
  return names;
}

// The column list of the service queries.
const char *k_service_select = R"(
SELECT se.id, se.enabled, se.published, se.url_protocol, h.name AS url_host_name,
    se.url_context_root, se.comments, se.options, se.url_host_id,
    CONCAT(h.name, se.url_context_root) AS host_ctx,
    (SELECT CONCAT(COALESCE(CONCAT(GROUP_CONCAT(IF(item REGEXP '^[A-Za-z0-9_]+$', item, QUOTE(item)) ORDER BY item), '@'), ''), h.name, se.url_context_root) FROM JSON_TABLE(
        JSON_UNQUOTE(JSON_EXTRACT(se.in_development, '$.developers')), '$[*]' COLUMNS (item text path '$')
        ) AS jt) AS full_service_path,
    se.auth_path, se.auth_completed_url,
    se.auth_completed_url_validation,
    se.auth_completed_page_content,
    se.metadata, se.parent_id,
    se.in_development,
    (SELECT GROUP_CONCAT(IF(item REGEXP '^[A-Za-z0-9_]+$', item, QUOTE(item)) ORDER BY item)
        FROM JSON_TABLE(
        JSON_UNQUOTE(JSON_EXTRACT(se.in_development, '$.developers')), '$[*]' COLUMNS (item text path '$')
        ) AS jt) AS sorted_developers,
    se.name,
    (SELECT JSON_ARRAYAGG(aa.name) FROM `mysql_rest_service_metadata`.`service_has_auth_app` sa2
        JOIN `mysql_rest_service_metadata`.`auth_app` AS aa ON
            sa2.auth_app_id = aa.id
    WHERE sa2.service_id = se.id) AS auth_apps
FROM `mysql_rest_service_metadata`.`service` se
    LEFT JOIN `mysql_rest_service_metadata`.url_host h
        ON se.url_host_id = h.id
)";

Service service_from_row(const Db_row &row) {
  Service s;
  s.id = row["id"].as_string();
  s.url_host_id = row["url_host_id"].as_string();
  if (!row["parent_id"].is_null()) s.parent_id = row["parent_id"].as_string();
  s.url_host_name = row["url_host_name"].as_string();
  s.url_context_root = row["url_context_root"].as_string();
  s.url_protocol = row["url_protocol"].as_string();
  s.name = row["name"].as_string();
  s.enabled = static_cast<int>(row["enabled"].as_int());
  s.published = row["published"].as_bool();
  s.comments = row["comments"].as_optional_string();
  s.options = row["options"].as_optional_string();
  s.metadata = row["metadata"].as_optional_string();
  s.auth_path = row["auth_path"].as_string();
  s.auth_completed_url = row["auth_completed_url"].as_optional_string();
  s.auth_completed_url_validation =
      row["auth_completed_url_validation"].as_optional_string();
  s.auth_completed_page_content =
      row["auth_completed_page_content"].as_optional_string();
  s.in_development = row["in_development"].as_optional_string();
  s.developers = developers_of(s.in_development);
  s.host_ctx = row["host_ctx"].as_string();
  s.full_service_path = row["full_service_path"].as_string();
  s.auth_apps = names_of(row["auth_apps"].as_optional_string());
  return s;
}

std::vector<Service> query_services(Db_session *session,
                                    const std::string &where,
                                    const std::string &having = {}) {
  std::string sql = k_service_select;
  if (!where.empty()) sql += " WHERE " + where;
  if (!having.empty()) sql += " HAVING " + having;
  sql += " ORDER BY se.url_context_root, h.name, sorted_developers";

  std::vector<Service> services;
  for (const auto &row : session->query(sql).rows) {
    services.push_back(service_from_row(row));
  }
  return services;
}

Id url_host_id(Db_session *session, const std::string &host_name) {
  const auto result = session->query(
      "SELECT id FROM " + sql::metadata_table("url_host") +
      " WHERE name = " + sql::quote(host_name));
  if (!result.empty()) return result.first()["id"].as_string();

  const Id id = new_id(session);
  session->execute(sql::Insert("url_host")
                       .set("id", Value::id(id))
                       .set("name", host_name)
                       .str());
  return id;
}

Schema schema_from_row(const Db_row &row) {
  Schema s;
  s.id = row["id"].as_string();
  s.service_id = row["service_id"].as_string();
  s.name = row["name"].as_string();
  s.schema_type = row["schema_type"].as_string();
  s.request_path = row["request_path"].as_string();
  s.requires_auth = row["requires_auth"].as_bool();
  s.enabled = static_cast<int>(row["enabled"].as_int());
  s.internal = row["internal"].as_bool();
  if (!row["items_per_page"].is_null()) {
    s.items_per_page = row["items_per_page"].as_int();
  }
  s.comments = row["comments"].as_optional_string();
  s.options = row["options"].as_optional_string();
  s.metadata = row["metadata"].as_optional_string();
  s.host_ctx = row["host_ctx"].as_string();
  return s;
}

const char *k_schema_select = R"(
SELECT sc.id, sc.name, sc.service_id, sc.request_path,
    sc.requires_auth, sc.enabled, sc.items_per_page, sc.comments, se.url_host_id,
    CONCAT(h.name, se.url_context_root) AS host_ctx,
    sc.options, sc.metadata, sc.schema_type, sc.internal
FROM `mysql_rest_service_metadata`.db_schema sc
    LEFT OUTER JOIN `mysql_rest_service_metadata`.service se
        ON se.id = sc.service_id
    LEFT JOIN `mysql_rest_service_metadata`.url_host h
        ON se.url_host_id = h.id
)";

std::vector<Schema> query_schemas(Db_session *session, const std::string &where) {
  std::string sql = k_schema_select;
  if (!where.empty()) sql += " WHERE " + where;
  sql += " ORDER BY sc.request_path";

  std::vector<Schema> schemas;
  for (const auto &row : session->query(sql).rows) {
    schemas.push_back(schema_from_row(row));
  }
  return schemas;
}

}  // namespace

// -- Common ---------------------------------------------------------------

void set_json_options(Db_session *session, sql::Update *update,
                      std::string_view table, const Id &id,
                      const std::string &options, bool merge) {
  if (merge) {
    const auto row = session->query(
        "SELECT options IS NULL AS options_is_null FROM " +
        sql::metadata_table(table) + " WHERE id = " + sql::id(id));
    if (!row.empty() && !row.first()["options_is_null"].as_bool()) {
      update->set_raw("options = JSON_MERGE_PATCH(options, " +
                      sql::quote(options) + ")");
      return;
    }
  }
  update->set("options", options);
}

bool schema_exists(Db_session *session) {
  const auto result = session->query(
      "SELECT COUNT(*) AS schema_exists FROM INFORMATION_SCHEMA.SCHEMATA "
      "WHERE SCHEMA_NAME = " +
      sql::quote(k_metadata_schema));
  return !result.empty() && result.first()["schema_exists"].as_int() > 0;
}

Version schema_version(Db_session *session) {
  for (const auto view : {"msm_schema_version", "schema_version"}) {
    try {
      const auto result = session->query(
          "SELECT major, minor, patch FROM " + sql::metadata_table(view));
      if (result.empty()) continue;
      const auto &row = result.first();
      return Version{static_cast<int>(row["major"].as_int()),
                     static_cast<int>(row["minor"].as_int()),
                     static_cast<int>(row["patch"].as_int())};
    } catch (const Db_error &) {
      // the view of the other naming scheme is tried next
    }
  }
  throw std::runtime_error(
      "Unable to fetch MRS metadata database schema version.");
}

void check_schema(Db_session *session) {
  if (!schema_exists(session)) {
    throw std::runtime_error(
        "The MRS metadata schema `mysql_rest_service_metadata` is not "
        "installed. Run CONFIGURE REST METADATA first.");
  }
  const auto version = schema_version(session);
  if (version.major < k_supported_major_version) {
    throw std::runtime_error(
        "The MRS metadata schema version " + version.str() +
        " is too old to be managed by this version of MariaDB Shell. Run "
        "CONFIGURE REST METADATA UPDATE IF AVAILABLE to update it to version " +
        k_schema_version.str() + ".");
  }
  if (version.major > k_schema_version.major) {
    throw std::runtime_error(
        "This version of MariaDB Shell does not support the MRS metadata "
        "schema version " +
        version.str() + ". Please update MariaDB Shell.");
  }
}

bool row_exists(Db_session *session, std::string_view table, const Id &id) {
  return !session
              ->query("SELECT 1 FROM " + sql::metadata_table(table) +
                      " WHERE id = " + sql::id(id))
              .empty();
}

Id new_id(Db_session *session) {
  const auto result = session->query(
      "SELECT " + sql::metadata_table("get_sequence_id") + "() AS id");
  if (result.empty()) throw std::runtime_error("Could not generate a new id.");
  return result.first()["id"].as_string();
}

std::string enabled_caption(int enabled) {
  if (enabled == 2) return "PRIVATE";
  if (enabled == 1) return "ENABLED";
  return "DISABLED";
}

std::string format_developers(std::vector<std::string> developers) {
  if (developers.empty()) return {};

  // The metadata orders the names case-insensitively (collation order).
  std::sort(developers.begin(), developers.end(),
            [](const std::string &a, const std::string &b) {
              const auto la = to_lower(a);
              const auto lb = to_lower(b);
              return la != lb ? la < lb : a < b;
            });

  std::string result;
  for (const auto &developer : developers) {
    if (!result.empty()) result += ",";
    const bool plain = !developer.empty() &&
                       std::all_of(developer.begin(), developer.end(),
                                   [](unsigned char c) {
                                     return std::isalnum(c) || c == '_';
                                   });
    result += plain ? developer : sql::quote(developer);
  }
  return result + "@";
}

std::string quote_request_path(std::string_view path) {
  if (path.empty() || path[0] != '/') return sql::quote_identifier(path);
  for (const char c : path) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '/')) {
      return sql::quote_identifier(path);
    }
  }
  return std::string(path);
}

std::string format_json_entry(std::string_view key,
                              const std::optional<std::string> &json_text) {
  if (!json_text || json_text->empty()) return {};
  const auto text = json::pretty(*json_text);

  std::string result = "    " + std::string(key) + " ";
  size_t start = 0;
  bool first = true;
  while (start <= text.size()) {
    auto end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    if (!first) result += "\n    ";
    result += text.substr(start, end - start);
    first = false;
    start = end + 1;
  }
  return result;
}

// -- Services -------------------------------------------------------------

std::optional<Service> get_service(Db_session *session, const Id &id) {
  auto services = query_services(session, "se.id = " + sql::id(id));
  if (services.empty()) return std::nullopt;
  return std::move(services.front());
}

std::optional<Service> find_service(Db_session *session,
                                    std::string_view url_context_root,
                                    const std::vector<std::string> &developers) {
  if (url_context_root.empty() || url_context_root[0] != '/') {
    throw std::runtime_error("The url_context_root has to start with '/'.");
  }

  std::string where = "h.name = '' AND se.url_context_root = " +
                      sql::quote(url_context_root);
  std::string having;
  if (developers.empty()) {
    where += " AND se.in_development IS NULL";
  } else {
    auto sorted = format_developers(developers);
    sorted.pop_back();  // the trailing @
    having = "sorted_developers = " + sql::quote(sorted);
  }

  auto services = query_services(session, where, having);
  if (services.size() != 1) return std::nullopt;
  return std::move(services.front());
}

std::vector<Service> get_services(Db_session *session) {
  return query_services(session, {});
}

std::vector<Service> get_services_of_auth_app(Db_session *session,
                                              const Id &auth_app_id) {
  return query_services(
      session,
      "se.id IN (SELECT service_id FROM " +
          sql::metadata_table("service_has_auth_app") +
          " WHERE auth_app_id = " + sql::id(auth_app_id) + ")");
}

// -- Daemons --------------------------------------------------------------

namespace {

std::vector<Daemon> query_daemons(Db_session *session, const std::string &where) {
  std::string query =
      "SELECT id, router_name, address, product_name, version, last_check_in, "
      "last_check_in > CURRENT_TIMESTAMP - INTERVAL 10 SECOND AS active, "
      "JSON_UNQUOTE(JSON_EXTRACT(options, '$.developer')) AS developer, "
      "attributes, options FROM " +
      sql::metadata_table("router");
  if (!where.empty()) query += " WHERE " + where;
  query += " ORDER BY id";

  std::vector<Daemon> daemons;
  for (const auto &row : session->query(query).rows) {
    Daemon d;
    d.id = row["id"].as_int();
    d.name = row["router_name"].as_string();
    d.address = row["address"].as_string();
    d.product_name = row["product_name"].as_string();
    d.version = row["version"].as_optional_string();
    d.last_check_in = row["last_check_in"].as_optional_string();
    d.active = !row["active"].is_null() && row["active"].as_int() == 1;
    d.developer = row["developer"].as_optional_string();
    d.attributes = row["attributes"].as_optional_string();
    d.options = row["options"].as_optional_string();
    daemons.push_back(std::move(d));
  }
  return daemons;
}

}  // namespace

std::vector<Daemon> get_daemons(Db_session *session) {
  return query_daemons(session, {});
}

std::optional<Daemon> get_daemon(Db_session *session, int64_t id) {
  auto daemons = query_daemons(session, "id = " + std::to_string(id));
  if (daemons.empty()) return std::nullopt;
  return std::move(daemons.front());
}

std::vector<Service> get_services_of_daemon(Db_session *session, int64_t id) {
  return query_services(
      session, "se.id IN (SELECT service_id FROM " +
                   sql::metadata_table("router_services") +
                   " WHERE router_id = " + std::to_string(id) + ")");
}

void delete_daemon(Db_session *session, int64_t id) {
  const auto where = " WHERE router_id = " + std::to_string(id);
  session->execute("DELETE FROM " + sql::metadata_table("router_general_log") +
                   where);
  session->execute("DELETE FROM " + sql::metadata_table("router_status") + where);
  session->execute("DELETE FROM " + sql::metadata_table("router") +
                   " WHERE id = " + std::to_string(id));
}

Id add_service(Db_session *session, const Service_definition &definition) {
  if (const auto lower = to_lower(definition.url_context_root);
      lower == "/mrs") {
    throw std::runtime_error("The REST service path `" + lower +
                             "` is reserved and cannot be used.");
  }

  const Id id = new_id(session);
  sql::Insert insert("service");
  insert.set("id", Value::id(id));
  insert.set("url_host_id", Value::id(url_host_id(session, "")));
  insert.set("url_context_root", definition.url_context_root);
  insert.set("options", definition.options
                            ? *definition.options
                            : json::parse(k_default_service_options).dump());
  if (!definition.developers.empty()) {
    insert.set("in_development", in_development_json(definition.developers));
  }
  if (definition.enabled) insert.set("enabled", *definition.enabled);
  if (definition.published) insert.set("published", *definition.published);
  if (definition.url_protocol) insert.set("url_protocol", *definition.url_protocol);
  if (definition.comments) insert.set("comments", *definition.comments);
  if (definition.metadata) insert.set("metadata", *definition.metadata);
  if (definition.auth_path) insert.set("auth_path", *definition.auth_path);
  if (definition.auth_completed_url) {
    insert.set("auth_completed_url", *definition.auth_completed_url);
  }
  if (definition.auth_completed_url_validation) {
    insert.set("auth_completed_url_validation",
               *definition.auth_completed_url_validation);
  }
  if (definition.auth_completed_page_content) {
    insert.set("auth_completed_page_content",
               *definition.auth_completed_page_content);
  }

  if (session->execute(insert.str()) == 0) {
    throw std::runtime_error("Failed to add the new service.");
  }
  return id;
}

void update_service(Db_session *session, const Id &id,
                    const Service_changes &changes) {
  sql::Update update("service");
  if (changes.url_context_root) {
    update.set("url_context_root", *changes.url_context_root);
    update.set("url_host_id", Value::id(url_host_id(session, "")));
  }
  if (changes.developers) {
    if (changes.developers->empty()) {
      update.set("in_development", nullptr);
    } else {
      update.set("in_development", in_development_json(*changes.developers));
    }
  }
  if (changes.enabled) update.set("enabled", *changes.enabled);
  if (changes.published) update.set("published", *changes.published);
  if (changes.url_protocol) update.set("url_protocol", *changes.url_protocol);
  if (changes.comments) update.set("comments", *changes.comments);
  if (changes.metadata) update.set("metadata", *changes.metadata);
  if (changes.auth_path) {
    update.set("auth_path", changes.auth_path->has_value()
                                ? Value(**changes.auth_path)
                                : Value::raw("DEFAULT"));
  }
  if (changes.auth_completed_url) {
    update.set("auth_completed_url", Value(*changes.auth_completed_url));
  }
  if (changes.auth_completed_url_validation) {
    update.set("auth_completed_url_validation",
               Value(*changes.auth_completed_url_validation));
  }
  if (changes.auth_completed_page_content) {
    update.set("auth_completed_page_content",
               Value(*changes.auth_completed_page_content));
  }
  if (changes.options) {
    set_json_options(session, &update, "service", id, *changes.options,
                     changes.merge_options);
  }

  if (update.empty()) return;
  update.where("id = " + sql::id(id));
  session->execute(update.str());
}

void delete_service(Db_session *session, const Id &id) {
  if (session->execute("DELETE FROM " + sql::metadata_table("service") +
                       " WHERE id = " + sql::id(id)) == 0) {
    throw std::runtime_error("The specified service with id " + id +
                             " was not found.");
  }
}

std::string service_create_statement(Db_session *session,
                                     const Service &service,
                                     bool include_database_endpoints,
                                     bool include_static_endpoints,
                                     bool include_dynamic_endpoints) {
  std::string output =
      "CREATE OR REPLACE REST SERVICE " + service.full_service_path;

  if (service.enabled != 1) output += "\n    DISABLED";
  if (service.comments && !service.comments->empty()) {
    output += "\n    COMMENT " + sql::quote(*service.comments);
  }
  if (service.published) output += "\n    PUBLISHED";

  std::string auth;
  if (service.auth_path != "/authentication") {
    auth += "\n        PATH " + sql::quote(service.auth_path);
  }
  if (service.auth_completed_url && !service.auth_completed_url->empty()) {
    auth += "\n        REDIRECTION " + sql::quote(*service.auth_completed_url);
  }
  if (service.auth_completed_url_validation &&
      !service.auth_completed_url_validation->empty()) {
    auth += "\n        VALIDATION " +
            sql::quote(*service.auth_completed_url_validation);
  }
  if (service.auth_completed_page_content &&
      !service.auth_completed_page_content->empty()) {
    auth += "\n        PAGE CONTENT " +
            sql::quote(*service.auth_completed_page_content);
  }
  if (!auth.empty()) output += "\n    AUTHENTICATION" + auth;

  const auto options = format_json_entry("OPTIONS", service.options);
  if (!options.empty()) output += "\n" + options;
  const auto metadata = format_json_entry("METADATA", service.metadata);
  if (!metadata.empty()) output += "\n" + metadata;

  for (const auto &auth_app : service.auth_apps) {
    output += "\n    ADD AUTH APP " + sql::quote_identifier(auth_app) +
              " IF EXISTS";
  }
  output += ";";

  std::vector<std::string> statements{output};

  if (include_database_endpoints) {
    for (auto &statement : role_create_statements(session, service.id)) {
      statements.push_back(std::move(statement));
    }
    for (const auto &schema : get_schemas(session, service.id)) {
      if (schema.schema_type == "SCRIPT_MODULE") continue;
      statements.push_back(schema_create_statement(session, schema, true));
    }
  }

  if (include_static_endpoints || include_dynamic_endpoints) {
    for (auto &statement : content_set_create_statements(
             session, service.id, include_dynamic_endpoints)) {
      statements.push_back(std::move(statement));
    }
  }

  std::string result;
  for (const auto &statement : statements) {
    if (!result.empty()) result += "\n\n";
    result += statement;
  }
  return result;
}

Id clone_service(Db_session *session, const Service &service,
                 const std::string &new_url_context_root,
                 const std::vector<std::string> &new_developers) {
  Service_definition definition;
  definition.url_context_root = new_url_context_root;
  definition.developers = new_developers;
  definition.enabled = service.enabled != 0;
  definition.published = false;
  definition.url_protocol = service.url_protocol;
  definition.comments = service.comments;
  definition.options = service.options;
  definition.metadata = service.metadata;
  definition.auth_path = service.auth_path;
  definition.auth_completed_url = service.auth_completed_url;
  definition.auth_completed_url_validation = service.auth_completed_url_validation;
  definition.auth_completed_page_content = service.auth_completed_page_content;

  const Id new_id = add_service(session, definition);

  for (const auto &auth_app : service.auth_apps) {
    if (const auto app = find_auth_app(session, auth_app)) {
      link_auth_app(session, app->id, new_id);
    }
  }

  for (const auto &schema : get_schemas(session, service.id)) {
    clone_schema(session, schema, new_id);
  }

  for (const auto &content_set : get_content_sets(session, service.id)) {
    clone_content_set(session, content_set, new_id);
  }

  return new_id;
}

// -- Schemas --------------------------------------------------------------

std::optional<Schema> get_schema(Db_session *session, const Id &id) {
  auto schemas = query_schemas(session, "sc.id = " + sql::id(id));
  if (schemas.empty()) return std::nullopt;
  return std::move(schemas.front());
}

std::optional<Schema> find_schema(Db_session *session, const Id &service_id,
                                  std::string_view request_path) {
  if (request_path.empty() || request_path[0] != '/') {
    throw std::runtime_error("The request_path has to start with '/'.");
  }
  auto schemas = query_schemas(session, "sc.service_id = " + sql::id(service_id) +
                                            " AND sc.request_path = " +
                                            sql::quote(request_path));
  if (schemas.empty()) return std::nullopt;
  return std::move(schemas.front());
}

std::vector<Schema> get_schemas(Db_session *session, const Id &service_id) {
  return query_schemas(session, "sc.service_id = " + sql::id(service_id));
}

Id add_schema(Db_session *session, const Schema_definition &definition) {
  std::string name = definition.name;
  if (definition.schema_type == "DATABASE_SCHEMA") {
    const auto actual = database_schema_name(session, name);
    if (!actual) {
      throw std::runtime_error("The given database schema name '" + name +
                               "' does not exists.");
    }
    name = *actual;
  } else if (name.empty()) {
    throw std::runtime_error("No schema name given.");
  }

  const std::string request_path =
      definition.request_path ? *definition.request_path : "/" + name;
  if (request_path.empty() || request_path[0] != '/') {
    throw std::runtime_error("The request_path has to start with '/'.");
  }

  const Id id = definition.id ? *definition.id : new_id(session);
  sql::Insert insert("db_schema");
  insert.set("id", Value::id(id));
  insert.set("service_id", Value::id(definition.service_id));
  insert.set("name", name);
  insert.set("request_path", request_path);
  insert.set("requires_auth", definition.requires_auth.value_or(false));
  insert.set("enabled", definition.enabled.value_or(1));
  insert.set("items_per_page", definition.items_per_page.value_or(25));
  insert.set("comments", definition.comments.value_or(""));
  insert.set("options", definition.options);
  insert.set("metadata", definition.metadata);
  insert.set("schema_type", definition.schema_type);
  insert.set("internal", definition.internal);
  session->execute(insert.str());
  return id;
}

void update_schema(Db_session *session, const Id &id,
                   const Schema_changes &changes) {
  sql::Update update("db_schema");
  if (changes.service_id) update.set("service_id", Value::id(*changes.service_id));
  if (changes.name) update.set("name", *changes.name);
  if (changes.request_path) update.set("request_path", *changes.request_path);
  if (changes.requires_auth) update.set("requires_auth", *changes.requires_auth);
  if (changes.enabled) update.set("enabled", *changes.enabled);
  if (changes.items_per_page) update.set("items_per_page", *changes.items_per_page);
  if (changes.comments) update.set("comments", *changes.comments);
  if (changes.metadata) update.set("metadata", *changes.metadata);
  if (changes.options) {
    set_json_options(session, &update, "db_schema", id, *changes.options,
                     changes.merge_options);
  }
  if (update.empty()) return;
  update.where("id = " + sql::id(id));
  session->execute(update.str());
}

void delete_schema(Db_session *session, const Id &id) {
  if (session->execute("DELETE FROM " + sql::metadata_table("db_schema") +
                       " WHERE id = " + sql::id(id)) == 0) {
    throw std::runtime_error("The specified schema with id " + id +
                             " was not found.");
  }
}

std::string schema_create_statement(Db_session *session, const Schema &schema,
                                    bool include_database_endpoints) {
  std::string output = "CREATE OR REPLACE REST SCHEMA " +
                       quote_request_path(schema.request_path) +
                       " ON SERVICE " + schema.host_ctx + "\n    FROM " +
                       sql::quote_identifier(schema.name);

  if (schema.enabled == 2) {
    output += "\n    PRIVATE";
  } else if (schema.enabled != 1) {
    output += "\n    DISABLED";
  }
  output += schema.requires_auth ? "\n    AUTHENTICATION REQUIRED"
                                 : "\n    AUTHENTICATION NOT REQUIRED";
  if (schema.items_per_page && *schema.items_per_page != 25) {
    output += "\n    ITEMS PER PAGE " + std::to_string(*schema.items_per_page);
  }
  if (schema.comments && !schema.comments->empty()) {
    output += "\n    COMMENT " + sql::quote(*schema.comments);
  }
  const auto options = format_json_entry("OPTIONS", schema.options);
  if (!options.empty()) output += "\n" + options;
  const auto metadata = format_json_entry("METADATA", schema.metadata);
  if (!metadata.empty()) output += "\n" + metadata;
  output += ";";

  if (include_database_endpoints) {
    for (const auto &db_object : get_db_objects(session, schema.id, {})) {
      output += "\n\n" + db_object_create_statement(session, db_object);
    }
  }
  return output;
}

Id clone_schema(Db_session *session, const Schema &schema,
                const Id &new_service_id) {
  Schema_definition definition;
  definition.service_id = new_service_id;
  definition.name = schema.name;
  definition.request_path = schema.request_path;
  definition.requires_auth = schema.requires_auth;
  definition.enabled = schema.enabled;
  definition.items_per_page = schema.items_per_page;
  definition.comments = schema.comments;
  definition.options = schema.options;
  definition.metadata = schema.metadata;
  definition.schema_type = schema.schema_type;
  definition.internal = schema.internal;

  const Id new_schema_id = add_schema(session, definition);

  for (const auto &db_object : get_db_objects(session, schema.id, {})) {
    clone_db_object(session, db_object, new_schema_id);
  }
  return new_schema_id;
}

// -- Database schema ------------------------------------------------------

std::optional<std::string> database_schema_name(Db_session *session,
                                                std::string_view name) {
  const auto result = session->query(
      "SELECT SCHEMA_NAME FROM INFORMATION_SCHEMA.SCHEMATA WHERE SCHEMA_NAME = " +
      sql::quote(name));
  if (result.empty()) return std::nullopt;
  return result.first()["SCHEMA_NAME"].as_string();
}

}  // namespace metadata
}  // namespace mrs
