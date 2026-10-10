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

#ifndef MODULES_MRS_CORE_MRS_METADATA_REST_OBJECTS_H_
#define MODULES_MRS_CORE_MRS_METADATA_REST_OBJECTS_H_

// Access to the REST database objects of the metadata schema: the rest_object
// table and its data mapping (data_mapping, data_mapping_field,
// data_mapping_reference).

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "modules/mrs/core/mrs_db_session.h"
#include "modules/mrs/core/mrs_json.h"
#include "modules/mrs/core/mrs_metadata.h"

namespace mrs {
namespace metadata {

// The role the REST data is read through (role_name(session,
// k_data_provider_role)); the objects exposed via REST are granted to it.
inline constexpr std::string_view k_data_provider_role = "data_provider";

struct Rest_object {
  Id id;
  Id rest_schema_id;
  Id service_id;
  std::string name;         // the table, view, procedure or function
  std::string schema_name;  // its database schema
  std::string request_path;
  std::string schema_request_path;
  std::string host_ctx;
  std::string object_type;  // TABLE, VIEW, PROCEDURE, FUNCTION, SCRIPT
  std::vector<std::string> crud_operations;  // CREATE, READ, UPDATE, DELETE
  std::string format;  // FEED, ITEM, MEDIA
  int enabled = 1;
  bool internal = false;
  bool requires_auth = false;
  std::optional<int64_t> items_per_page;
  std::optional<std::string> media_type;
  bool auto_detect_media_type = false;
  std::optional<std::string> auth_stored_procedure;
  std::optional<std::string> comments;
  std::optional<std::string> options;
  std::optional<std::string> metadata;

  bool is_routine() const {
    return object_type == "PROCEDURE" || object_type == "FUNCTION";
  }
};

std::optional<Rest_object> get_rest_object(Db_session *session, const Id &id);
std::optional<Rest_object> find_rest_object(Db_session *session,
                                        const Id &schema_id,
                                        std::string_view request_path);
// The objects of a schema, optionally limited to the given object types.
std::vector<Rest_object> get_rest_objects(Db_session *session, const Id &schema_id,
                                      const std::vector<std::string> &object_types);

void delete_rest_object(Db_session *session, const Id &id);

// The CREATE OR REPLACE REST VIEW / PROCEDURE / FUNCTION statement of an
// object, with its data mapping. With on_current_service, the
// statement names no service (ON SERVICE ...) and acts on the current one,
// as in the script of SHOW CREATE REST SERVICE ... INCLUDING ... ENDPOINTS.
std::string rest_object_create_statement(Db_session *session,
                                       const Rest_object &rest_object,
                                       bool on_current_service = false);

// Copies an object with its data mapping into another schema.
Id clone_rest_object(Db_session *session, const Rest_object &rest_object,
                   const Id &new_schema_id);

// -- The data mapping -----------------------------------------------------
//
// A rest_object owns a list of objects (the RESULT of a view, the PARAMETERS
// and RESULTs of a routine). Each object holds a flat list of fields; the
// fields below a reference carry that reference's id as parent_reference_id,
// which is also how the metadata stores them.

// A reference from a table to another one (a foreign key in either
// direction), represented by a field of the mapping.
struct Data_mapping_reference {
  Id id;
  std::optional<Id> reduce_to_value_of_field_id;
  std::optional<Id> row_ownership_field_id;
  // {kind, constraint, to_many, referenced_schema, referenced_table,
  //  column_mapping: [{base, ref}]}
  json::Value reference_mapping;
  bool unnest = false;
  std::optional<std::string> options;  // JSON, see mapping_options_json()
  std::optional<std::string> sdk_options;
  std::optional<std::string> comments;

  std::string referenced_schema() const {
    return reference_mapping.get_string("referenced_schema");
  }
  std::string referenced_table() const {
    return reference_mapping.get_string("referenced_table");
  }
  std::string kind() const { return reference_mapping.get_string("kind"); }
};

struct Data_mapping_field {
  Id id;
  Id data_mapping_id;
  std::optional<Id> parent_reference_id;
  std::string name;  // the name in the JSON document
  int position = 0;
  // The column / parameter document: {name, datatype, not_null, is_primary,
  // is_unique, is_generated, in, out, ...}. Absent for a reference.
  std::optional<json::Value> db_column;
  bool enabled = false;
  bool allow_filtering = true;
  bool allow_sorting = false;
  bool no_check = false;
  bool no_update = false;
  std::optional<std::string> json_schema;  // JSON
  std::optional<std::string> options;      // JSON
  std::optional<std::string> sdk_options;  // JSON
  std::optional<std::string> comments;

  // The reference this field represents. While a mapping is being built,
  // a field for a reference the statement did not enable keeps only the
  // candidate mapping below and is stored as a plain, disabled field.
  std::optional<Data_mapping_reference> reference;
  std::optional<json::Value> candidate_reference_mapping;

  bool is_reference() const { return reference.has_value(); }
  std::string column_name() const {
    return db_column ? db_column->get_string("name") : std::string();
  }
};

struct Data_mapping {
  Id id;
  std::string name;
  std::string kind = "RESULT";  // RESULT or PARAMETERS
  int position = 0;
  std::optional<Id> row_ownership_field_id;
  std::optional<std::string> options;  // JSON, see mapping_options_json()
  std::optional<std::string> sdk_options;
  std::optional<std::string> comments;
  std::vector<Data_mapping_field> fields;
};

// The options document of an object or reference holding the data mapping
// view flags: {"dataMappingViewInsert": ..., "dataMappingViewUpdate": ...,
// "dataMappingViewDelete": ..., "dataMappingViewNoCheck": ...}.
std::string mapping_options_json(bool insert, bool update, bool del,
                                 bool no_check);
// Reads one of the dataMappingView* flags of such a document.
bool mapping_option(const std::optional<std::string> &options,
                    std::string_view key);

// The objects of a rest_object with their fields, ordered by position.
std::vector<Data_mapping> get_objects(Db_session *session,
                                           const Id &rest_object_id);
// Whether a rest_object has any object (a result definition).
bool has_objects(Db_session *session, const Id &rest_object_id);

// Replaces the objects of a rest_object. The object names have to be unique
// within the REST schema (case-insensitively).
void set_objects(Db_session *session, const Id &rest_object_id,
                 const std::vector<Data_mapping> &objects);

// Renames the object and replaces its options, keeping the fields.
void update_object(Db_session *session, const Id &data_mapping_id,
                   const std::string &name,
                   const std::optional<std::string> &options);

// Whether an object of the given name exists in the REST schema, other
// than the one with the given id.
bool object_name_in_use(Db_session *session, const Id &schema_id,
                        const Id &data_mapping_id, std::string_view name);

// The crud_operations of a rest_object derived from its type and the options
// of its first object.
std::vector<std::string> calculate_crud_operations(
    std::string_view object_type, const std::vector<Data_mapping> &objects);

// -- rest_object rows ------------------------------------------------------

struct Rest_object_definition {
  Id rest_schema_id;
  std::string name;
  std::string request_path;
  std::string object_type;
  std::optional<int> enabled;
  std::optional<int64_t> items_per_page;
  std::optional<bool> requires_auth;  // defaults to true
  std::optional<std::string> format;  // defaults to FEED
  std::optional<std::string> comments;
  std::optional<std::string> media_type;
  bool auto_detect_media_type = false;
  std::optional<std::string> auth_stored_procedure;
  std::optional<std::string> options;
  std::optional<std::string> metadata;
  bool internal = false;
  std::optional<Id> id;  // a fixed id, for cloning
};

// Inserts the rest_object with its objects; the crud_operations are
// calculated from the objects. Returns the id. The GRANTs are not run, see
// grant_statements().
Id add_rest_object(Db_session *session, const Rest_object_definition &definition,
                 const std::vector<Data_mapping> &objects);

struct Rest_object_changes {
  std::optional<std::string> request_path;
  std::optional<int> enabled;
  std::optional<bool> requires_auth;
  std::optional<int64_t> items_per_page;
  std::optional<std::string> format;
  std::optional<std::string> comments;
  std::optional<std::string> media_type;
  std::optional<bool> auto_detect_media_type;
  std::optional<std::string> auth_stored_procedure;
  std::optional<std::string> options;
  bool merge_options = false;
  std::optional<std::string> metadata;
  std::optional<std::vector<std::string>> crud_operations;
};

void update_rest_object(Db_session *session, const Id &id,
                      const Rest_object_changes &changes);

// The options update_rest_object() leaves the rest_object with: the given ones,
// or a MERGE OPTIONS patch applied with the server's JSON_MERGE_PATCH.
std::optional<std::string> options_after(Db_session *session,
                                         const Rest_object &rest_object,
                                         const Rest_object_changes &changes);

// -- Privileges of the data provider role ---------------------------------

// The GRANT statements that let the data provider role access the database
// object (and the tables of its references), as the crud_operations of the
// rest_object require, plus the explicit grants of its options document.
std::vector<std::string> grant_statements(
    const Db_session *session, std::string_view schema_name, std::string_view name,
    std::string_view object_type,
    const std::vector<std::string> &crud_operations,
    const std::vector<Data_mapping> &objects,
    const std::optional<std::string> &options);

// The GRANT statements of the "grants" entry of an options document (one
// grant or a list), e.g. the grants an MRS script declares.
std::vector<std::string> option_grant_statements(
    const Db_session *session, const std::optional<std::string> &options);

// Revokes the privileges of the data provider role on the database object.
// Privileges that are not granted are not an error.
void revoke_all_from_rest_object(Db_session *session, std::string_view schema_name,
                               std::string_view name,
                               std::string_view object_type);

// -- Database schema (INFORMATION_SCHEMA) ---------------------------------

// TABLE or VIEW for an existing table or view, nullopt otherwise.
std::optional<std::string> database_object_type(Db_session *session,
                                                std::string_view schema_name,
                                                std::string_view name);

struct Routine_parameter {
  int position = 0;
  std::string name;
  std::string mode;  // IN, OUT, INOUT
  std::string datatype;
  std::optional<std::string> charset;
  std::optional<std::string> collation;
};

// The parameters of a PROCEDURE or FUNCTION, in declaration order.
std::vector<Routine_parameter> get_routine_parameters(
    Db_session *session, std::string_view schema_name, std::string_view name,
    std::string_view routine_type);

// Whether a PROCEDURE or FUNCTION of that name exists.
bool routine_exists(Db_session *session, std::string_view schema_name,
                    std::string_view name, std::string_view routine_type);

// The return type of a function, nullopt when there is no such function.
std::optional<std::string> get_function_return_type(Db_session *session,
                                                    std::string_view schema_name,
                                                    std::string_view name);

// A column of a table or view, or a reference to or from another table,
// as returned by the table_columns_with_references procedure.
struct Table_column {
  int position = 0;
  std::string name;  // column name, or the related table of a reference
  std::optional<json::Value> db_column;
  std::optional<json::Value> reference_mapping;

  bool is_reference() const { return reference_mapping.has_value(); }
};

std::vector<Table_column> get_table_columns_with_references(
    Db_session *session, std::string_view schema_name, std::string_view name);

// -- Naming -----------------------------------------------------------------

// city_id -> cityId
std::string snake_to_camel_case(std::string_view snake);
// /my_module/sub_path -> myModuleSubPath (alphanumeric characters only)
std::string path_to_camel_case(std::string_view path);
// /myService/sakila/city -> MyServiceSakilaCity
std::string path_to_pascal_case(std::string_view path);

}  // namespace metadata
}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_METADATA_REST_OBJECTS_H_
