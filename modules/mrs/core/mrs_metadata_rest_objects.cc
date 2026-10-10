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

#include "modules/mrs/core/mrs_metadata_rest_objects.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>

#include "modules/mrs/core/mrs_strings.h"

namespace mrs {
namespace metadata {

using sql::Value;

namespace {

bool is_routine_type(std::string_view object_type) {
  return object_type == "PROCEDURE" || object_type == "FUNCTION";
}

// The column list of the rest_object queries.
const char *k_rest_object_select = R"(
SELECT o.id, o.rest_schema_id, o.name, o.request_path,
    o.requires_auth, o.enabled, o.object_type,
    o.items_per_page, o.comments,
    sc.request_path AS schema_request_path,
    CONCAT(h.name, se.url_context_root) AS host_ctx,
    o.crud_operations, o.format,
    o.media_type, o.auto_detect_media_type,
    o.auth_stored_procedure, o.options,
    o.metadata, o.internal,
    se.id AS service_id, sc.name AS schema_name
FROM `{metadata_schema}`.`rest_object` o
    LEFT OUTER JOIN `{metadata_schema}`.`rest_schema` sc
        ON sc.id = o.rest_schema_id
    LEFT OUTER JOIN `{metadata_schema}`.`service` se
        ON se.id = sc.service_id
    LEFT JOIN `{metadata_schema}`.`url_host` h
        ON se.url_host_id = h.id
)";

Rest_object rest_object_from_row(const Db_row &row) {
  Rest_object o;
  o.id = row["id"].as_string();
  o.rest_schema_id = row["rest_schema_id"].as_string();
  o.service_id = row["service_id"].as_string();
  o.name = row["name"].as_string();
  o.schema_name = row["schema_name"].as_string();
  o.request_path = row["request_path"].as_string();
  o.schema_request_path = row["schema_request_path"].as_string();
  o.host_ctx = row["host_ctx"].as_string();
  o.object_type = row["object_type"].as_string();
  if (const auto crud = row["crud_operations"].as_string(); !crud.empty()) {
    o.crud_operations = split(crud, ',');
  }
  o.format = row["format"].as_string();
  o.enabled = static_cast<int>(row["enabled"].as_int());
  o.internal = row["internal"].as_bool();
  o.requires_auth = row["requires_auth"].as_bool();
  if (!row["items_per_page"].is_null()) {
    o.items_per_page = row["items_per_page"].as_int();
  }
  o.media_type = row["media_type"].as_optional_string();
  o.auto_detect_media_type = row["auto_detect_media_type"].as_bool();
  o.auth_stored_procedure = row["auth_stored_procedure"].as_optional_string();
  o.comments = row["comments"].as_optional_string();
  o.options = row["options"].as_optional_string();
  o.metadata = row["metadata"].as_optional_string();
  return o;
}

std::vector<Rest_object> query_rest_objects(Db_session *session,
                                        const std::string &where,
                                        std::vector<Value> params) {
  std::string sql = k_rest_object_select;
  if (!where.empty()) sql += " WHERE " + where;
  sql += " ORDER BY o.request_path";

  std::vector<Rest_object> rest_objects;
  for (const auto &row : session->query(sql, std::move(params)).rows) {
    rest_objects.push_back(rest_object_from_row(row));
  }
  return rest_objects;
}

// The router of the previous metadata major version read the data mapping
// flags under their old names, so they are stored twice.
std::string with_legacy_option_keys(const std::string &options) {
  auto doc = json::parse(options);
  if (!doc.is_object()) return options;

  for (const auto &[key, legacy_key] :
       {std::pair{"dataMappingViewInsert", "duality_view_insert"},
        std::pair{"dataMappingViewUpdate", "duality_view_update"},
        std::pair{"dataMappingViewDelete", "duality_view_delete"}}) {
    const auto *value = doc.get(key);
    doc.set(legacy_key, value ? *value : json::Value());
  }
  if (const auto *no_check = doc.get("dataMappingViewNoCheck")) {
    doc.set("duality_view_no_check", *no_check);
  }
  return doc.dump();
}

sql::Value json_value(const std::optional<std::string> &text) {
  return text ? sql::Value(*text) : sql::Value();
}

sql::Value json_value(const std::optional<json::Value> &doc) {
  return doc ? sql::Value(doc->dump()) : sql::Value();
}

// Starts the next row of a multi-row insert, unless it is the first.
sql::Insert &next_row(sql::Insert *insert) {
  return insert->empty() ? *insert : insert->next_row();
}

void add_reference_row(sql::Insert *insert, const Data_mapping_reference &ref) {
  next_row(insert)
      .set("id", sql::Value::id(ref.id))
      .set("reduce_to_value_of_field_id",
           sql::Value::id(ref.reduce_to_value_of_field_id))
      .set("row_ownership_field_id", sql::Value::id(ref.row_ownership_field_id))
      .set("reference_mapping", ref.reference_mapping.dump())
      .set("unnest", ref.unnest)
      .set("options", ref.options ? sql::Value(with_legacy_option_keys(*ref.options))
                                  : sql::Value())
      .set("sdk_options", json_value(ref.sdk_options))
      .set("comments", ref.comments);
}

void add_field_row(sql::Insert *insert, const Id &data_mapping_id,
                   const Data_mapping_field &field) {
  next_row(insert)
      .set("id", sql::Value::id(field.id))
      .set("data_mapping_id", sql::Value::id(data_mapping_id))
      .set("parent_reference_id", sql::Value::id(field.parent_reference_id))
      .set("represents_reference_id",
           field.reference ? sql::Value::id(field.reference->id) : sql::Value())
      .set("name", field.name)
      .set("position", field.position)
      .set("db_column", json_value(field.db_column))
      .set("enabled", field.enabled)
      .set("allow_filtering", field.allow_filtering)
      .set("allow_sorting", field.allow_sorting)
      .set("no_check", field.no_check)
      .set("no_update", field.no_update)
      .set("json_schema", json_value(field.json_schema))
      .set("options", json_value(field.options))
      .set("sdk_options", json_value(field.sdk_options))
      .set("comments", field.comments);
}

void add_object_row(sql::Insert *insert, const Id &rest_object_id,
                    const Data_mapping &object) {
  next_row(insert)
      .set("id", sql::Value::id(object.id))
      .set("rest_object_id", sql::Value::id(rest_object_id))
      .set("name", object.name)
      .set("kind", object.kind)
      .set("position", object.position)
      .set("row_ownership_field_id", sql::Value::id(object.row_ownership_field_id))
      .set("options", object.options
                          ? sql::Value(with_legacy_option_keys(*object.options))
                          : sql::Value())
      .set("sdk_options", json_value(object.sdk_options))
      .set("comments", object.comments);
}

// The objects of a rest_object with their references and fields, in three
// multi-row statements. The fields point to the references, so those go
// first.
void insert_objects(Db_session *session, const Id &rest_object_id,
                    const std::vector<Data_mapping> &objects) {
  sql::Insert object_rows("data_mapping");
  sql::Insert reference_rows("data_mapping_reference");
  sql::Insert field_rows("data_mapping_field");
  for (const auto &object : objects) {
    add_object_row(&object_rows, rest_object_id, object);
    for (const auto &field : object.fields) {
      if (field.reference) add_reference_row(&reference_rows, *field.reference);
      add_field_row(&field_rows, object.id, field);
    }
  }
  for (const auto *insert : {&object_rows, &reference_rows, &field_rows}) {
    if (!insert->empty()) session->execute(*insert);
  }
}

Data_mapping_field field_from_row(const Db_row &row) {
  Data_mapping_field f;
  f.id = row["id"].as_string();
  f.data_mapping_id = row["data_mapping_id"].as_string();
  f.parent_reference_id = row["parent_reference_id"].as_optional_string();
  f.name = row["name"].as_string();
  f.position = static_cast<int>(row["position"].as_int());
  if (const auto db_column = row["db_column"].as_optional_string()) {
    f.db_column = json::parse(*db_column);
  }
  f.enabled = row["enabled"].as_bool();
  f.allow_filtering = row["allow_filtering"].as_bool();
  f.allow_sorting = row["allow_sorting"].as_bool();
  f.no_check = row["no_check"].as_bool();
  f.no_update = row["no_update"].as_bool();
  f.json_schema = row["json_schema"].as_optional_string();
  f.options = row["options"].as_optional_string();
  f.sdk_options = row["sdk_options"].as_optional_string();
  f.comments = row["comments"].as_optional_string();

  if (const auto reference_id = row["represents_reference_id"].as_optional_string()) {
    Data_mapping_reference r;
    r.id = *reference_id;
    r.reduce_to_value_of_field_id =
        row["reduce_to_value_of_field_id"].as_optional_string();
    r.row_ownership_field_id = row["ref_row_ownership_field_id"].as_optional_string();
    r.reference_mapping = json::parse(row["reference_mapping"].as_string());
    r.unnest = row["unnest"].as_bool();
    r.options = row["ref_options"].as_optional_string();
    r.sdk_options = row["ref_sdk_options"].as_optional_string();
    r.comments = row["ref_comments"].as_optional_string();
    f.reference = std::move(r);
  }
  return f;
}

// The fields of all objects of a rest_object, in one query, by object id.
std::map<Id, std::vector<Data_mapping_field>> get_data_mapping_fields(
    Db_session *session, const Id &rest_object_id) {
  const auto result = session->query(R"(
SELECT f.id, f.data_mapping_id, f.parent_reference_id, f.represents_reference_id,
    f.name, f.position, f.db_column, f.enabled, f.allow_filtering,
    f.allow_sorting, f.no_check, f.no_update, f.json_schema, f.options,
    f.sdk_options, f.comments,
    r.reduce_to_value_of_field_id,
    r.row_ownership_field_id AS ref_row_ownership_field_id,
    r.reference_mapping, r.unnest, r.options AS ref_options,
    r.sdk_options AS ref_sdk_options, r.comments AS ref_comments
FROM `{metadata_schema}`.`data_mapping_field` f
    LEFT OUTER JOIN `{metadata_schema}`.`data_mapping_reference` r
        ON r.id = f.represents_reference_id
WHERE f.data_mapping_id IN (SELECT id FROM `{metadata_schema}`.`data_mapping`
                      WHERE rest_object_id = ?)
ORDER BY f.data_mapping_id, f.position, f.id)",
                                     {Value::id(rest_object_id)});

  std::map<Id, std::vector<Data_mapping_field>> fields;
  for (const auto &row : result.rows) {
    auto field = field_from_row(row);
    fields[field.data_mapping_id].push_back(std::move(field));
  }
  return fields;
}

Id schema_id_of_rest_object(Db_session *session, const Id &rest_object_id) {
  const auto result = session->query(
      "SELECT rest_schema_id FROM " + sql::metadata_table("rest_object") +
          " WHERE id = ?",
      {Value::id(rest_object_id)});
  if (result.empty()) {
    throw std::runtime_error("The specified rest_object with id " +
                             rest_object_id + " was not found.");
  }
  return result.first()["rest_schema_id"].as_string();
}

void check_object_names(Db_session *session, const Id &schema_id,
                        const std::vector<Data_mapping> &objects) {
  std::vector<std::string> assigned;
  for (const auto &object : objects) {
    if (std::find(assigned.begin(), assigned.end(), object.name) != assigned.end()) {
      throw std::runtime_error("The object name " + object.name +
                               " has been used more than once.");
    }
    if (object_name_in_use(session, schema_id, object.id, object.name)) {
      throw std::runtime_error("The object name " + object.name +
                               " is already in use on this REST schema.");
    }
    assigned.push_back(object.name);
  }
}

// The privileges of a crud_operations SET value.
std::vector<std::string> privileges_of(const std::vector<std::string> &crud_operations) {
  std::vector<std::string> privileges;
  for (const auto &operation : crud_operations) {
    if (operation == "CREATE") {
      privileges.push_back("INSERT");
    } else if (operation == "READ") {
      privileges.push_back("SELECT");
    } else if (operation == "UPDATE") {
      privileges.push_back("UPDATE");
    } else if (operation == "DELETE") {
      privileges.push_back("DELETE");
    } else {
      throw std::runtime_error("The given CRUD operation " + operation +
                               " does not exist.");
    }
  }
  return privileges;
}

std::string grant_statement(const Db_session *session, const std::string &privileges,
                            std::string_view object_type,
                            std::string_view schema_name, std::string_view name) {
  std::string statement = "GRANT " + privileges + " ON ";
  if (is_routine_type(object_type)) statement += std::string(object_type) + " ";
  statement += sql::quote_qualified(schema_name, name);
  statement +=
      " TO " + sql::quote_identifier(role_name(session, k_data_provider_role));
  return statement;
}

std::string verified_privilege(const std::string &privilege) {
  static const std::vector<std::string> k_valid{
      "ALTER",       "ALTER ROUTINE", "CREATE",  "CREATE ROUTINE",
      "CREATE TEMPORARY TABLES",      "CREATE VIEW", "DELETE",  "DROP",
      "EVENT",       "EXECUTE",       "INDEX",   "INSERT",      "LOCK TABLES",
      "REFERENCES",  "SELECT",        "SHOW DATABASES", "SHOW VIEW",
      "TRIGGER",     "UPDATE",        "USAGE"};
  if (std::find(k_valid.begin(), k_valid.end(), to_upper(privilege)) ==
      k_valid.end()) {
    throw std::runtime_error("Invalid privilege " + privilege +
                             " specified. Valid privileges are " +
                             join(k_valid, ", ") + ".");
  }
  return privilege;
}

// One privilege of an explicit grant: "SELECT" or {"privilege": "SELECT",
// "columnList": ["a", "b"]}.
std::string explicit_privilege(const json::Value &value) {
  if (value.is_string()) return verified_privilege(value.as_string());
  if (!value.is_object()) {
    throw std::runtime_error("Invalid privilege definition in the grants option.");
  }
  std::string result = verified_privilege(value.get_string("privilege"));
  if (const auto *columns = value.get("columnList"); columns && columns->is_array()) {
    std::vector<std::string> names;
    for (const auto &column : columns->as_array()) {
      names.push_back(column.is_string() ? column.as_string() : column.dump());
    }
    result += " (" + join(names, ", ") + ")";
  }
  return result;
}

// The statements of the "grants" option: one grant document or a list of
// {"schema", "object", "objectType", "privileges"}.
std::vector<std::string> explicit_grant_statements(const Db_session *session,
                                                   const json::Value &grants) {
  std::vector<json::Value> entries;
  if (grants.is_object()) {
    entries.push_back(grants);
  } else if (grants.is_array()) {
    entries = grants.as_array();
  }

  std::vector<std::string> statements;
  for (const auto &grant : entries) {
    std::vector<std::string> privileges;
    if (const auto *list = grant.get("privileges")) {
      if (list->is_array()) {
        for (const auto &privilege : list->as_array()) {
          privileges.push_back(explicit_privilege(privilege));
        }
      } else {
        privileges.push_back(explicit_privilege(*list));
      }
    }
    if (privileges.empty()) continue;

    statements.push_back(grant_statement(session, join(privileges, ", "),
                                         grant.get_string("objectType"),
                                         grant.get_string("schema"),
                                         grant.get_string("object")));
  }
  return statements;
}

// -- SHOW CREATE output ------------------------------------------------------

std::string cut_last_comma(const std::string &text) {
  if (text.size() >= 2 && text.compare(text.size() - 2, 2, ",\n") == 0) {
    return text.substr(0, text.size() - 2);
  }
  if (text.empty()) return text;
  return text.substr(0, text.size() - 1);
}

std::string field_attributes(const Data_mapping_field &field, bool add_datatype,
                             const std::optional<Id> &row_ownership_field_id) {
  std::vector<std::string> attributes;
  const auto &column = *field.db_column;

  std::string inout = "@";
  if (column.get_bool("in")) inout += "IN";
  if (column.get_bool("out")) inout += "OUT";
  if (inout != "@") attributes.push_back(inout);

  if (column.get_bool("is_primary")) attributes.push_back("@KEY");
  if (field.no_check) attributes.push_back("@NOCHECK");
  if (field.no_update) attributes.push_back("@NOUPDATE");
  if (field.allow_sorting) attributes.push_back("@SORTABLE");
  if (!field.allow_filtering) attributes.push_back("@NOFILTERING");
  if (add_datatype) {
    const auto datatype = column.get_string("datatype");
    if (!datatype.empty()) attributes.push_back("@DATATYPE(\"" + datatype + "\")");
  }
  if (row_ownership_field_id == field.id) attributes.push_back("@ROWOWNERSHIP");

  return join(attributes, " ");
}

std::string reference_attributes(const Data_mapping_reference &reference) {
  std::vector<std::string> attributes;
  if (mapping_option(reference.options, "dataMappingViewInsert")) {
    attributes.push_back("@INSERT");
  }
  if (mapping_option(reference.options, "dataMappingViewUpdate")) {
    attributes.push_back("@UPDATE");
  }
  if (mapping_option(reference.options, "dataMappingViewDelete")) {
    attributes.push_back("@DELETE");
  }
  if (mapping_option(reference.options, "dataMappingViewNoCheck")) {
    attributes.push_back("@NOCHECK");
  }
  if (reference.unnest || reference.reduce_to_value_of_field_id) {
    attributes.push_back("@UNNEST");
  }
  return join(attributes, " ");
}

// The fields below one parent as `name: column @ATTRIBUTES,` lines, nested
// references with their own block. Every line ends in ",\n"; the caller
// cuts the last comma.
std::string walk(const std::vector<Data_mapping_field> &fields,
                 const std::optional<Id> &parent_reference_id, int level,
                 bool add_datatype,
                 const std::optional<Id> &row_ownership_field_id) {
  const std::string indent(static_cast<size_t>(level) * 4, ' ');
  std::string result;

  for (const auto &field : fields) {
    if (field.parent_reference_id != parent_reference_id) continue;

    if (!field.reference) {
      if (!field.enabled || !field.db_column) continue;
      result += indent + field.name + ": " + field.column_name();
      const auto attributes =
          field_attributes(field, add_datatype, row_ownership_field_id);
      if (!attributes.empty()) result += " " + attributes;
      result += ",\n";
      continue;
    }

    const auto &reference = *field.reference;
    if (!reference.unnest && !field.enabled) continue;

    result += indent + field.name + ": " + reference.referenced_schema() + "." +
              reference.referenced_table();
    const auto attributes = reference_attributes(reference);
    if (!attributes.empty()) result += " " + attributes;

    const auto children =
        cut_last_comma(walk(fields, reference.id, level + 1, add_datatype,
                            reference.row_ownership_field_id));
    if (!children.empty()) {
      result += " {\n" + children + "\n" + indent + "}";
    }
    result += ",\n";
  }
  return result;
}

std::string class_header(const Data_mapping &object) {
  std::string header = "CLASS " + object.name;
  if (mapping_option(object.options, "dataMappingViewInsert")) header += " @INSERT";
  if (mapping_option(object.options, "dataMappingViewUpdate")) header += " @UPDATE";
  if (mapping_option(object.options, "dataMappingViewDelete")) header += " @DELETE";
  if (mapping_option(object.options, "dataMappingViewNoCheck")) header += " @NOCHECK";
  return header;
}

bool is_empty_json_object(const std::string &text) {
  const auto doc = json::try_parse(text);
  return doc && doc->is_object() && doc->as_object().empty();
}

}  // namespace

// -- rest_object rows -----------------------------------------------------------

std::optional<Rest_object> get_rest_object(Db_session *session, const Id &id) {
  auto rest_objects = query_rest_objects(session, "o.id = ?", {Value::id(id)});
  if (rest_objects.empty()) return std::nullopt;
  return std::move(rest_objects.front());
}

std::optional<Rest_object> find_rest_object(Db_session *session,
                                        const Id &schema_id,
                                        std::string_view request_path) {
  auto rest_objects =
      query_rest_objects(session, "o.rest_schema_id = ? AND o.request_path = ?",
                       {Value::id(schema_id), request_path});
  if (rest_objects.empty()) return std::nullopt;
  return std::move(rest_objects.front());
}

std::vector<Rest_object> get_rest_objects(Db_session *session, const Id &schema_id,
                                      const std::vector<std::string> &object_types) {
  std::string where = "o.rest_schema_id = ?";
  std::vector<Value> params{Value::id(schema_id)};
  if (!object_types.empty()) {
    std::vector<std::string> placeholders(object_types.size(), "?");
    where += " AND o.object_type IN (" + join(placeholders, ", ") + ")";
    params.insert(params.end(), object_types.begin(), object_types.end());
  }
  return query_rest_objects(session, where, std::move(params));
}

Id add_rest_object(Db_session *session, const Rest_object_definition &definition,
                 const std::vector<Data_mapping> &objects) {
  static const std::vector<std::string> k_types{"TABLE", "VIEW", "PROCEDURE",
                                                "FUNCTION", "SCRIPT"};
  if (std::find(k_types.begin(), k_types.end(), definition.object_type) ==
      k_types.end()) {
    throw std::runtime_error(
        "Invalid rest_object_type. Only valid types are TABLE, VIEW, PROCEDURE, "
        "FUNCTION and SCRIPT.");
  }

  const Id id = definition.id ? *definition.id : new_id(session);
  const auto crud_operations =
      calculate_crud_operations(definition.object_type, objects);

  sql::Insert insert("rest_object");
  insert.set("id", sql::Value::id(id));
  insert.set("rest_schema_id", sql::Value::id(definition.rest_schema_id));
  insert.set("name", definition.name);
  insert.set("request_path", definition.request_path);
  insert.set("object_type", definition.object_type);
  insert.set("enabled", definition.enabled.value_or(1));
  insert.set("items_per_page", definition.items_per_page);
  insert.set("requires_auth", definition.requires_auth.value_or(true));
  insert.set("crud_operations", join(crud_operations, ","));
  insert.set("format", definition.format.value_or("FEED"));
  insert.set("comments", definition.comments.value_or(""));
  insert.set("media_type", definition.media_type);
  insert.set("metadata", definition.metadata);
  insert.set("auto_detect_media_type", definition.auto_detect_media_type);
  insert.set("auth_stored_procedure", definition.auth_stored_procedure);
  insert.set("options", definition.options);
  insert.set("internal", definition.internal);
  session->execute(insert);

  // A new rest_object has no objects to delete yet (see set_objects)
  check_object_names(session, definition.rest_schema_id, objects);
  insert_objects(session, id, objects);
  return id;
}

void update_rest_object(Db_session *session, const Id &id,
                      const Rest_object_changes &changes) {
  sql::Update update("rest_object");
  update.set_if("request_path", changes.request_path);
  update.set_if("enabled", changes.enabled);
  update.set_if("requires_auth", changes.requires_auth);
  update.set_if("items_per_page", changes.items_per_page);
  update.set_if("format", changes.format);
  update.set_if("comments", changes.comments);
  update.set_if("media_type", changes.media_type);
  if (changes.auto_detect_media_type) {
    update.set("auto_detect_media_type", *changes.auto_detect_media_type);
  }
  if (changes.auth_stored_procedure) {
    update.set("auth_stored_procedure", *changes.auth_stored_procedure);
  }
  update.set_if("metadata", changes.metadata);
  if (changes.crud_operations) {
    update.set("crud_operations", join(*changes.crud_operations, ","));
  }
  if (changes.options) {
    set_json_options(session, &update, "rest_object", id, *changes.options,
                     changes.merge_options);
  }

  if (update.empty()) return;
  update.where("id", Value::id(id));
  session->execute(update);
}

std::optional<std::string> options_after(Db_session *session,
                                         const Rest_object &rest_object,
                                         const Rest_object_changes &changes) {
  if (!changes.options) return rest_object.options;
  if (!changes.merge_options || !rest_object.options) return changes.options;
  return session
      ->query("SELECT JSON_MERGE_PATCH(?, ?) AS options",
              {Value(*rest_object.options), Value(*changes.options)})
      .first()["options"]
      .as_string();
}

void delete_rest_object(Db_session *session, const Id &id) {
  const auto rest_object = get_rest_object(session, id);
  if (!rest_object) {
    throw std::runtime_error("The specified rest_object with id " + id +
                             " was not found.");
  }
  revoke_all_from_rest_object(session, rest_object->schema_name, rest_object->name,
                            rest_object->object_type);

  // The objects, fields and references go with it (BEFORE DELETE triggers)
  if (session->execute(sql::Delete("rest_object").where("id", Value::id(id))) == 0) {
    throw std::runtime_error("The specified rest_object with id " + id +
                             " was not found.");
  }
}

// -- The data mapping ---------------------------------------------------------

std::string mapping_options_json(bool insert, bool update, bool del,
                                 bool no_check) {
  json::Value doc = json::Value::object();
  doc.set("dataMappingViewInsert", json::Value(insert));
  doc.set("dataMappingViewUpdate", json::Value(update));
  doc.set("dataMappingViewDelete", json::Value(del));
  doc.set("dataMappingViewNoCheck", json::Value(no_check));
  return doc.dump();
}

bool mapping_option(const std::optional<std::string> &options,
                    std::string_view key) {
  if (!options) return false;
  const auto doc = json::try_parse(*options);
  return doc && doc->get_bool(key);
}

bool has_objects(Db_session *session, const Id &rest_object_id) {
  return !session
              ->query("SELECT 1 FROM " + sql::metadata_table("data_mapping") +
                          " WHERE rest_object_id = ? LIMIT 1",
                      {Value::id(rest_object_id)})
              .empty();
}

std::vector<Data_mapping> get_objects(Db_session *session,
                                           const Id &rest_object_id) {
  const auto result = session->query(
      "SELECT id, name, kind, position, row_ownership_field_id, options, "
      "sdk_options, comments FROM " +
          sql::metadata_table("data_mapping") +
          " WHERE rest_object_id = ? ORDER BY position",
      {Value::id(rest_object_id)});

  std::vector<Data_mapping> objects;
  for (const auto &row : result.rows) {
    Data_mapping o;
    o.id = row["id"].as_string();
    o.name = row["name"].as_string();
    o.kind = row["kind"].as_string();
    o.position = static_cast<int>(row["position"].as_int());
    o.row_ownership_field_id = row["row_ownership_field_id"].as_optional_string();
    o.options = row["options"].as_optional_string();
    o.sdk_options = row["sdk_options"].as_optional_string();
    o.comments = row["comments"].as_optional_string();
    objects.push_back(std::move(o));
  }

  if (!objects.empty()) {
    auto fields = get_data_mapping_fields(session, rest_object_id);
    for (auto &object : objects) {
      if (auto it = fields.find(object.id); it != fields.end()) {
        object.fields = std::move(it->second);
      }
    }
  }
  return objects;
}

void set_objects(Db_session *session, const Id &rest_object_id,
                 const std::vector<Data_mapping> &objects) {
  // The fields and references of the objects go with them (triggers)
  session->execute(
      sql::Delete("data_mapping").where("rest_object_id", Value::id(rest_object_id)));

  check_object_names(session, schema_id_of_rest_object(session, rest_object_id),
                     objects);

  insert_objects(session, rest_object_id, objects);
}

void update_object(Db_session *session, const Id &data_mapping_id,
                   const std::string &name,
                   const std::optional<std::string> &options) {
  sql::Update update("data_mapping");
  update.set("name", name);
  update.set("options", options ? sql::Value(with_legacy_option_keys(*options))
                                : sql::Value());
  update.where("id", Value::id(data_mapping_id));
  session->execute(update);
}

bool object_name_in_use(Db_session *session, const Id &schema_id,
                        const Id &data_mapping_id, std::string_view name) {
  const auto result = session->query(
      "SELECT o.name FROM " + sql::metadata_table("data_mapping") + " o LEFT JOIN " +
      sql::metadata_table("rest_object") +
          " dbo ON o.rest_object_id = dbo.id WHERE dbo.rest_schema_id = ? AND "
          "UPPER(o.name) = UPPER(?) AND o.id <> ?",
      {Value::id(schema_id), name, Value(data_mapping_id)});
  return !result.empty();
}

std::vector<std::string> calculate_crud_operations(
    std::string_view object_type, const std::vector<Data_mapping> &objects) {
  if (object_type == "SCRIPT") return {"CREATE", "READ", "UPDATE"};

  // A routine is called with POST
  if (is_routine_type(object_type)) return {"CREATE"};

  if (objects.empty()) {
    throw std::runtime_error("No object result definition present.");
  }

  const auto &object = objects.front();
  std::vector<std::string> crud{"READ"};
  if (mapping_option(object.options, "dataMappingViewInsert")) crud.push_back("CREATE");
  if (mapping_option(object.options, "dataMappingViewUpdate")) crud.push_back("UPDATE");
  if (mapping_option(object.options, "dataMappingViewDelete")) crud.push_back("DELETE");

  // Changing nested documents updates the top level document, so a
  // reference allowing any change makes the object updatable
  const bool has_update = std::find(crud.begin(), crud.end(), "UPDATE") != crud.end();
  if (!has_update) {
    for (const auto &field : object.fields) {
      if (!field.reference) continue;
      const auto &ref_options = field.reference->options;
      if (mapping_option(ref_options, "dataMappingViewInsert") ||
          mapping_option(ref_options, "dataMappingViewUpdate") ||
          mapping_option(ref_options, "dataMappingViewDelete")) {
        crud.push_back("UPDATE");
        break;
      }
    }
  }
  return crud;
}

// -- Privileges ---------------------------------------------------------------

std::vector<std::string> grant_statements(
    const Db_session *session, std::string_view schema_name, std::string_view name,
    std::string_view object_type,
    const std::vector<std::string> &crud_operations,
    const std::vector<Data_mapping> &objects,
    const std::optional<std::string> &options) {
  // The information_schema cannot be granted
  if (to_lower(schema_name) == "information_schema") return {};

  const auto doc = options ? json::try_parse(*options) : std::nullopt;
  const bool automatic = !(doc && doc->get_bool("disableAutomaticGrants"));

  std::vector<std::string> statements;

  std::vector<std::string> privileges;
  if (is_routine_type(object_type)) {
    privileges = {"EXECUTE"};
  } else if (to_lower(schema_name) == "performance_schema") {
    privileges = {"SELECT"};
  } else {
    privileges = privileges_of(crud_operations);
  }
  if (privileges.empty()) {
    throw std::runtime_error("No valid CRUD Operation specified");
  }

  if (automatic) {
    const auto privilege_list = join(privileges, ",");
    statements.push_back(
        grant_statement(session, privilege_list, object_type, schema_name, name));

    // The tables of the references are read and changed through the
    // data mapping as well
    if (!is_routine_type(object_type)) {
      for (const auto &object : objects) {
        for (const auto &field : object.fields) {
          if (!field.reference) continue;
          if (!field.reference->unnest && !field.enabled) continue;
          statements.push_back(grant_statement(
              session, privilege_list, "TABLE", field.reference->referenced_schema(),
              field.reference->referenced_table()));
        }
      }
    }
  }

  if (doc) {
    if (const auto *grants = doc->get("grants")) {
      for (auto &statement : explicit_grant_statements(session, *grants)) {
        statements.push_back(std::move(statement));
      }
    }
  }
  return statements;
}

std::vector<std::string> option_grant_statements(
    const Db_session *session, const std::optional<std::string> &options) {
  const auto doc = options ? json::try_parse(*options) : std::nullopt;
  if (!doc) return {};
  const auto *grants = doc->get("grants");
  return grants ? explicit_grant_statements(session, *grants)
                : std::vector<std::string>{};
}

void revoke_all_from_rest_object(Db_session *session, std::string_view schema_name,
                               std::string_view name,
                               std::string_view object_type) {
  const auto schema = to_lower(schema_name);
  if (schema == "information_schema") return;

  std::string what;
  if (schema == "performance_schema") {
    what = "SELECT ON";  // ALL PRIVILEGES cannot be revoked there
  } else if (object_type == "PROCEDURE") {
    what = "EXECUTE ON PROCEDURE";
  } else if (object_type == "FUNCTION") {
    what = "EXECUTE ON FUNCTION";
  } else {
    what = "ALL PRIVILEGES ON";
  }

  try {
    session->execute("REVOKE " + what + " " + sql::quote_qualified(schema_name, name) +
                     " FROM " +
                     sql::quote_identifier(role_name(session, k_data_provider_role)));
  } catch (const Db_error &e) {
    // Nothing to revoke is fine: the privileges may have been revoked
    // before, or never granted (ER_NONEXISTING_GRANT,
    // ER_NONEXISTING_TABLE_GRANT, ER_NONEXISTING_PROC_GRANT)
    if (e.code() != 1141 && e.code() != 1147 && e.code() != 1403) throw;
  }
}

// -- SHOW CREATE --------------------------------------------------------------

std::string rest_object_create_statement(Db_session *session,
                                       const Rest_object &rest_object,
                                       bool on_current_service) {
  const auto objects = get_objects(session, rest_object.id);
  const std::string object_type =
      rest_object.object_type == "TABLE" ? "VIEW" : rest_object.object_type;

  std::vector<std::string> lines{
      "CREATE OR REPLACE REST " + object_type + " " +
          quote_request_path(rest_object.request_path),
      "    ON " + (on_current_service ? "" : "SERVICE " + rest_object.host_ctx + " ") +
          "SCHEMA " + quote_request_path(rest_object.schema_request_path),
      "    AS " + sql::quote_qualified(rest_object.schema_name, rest_object.name)};

  if (!rest_object.is_routine()) {
    if (!objects.empty()) {
      const auto &object = objects.front();
      lines.back() += " " + class_header(object) + " {";
      lines.push_back(cut_last_comma(
          walk(object.fields, std::nullopt, 2, false, object.row_ownership_field_id)));
      lines.push_back("    }");
    }
  } else {
    for (const auto &object : objects) {
      std::string line = "    " + object.kind + " " + object.name;
      const auto children =
          cut_last_comma(walk(object.fields, std::nullopt, 2, object.kind == "RESULT",
                              object.row_ownership_field_id));
      // The grammar needs the braces, also when there are no fields
      line += children.empty() ? std::string(" {}")
                               : " {\n" + children + "\n    }";
      lines.push_back(std::move(line));
    }
  }

  if (rest_object.enabled == 2) {
    lines.push_back("    PRIVATE");
  } else if (rest_object.enabled == 0) {
    lines.push_back("    DISABLED");
  }
  lines.push_back(rest_object.requires_auth ? "    AUTHENTICATION REQUIRED"
                                          : "    AUTHENTICATION NOT REQUIRED");
  if (rest_object.items_per_page && *rest_object.items_per_page != 25) {
    lines.push_back("    ITEMS PER PAGE " + std::to_string(*rest_object.items_per_page));
  }
  if (rest_object.comments && !rest_object.comments->empty()) {
    lines.push_back("    COMMENT " + sql::quote(*rest_object.comments));
  }
  if (rest_object.media_type) {
    lines.push_back("    MEDIA TYPE " + sql::quote(*rest_object.media_type));
  }
  if (rest_object.auto_detect_media_type) lines.push_back("    MEDIA TYPE AUTODETECT");
  if (rest_object.format != "FEED") lines.push_back("    FORMAT " + rest_object.format);
  if (rest_object.auth_stored_procedure && !rest_object.auth_stored_procedure->empty()) {
    lines.push_back("    AUTHENTICATION PROCEDURE " + *rest_object.auth_stored_procedure);
  }
  if (rest_object.options && !is_empty_json_object(*rest_object.options)) {
    lines.push_back(format_json_entry("OPTIONS", rest_object.options));
  }
  if (rest_object.metadata && !is_empty_json_object(*rest_object.metadata)) {
    lines.push_back(format_json_entry("METADATA", rest_object.metadata));
  }

  return join(lines, "\n") + ";";
}

// -- Cloning ------------------------------------------------------------------

Id clone_rest_object(Db_session *session, const Rest_object &rest_object,
                   const Id &new_schema_id) {
  auto objects = get_objects(session, rest_object.id);

  // Every id of the mapping gets a new one; the links between fields and
  // references are kept by mapping each old id to the same new id
  std::map<Id, Id> ids;
  const auto remap = [&](Id *id) {
    auto it = ids.find(*id);
    if (it == ids.end()) it = ids.emplace(*id, new_id(session)).first;
    *id = it->second;
  };
  const auto remap_optional = [&](std::optional<Id> *id) {
    if (*id) remap(&**id);
  };

  for (auto &object : objects) {
    object.id = new_id(session);
    remap_optional(&object.row_ownership_field_id);
    for (auto &field : object.fields) {
      field.data_mapping_id = object.id;
      remap(&field.id);
      remap_optional(&field.parent_reference_id);
      if (field.reference) {
        remap(&field.reference->id);
        remap_optional(&field.reference->reduce_to_value_of_field_id);
        remap_optional(&field.reference->row_ownership_field_id);
      }
    }
  }

  Rest_object_definition definition;
  definition.rest_schema_id = new_schema_id;
  definition.name = rest_object.name;
  definition.request_path = rest_object.request_path;
  definition.object_type = rest_object.object_type;
  definition.enabled = rest_object.enabled;
  definition.items_per_page = rest_object.items_per_page;
  definition.requires_auth = rest_object.requires_auth;
  definition.format = rest_object.format;
  definition.comments = rest_object.comments;
  definition.media_type = rest_object.media_type;
  definition.auto_detect_media_type = rest_object.auto_detect_media_type;
  definition.auth_stored_procedure = rest_object.auth_stored_procedure;
  definition.options = rest_object.options;
  definition.metadata = rest_object.metadata;
  definition.internal = rest_object.internal;

  // The database object is the same one, so its grants are in place already
  return add_rest_object(session, definition, objects);
}

// -- Database schema ----------------------------------------------------------

std::optional<std::string> database_object_type(Db_session *session,
                                                std::string_view schema_name,
                                                std::string_view name) {
  const auto result = session->query(
      "SELECT TABLE_TYPE FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_SCHEMA = ? "
      "AND TABLE_NAME = ?",
      {schema_name, name});
  if (result.empty()) return std::nullopt;
  // BASE TABLE, SYSTEM VERSIONED, ... are tables; VIEW and SYSTEM VIEW not
  const auto type = result.first()["TABLE_TYPE"].as_string();
  return type.find("TABLE") != std::string::npos ? "TABLE" : "VIEW";
}

std::vector<Routine_parameter> get_routine_parameters(
    Db_session *session, std::string_view schema_name, std::string_view name,
    std::string_view routine_type) {
  // The return value of a function has no mode and is left out
  const auto result = session->query(
      "SELECT ORDINAL_POSITION AS position, PARAMETER_NAME AS name, "
      "PARAMETER_MODE AS mode, DTD_IDENTIFIER AS datatype, "
      "CHARACTER_SET_NAME AS charset, COLLATION_NAME AS collation "
      "FROM INFORMATION_SCHEMA.PARAMETERS WHERE SPECIFIC_SCHEMA = ? AND "
      "SPECIFIC_NAME = ? AND ROUTINE_TYPE = ? AND NOT ISNULL(PARAMETER_MODE) "
      "ORDER BY ORDINAL_POSITION",
      {schema_name, name, routine_type});

  std::vector<Routine_parameter> parameters;
  for (const auto &row : result.rows) {
    Routine_parameter p;
    p.position = static_cast<int>(row["position"].as_int());
    p.name = row["name"].as_string();
    p.mode = row["mode"].as_string();
    p.datatype = row["datatype"].as_string();
    p.charset = row["charset"].as_optional_string();
    p.collation = row["collation"].as_optional_string();
    parameters.push_back(std::move(p));
  }
  return parameters;
}

bool routine_exists(Db_session *session, std::string_view schema_name,
                    std::string_view name, std::string_view routine_type) {
  return !session
              ->query("SELECT 1 FROM INFORMATION_SCHEMA.ROUTINES WHERE "
                      "ROUTINE_SCHEMA = ? AND ROUTINE_NAME = ? AND "
                      "ROUTINE_TYPE = ?",
                      {schema_name, name, routine_type})
              .empty();
}

std::optional<std::string> get_function_return_type(Db_session *session,
                                                    std::string_view schema_name,
                                                    std::string_view name) {
  const auto result = session->query(
      "SELECT DATA_TYPE FROM INFORMATION_SCHEMA.ROUTINES WHERE ROUTINE_SCHEMA = ? "
      "AND ROUTINE_NAME = ? AND ROUTINE_TYPE = 'FUNCTION'",
      {schema_name, name});
  if (result.empty()) return std::nullopt;
  return result.first()["DATA_TYPE"].as_string();
}

namespace {

// The plain columns of a table or view, in the layout of the
// table_columns_with_references procedure. The procedure runs with the
// ONLY_FULL_GROUP_BY mode it was created with, so on MariaDB it cannot open
// views that group loosely (like sakila.film_list) and returns no rows for
// them; this query runs with the session's mode instead.
Db_result query_plain_columns(Db_session *session, std::string_view schema_name,
                              std::string_view name) {
  return session->query(
      "SELECT c.ORDINAL_POSITION AS position, c.COLUMN_NAME AS name, "
      "JSON_OBJECT('name', c.COLUMN_NAME, 'datatype', c.COLUMN_TYPE, "
      "'not_null', c.IS_NULLABLE = 'NO', 'is_primary', c.COLUMN_KEY = 'PRI', "
      "'is_unique', c.COLUMN_KEY = 'UNI', "
      "'is_generated', c.GENERATION_EXPRESSION <> '', "
      "'id_generation', IF(c.EXTRA = 'auto_increment', 'auto_inc', NULL), "
      "'comment', c.COLUMN_COMMENT, 'srid', NULL, "
      "'column_default', c.COLUMN_DEFAULT, "
      "'charset', c.CHARACTER_SET_NAME, 'collation', c.COLLATION_NAME) "
      "AS db_column, NULL AS reference_mapping "
      "FROM INFORMATION_SCHEMA.COLUMNS AS c WHERE c.TABLE_SCHEMA = ? AND "
      "c.TABLE_NAME = ? ORDER BY c.ORDINAL_POSITION",
      {schema_name, name});
}

}  // namespace

std::vector<Table_column> get_table_columns_with_references(
    Db_session *session, std::string_view schema_name, std::string_view name) {
  auto result = session->query(
      "CALL " + sql::metadata_table("table_columns_with_references") + "(?, ?)",
      {schema_name, name});
  if (result.rows.empty()) {
    result = query_plain_columns(session, schema_name, name);
  }

  std::vector<Table_column> columns;
  for (const auto &row : result.rows) {
    Table_column c;
    c.position = static_cast<int>(row["position"].as_int());
    c.name = row["name"].as_string();
    if (const auto db_column = row["db_column"].as_optional_string()) {
      c.db_column = json::parse(*db_column);
    }
    if (const auto mapping = row["reference_mapping"].as_optional_string()) {
      c.reference_mapping = json::parse(*mapping);
    }
    columns.push_back(std::move(c));
  }
  return columns;
}

// -- Naming -------------------------------------------------------------------

std::string snake_to_camel_case(std::string_view snake) {
  std::string result;
  for (const auto &part : split(to_lower(snake), '_')) {
    if (part.empty()) continue;
    result += static_cast<char>(std::toupper(static_cast<unsigned char>(part[0])));
    result += part.substr(1);
  }
  if (!result.empty()) {
    result[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[0])));
  }
  return result;
}

std::string path_to_camel_case(std::string_view path) {
  if (!path.empty() && path[0] == '/') path.remove_prefix(1);

  std::string joined(path);
  std::replace(joined.begin(), joined.end(), '/', '_');

  std::string camel;
  bool first = true;
  for (const auto &part : split(joined, '_')) {
    if (first) {
      camel += part;
      first = false;
    } else if (!part.empty()) {
      camel += static_cast<char>(std::toupper(static_cast<unsigned char>(part[0])));
      camel += part.substr(1);
    }
  }

  std::string result;
  for (const char c : camel) {
    if (std::isalnum(static_cast<unsigned char>(c))) result += c;
  }
  return result;
}

std::string path_to_pascal_case(std::string_view path) {
  auto result = path_to_camel_case(path);
  if (!result.empty()) {
    result[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(result[0])));
  }
  return result;
}

}  // namespace metadata
}  // namespace mrs
