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

#ifndef MODULES_MRS_CORE_MRS_AST_H_
#define MODULES_MRS_CORE_MRS_AST_H_

// The abstract syntax tree of the MariaDB REST Service (MRS) SQL extension.
//
// The parser (mrs_parser.yy) builds one Statement per REST SQL statement; the
// DDL executor (mrs_ddl_executor.h) consumes them. The tree deliberately
// depends on the standard library only, so it can be shared by the shell
// module and a server plugin.

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mrs {
namespace ast {

// A JSON value as written in the statement, re-serialised in compact form.
using Json = std::string;

// ENABLED / DISABLED / PRIVATE. The numeric values match the `enabled`
// columns of the metadata schema.
enum class Enabled_state { disabled = 0, enabled = 1, private_ = 2 };

// A string option that may also be reset to its default (`PATH DEFAULT`).
struct Text_or_default {
  bool is_default = false;
  std::string text;
};

// A REST service request path, optionally restricted to developers:
// `mike,'joe'@/myService`.
struct Service_path {
  std::vector<std::string> developers;
  std::string path;  // url_context_root, starts with '/'

  bool in_development() const { return !developers.empty(); }
};

// `[SERVICE <service>] SCHEMA <schema>` (serviceSchemaSelector)
struct Schema_selector {
  std::optional<Service_path> service;
  std::string schema_path;
};

// `ON ANY SERVICE` or `ON [SERVICE] <service>` (roleService)
struct Role_service {
  bool any_service = false;
  std::optional<Service_path> service;
};

// `schema.name` or `name`
struct Qualified_name {
  std::optional<std::string> schema;
  std::string name;
};

// A JSON options clause: `[MERGE] OPTIONS {...}`
struct Json_options {
  Json value;
  bool merge = false;
};

// -- Data mapping (GraphQL-like) definitions ---------------------------------

// @INSERT @NOINSERT @UPDATE @NOUPDATE @DELETE @NODELETE @CHECK @NOCHECK
struct Crud_annotations {
  bool insert = false, no_insert = false;
  bool update = false, no_update = false;
  bool del = false, no_delete = false;
  bool check = false, no_check = false;

  bool any() const {
    return insert || no_insert || update || no_update || del || no_delete ||
           check || no_check;
  }
  // The effective flags, as the Python implementation computed them: the
  // negated form always wins.
  bool allow_insert() const { return insert && !no_insert; }
  bool allow_update() const { return update && !no_update; }
  bool allow_delete() const { return del && !no_delete; }
  bool is_no_check() const { return no_check && !check; }
};

struct Graphql_object;

// One `key: value ...` pair of a data mapping object.
struct Graphql_field {
  enum class Mode { none, in, out, inout };

  std::string name;        // the REST field name (graphQlPairKey)
  Qualified_name source;   // the column, parameter or referenced table
  Mode mode = Mode::none;  // @IN @OUT @INOUT

  // graphQlValueOptions
  bool no_check = false;
  bool sortable = false;
  bool no_filtering = false;
  bool row_ownership = false;
  bool unnest = false;
  bool key = false;

  std::optional<std::string> datatype;  // @DATATYPE("...")
  Crud_annotations crud;                // graphQlCrudOptions
  std::optional<Json> json_schema;      // JSON SCHEMA {...}
  // Present when the pair maps a referenced table: `name: schema.table {...}`
  std::vector<Graphql_object> nested;  // 0 or 1 element
};

struct Graphql_object {
  std::vector<Graphql_field> fields;
};

// `PARAMETERS [name] {...}` and `RESULT [name] {...}`
struct Named_graphql_object {
  std::optional<std::string> name;
  Graphql_object object;
};

// -- Option groups --------------------------------------------------------

// restServiceOptions
struct Auth_app_reference {
  std::string name;
  bool if_exists = false;
};

struct Service_options {
  std::optional<bool> enabled;
  std::optional<bool> published;
  std::optional<std::string> protocol;  // HTTP or HTTPS
  std::optional<Text_or_default> auth_path;
  std::optional<Text_or_default> auth_redirection;
  std::optional<Text_or_default> auth_validation;
  std::optional<Text_or_default> auth_page_content;
  std::optional<Json_options> options;
  std::optional<std::string> comments;
  std::optional<Json> metadata;
  std::vector<Auth_app_reference> add_auth_apps;
  std::vector<Auth_app_reference> remove_auth_apps;
};

// restSchemaOptions
struct Schema_options {
  std::optional<Enabled_state> enabled;
  std::optional<bool> requires_auth;
  std::optional<int64_t> items_per_page;
  std::optional<Json_options> options;
  std::optional<std::string> comments;
  std::optional<Json> metadata;
};

// restObjectOptions
enum class Result_format { feed, item, media };

struct Object_options : public Schema_options {
  std::optional<std::string> media_type;
  bool media_type_autodetect = false;
  std::optional<Result_format> format;
  std::optional<Qualified_name> auth_procedure;
};

// restContentSetOptions
struct Content_set_options {
  std::optional<Enabled_state> enabled;
  std::optional<bool> requires_auth;
  std::optional<Json_options> options;
  std::optional<std::string> comments;
  // ALTER only: analyse the stored files and register their MRS scripts
  // (LOAD SCRIPTS and LOAD TYPESCRIPT SCRIPTS are the same: TypeScript is
  // the only scripting language)
  bool load_scripts = false;
};

// restContentFileOptions
struct Content_file_options {
  std::optional<Enabled_state> enabled;
  std::optional<bool> requires_auth;
  std::optional<Json_options> options;
};

// restAuthAppOptions
struct Auth_app_options {
  std::optional<bool> enabled;
  std::optional<std::string> comments;
  std::optional<bool> allow_new_users;  // ALLOW NEW USERS [TO REGISTER]
  std::optional<std::string> default_role;
  std::optional<std::string> app_id;
  std::optional<std::string> app_secret;
  std::optional<std::string> url;
};

// userOptions
struct User_options {
  std::optional<bool> account_locked;  // ACCOUNT LOCK / UNLOCK
  std::optional<Json> app_options;
  std::optional<Json_options> options;
};

// restRoleOptions
struct Role_options {
  std::optional<Json_options> options;
  std::optional<std::string> comments;
};

// -- Statements -----------------------------------------------------------

struct Create_flags {
  bool or_replace = false;
  bool if_not_exists = false;
};

struct Configure_rest_metadata {
  std::optional<bool> enabled;
  std::optional<Json_options> options;
  bool update_if_available = false;
};

struct Create_rest_service {
  Create_flags flags;
  Service_path path;
  Service_options options;
};

struct Create_rest_schema {
  Create_flags flags;
  std::optional<std::string> schema_path;  // defaults to /<schema_name>
  std::optional<Service_path> service;
  std::string schema_name;
  Schema_options options;
};

struct Create_rest_view {
  Create_flags flags;
  std::string path;
  std::optional<Schema_selector> on;
  Qualified_name object;
  std::optional<std::string> class_name;
  Crud_annotations crud;
  std::optional<Graphql_object> mapping;
  Object_options options;
};

// The kind of a REST db object, i.e. which statements it belongs to.
enum class Db_object_kind { view, procedure, function };

// CREATE REST PROCEDURE and CREATE REST FUNCTION
struct Create_rest_routine {
  Db_object_kind kind = Db_object_kind::procedure;  // procedure or function
  Create_flags flags;
  std::string path;
  std::optional<Schema_selector> on;
  Qualified_name object;
  bool force = false;
  std::optional<Named_graphql_object> parameters;
  std::vector<Named_graphql_object> results;
  Object_options options;
};

struct Create_rest_content_set {
  Create_flags flags;
  std::string path;
  std::optional<Service_path> service;
  Content_set_options options;
};

struct Create_rest_content_file {
  Create_flags flags;
  std::string path;
  std::optional<Service_path> service;
  std::string content_set_path;
  std::optional<std::string> content;
  bool binary = false;
  Content_file_options options;
};

struct Create_rest_auth_app {
  Create_flags flags;
  std::string name;
  std::string vendor;  // "MRS", "MySQL Internal" or a custom vendor name
  Auth_app_options options;
};

struct Create_rest_user {
  Create_flags flags;
  std::string name;
  std::string auth_app;
  std::optional<std::string> password;
  User_options options;
};

struct Create_rest_role {
  Create_flags flags;
  std::string name;
  std::optional<std::string> extends;
  std::optional<Role_service> on;
  Role_options options;
};

struct Clone_rest_service {
  Service_path path;
  Service_path new_path;
};

struct Alter_rest_service {
  Service_path path;
  std::optional<Service_path> new_path;
  Service_options options;
};

struct Alter_rest_schema {
  std::optional<std::string> schema_path;
  std::optional<Service_path> service;
  std::optional<std::string> new_path;
  std::optional<std::string> schema_name;
  Schema_options options;
};

struct Alter_rest_view {
  struct Class_definition {
    std::string name;
    Crud_annotations crud;
    std::optional<Graphql_object> mapping;
  };

  std::string path;
  std::optional<Schema_selector> on;
  std::optional<std::string> new_path;
  std::optional<Class_definition> class_def;
  Object_options options;
};

struct Alter_rest_routine {
  Db_object_kind kind = Db_object_kind::procedure;  // procedure or function
  std::string path;
  std::optional<Schema_selector> on;
  std::optional<std::string> new_path;
  std::optional<Named_graphql_object> parameters;
  std::vector<Named_graphql_object> results;
  Object_options options;
};

struct Alter_rest_content_set {
  std::string path;
  std::optional<Service_path> service;
  std::optional<std::string> new_path;
  Content_set_options options;
};

struct Alter_rest_auth_app {
  std::string name;
  std::optional<std::string> new_name;
  Auth_app_options options;
};

struct Alter_rest_user {
  std::string name;
  std::string auth_app;
  std::optional<std::string> password;
  User_options options;
};

struct Drop_rest_service {
  bool if_exists = false;
  Service_path path;
};

struct Drop_rest_schema {
  bool if_exists = false;
  std::string schema_path;
  std::optional<Service_path> service;
};

// FORMAT=TRADITIONAL (the default) or FORMAT=JSON, as EXPLAIN takes it
enum class Output_format { traditional, json };

struct Drop_rest_db_object {
  Db_object_kind kind = Db_object_kind::view;
  bool if_exists = false;
  std::string path;
  std::optional<Schema_selector> from;
};

struct Drop_rest_content_set {
  bool if_exists = false;
  std::string path;
  std::optional<Service_path> service;
};

struct Drop_rest_content_file {
  bool if_exists = false;
  std::string path;
  std::optional<Service_path> service;
  std::string content_set_path;
};

struct Drop_rest_auth_app {
  bool if_exists = false;
  std::string name;
};

struct Drop_rest_user {
  bool if_exists = false;
  std::string name;
  std::string auth_app;
};

struct Drop_rest_role {
  bool if_exists = false;
  std::string name;
  std::optional<Role_service> on;
};

enum class Privilege { create, read, update, del };

// GRANT REST ... TO role and REVOKE REST ... FROM role
struct Rest_privilege_statement {
  bool revoke = false;
  std::vector<Privilege> privileges;
  // `ON [SERVICE] <pattern>` or `ON [SERVICE <pattern>] SCHEMA <pattern>
  // [OBJECT <pattern>]`; patterns may contain * and ? wildcards.
  std::optional<std::string> service_pattern;
  std::optional<std::string> schema_pattern;
  std::optional<std::string> object_pattern;
  std::string role;
  std::optional<Role_service> role_service;
};

// GRANT REST ROLE ... TO user@app and REVOKE REST ROLE ... FROM user@app
struct Rest_role_statement {
  bool revoke = false;
  std::string role;
  std::optional<Role_service> role_service;
  std::string user;
  std::string auth_app;
  std::optional<std::string> comments;
};

// USE REST SERVICE <service> | USE REST [SERVICE <service>] SCHEMA <schema>
struct Use_rest {
  std::optional<Service_path> service;
  std::optional<std::string> schema_path;
};

// SHOW REST [METADATA] STATUS [FORMAT=JSON]
struct Show_rest_metadata_status {
  Output_format format = Output_format::traditional;
};

// SHOW REST SERVICES [FOR AUTH APP name | FOR DAEMON id]
struct Show_rest_services {
  std::optional<std::string> auth_app;
  std::optional<int64_t> daemon;
};

// SHOW REST DAEMONS: the MariaDB REST Daemon instances (the router table)
struct Show_rest_daemons {
  Output_format format = Output_format::traditional;
};

// DROP REST DAEMON [IF EXISTS] id
struct Drop_rest_daemon {
  bool if_exists = false;
  int64_t id = 0;
};

struct Show_rest_schemas {
  std::optional<Service_path> service;
};

struct Show_rest_db_objects {
  Db_object_kind kind = Db_object_kind::view;
  std::optional<Schema_selector> on;
};

struct Show_rest_content_sets {
  std::optional<Service_path> service;
};

struct Show_rest_content_files {
  std::optional<Service_path> service;
  std::string content_set_path;
};

struct Show_rest_auth_apps {
  std::optional<Service_path> service;
};

struct Show_rest_auth_vendors {};

// SHOW REST USERS [ON SERVICE path] [FOR AUTH APP name]
struct Show_rest_users {
  std::optional<Service_path> service;
  std::optional<std::string> auth_app;
};

struct Show_rest_roles {
  std::optional<Role_service> on;
  std::optional<std::string> user;
  std::optional<std::string> auth_app;
};

struct Show_rest_grants {
  std::string role;
  std::optional<Role_service> on;
};

// SHOW REST COLUMNS FROM [TABLE | VIEW | PROCEDURE | FUNCTION] [schema.]name
struct Show_rest_columns {
  enum class Source { any, table, view, procedure, function };
  Source source = Source::any;
  Qualified_name object;
  Output_format format = Output_format::traditional;
};

// INCLUDING DATABASE [AND STATIC [AND DYNAMIC]] | ALL ENDPOINTS
struct Endpoint_selection {
  bool database = false;
  bool static_ = false;
  bool dynamic = false;
};

struct Show_create_rest_service {
  std::optional<Service_path> path;
  Endpoint_selection endpoints;
  Output_format format = Output_format::traditional;
};

struct Show_create_rest_schema {
  std::optional<std::string> schema_path;
  std::optional<Service_path> service;
  Output_format format = Output_format::traditional;
};

struct Show_create_rest_db_object {
  Db_object_kind kind = Db_object_kind::view;
  std::string path;
  std::optional<Schema_selector> on;
  Output_format format = Output_format::traditional;
};

struct Show_create_rest_content_set {
  std::string path;
  std::optional<Service_path> service;
  Output_format format = Output_format::traditional;
};

struct Show_create_rest_content_file {
  std::string path;
  std::optional<Service_path> service;
  std::string content_set_path;
  Output_format format = Output_format::traditional;
};

struct Show_create_rest_auth_app {
  std::string name;
  Output_format format = Output_format::traditional;
};

struct Show_create_rest_role {
  std::string name;
  std::optional<Role_service> on;
  Output_format format = Output_format::traditional;
};

struct Show_create_rest_user {
  std::string name;
  std::string auth_app;
  Output_format format = Output_format::traditional;
};

using Statement_variant = std::variant<
    Configure_rest_metadata, Create_rest_service, Create_rest_schema,
    Create_rest_view, Create_rest_routine, Create_rest_content_set,
    Create_rest_content_file, Create_rest_auth_app, Create_rest_user,
    Create_rest_role, Clone_rest_service, Alter_rest_service,
    Alter_rest_schema, Alter_rest_view, Alter_rest_routine,
    Alter_rest_content_set, Alter_rest_auth_app, Alter_rest_user,
    Drop_rest_service, Drop_rest_schema, Drop_rest_db_object,
    Drop_rest_content_set, Drop_rest_content_file, Drop_rest_auth_app,
    Drop_rest_user, Drop_rest_role, Rest_privilege_statement,
    Rest_role_statement, Use_rest, Show_rest_metadata_status,
    Show_rest_services, Show_rest_schemas, Show_rest_db_objects,
    Show_rest_content_sets, Show_rest_content_files, Show_rest_auth_apps,
    Show_rest_auth_vendors, Show_rest_users, Show_rest_columns,
    Show_rest_daemons, Drop_rest_daemon,
    Show_rest_roles, Show_rest_grants, Show_create_rest_service,
    Show_create_rest_schema, Show_create_rest_db_object,
    Show_create_rest_content_set, Show_create_rest_content_file,
    Show_create_rest_auth_app, Show_create_rest_role, Show_create_rest_user>;

// One parsed statement together with its position in the script.
struct Statement {
  int line = 0;
  int column = 0;
  Statement_variant value;

  template <typename T>
  const T *as() const {
    return std::get_if<T>(&value);
  }
  template <typename T>
  bool is() const {
    return std::holds_alternative<T>(value);
  }
};

using Script = std::vector<Statement>;

}  // namespace ast
}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_AST_H_
