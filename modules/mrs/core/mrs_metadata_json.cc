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

#include "modules/mrs/core/mrs_metadata_json.h"

#include <string>
#include <utility>

namespace mrs {
namespace metadata {

namespace {

json::Value text(const std::optional<std::string> &value) {
  return value ? json::Value(*value) : json::Value();
}

json::Value id(const std::optional<Id> &value) {
  return value ? json::Value(*value) : json::Value();
}

json::Value number(const std::optional<int64_t> &value) {
  return value ? json::Value(*value) : json::Value();
}

// A JSON column embedded as a document; text that is no valid JSON stays a
// string.
json::Value document(const std::optional<std::string> &value) {
  if (!value) return json::Value();
  if (auto doc = json::try_parse(*value)) return std::move(*doc);
  return json::Value(*value);
}

json::Value strings(const std::vector<std::string> &values) {
  json::Value::Array array;
  for (const auto &value : values) array.emplace_back(value);
  return json::Value(std::move(array));
}

json::Value data_mapping_reference_json(const Data_mapping_reference &reference) {
  json::Value doc = json::Value::object();
  doc.set("id", reference.id);
  doc.set("reduce_to_value_of_field_id", id(reference.reduce_to_value_of_field_id));
  doc.set("row_ownership_field_id", id(reference.row_ownership_field_id));
  doc.set("reference_mapping", reference.reference_mapping);
  doc.set("unnest", reference.unnest);
  doc.set("options", document(reference.options));
  doc.set("sdk_options", document(reference.sdk_options));
  doc.set("comments", text(reference.comments));
  return doc;
}

json::Value data_mapping_field_json(const Data_mapping_field &field) {
  json::Value doc = json::Value::object();
  doc.set("id", field.id);
  doc.set("data_mapping_id", field.data_mapping_id);
  doc.set("parent_reference_id", id(field.parent_reference_id));
  doc.set("represents_reference_id",
          field.reference ? json::Value(field.reference->id) : json::Value());
  doc.set("name", field.name);
  doc.set("position", field.position);
  doc.set("db_column", field.db_column ? *field.db_column : json::Value());
  doc.set("enabled", field.enabled);
  doc.set("allow_filtering", field.allow_filtering);
  doc.set("allow_sorting", field.allow_sorting);
  doc.set("no_check", field.no_check);
  doc.set("no_update", field.no_update);
  doc.set("json_schema", document(field.json_schema));
  doc.set("options", document(field.options));
  doc.set("sdk_options", document(field.sdk_options));
  doc.set("comments", text(field.comments));
  doc.set("data_mapping_reference", field.reference
                                  ? data_mapping_reference_json(*field.reference)
                                  : json::Value());
  return doc;
}

json::Value object_json(const Data_mapping &object) {
  json::Value doc = json::Value::object();
  doc.set("id", object.id);
  doc.set("name", object.name);
  doc.set("kind", object.kind);
  doc.set("position", object.position);
  doc.set("row_ownership_field_id", id(object.row_ownership_field_id));
  doc.set("options", document(object.options));
  doc.set("sdk_options", document(object.sdk_options));
  doc.set("comments", text(object.comments));
  json::Value::Array fields;
  for (const auto &field : object.fields) {
    fields.push_back(data_mapping_field_json(field));
  }
  doc.set("fields", json::Value(std::move(fields)));
  return doc;
}

}  // namespace

json::Value service_json(Db_session *session, const Service &service,
                         bool include_database_endpoints) {
  json::Value doc = json::Value::object();
  doc.set("id", service.id);
  doc.set("parent_id", id(service.parent_id));
  doc.set("url_host_id", service.url_host_id);
  doc.set("url_host_name", service.url_host_name);
  doc.set("url_context_root", service.url_context_root);
  doc.set("full_service_path", service.full_service_path);
  doc.set("url_protocol", service.url_protocol);
  doc.set("name", service.name);
  doc.set("enabled", service.enabled);
  doc.set("published", service.published);
  doc.set("comments", text(service.comments));
  doc.set("options", document(service.options));
  doc.set("metadata", document(service.metadata));
  doc.set("auth_path", service.auth_path);
  doc.set("auth_completed_url", text(service.auth_completed_url));
  doc.set("auth_completed_url_validation",
          text(service.auth_completed_url_validation));
  doc.set("auth_completed_page_content",
          text(service.auth_completed_page_content));
  doc.set("in_development", document(service.in_development));
  doc.set("developers", strings(service.developers));
  doc.set("auth_apps", strings(service.auth_apps));

  if (include_database_endpoints) {
    json::Value::Array schemas;
    for (const auto &schema : get_schemas(session, service.id)) {
      json::Value schema_doc = schema_json(schema);
      json::Value::Array rest_objects;
      for (const auto &rest_object : get_rest_objects(session, schema.id, {})) {
        rest_objects.push_back(rest_object_json(session, rest_object));
      }
      schema_doc.set("rest_objects", json::Value(std::move(rest_objects)));
      schemas.push_back(std::move(schema_doc));
    }
    doc.set("rest_schemas", json::Value(std::move(schemas)));
  }
  return doc;
}

json::Value status_json(const Status &status,
                        const std::vector<Version> &available_versions) {
  json::Value doc = json::Value::object();
  doc.set("service_configured", status.service_configured);
  doc.set("service_enabled", status.service_enabled);
  doc.set("service_upgradeable", status.service_upgradeable);
  doc.set("service_upgrade_ignored", status.service_upgrade_ignored);
  doc.set("service_count", status.service_count);
  doc.set("service_being_upgraded", status.service_being_upgraded);
  doc.set("major_upgrade_required", status.major_upgrade_required);
  doc.set("current_metadata_version", text(status.current_metadata_version));
  doc.set("available_metadata_version", text(status.available_metadata_version));
  doc.set("required_rest_daemon_version",
          text(status.required_rest_daemon_version));
  doc.set("metadata_version", number(status.metadata_version));
  doc.set("metadata_schema", status.metadata_schema);
  json::Value::Array versions;
  for (const auto &version : available_versions) versions.emplace_back(version.str());
  doc.set("available_metadata_versions", json::Value(std::move(versions)));
  doc.set("configuration_options",
          status.configuration_options ? document(status.configuration_options)
                                       : json::Value::object());
  return doc;
}

json::Value daemon_json(const Daemon &daemon) {
  json::Value doc = json::Value::object();
  doc.set("id", daemon.id);
  doc.set("name", daemon.name);
  doc.set("address", daemon.address);
  doc.set("product_name", daemon.product_name);
  doc.set("version", text(daemon.version));
  doc.set("last_check_in", text(daemon.last_check_in));
  doc.set("active", daemon.active);
  doc.set("developer", text(daemon.developer));
  doc.set("attributes", document(daemon.attributes));
  doc.set("options", document(daemon.options));
  return doc;
}

json::Value schema_json(const Schema &schema) {
  json::Value doc = json::Value::object();
  doc.set("id", schema.id);
  doc.set("service_id", schema.service_id);
  doc.set("name", schema.name);
  doc.set("schema_type", schema.schema_type);
  doc.set("request_path", schema.request_path);
  doc.set("requires_auth", schema.requires_auth);
  doc.set("enabled", schema.enabled);
  doc.set("internal", schema.internal);
  doc.set("items_per_page", number(schema.items_per_page));
  doc.set("comments", text(schema.comments));
  doc.set("options", document(schema.options));
  doc.set("metadata", document(schema.metadata));
  doc.set("host_ctx", schema.host_ctx);
  return doc;
}

json::Value rest_object_json(Db_session *session, const Rest_object &rest_object,
                             bool include_data_mappings) {
  json::Value doc = json::Value::object();
  doc.set("id", rest_object.id);
  doc.set("rest_schema_id", rest_object.rest_schema_id);
  doc.set("service_id", rest_object.service_id);
  doc.set("name", rest_object.name);
  doc.set("schema_name", rest_object.schema_name);
  doc.set("request_path", rest_object.request_path);
  doc.set("schema_request_path", rest_object.schema_request_path);
  doc.set("host_ctx", rest_object.host_ctx);
  doc.set("object_type", rest_object.object_type);
  doc.set("crud_operations", strings(rest_object.crud_operations));
  doc.set("format", rest_object.format);
  doc.set("enabled", rest_object.enabled);
  doc.set("internal", rest_object.internal);
  doc.set("requires_auth", rest_object.requires_auth);
  doc.set("items_per_page", number(rest_object.items_per_page));
  doc.set("media_type", text(rest_object.media_type));
  doc.set("auto_detect_media_type", rest_object.auto_detect_media_type);
  doc.set("auth_stored_procedure", text(rest_object.auth_stored_procedure));
  doc.set("comments", text(rest_object.comments));
  doc.set("options", document(rest_object.options));
  doc.set("metadata", document(rest_object.metadata));
  if (!include_data_mappings) return doc;
  json::Value::Array objects;
  for (const auto &object : get_objects(session, rest_object.id)) {
    objects.push_back(object_json(object));
  }
  doc.set("data_mappings", json::Value(std::move(objects)));
  return doc;
}

json::Value content_set_json(const Content_set &content_set) {
  json::Value doc = json::Value::object();
  doc.set("id", content_set.id);
  doc.set("service_id", content_set.service_id);
  doc.set("content_type", content_set.content_type);
  doc.set("request_path", content_set.request_path);
  doc.set("requires_auth", content_set.requires_auth);
  doc.set("enabled", content_set.enabled);
  doc.set("internal", content_set.internal);
  doc.set("comments", text(content_set.comments));
  doc.set("options", document(content_set.options));
  doc.set("host_ctx", content_set.host_ctx);
  return doc;
}

json::Value auth_vendor_json(const Auth_vendor &vendor) {
  json::Value doc = json::Value::object();
  doc.set("id", vendor.id);
  doc.set("name", vendor.name);
  doc.set("comments", text(vendor.comments));
  doc.set("enabled", vendor.enabled);
  doc.set("validation_url", text(vendor.validation_url));
  return doc;
}

json::Value content_file_json(const Content_file &content_file) {
  json::Value doc = json::Value::object();
  doc.set("id", content_file.id);
  doc.set("content_set_id", content_file.content_set_id);
  doc.set("request_path", content_file.request_path);
  doc.set("requires_auth", content_file.requires_auth);
  doc.set("enabled", content_file.enabled);
  doc.set("size", content_file.size);
  doc.set("options", document(content_file.options));
  doc.set("content_set_request_path", content_file.content_set_request_path);
  doc.set("host_ctx", content_file.host_ctx);
  return doc;
}

json::Value auth_app_json(Db_session *session, const Auth_app &auth_app) {
  json::Value doc = json::Value::object();
  doc.set("id", auth_app.id);
  doc.set("auth_vendor_id", auth_app.auth_vendor_id);
  doc.set("auth_vendor", auth_app.auth_vendor);
  doc.set("name", auth_app.name);
  doc.set("description", text(auth_app.description));
  doc.set("url", text(auth_app.url));
  doc.set("url_direct_auth", text(auth_app.url_direct_auth));
  doc.set("app_id", text(auth_app.app_id));
  doc.set("has_app_secret", auth_app.access_token.has_value() &&
                                !auth_app.access_token->empty());
  doc.set("enabled", auth_app.enabled);
  doc.set("limit_to_registered_users", auth_app.limit_to_registered_users);
  doc.set("default_role_id", id(auth_app.default_role_id));
  doc.set("options", document(auth_app.options));
  json::Value::Array services;
  for (const auto &service : get_services_of_auth_app(session, auth_app.id)) {
    services.emplace_back(service.full_service_path);
  }
  doc.set("services", json::Value(std::move(services)));
  return doc;
}

json::Value user_json(Db_session *session, const User &user) {
  json::Value doc = json::Value::object();
  doc.set("id", user.id);
  doc.set("auth_app_id", user.auth_app_id);
  doc.set("auth_app_name", user.auth_app_name);
  doc.set("name", user.name);
  doc.set("email", text(user.email));
  doc.set("vendor_user_id", text(user.vendor_user_id));
  doc.set("mapped_user_id", text(user.mapped_user_id));
  doc.set("login_permitted", user.login_permitted);
  doc.set("has_password", user.has_auth_string);
  doc.set("app_options", document(user.app_options));
  doc.set("options", document(user.options));
  json::Value::Array roles;
  for (const auto &user_role : get_user_roles(session, user.id)) {
    json::Value role = json::Value::object();
    role.set("role_id", user_role.role.id);
    role.set("caption", user_role.role.caption);
    role.set("specific_to_service_id", id(user_role.role.specific_to_service_id));
    role.set("comments", text(user_role.comments));
    role.set("options", document(user_role.options));
    roles.push_back(std::move(role));
  }
  doc.set("roles", json::Value(std::move(roles)));
  return doc;
}

json::Value role_json(Db_session *session, const Role &role) {
  json::Value doc = json::Value::object();
  doc.set("id", role.id);
  doc.set("caption", role.caption);
  doc.set("derived_from_role_id", id(role.derived_from_role_id));
  doc.set("derived_from_role_caption", text(role.derived_from_role_caption));
  doc.set("specific_to_service_id", id(role.specific_to_service_id));
  doc.set("specific_to_service", text(role.specific_to_service));
  doc.set("description", text(role.description));
  doc.set("options", document(role.options));
  json::Value::Array privileges;
  for (const auto &privilege : get_role_privileges(session, role.id)) {
    json::Value p = json::Value::object();
    p.set("id", privilege.id);
    p.set("crud_operations", strings(privilege.crud_operations));
    p.set("service_path", privilege.service_path);
    p.set("schema_path", privilege.schema_path);
    p.set("object_path", privilege.object_path);
    privileges.push_back(std::move(p));
  }
  doc.set("privileges", json::Value(std::move(privileges)));
  return doc;
}

json::Value table_columns_json(std::string_view schema_name,
                               std::string_view name, std::string_view type,
                               const std::vector<Table_column> &columns) {
  json::Value doc = json::Value::object();
  doc.set("schema", std::string(schema_name));
  doc.set("name", std::string(name));
  doc.set("type", std::string(type));
  json::Value::Array items;
  for (const auto &column : columns) {
    json::Value item = json::Value::object();
    item.set("position", column.position);
    item.set("name", column.name);
    item.set("db_column", column.db_column ? *column.db_column : json::Value());
    item.set("reference_mapping",
             column.reference_mapping ? *column.reference_mapping : json::Value());
    items.push_back(std::move(item));
  }
  doc.set("columns", json::Value(std::move(items)));
  return doc;
}

json::Value routine_json(std::string_view schema_name, std::string_view name,
                         std::string_view type,
                         const std::vector<Routine_parameter> &parameters,
                         const std::optional<std::string> &return_type) {
  json::Value doc = json::Value::object();
  doc.set("schema", std::string(schema_name));
  doc.set("name", std::string(name));
  doc.set("type", std::string(type));
  json::Value::Array items;
  for (const auto &parameter : parameters) {
    json::Value item = json::Value::object();
    item.set("position", parameter.position);
    item.set("name", parameter.name);
    item.set("mode", parameter.mode);
    item.set("datatype", parameter.datatype);
    item.set("charset", text(parameter.charset));
    item.set("collation", text(parameter.collation));
    items.push_back(std::move(item));
  }
  doc.set("parameters", json::Value(std::move(items)));
  if (type == "FUNCTION") doc.set("return_type", text(return_type));
  return doc;
}

}  // namespace metadata
}  // namespace mrs
