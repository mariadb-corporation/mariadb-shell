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

/*
 * The grammar of the MariaDB REST Service (MRS) SQL extension.
 *
 * It is the bison counterpart of the ANTLR grammar the Python mrs_plugin used
 * (grammar/MRSParser.g4), kept rule for rule so the two can be compared, and
 * written in the style of the MariaDB server grammar (sql_yacc.yy) so the
 * statements can later be merged into it. The rules between the
 * "START OF MERGE PART" and "END OF MERGE PART" markers are the ones that
 * would move to the server; the rest are basic rules the server already has.
 *
 * The tokens are produced by the hand-written lexer in mrs_lexer.cc; the
 * actions build the tree defined in mrs_ast.h.
 */

%require "3.6"
%skeleton "lalr1.cc"
%language "c++"
%define api.namespace {mrs::parser}
%define api.parser.class {Parser}
%define api.value.type variant
%define api.token.constructor
%define api.token.prefix {T_}
%define api.location.file none
%define parse.error verbose
%locations
%expect 0

%parse-param {mrs::parser::Driver &driver}
%lex-param {mrs::parser::Driver &driver}

%code requires {
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "modules/mrs/core/mrs_ast.h"

namespace mrs {
namespace parser {
class Driver;

using namespace mrs::ast;

// Types with commas in their names cannot be used in %nterm declarations.
// A double quoted token: its unquoted text, and its source for JSON values.
struct Quoted {
  std::string text;
  std::string raw;
};

using Opt_string = std::optional<std::string>;
using Opt_service_path = std::optional<Service_path>;
using Opt_schema_selector = std::optional<Schema_selector>;
using Opt_role_service = std::optional<Role_service>;
using Opt_graphql_object = std::optional<Graphql_object>;
using Opt_named_graphql_object = std::optional<Named_graphql_object>;
using Opt_class_definition = std::optional<Alter_rest_view::Class_definition>;
using String_list = std::vector<std::string>;
using Named_graphql_object_list = std::vector<Named_graphql_object>;
using Privilege_list = std::vector<Privilege>;
}  // namespace parser
}  // namespace mrs
}

%code {
#include <algorithm>
#include <cctype>

#include "modules/mrs/core/mrs_parser_driver.h"

namespace mrs {
namespace parser {
Parser::symbol_type yylex(Driver &driver);

// A request path given in quotes must start with '/', or with a wildcard
// where wildcards are allowed.
void validate_request_path(const std::string &path, bool allow_wildcards,
                           const Parser::location_type &loc);

// The source text of a keyword token, for keywords used as names.
std::string keyword_text(const Driver &driver, const Parser::location_type &loc);

// A double quoted string is an identifier under ANSI_QUOTES and a text
// otherwise; where only one of them is allowed, the other mode is a syntax
// error (the ANTLR grammar's isSqlModeActive(AnsiQuotes) predicates).
void require_ansi_quotes(const Driver &driver, bool ansi_quotes,
                         const Parser::location_type &loc);

// The output format named by FORMAT=<name>: JSON or TRADITIONAL.
Output_format output_format(const std::string &name,
                            const Parser::location_type &loc);

// A SHOW CREATE statement with the format of its FORMAT=<name> clause.
template <typename T>
T with_format(T &&value, Output_format format) {
  value.format = format;
  return std::forward<T>(value);
}

// Statements with a position.
template <typename T>
Statement make_statement(T &&value, const Parser::location_type &loc) {
  Statement s;
  s.line = loc.begin.line;
  s.column = loc.begin.column;
  s.value = std::forward<T>(value);
  return s;
}
}  // namespace parser
}  // namespace mrs
}

/* Keywords. The names match the ANTLR grammar and MRS_KEYWORD_LIST. */
%token
  CREATE_SYMBOL "CREATE" OR_SYMBOL "OR" REPLACE_SYMBOL "REPLACE"
  ALTER_SYMBOL "ALTER" SHOW_SYMBOL "SHOW" STATUS_SYMBOL "STATUS"
  NEW_SYMBOL "NEW" ON_SYMBOL "ON" FROM_SYMBOL "FROM" IN_SYMBOL "IN"
  DATABASES_SYMBOL "SCHEMAS" DATABASE_SYMBOL "SCHEMA" JSON_SYMBOL "JSON"
  VIEW_SYMBOL "VIEW" PROCEDURE_SYMBOL "PROCEDURE" FUNCTION_SYMBOL "FUNCTION"
  DROP_SYMBOL "DROP" USE_SYMBOL "USE" AS_SYMBOL "AS" FILTER_SYMBOL "FILTER"
  AUTHENTICATION_SYMBOL "AUTHENTICATION" PATH_SYMBOL "PATH"
  VALIDATION_SYMBOL "VALIDATION" DEFAULT_SYMBOL "DEFAULT" USER_SYMBOL "USER"
  OPTIONS_SYMBOL "OPTIONS" IF_SYMBOL "IF" NOT_SYMBOL "NOT"
  EXISTS_SYMBOL "EXISTS" PAGE_SYMBOL "PAGE" HOST_SYMBOL "HOST"
  TYPE_SYMBOL "TYPE" FORMAT_SYMBOL "FORMAT" FORCE_SYMBOL "FORCE"
  UPDATE_SYMBOL "UPDATE" NULL_SYMBOL "NULL" TRUE_SYMBOL "TRUE"
  FALSE_SYMBOL "FALSE" SET_SYMBOL "SET" IDENTIFIED_SYMBOL "IDENTIFIED"
  BY_SYMBOL "BY" ROLE_SYMBOL "ROLE" TO_SYMBOL "TO"
  CLONE_SYMBOL "CLONE" FILE_SYMBOL "FILE" FILES_SYMBOL "FILES"
  BINARY_SYMBOL "BINARY" DATA_SYMBOL "DATA" LOAD_SYMBOL "LOAD"
  GRANT_SYMBOL "GRANT" READ_SYMBOL "READ" DELETE_SYMBOL "DELETE"
  GROUP_SYMBOL "GROUP" REVOKE_SYMBOL "REVOKE" ACCOUNT_SYMBOL "ACCOUNT"
  LOCK_SYMBOL "LOCK" UNLOCK_SYMBOL "UNLOCK" GRANTS_SYMBOL "GRANTS"
  FOR_SYMBOL "FOR" LEVEL_SYMBOL "LEVEL" ANY_SYMBOL "ANY"
  CLIENT_SYMBOL "CLIENT" URL_SYMBOL "URL" NAME_SYMBOL "NAME" DO_SYMBOL "DO"
  ALL_SYMBOL "ALL" PARAMETERS_SYMBOL "PARAMETERS" ADD_SYMBOL "ADD"
  REMOVE_SYMBOL "REMOVE" MERGE_SYMBOL "MERGE" COMMENT_SYMBOL "COMMENT"
  DYNAMIC_SYMBOL "DYNAMIC" AND_SYMBOL "AND"
  SETS_SYMBOL "SETS"
  CONFIGURE_SYMBOL "CONFIGURE" REST_SYMBOL "REST" METADATA_SYMBOL "METADATA"
  SERVICES_SYMBOL "SERVICES" SERVICE_SYMBOL "SERVICE" VIEWS_SYMBOL "VIEWS"
  PROCEDURES_SYMBOL "PROCEDURES" FUNCTIONS_SYMBOL "FUNCTIONS"
  RESULT_SYMBOL "RESULT" ENABLED_SYMBOL "ENABLED" PUBLISHED_SYMBOL "PUBLISHED"
  DISABLED_SYMBOL "DISABLED" PRIVATE_SYMBOL "PRIVATE"
  UNPUBLISHED_SYMBOL "UNPUBLISHED" PROTOCOL_SYMBOL "PROTOCOL"
  HTTP_SYMBOL "HTTP" HTTPS_SYMBOL "HTTPS" REQUEST_SYMBOL "REQUEST"
  REDIRECTION_SYMBOL "REDIRECTION" MANAGEMENT_SYMBOL "MANAGEMENT"
  AVAILABLE_SYMBOL "AVAILABLE" REQUIRED_SYMBOL "REQUIRED" ITEMS_SYMBOL "ITEMS"
  PER_SYMBOL "PER" CONTENT_SYMBOL "CONTENT" MEDIA_SYMBOL "MEDIA"
  AUTODETECT_SYMBOL "AUTODETECT" FEED_SYMBOL "FEED" ITEM_SYMBOL "ITEM"
  AUTH_SYMBOL "AUTH" APPS_SYMBOL "APPS" APP_SYMBOL "APP" ID_SYMBOL "ID"
  SECRET_SYMBOL "SECRET" VENDOR_SYMBOL "VENDOR" MRS_SYMBOL "MRS"
  MYSQL_SYMBOL "MYSQL" USERS_SYMBOL "USERS" ALLOW_SYMBOL "ALLOW"
  REGISTER_SYMBOL "REGISTER" CLASS_SYMBOL "CLASS"
  DEVELOPMENT_SYMBOL "DEVELOPMENT" SCRIPTS_SYMBOL "SCRIPTS"
  MAPPING_SYMBOL "MAPPING" TYPESCRIPT_SYMBOL "TYPESCRIPT" ROLES_SYMBOL "ROLES"
  EXTENDS_SYMBOL "EXTENDS" OBJECT_SYMBOL "OBJECT" HIERARCHY_SYMBOL "HIERARCHY"
  INCLUDE_SYMBOL "INCLUDE" INCLUDING_SYMBOL "INCLUDING"
  ENDPOINTS_SYMBOL "ENDPOINTS" OBJECTS_SYMBOL "OBJECTS"
  STATIC_SYMBOL "STATIC"
  VENDORS_SYMBOL "VENDORS" TABLE_SYMBOL "TABLE" COLUMNS_SYMBOL "COLUMNS"
  DAEMON_SYMBOL "DAEMON" DAEMONS_SYMBOL "DAEMONS"

/* Data mapping annotations. */
%token
  AT_INOUT_SYMBOL "@INOUT" AT_IN_SYMBOL "@IN" AT_OUT_SYMBOL "@OUT"
  AT_CHECK_SYMBOL "@CHECK" AT_NOCHECK_SYMBOL "@NOCHECK"
  AT_NOUPDATE_SYMBOL "@NOUPDATE" AT_SORTABLE_SYMBOL "@SORTABLE"
  AT_NOFILTERING_SYMBOL "@NOFILTERING" AT_ROWOWNERSHIP_SYMBOL "@ROWOWNERSHIP"
  AT_UNNEST_SYMBOL "@UNNEST" AT_DATATYPE_SYMBOL "@DATATYPE"
  AT_SELECT_SYMBOL "@SELECT" AT_NOSELECT_SYMBOL "@NOSELECT"
  AT_INSERT_SYMBOL "@INSERT" AT_NOINSERT_SYMBOL "@NOINSERT"
  AT_UPDATE_SYMBOL "@UPDATE" AT_DELETE_SYMBOL "@DELETE"
  AT_NODELETE_SYMBOL "@NODELETE" AT_KEY_SYMBOL "@KEY"

/* Punctuation. */
%token
  EQUAL_OPERATOR "=" PLUS_OPERATOR "+" MINUS_OPERATOR "-" MULT_OPERATOR "*"
  DIV_OPERATOR "/" DOT_SYMBOL "." COMMA_SYMBOL "," SEMICOLON_SYMBOL ";"
  COLON_SYMBOL ":" OPEN_PAR_SYMBOL "(" CLOSE_PAR_SYMBOL ")"
  OPEN_CURLY_SYMBOL "{" CLOSE_CURLY_SYMBOL "}" OPEN_SQUARE_SYMBOL "["
  CLOSE_SQUARE_SYMBOL "]" AT_SIGN_SYMBOL "@"

/* Tokens with a value. The text of quoted tokens is already unquoted. */
%token <std::string> IDENTIFIER "identifier"
%token <std::string> BACK_TICK_QUOTED_ID "quoted identifier"
%token <std::string> SINGLE_QUOTED_TEXT "string"
%token <Quoted> DOUBLE_QUOTED_TEXT "double quoted string"
%token <std::string> INT_NUMBER "integer"
%token <std::string> DECIMAL_NUMBER "decimal number"
%token <std::string> FLOAT_NUMBER "float number"
%token <std::string> REST_REQUEST_PATH "request path"

%token END 0 "end of input"

/*
 * @NOCHECK belongs to both the value options and the CRUD options of a data
 * mapping field. It is taken as a value option, as the ANTLR grammar did.
 */
%precedence PREC_BELOW_NOCHECK
%precedence AT_NOCHECK_SYMBOL

%nterm <Statement> mrs_statement
%nterm <Statement> configure_rest_metadata_statement
%nterm <Statement> create_rest_service_statement
%nterm <Statement> create_rest_schema_statement
%nterm <Statement> create_rest_view_statement
%nterm <Statement> create_rest_procedure_statement
%nterm <Statement> create_rest_function_statement
%nterm <Statement> create_rest_content_set_statement
%nterm <Statement> create_rest_content_file_statement
%nterm <Statement> create_rest_auth_app_statement
%nterm <Statement> create_rest_role_statement
%nterm <Statement> create_rest_user_statement
%nterm <Statement> clone_rest_service_statement
%nterm <Statement> alter_rest_service_statement
%nterm <Statement> alter_rest_schema_statement
%nterm <Statement> alter_rest_view_statement
%nterm <Statement> alter_rest_procedure_statement
%nterm <Statement> alter_rest_function_statement
%nterm <Statement> alter_rest_content_set_statement
%nterm <Statement> alter_rest_auth_app_statement
%nterm <Statement> alter_rest_user_statement
%nterm <Statement> drop_rest_service_statement
%nterm <Statement> drop_rest_schema_statement
%nterm <Statement> drop_rest_view_statement
%nterm <Statement> drop_rest_procedure_statement
%nterm <Statement> drop_rest_function_statement
%nterm <Statement> drop_rest_content_set_statement
%nterm <Statement> drop_rest_content_file_statement
%nterm <Statement> drop_rest_auth_app_statement
%nterm <Statement> drop_rest_user_statement
%nterm <Statement> drop_rest_role_statement
%nterm <Statement> grant_rest_role_statement
%nterm <Statement> grant_rest_privilege_statement
%nterm <Statement> revoke_rest_privilege_statement
%nterm <Statement> revoke_rest_role_statement
%nterm <Statement> use_statement
%nterm <Statement> show_rest_metadata_status_statement
%nterm <Statement> show_rest_services_statement
%nterm <Statement> show_rest_schemas_statement
%nterm <Statement> show_rest_views_statement
%nterm <Statement> show_rest_procedures_statement
%nterm <Statement> show_rest_functions_statement
%nterm <Statement> show_rest_content_sets_statement
%nterm <Statement> show_rest_content_files_statement
%nterm <Statement> show_rest_auth_apps_statement
%nterm <Statement> show_rest_auth_vendors_statement
%nterm <Statement> show_rest_users_statement
%nterm <Statement> show_rest_columns_statement
%nterm <Statement> show_rest_daemons_statement
%nterm <Statement> drop_rest_daemon_statement
%nterm <int64_t> daemon_id
%nterm <Output_format> opt_output_format
%nterm <Show_rest_columns::Source> opt_columns_source
%nterm <Statement> show_rest_roles_statement
%nterm <Statement> show_rest_grants_statement
%nterm <Statement> show_create_rest_service_statement
%nterm <Statement> show_create_rest_schema_statement
%nterm <Statement> show_create_rest_view_statement
%nterm <Statement> show_create_rest_procedure_statement
%nterm <Statement> show_create_rest_function_statement
%nterm <Statement> show_create_rest_content_set_statement
%nterm <Statement> show_create_rest_content_file_statement
%nterm <Statement> show_create_rest_auth_app_statement
%nterm <Statement> show_create_rest_role_statement
%nterm <Statement> show_create_rest_user_statement

%nterm <Create_flags> create_rest_service_prefix create_rest_schema_prefix
%nterm <Create_flags> create_rest_view_prefix create_rest_procedure_prefix
%nterm <Create_flags> create_rest_function_prefix
%nterm <Create_flags> create_rest_content_set_prefix
%nterm <Create_flags> create_rest_content_file_prefix
%nterm <Create_flags> create_rest_auth_app_prefix create_rest_user_prefix
%nterm <Create_flags> create_rest_role_prefix
%nterm <bool> opt_if_not_exists opt_if_exists

%nterm <bool> enabled_disabled published_unpublished
%nterm <Enabled_state> enabled_disabled_private
%nterm <bool> authentication_required
%nterm <int64_t> items_per_page
%nterm <Json_options> json_options
%nterm <std::string> metadata comments
%nterm <Text_or_default> quoted_text_or_default
%nterm <Schema_selector> service_schema_selector
%nterm <Opt_schema_selector> opt_on_service_schema_selector
%nterm <Opt_schema_selector> opt_from_service_schema_selector
%nterm <Opt_schema_selector> opt_on_from_service_schema_selector
%nterm <Role_service> role_service
%nterm <Opt_role_service> opt_role_service
%nterm <Opt_role_service> opt_on_from_role_service

%nterm <Configure_rest_metadata> rest_metadata_options
%nterm <Service_options> rest_service_options
%nterm <std::string> rest_protocol
%nterm <Service_options> rest_authentication
%nterm <Auth_app_reference> add_auth_app remove_auth_app
%nterm <Schema_options> rest_schema_options
%nterm <Object_options> rest_object_options
%nterm <Result_format> rest_view_format
%nterm <Named_graphql_object> rest_procedure_result rest_function_result
%nterm <Named_graphql_object_list> rest_procedure_results
%nterm <Named_graphql_object_list> opt_rest_function_result
%nterm <Named_graphql_object_list> rest_function_results
%nterm <Opt_named_graphql_object> opt_rest_parameters
%nterm <Content_set_options> rest_content_set_options alter_rest_content_set_options
%nterm <Content_file_options> rest_content_file_options
%nterm <Auth_app_options> rest_auth_app_options
%nterm <bool> allow_new_users_to_register
%nterm <User_options> user_options
%nterm <Role_options> rest_role_options
%nterm <std::string> vendor

%nterm <Opt_string> opt_identified_by opt_class opt_new_service_name
%nterm <Opt_string> opt_from_schema_name
%nterm <Opt_string> opt_new_request_path opt_schema_request_path
%nterm <Opt_string> opt_extends opt_comments opt_for_auth_app
%nterm <Opt_service_path> opt_on_service opt_from_service opt_on_from_service
%nterm <Opt_service_path> opt_service_request_path
%nterm <Opt_class_definition> opt_alter_view_class
%nterm <Opt_graphql_object> opt_graphql_obj

%nterm <Privilege_list> privilege_list
%nterm <Privilege> privilege_name
%nterm <Rest_privilege_statement> privilege_target
%nterm <Use_rest> service_and_schema_request_paths
%nterm <Endpoint_selection> opt_including_endpoints
%nterm <Endpoint_selection> endpoint_selection

%nterm <Service_path> service_request_path new_service_request_path
%nterm <std::string> service_request_path_wildcard schema_request_path
%nterm <std::string> schema_request_path_wildcard view_request_path
%nterm <std::string> rest_object_name rest_result_name
%nterm <std::string> object_request_path_wildcard procedure_request_path
%nterm <std::string> function_request_path content_set_request_path
%nterm <std::string> content_file_request_path
%nterm <std::string> auth_app_name vendor_name
%nterm <std::string> user_name user_password role_name parent_role_name
%nterm <std::string> new_auth_app_name schema_name
%nterm <String_list> service_developers_identifier service_developer_list
%nterm <std::string> service_developer_identifier
%nterm <std::string> request_path_identifier
%nterm <std::string> request_path_identifier_with_wildcard

%nterm <std::string> json_obj json_pair json_pairs json_arr json_values
%nterm <std::string> json_value json_number
%nterm <Graphql_object> graphql_obj graphql_pairs
%nterm <Crud_annotations> graphql_crud_options opt_graphql_crud_options
%nterm <Graphql_field> graphql_pair graphql_value_options
%nterm <Graphql_field::Mode> opt_graphql_mode
%nterm <Opt_string> opt_graphql_datatype
%nterm <Opt_string> opt_graphql_value_json_schema
%nterm <std::string> graphql_pair_key graphql_allowed_keyword
%nterm <Qualified_name> graphql_pair_value
%nterm <std::string> graphql_datatype_text

%nterm <std::string> pure_identifier identifier identifier_keyword
%nterm <std::string> unquoted_identifier
%nterm <Qualified_name> qualified_identifier
%nterm <std::string> text_string_literal text_or_identifier

%%

/* The script: statements separated by semicolons. */

mrs_script:
    opt_semicolons opt_statement_list END
  ;

opt_statement_list:
    %empty
  | statement_list opt_semicolons
  ;

statement_list:
    mrs_statement { driver.script().push_back(std::move($1)); }
  | statement_list semicolons mrs_statement
    { driver.script().push_back(std::move($3)); }
  ;

opt_semicolons:
    %empty
  | semicolons
  ;

semicolons:
    SEMICOLON_SYMBOL
  | semicolons SEMICOLON_SYMBOL
  ;

/* START OF MERGE PART */

mrs_statement:
    configure_rest_metadata_statement { $$ = std::move($1); }
  | create_rest_service_statement { $$ = std::move($1); }
  | create_rest_schema_statement { $$ = std::move($1); }
  | create_rest_view_statement { $$ = std::move($1); }
  | create_rest_procedure_statement { $$ = std::move($1); }
  | create_rest_function_statement { $$ = std::move($1); }
  | create_rest_content_set_statement { $$ = std::move($1); }
  | create_rest_content_file_statement { $$ = std::move($1); }
  | create_rest_auth_app_statement { $$ = std::move($1); }
  | create_rest_role_statement { $$ = std::move($1); }
  | create_rest_user_statement { $$ = std::move($1); }
  | clone_rest_service_statement { $$ = std::move($1); }
  | alter_rest_service_statement { $$ = std::move($1); }
  | alter_rest_schema_statement { $$ = std::move($1); }
  | alter_rest_view_statement { $$ = std::move($1); }
  | alter_rest_procedure_statement { $$ = std::move($1); }
  | alter_rest_function_statement { $$ = std::move($1); }
  | alter_rest_content_set_statement { $$ = std::move($1); }
  | alter_rest_auth_app_statement { $$ = std::move($1); }
  | alter_rest_user_statement { $$ = std::move($1); }
  | drop_rest_service_statement { $$ = std::move($1); }
  | drop_rest_schema_statement { $$ = std::move($1); }
  | drop_rest_view_statement { $$ = std::move($1); }
  | drop_rest_procedure_statement { $$ = std::move($1); }
  | drop_rest_function_statement { $$ = std::move($1); }
  | drop_rest_content_set_statement { $$ = std::move($1); }
  | drop_rest_content_file_statement { $$ = std::move($1); }
  | drop_rest_auth_app_statement { $$ = std::move($1); }
  | drop_rest_user_statement { $$ = std::move($1); }
  | drop_rest_role_statement { $$ = std::move($1); }
  | grant_rest_role_statement { $$ = std::move($1); }
  | grant_rest_privilege_statement { $$ = std::move($1); }
  | revoke_rest_privilege_statement { $$ = std::move($1); }
  | revoke_rest_role_statement { $$ = std::move($1); }
  | use_statement { $$ = std::move($1); }
  | show_rest_metadata_status_statement { $$ = std::move($1); }
  | show_rest_services_statement { $$ = std::move($1); }
  | show_rest_schemas_statement { $$ = std::move($1); }
  | show_rest_views_statement { $$ = std::move($1); }
  | show_rest_procedures_statement { $$ = std::move($1); }
  | show_rest_functions_statement { $$ = std::move($1); }
  | show_rest_content_sets_statement { $$ = std::move($1); }
  | show_rest_content_files_statement { $$ = std::move($1); }
  | show_rest_auth_apps_statement { $$ = std::move($1); }
  | show_rest_auth_vendors_statement { $$ = std::move($1); }
  | show_rest_users_statement { $$ = std::move($1); }
  | show_rest_columns_statement { $$ = std::move($1); }
  | show_rest_daemons_statement { $$ = std::move($1); }
  | drop_rest_daemon_statement { $$ = std::move($1); }
  | show_rest_roles_statement { $$ = std::move($1); }
  | show_rest_grants_statement { $$ = std::move($1); }
  | show_create_rest_service_statement { $$ = std::move($1); }
  | show_create_rest_schema_statement { $$ = std::move($1); }
  | show_create_rest_view_statement { $$ = std::move($1); }
  | show_create_rest_procedure_statement { $$ = std::move($1); }
  | show_create_rest_function_statement { $$ = std::move($1); }
  | show_create_rest_content_set_statement { $$ = std::move($1); }
  | show_create_rest_content_file_statement { $$ = std::move($1); }
  | show_create_rest_auth_app_statement { $$ = std::move($1); }
  | show_create_rest_role_statement { $$ = std::move($1); }
  | show_create_rest_user_statement { $$ = std::move($1); }
  ;

/* Common Definitions ====================================================== */

enabled_disabled:
    ENABLED_SYMBOL { $$ = true; }
  | DISABLED_SYMBOL { $$ = false; }
  ;

enabled_disabled_private:
    ENABLED_SYMBOL { $$ = Enabled_state::enabled; }
  | DISABLED_SYMBOL { $$ = Enabled_state::disabled; }
  | PRIVATE_SYMBOL { $$ = Enabled_state::private_; }
  ;

quoted_text_or_default:
    text_string_literal { $$ = Text_or_default{false, std::move($1)}; }
  | DEFAULT_SYMBOL { $$ = Text_or_default{true, {}}; }
  ;

json_options:
    OPTIONS_SYMBOL json_value { $$ = Json_options{std::move($2), false}; }
  | MERGE_SYMBOL OPTIONS_SYMBOL json_value
    { $$ = Json_options{std::move($3), true}; }
  ;

metadata:
    METADATA_SYMBOL json_value { $$ = std::move($2); }
  ;

comments:
    COMMENT_SYMBOL text_string_literal { $$ = std::move($2); }
  ;

opt_comments:
    %empty { $$ = std::nullopt; }
  | comments { $$ = std::move($1); }
  ;

authentication_required:
    AUTHENTICATION_SYMBOL REQUIRED_SYMBOL { $$ = true; }
  | AUTHENTICATION_SYMBOL NOT_SYMBOL REQUIRED_SYMBOL { $$ = false; }
  ;

items_per_page:
    ITEMS_SYMBOL PER_SYMBOL PAGE_SYMBOL INT_NUMBER
    { $$ = std::stoll($4); }
  ;

service_schema_selector:
    DATABASE_SYMBOL schema_request_path
    { $$ = Schema_selector{std::nullopt, std::move($2)}; }
  | SERVICE_SYMBOL service_request_path DATABASE_SYMBOL schema_request_path
    { $$ = Schema_selector{std::move($2), std::move($4)}; }
  ;

opt_on_service_schema_selector:
    %empty { $$ = std::nullopt; }
  | ON_SYMBOL service_schema_selector { $$ = std::move($2); }
  ;

opt_from_service_schema_selector:
    %empty { $$ = std::nullopt; }
  | FROM_SYMBOL service_schema_selector { $$ = std::move($2); }
  ;

opt_on_from_service_schema_selector:
    %empty { $$ = std::nullopt; }
  | ON_SYMBOL service_schema_selector { $$ = std::move($2); }
  | FROM_SYMBOL service_schema_selector { $$ = std::move($2); }
  ;

role_service:
    ON_SYMBOL ANY_SYMBOL SERVICE_SYMBOL
    { $$ = Role_service{true, std::nullopt}; }
  | ON_SYMBOL SERVICE_SYMBOL service_request_path
    { $$ = Role_service{false, std::move($3)}; }
  | ON_SYMBOL service_request_path
    { $$ = Role_service{false, std::move($2)}; }
  ;

opt_role_service:
    %empty { $$ = std::nullopt; }
  | role_service { $$ = std::move($1); }
  ;

/* (ON | FROM) (ANY SERVICE | SERVICE? serviceRequestPath) */
opt_on_from_role_service:
    %empty { $$ = std::nullopt; }
  | role_service { $$ = std::move($1); }
  | FROM_SYMBOL ANY_SYMBOL SERVICE_SYMBOL
    { $$ = Role_service{true, std::nullopt}; }
  | FROM_SYMBOL SERVICE_SYMBOL service_request_path
    { $$ = Role_service{false, std::move($3)}; }
  | FROM_SYMBOL service_request_path
    { $$ = Role_service{false, std::move($2)}; }
  ;

opt_on_service:
    %empty { $$ = std::nullopt; }
  | ON_SYMBOL SERVICE_SYMBOL service_request_path { $$ = std::move($3); }
  | ON_SYMBOL service_request_path { $$ = std::move($2); }
  ;

opt_from_service:
    %empty { $$ = std::nullopt; }
  | FROM_SYMBOL SERVICE_SYMBOL service_request_path { $$ = std::move($3); }
  | FROM_SYMBOL service_request_path { $$ = std::move($2); }
  ;

opt_on_from_service:
    %empty { $$ = std::nullopt; }
  | ON_SYMBOL SERVICE_SYMBOL service_request_path { $$ = std::move($3); }
  | ON_SYMBOL service_request_path { $$ = std::move($2); }
  | FROM_SYMBOL SERVICE_SYMBOL service_request_path { $$ = std::move($3); }
  | FROM_SYMBOL service_request_path { $$ = std::move($2); }
  ;

/* (SERVICE? serviceRequestPath)? in front of CONTENT SET */
opt_service_request_path:
    %empty { $$ = std::nullopt; }
  | SERVICE_SYMBOL service_request_path { $$ = std::move($2); }
  | service_request_path { $$ = std::move($1); }
  ;

opt_if_not_exists:
    %empty { $$ = false; }
  | IF_SYMBOL NOT_SYMBOL EXISTS_SYMBOL { $$ = true; }
  ;

opt_if_exists:
    %empty { $$ = false; }
  | IF_SYMBOL EXISTS_SYMBOL { $$ = true; }
  ;

/* CONFIGURE statements ==================================================== */

configure_rest_metadata_statement:
    CONFIGURE_SYMBOL REST_SYMBOL METADATA_SYMBOL rest_metadata_options
    { $$ = make_statement(std::move($4), @1); }
  ;

rest_metadata_options:
    %empty { $$ = Configure_rest_metadata{}; }
  | rest_metadata_options enabled_disabled
    { $$ = std::move($1); $$.enabled = $2; }
  | rest_metadata_options json_options
    { $$ = std::move($1); $$.options = std::move($2); }
  | rest_metadata_options UPDATE_SYMBOL
    { $$ = std::move($1); $$.update_if_available = true; }
  | rest_metadata_options UPDATE_SYMBOL IF_SYMBOL AVAILABLE_SYMBOL
    { $$ = std::move($1); $$.update_if_available = true; }
  ;

/* CREATE statements ======================================================= */

/* - CREATE REST SERVICE --------------------------------------------------- */

create_rest_service_statement:
    create_rest_service_prefix service_request_path rest_service_options
    {
      Create_rest_service s;
      s.flags = $1;
      s.path = std::move($2);
      s.options = std::move($3);
      $$ = make_statement(std::move(s), @1);
    }
  ;

create_rest_service_prefix:
    CREATE_SYMBOL OR_SYMBOL REPLACE_SYMBOL REST_SYMBOL SERVICE_SYMBOL
    { $$ = Create_flags{true, false}; }
  | CREATE_SYMBOL REST_SYMBOL SERVICE_SYMBOL opt_if_not_exists
    { $$ = Create_flags{false, $4}; }
  ;

rest_service_options:
    %empty { $$ = Service_options{}; }
  | rest_service_options enabled_disabled
    { $$ = std::move($1); $$.enabled = $2; }
  | rest_service_options published_unpublished
    { $$ = std::move($1); $$.published = $2; }
  | rest_service_options rest_protocol
    { $$ = std::move($1); $$.protocol = std::move($2); }
  | rest_service_options rest_authentication
    {
      $$ = std::move($1);
      if ($2.auth_path) $$.auth_path = std::move($2.auth_path);
      if ($2.auth_redirection) $$.auth_redirection = std::move($2.auth_redirection);
      if ($2.auth_validation) $$.auth_validation = std::move($2.auth_validation);
      if ($2.auth_page_content) $$.auth_page_content = std::move($2.auth_page_content);
    }
  | rest_service_options json_options
    { $$ = std::move($1); $$.options = std::move($2); }
  | rest_service_options comments
    { $$ = std::move($1); $$.comments = std::move($2); }
  | rest_service_options metadata
    { $$ = std::move($1); $$.metadata = std::move($2); }
  | rest_service_options add_auth_app
    { $$ = std::move($1); $$.add_auth_apps.push_back(std::move($2)); }
  | rest_service_options remove_auth_app
    { $$ = std::move($1); $$.remove_auth_apps.push_back(std::move($2)); }
  ;

published_unpublished:
    PUBLISHED_SYMBOL { $$ = true; }
  | UNPUBLISHED_SYMBOL { $$ = false; }
  ;

rest_protocol:
    PROTOCOL_SYMBOL HTTP_SYMBOL { $$ = "HTTP"; }
  | PROTOCOL_SYMBOL HTTPS_SYMBOL { $$ = "HTTPS"; }
  ;

/* AUTHENTICATION (authPath | authRedirection | authValidation | authPageContent)* */
rest_authentication:
    AUTHENTICATION_SYMBOL { $$ = Service_options{}; }
  | rest_authentication PATH_SYMBOL quoted_text_or_default
    { $$ = std::move($1); $$.auth_path = std::move($3); }
  | rest_authentication REDIRECTION_SYMBOL quoted_text_or_default
    { $$ = std::move($1); $$.auth_redirection = std::move($3); }
  | rest_authentication VALIDATION_SYMBOL quoted_text_or_default
    { $$ = std::move($1); $$.auth_validation = std::move($3); }
  | rest_authentication PAGE_SYMBOL CONTENT_SYMBOL quoted_text_or_default
    { $$ = std::move($1); $$.auth_page_content = std::move($4); }
  ;

add_auth_app:
    ADD_SYMBOL AUTH_SYMBOL APP_SYMBOL auth_app_name opt_if_exists
    { $$ = Auth_app_reference{std::move($4), $5}; }
  ;

remove_auth_app:
    REMOVE_SYMBOL AUTH_SYMBOL APP_SYMBOL auth_app_name opt_if_exists
    { $$ = Auth_app_reference{std::move($4), $5}; }
  ;

/* - CREATE REST SCHEMA ---------------------------------------------------- */

create_rest_schema_statement:
    create_rest_schema_prefix opt_schema_request_path opt_on_service
    FROM_SYMBOL schema_name rest_schema_options
    {
      Create_rest_schema s;
      s.flags = $1;
      s.schema_path = std::move($2);
      s.service = std::move($3);
      s.schema_name = std::move($5);
      s.options = std::move($6);
      $$ = make_statement(std::move(s), @1);
    }
  ;

create_rest_schema_prefix:
    CREATE_SYMBOL OR_SYMBOL REPLACE_SYMBOL REST_SYMBOL DATABASE_SYMBOL
    { $$ = Create_flags{true, false}; }
  | CREATE_SYMBOL REST_SYMBOL DATABASE_SYMBOL opt_if_not_exists
    { $$ = Create_flags{false, $4}; }
  ;

opt_schema_request_path:
    %empty { $$ = std::nullopt; }
  | schema_request_path { $$ = std::move($1); }
  ;

rest_schema_options:
    %empty { $$ = Schema_options{}; }
  | rest_schema_options enabled_disabled_private
    { $$ = std::move($1); $$.enabled = $2; }
  | rest_schema_options authentication_required
    { $$ = std::move($1); $$.requires_auth = $2; }
  | rest_schema_options items_per_page
    { $$ = std::move($1); $$.items_per_page = $2; }
  | rest_schema_options json_options
    { $$ = std::move($1); $$.options = std::move($2); }
  | rest_schema_options comments
    { $$ = std::move($1); $$.comments = std::move($2); }
  | rest_schema_options metadata
    { $$ = std::move($1); $$.metadata = std::move($2); }
  ;

/* - CREATE REST VIEW ------------------------------------------------------ */

create_rest_view_statement:
    create_rest_view_prefix view_request_path opt_on_service_schema_selector
    AS_SYMBOL qualified_identifier opt_class opt_graphql_crud_options
    opt_graphql_obj rest_object_options
    {
      Create_rest_view s;
      s.flags = $1;
      s.path = std::move($2);
      s.on = std::move($3);
      s.object = std::move($5);
      s.class_name = std::move($6);
      s.crud = $7;
      s.mapping = std::move($8);
      s.options = std::move($9);
      $$ = make_statement(std::move(s), @1);
    }
  ;

create_rest_view_prefix:
    CREATE_SYMBOL OR_SYMBOL REPLACE_SYMBOL REST_SYMBOL opt_data_mapping VIEW_SYMBOL
    { $$ = Create_flags{true, false}; }
  | CREATE_SYMBOL REST_SYMBOL opt_data_mapping VIEW_SYMBOL opt_if_not_exists
    { $$ = Create_flags{false, $5}; }
  ;

opt_data_mapping:
    %empty
  | DATA_SYMBOL
  | MAPPING_SYMBOL
  | DATA_SYMBOL MAPPING_SYMBOL
  ;

opt_class:
    %empty { $$ = std::nullopt; }
  | CLASS_SYMBOL rest_object_name { $$ = std::move($2); }
  ;

rest_object_options:
    %empty { $$ = Object_options{}; }
  | rest_object_options enabled_disabled_private
    { $$ = std::move($1); $$.enabled = $2; }
  | rest_object_options authentication_required
    { $$ = std::move($1); $$.requires_auth = $2; }
  | rest_object_options items_per_page
    { $$ = std::move($1); $$.items_per_page = $2; }
  | rest_object_options json_options
    { $$ = std::move($1); $$.options = std::move($2); }
  | rest_object_options comments
    { $$ = std::move($1); $$.comments = std::move($2); }
  | rest_object_options metadata
    { $$ = std::move($1); $$.metadata = std::move($2); }
  | rest_object_options MEDIA_SYMBOL TYPE_SYMBOL text_string_literal
    { $$ = std::move($1); $$.media_type = std::move($4); }
  | rest_object_options MEDIA_SYMBOL TYPE_SYMBOL AUTODETECT_SYMBOL
    { $$ = std::move($1); $$.media_type_autodetect = true; }
  | rest_object_options rest_view_format
    { $$ = std::move($1); $$.format = $2; }
  | rest_object_options AUTHENTICATION_SYMBOL PROCEDURE_SYMBOL qualified_identifier
    { $$ = std::move($1); $$.auth_procedure = std::move($4); }
  ;

rest_view_format:
    FORMAT_SYMBOL FEED_SYMBOL { $$ = Result_format::feed; }
  | FORMAT_SYMBOL ITEM_SYMBOL { $$ = Result_format::item; }
  | FORMAT_SYMBOL MEDIA_SYMBOL { $$ = Result_format::media; }
  ;

/* - CREATE REST PROCEDURE ------------------------------------------------- */

create_rest_procedure_statement:
    create_rest_procedure_prefix procedure_request_path
    opt_on_service_schema_selector AS_SYMBOL qualified_identifier opt_force
    opt_rest_parameters rest_procedure_results rest_object_options
    {
      Create_rest_routine s;
      s.kind = Create_rest_routine::Kind::procedure;
      s.flags = $1;
      s.path = std::move($2);
      s.on = std::move($3);
      s.object = std::move($5);
      s.force = $6;
      s.parameters = std::move($7);
      s.results = std::move($8);
      s.options = std::move($9);
      $$ = make_statement(std::move(s), @1);
    }
  ;

create_rest_procedure_prefix:
    CREATE_SYMBOL OR_SYMBOL REPLACE_SYMBOL REST_SYMBOL PROCEDURE_SYMBOL
    { $$ = Create_flags{true, false}; }
  | CREATE_SYMBOL REST_SYMBOL PROCEDURE_SYMBOL opt_if_not_exists
    { $$ = Create_flags{false, $4}; }
  ;

%nterm <bool> opt_force;
opt_force:
    %empty { $$ = false; }
  | FORCE_SYMBOL { $$ = true; }
  ;

opt_rest_parameters:
    %empty { $$ = std::nullopt; }
  | PARAMETERS_SYMBOL graphql_obj
    { $$ = Named_graphql_object{std::nullopt, std::move($2)}; }
  | PARAMETERS_SYMBOL rest_object_name graphql_obj
    { $$ = Named_graphql_object{std::move($2), std::move($3)}; }
  ;

rest_procedure_results:
    %empty { $$ = Named_graphql_object_list{}; }
  | rest_procedure_results rest_procedure_result
    { $$ = std::move($1); $$.push_back(std::move($2)); }
  ;

rest_procedure_result:
    RESULT_SYMBOL graphql_obj
    { $$ = Named_graphql_object{std::nullopt, std::move($2)}; }
  | RESULT_SYMBOL rest_result_name graphql_obj
    { $$ = Named_graphql_object{std::move($2), std::move($3)}; }
  ;

/* - CREATE REST FUNCTION -------------------------------------------------- */

create_rest_function_statement:
    create_rest_function_prefix function_request_path
    opt_on_service_schema_selector AS_SYMBOL qualified_identifier opt_force
    opt_rest_parameters opt_rest_function_result rest_object_options
    {
      Create_rest_routine s;
      s.kind = Create_rest_routine::Kind::function;
      s.flags = $1;
      s.path = std::move($2);
      s.on = std::move($3);
      s.object = std::move($5);
      s.force = $6;
      s.parameters = std::move($7);
      s.results = std::move($8);
      s.options = std::move($9);
      $$ = make_statement(std::move(s), @1);
    }
  ;

create_rest_function_prefix:
    CREATE_SYMBOL OR_SYMBOL REPLACE_SYMBOL REST_SYMBOL FUNCTION_SYMBOL
    { $$ = Create_flags{true, false}; }
  | CREATE_SYMBOL REST_SYMBOL FUNCTION_SYMBOL opt_if_not_exists
    { $$ = Create_flags{false, $4}; }
  ;

opt_rest_function_result:
    %empty { $$ = Named_graphql_object_list{}; }
  | rest_function_result
    { $$ = Named_graphql_object_list{}; $$.push_back(std::move($1)); }
  ;

rest_function_results:
    %empty { $$ = Named_graphql_object_list{}; }
  | rest_function_results rest_function_result
    { $$ = std::move($1); $$.push_back(std::move($2)); }
  ;

rest_function_result:
    RESULT_SYMBOL graphql_obj
    { $$ = Named_graphql_object{std::nullopt, std::move($2)}; }
  | RESULT_SYMBOL rest_result_name graphql_obj
    { $$ = Named_graphql_object{std::move($2), std::move($3)}; }
  ;

/* - CREATE REST CONTENT SET ----------------------------------------------- */

create_rest_content_set_statement:
    create_rest_content_set_prefix content_set_request_path opt_on_service
    rest_content_set_options
    {
      Create_rest_content_set s;
      s.flags = $1;
      s.path = std::move($2);
      s.service = std::move($3);
      s.options = std::move($4);
      $$ = make_statement(std::move(s), @1);
    }
  ;

create_rest_content_set_prefix:
    CREATE_SYMBOL OR_SYMBOL REPLACE_SYMBOL REST_SYMBOL CONTENT_SYMBOL SET_SYMBOL
    { $$ = Create_flags{true, false}; }
  | CREATE_SYMBOL REST_SYMBOL CONTENT_SYMBOL SET_SYMBOL opt_if_not_exists
    { $$ = Create_flags{false, $5}; }
  ;

rest_content_set_options:
    %empty { $$ = Content_set_options{}; }
  | rest_content_set_options enabled_disabled_private
    { $$ = std::move($1); $$.enabled = $2; }
  | rest_content_set_options authentication_required
    { $$ = std::move($1); $$.requires_auth = $2; }
  | rest_content_set_options json_options
    { $$ = std::move($1); $$.options = std::move($2); }
  | rest_content_set_options comments
    { $$ = std::move($1); $$.comments = std::move($2); }
  ;

/* ALTER takes LOAD [TYPESCRIPT] SCRIPTS as well: the stored files are
   analysed and their MRS scripts registered as REST endpoints */
alter_rest_content_set_options:
    %empty { $$ = Content_set_options{}; }
  | alter_rest_content_set_options enabled_disabled_private
    { $$ = std::move($1); $$.enabled = $2; }
  | alter_rest_content_set_options authentication_required
    { $$ = std::move($1); $$.requires_auth = $2; }
  | alter_rest_content_set_options json_options
    { $$ = std::move($1); $$.options = std::move($2); }
  | alter_rest_content_set_options comments
    { $$ = std::move($1); $$.comments = std::move($2); }
  | alter_rest_content_set_options LOAD_SYMBOL SCRIPTS_SYMBOL
    { $$ = std::move($1); $$.load_scripts = true; }
  | alter_rest_content_set_options LOAD_SYMBOL TYPESCRIPT_SYMBOL SCRIPTS_SYMBOL
    { $$ = std::move($1); $$.load_scripts = true; }
  ;

/* - CREATE REST CONTENT FILE ---------------------------------------------- */

create_rest_content_file_statement:
    create_rest_content_file_prefix content_file_request_path ON_SYMBOL
    opt_service_request_path CONTENT_SYMBOL SET_SYMBOL content_set_request_path
    opt_binary CONTENT_SYMBOL text_string_literal rest_content_file_options
    {
      Create_rest_content_file s;
      s.flags = $1;
      s.path = std::move($2);
      s.service = std::move($4);
      s.content_set_path = std::move($7);
      s.binary = $8;
      s.content = std::move($10);
      s.options = std::move($11);
      $$ = make_statement(std::move(s), @1);
    }
  ;

%nterm <bool> opt_binary;
opt_binary:
    %empty { $$ = false; }
  | BINARY_SYMBOL { $$ = true; }
  ;

create_rest_content_file_prefix:
    CREATE_SYMBOL OR_SYMBOL REPLACE_SYMBOL REST_SYMBOL CONTENT_SYMBOL FILE_SYMBOL
    { $$ = Create_flags{true, false}; }
  | CREATE_SYMBOL REST_SYMBOL CONTENT_SYMBOL FILE_SYMBOL opt_if_not_exists
    { $$ = Create_flags{false, $5}; }
  ;

rest_content_file_options:
    %empty { $$ = Content_file_options{}; }
  | rest_content_file_options enabled_disabled_private
    { $$ = std::move($1); $$.enabled = $2; }
  | rest_content_file_options authentication_required
    { $$ = std::move($1); $$.requires_auth = $2; }
  | rest_content_file_options json_options
    { $$ = std::move($1); $$.options = std::move($2); }
  ;

/* - CREATE REST AUTH APP -------------------------------------------------- */

create_rest_auth_app_statement:
    create_rest_auth_app_prefix auth_app_name VENDOR_SYMBOL vendor
    rest_auth_app_options
    {
      Create_rest_auth_app s;
      s.flags = $1;
      s.name = std::move($2);
      s.vendor = std::move($4);
      s.options = std::move($5);
      $$ = make_statement(std::move(s), @1);
    }
  ;

create_rest_auth_app_prefix:
    CREATE_SYMBOL OR_SYMBOL REPLACE_SYMBOL REST_SYMBOL auth_or_authentication APP_SYMBOL
    { $$ = Create_flags{true, false}; }
  | CREATE_SYMBOL REST_SYMBOL auth_or_authentication APP_SYMBOL opt_if_not_exists
    { $$ = Create_flags{false, $5}; }
  ;

auth_or_authentication:
    AUTH_SYMBOL
  | AUTHENTICATION_SYMBOL
  ;

vendor:
    MRS_SYMBOL { $$ = "MRS"; }
  | MYSQL_SYMBOL { $$ = "MySQL Internal"; }
  | vendor_name { $$ = std::move($1); }
  ;

auth_app_name:
    text_or_identifier { $$ = std::move($1); }
  ;

vendor_name:
    text_or_identifier { $$ = std::move($1); }
  ;

rest_auth_app_options:
    %empty { $$ = Auth_app_options{}; }
  | rest_auth_app_options enabled_disabled
    { $$ = std::move($1); $$.enabled = $2; }
  | rest_auth_app_options comments
    { $$ = std::move($1); $$.comments = std::move($2); }
  | rest_auth_app_options allow_new_users_to_register
    { $$ = std::move($1); $$.allow_new_users = $2; }
  | rest_auth_app_options DEFAULT_SYMBOL ROLE_SYMBOL text_or_identifier
    { $$ = std::move($1); $$.default_role = std::move($4); }
  | rest_auth_app_options app_or_client ID_SYMBOL text_string_literal
    { $$ = std::move($1); $$.app_id = std::move($4); }
  | rest_auth_app_options app_or_client SECRET_SYMBOL text_string_literal
    { $$ = std::move($1); $$.app_secret = std::move($4); }
  | rest_auth_app_options URL_SYMBOL text_string_literal
    { $$ = std::move($1); $$.url = std::move($3); }
  ;

app_or_client:
    APP_SYMBOL
  | CLIENT_SYMBOL
  ;

allow_new_users_to_register:
    ALLOW_SYMBOL NEW_SYMBOL USERS_SYMBOL opt_to_register { $$ = true; }
  | DO_SYMBOL NOT_SYMBOL ALLOW_SYMBOL NEW_SYMBOL USERS_SYMBOL opt_to_register
    { $$ = false; }
  ;

opt_to_register:
    %empty
  | TO_SYMBOL REGISTER_SYMBOL
  ;

/* - CREATE REST USER ------------------------------------------------------ */

create_rest_user_statement:
    create_rest_user_prefix user_name AT_SIGN_SYMBOL auth_app_name
    opt_identified_by user_options
    {
      Create_rest_user s;
      s.flags = $1;
      s.name = std::move($2);
      s.auth_app = std::move($4);
      s.password = std::move($5);
      s.options = std::move($6);
      $$ = make_statement(std::move(s), @1);
    }
  ;

create_rest_user_prefix:
    CREATE_SYMBOL OR_SYMBOL REPLACE_SYMBOL REST_SYMBOL USER_SYMBOL
    { $$ = Create_flags{true, false}; }
  | CREATE_SYMBOL REST_SYMBOL USER_SYMBOL opt_if_not_exists
    { $$ = Create_flags{false, $4}; }
  ;

opt_identified_by:
    %empty { $$ = std::nullopt; }
  | IDENTIFIED_SYMBOL BY_SYMBOL user_password { $$ = std::move($3); }
  ;

user_name:
    text_or_identifier { $$ = std::move($1); }
  ;

user_password:
    text_string_literal { $$ = std::move($1); }
  ;

user_options:
    %empty { $$ = User_options{}; }
  | user_options ACCOUNT_SYMBOL LOCK_SYMBOL
    { $$ = std::move($1); $$.account_locked = true; }
  | user_options ACCOUNT_SYMBOL UNLOCK_SYMBOL
    { $$ = std::move($1); $$.account_locked = false; }
  | user_options APP_SYMBOL OPTIONS_SYMBOL json_value
    { $$ = std::move($1); $$.app_options = std::move($4); }
  | user_options json_options
    { $$ = std::move($1); $$.options = std::move($2); }
  ;

/* - CREATE REST ROLE ------------------------------------------------------ */

create_rest_role_statement:
    create_rest_role_prefix role_name opt_extends opt_role_service
    rest_role_options
    {
      Create_rest_role s;
      s.flags = $1;
      s.name = std::move($2);
      s.extends = std::move($3);
      s.on = std::move($4);
      s.options = std::move($5);
      $$ = make_statement(std::move(s), @1);
    }
  ;

create_rest_role_prefix:
    CREATE_SYMBOL OR_SYMBOL REPLACE_SYMBOL REST_SYMBOL ROLE_SYMBOL
    { $$ = Create_flags{true, false}; }
  | CREATE_SYMBOL REST_SYMBOL ROLE_SYMBOL opt_if_not_exists
    { $$ = Create_flags{false, $4}; }
  ;

opt_extends:
    %empty { $$ = std::nullopt; }
  | EXTENDS_SYMBOL parent_role_name { $$ = std::move($2); }
  ;

rest_role_options:
    %empty { $$ = Role_options{}; }
  | rest_role_options json_options
    { $$ = std::move($1); $$.options = std::move($2); }
  | rest_role_options comments
    { $$ = std::move($1); $$.comments = std::move($2); }
  ;

parent_role_name:
    text_or_identifier { $$ = std::move($1); }
  ;

role_name:
    text_or_identifier { $$ = std::move($1); }
  ;

/* CLONE statements ======================================================== */

clone_rest_service_statement:
    CLONE_SYMBOL REST_SYMBOL SERVICE_SYMBOL service_request_path NEW_SYMBOL
    REQUEST_SYMBOL PATH_SYMBOL new_service_request_path
    {
      Clone_rest_service s;
      s.path = std::move($4);
      s.new_path = std::move($8);
      $$ = make_statement(std::move(s), @1);
    }
  ;

/* ALTER statements ======================================================== */

alter_rest_service_statement:
    ALTER_SYMBOL REST_SYMBOL SERVICE_SYMBOL service_request_path
    rest_service_options
    {
      Alter_rest_service s;
      s.path = std::move($4);
      s.options = std::move($5);
      $$ = make_statement(std::move(s), @1);
    }
  | ALTER_SYMBOL REST_SYMBOL SERVICE_SYMBOL service_request_path NEW_SYMBOL
    REQUEST_SYMBOL PATH_SYMBOL new_service_request_path rest_service_options
    {
      Alter_rest_service s;
      s.path = std::move($4);
      s.new_path = std::move($8);
      s.options = std::move($9);
      $$ = make_statement(std::move(s), @1);
    }
  ;

alter_rest_schema_statement:
    ALTER_SYMBOL REST_SYMBOL DATABASE_SYMBOL opt_schema_request_path
    opt_on_service opt_new_request_path opt_from_schema_name
    rest_schema_options
    {
      Alter_rest_schema s;
      s.schema_path = std::move($4);
      s.service = std::move($5);
      s.new_path = std::move($6);
      s.schema_name = std::move($7);
      s.options = std::move($8);
      $$ = make_statement(std::move(s), @1);
    }
  ;

opt_new_request_path:
    %empty { $$ = std::nullopt; }
  | NEW_SYMBOL REQUEST_SYMBOL PATH_SYMBOL request_path_identifier
    { $$ = std::move($4); }
  ;

opt_from_schema_name:
    %empty { $$ = std::nullopt; }
  | FROM_SYMBOL schema_name { $$ = std::move($2); }
  ;

alter_rest_view_statement:
    ALTER_SYMBOL REST_SYMBOL opt_data_mapping VIEW_SYMBOL view_request_path
    opt_on_service_schema_selector opt_new_request_path opt_alter_view_class
    rest_object_options
    {
      Alter_rest_view s;
      s.path = std::move($5);
      s.on = std::move($6);
      s.new_path = std::move($7);
      s.class_def = std::move($8);
      s.options = std::move($9);
      $$ = make_statement(std::move(s), @1);
    }
  ;

opt_alter_view_class:
    %empty { $$ = std::nullopt; }
  | CLASS_SYMBOL rest_object_name opt_graphql_crud_options opt_graphql_obj
    {
      Alter_rest_view::Class_definition c;
      c.name = std::move($2);
      c.crud = $3;
      c.mapping = std::move($4);
      $$ = std::move(c);
    }
  ;

alter_rest_procedure_statement:
    ALTER_SYMBOL REST_SYMBOL PROCEDURE_SYMBOL procedure_request_path
    opt_on_service_schema_selector opt_new_request_path opt_rest_parameters
    rest_procedure_results rest_object_options
    {
      Alter_rest_routine s;
      s.kind = Create_rest_routine::Kind::procedure;
      s.path = std::move($4);
      s.on = std::move($5);
      s.new_path = std::move($6);
      s.parameters = std::move($7);
      s.results = std::move($8);
      s.options = std::move($9);
      $$ = make_statement(std::move(s), @1);
    }
  ;

alter_rest_function_statement:
    ALTER_SYMBOL REST_SYMBOL FUNCTION_SYMBOL function_request_path
    opt_on_service_schema_selector opt_new_request_path opt_rest_parameters
    rest_function_results rest_object_options
    {
      Alter_rest_routine s;
      s.kind = Create_rest_routine::Kind::function;
      s.path = std::move($4);
      s.on = std::move($5);
      s.new_path = std::move($6);
      s.parameters = std::move($7);
      s.results = std::move($8);
      s.options = std::move($9);
      $$ = make_statement(std::move(s), @1);
    }
  ;

alter_rest_content_set_statement:
    ALTER_SYMBOL REST_SYMBOL CONTENT_SYMBOL SET_SYMBOL content_set_request_path
    opt_on_service opt_new_request_path alter_rest_content_set_options
    {
      Alter_rest_content_set s;
      s.path = std::move($5);
      s.service = std::move($6);
      s.new_path = std::move($7);
      s.options = std::move($8);
      $$ = make_statement(std::move(s), @1);
    }
  ;

alter_rest_auth_app_statement:
    ALTER_SYMBOL REST_SYMBOL auth_or_authentication APP_SYMBOL auth_app_name
    opt_new_service_name rest_auth_app_options
    {
      Alter_rest_auth_app s;
      s.name = std::move($5);
      s.new_name = std::move($6);
      s.options = std::move($7);
      $$ = make_statement(std::move(s), @1);
    }
  ;

opt_new_service_name:
    %empty { $$ = std::nullopt; }
  | NEW_SYMBOL NAME_SYMBOL new_auth_app_name { $$ = std::move($3); }
  ;

new_auth_app_name:
    text_or_identifier { $$ = std::move($1); }
  ;

alter_rest_user_statement:
    ALTER_SYMBOL REST_SYMBOL USER_SYMBOL user_name AT_SIGN_SYMBOL auth_app_name
    opt_identified_by user_options
    {
      Alter_rest_user s;
      s.name = std::move($4);
      s.auth_app = std::move($6);
      s.password = std::move($7);
      s.options = std::move($8);
      $$ = make_statement(std::move(s), @1);
    }
  ;

/* DROP statements ========================================================= */

drop_rest_service_statement:
    DROP_SYMBOL REST_SYMBOL SERVICE_SYMBOL opt_if_exists service_request_path
    {
      Drop_rest_service s;
      s.if_exists = $4;
      s.path = std::move($5);
      $$ = make_statement(std::move(s), @1);
    }
  ;

drop_rest_schema_statement:
    DROP_SYMBOL REST_SYMBOL DATABASE_SYMBOL opt_if_exists schema_request_path
    opt_from_service
    {
      Drop_rest_schema s;
      s.if_exists = $4;
      s.schema_path = std::move($5);
      s.service = std::move($6);
      $$ = make_statement(std::move(s), @1);
    }
  ;

drop_rest_view_statement:
    DROP_SYMBOL REST_SYMBOL opt_data_mapping VIEW_SYMBOL opt_if_exists
    view_request_path opt_from_service_schema_selector
    {
      Drop_rest_db_object s;
      s.kind = Db_object_kind::view;
      s.if_exists = $5;
      s.path = std::move($6);
      s.from = std::move($7);
      $$ = make_statement(std::move(s), @1);
    }
  ;

drop_rest_procedure_statement:
    DROP_SYMBOL REST_SYMBOL PROCEDURE_SYMBOL opt_if_exists
    procedure_request_path opt_from_service_schema_selector
    {
      Drop_rest_db_object s;
      s.kind = Db_object_kind::procedure;
      s.if_exists = $4;
      s.path = std::move($5);
      s.from = std::move($6);
      $$ = make_statement(std::move(s), @1);
    }
  ;

drop_rest_function_statement:
    DROP_SYMBOL REST_SYMBOL FUNCTION_SYMBOL opt_if_exists function_request_path
    opt_from_service_schema_selector
    {
      Drop_rest_db_object s;
      s.kind = Db_object_kind::function;
      s.if_exists = $4;
      s.path = std::move($5);
      s.from = std::move($6);
      $$ = make_statement(std::move(s), @1);
    }
  ;

drop_rest_content_set_statement:
    DROP_SYMBOL REST_SYMBOL CONTENT_SYMBOL SET_SYMBOL opt_if_exists
    content_set_request_path opt_from_service
    {
      Drop_rest_content_set s;
      s.if_exists = $5;
      s.path = std::move($6);
      s.service = std::move($7);
      $$ = make_statement(std::move(s), @1);
    }
  ;

drop_rest_content_file_statement:
    DROP_SYMBOL REST_SYMBOL CONTENT_SYMBOL FILE_SYMBOL opt_if_exists
    content_file_request_path FROM_SYMBOL opt_service_request_path
    CONTENT_SYMBOL SET_SYMBOL content_set_request_path
    {
      Drop_rest_content_file s;
      s.if_exists = $5;
      s.path = std::move($6);
      s.service = std::move($8);
      s.content_set_path = std::move($11);
      $$ = make_statement(std::move(s), @1);
    }
  ;

drop_rest_auth_app_statement:
    DROP_SYMBOL REST_SYMBOL auth_or_authentication APP_SYMBOL opt_if_exists
    auth_app_name
    {
      Drop_rest_auth_app s;
      s.if_exists = $5;
      s.name = std::move($6);
      $$ = make_statement(std::move(s), @1);
    }
  ;

drop_rest_user_statement:
    DROP_SYMBOL REST_SYMBOL USER_SYMBOL opt_if_exists user_name AT_SIGN_SYMBOL
    auth_app_name
    {
      Drop_rest_user s;
      s.if_exists = $4;
      s.name = std::move($5);
      s.auth_app = std::move($7);
      $$ = make_statement(std::move(s), @1);
    }
  ;

drop_rest_role_statement:
    DROP_SYMBOL REST_SYMBOL ROLE_SYMBOL opt_if_exists role_name opt_role_service
    {
      Drop_rest_role s;
      s.if_exists = $4;
      s.name = std::move($5);
      s.on = std::move($6);
      $$ = make_statement(std::move(s), @1);
    }
  ;

/* GRANT statements ======================================================== */

grant_rest_privilege_statement:
    GRANT_SYMBOL REST_SYMBOL privilege_list privilege_target TO_SYMBOL
    role_name opt_role_service
    {
      Rest_privilege_statement s = std::move($4);
      s.revoke = false;
      s.privileges = std::move($3);
      s.role = std::move($6);
      s.role_service = std::move($7);
      $$ = make_statement(std::move(s), @1);
    }
  ;

/*
 * (ON SERVICE? serviceRequestPathWildcard)
 * | (ON serviceSchemaSelectorWildcard (OBJECT objectRequestPathWildcard)?)
 */
privilege_target:
    %empty { $$ = Rest_privilege_statement{}; }
  | ON_SYMBOL service_request_path_wildcard
    { $$ = Rest_privilege_statement{}; $$.service_pattern = std::move($2); }
  | ON_SYMBOL SERVICE_SYMBOL service_request_path_wildcard
    { $$ = Rest_privilege_statement{}; $$.service_pattern = std::move($3); }
  | ON_SYMBOL SERVICE_SYMBOL service_request_path_wildcard
    DATABASE_SYMBOL schema_request_path_wildcard opt_object_wildcard
    {
      $$ = Rest_privilege_statement{};
      $$.service_pattern = std::move($3);
      $$.schema_pattern = std::move($5);
      $$.object_pattern = std::move($6);
    }
  | ON_SYMBOL DATABASE_SYMBOL schema_request_path_wildcard opt_object_wildcard
    {
      $$ = Rest_privilege_statement{};
      $$.schema_pattern = std::move($3);
      $$.object_pattern = std::move($4);
    }
  ;

%nterm <Opt_string> opt_object_wildcard;
opt_object_wildcard:
    %empty { $$ = std::nullopt; }
  | OBJECT_SYMBOL object_request_path_wildcard { $$ = std::move($2); }
  ;

privilege_list:
    privilege_name { $$ = Privilege_list{$1}; }
  | privilege_list COMMA_SYMBOL privilege_name
    { $$ = std::move($1); $$.push_back($3); }
  ;

privilege_name:
    CREATE_SYMBOL { $$ = Privilege::create; }
  | READ_SYMBOL { $$ = Privilege::read; }
  | UPDATE_SYMBOL { $$ = Privilege::update; }
  | DELETE_SYMBOL { $$ = Privilege::del; }
  ;

grant_rest_role_statement:
    GRANT_SYMBOL REST_SYMBOL ROLE_SYMBOL role_name opt_role_service TO_SYMBOL
    user_name AT_SIGN_SYMBOL auth_app_name opt_comments
    {
      Rest_role_statement s;
      s.revoke = false;
      s.role = std::move($4);
      s.role_service = std::move($5);
      s.user = std::move($7);
      s.auth_app = std::move($9);
      s.comments = std::move($10);
      $$ = make_statement(std::move(s), @1);
    }
  ;

/* REVOKE statements ======================================================= */

revoke_rest_privilege_statement:
    REVOKE_SYMBOL REST_SYMBOL privilege_list privilege_target FROM_SYMBOL
    role_name opt_role_service
    {
      Rest_privilege_statement s = std::move($4);
      s.revoke = true;
      s.privileges = std::move($3);
      s.role = std::move($6);
      s.role_service = std::move($7);
      $$ = make_statement(std::move(s), @1);
    }
  ;

revoke_rest_role_statement:
    REVOKE_SYMBOL REST_SYMBOL ROLE_SYMBOL role_name opt_role_service
    FROM_SYMBOL user_name AT_SIGN_SYMBOL auth_app_name
    {
      Rest_role_statement s;
      s.revoke = true;
      s.role = std::move($4);
      s.role_service = std::move($5);
      s.user = std::move($7);
      s.auth_app = std::move($9);
      $$ = make_statement(std::move(s), @1);
    }
  ;

/* USE statements ========================================================== */

use_statement:
    USE_SYMBOL REST_SYMBOL service_and_schema_request_paths
    { $$ = make_statement(std::move($3), @1); }
  ;

service_and_schema_request_paths:
    SERVICE_SYMBOL service_request_path
    { $$ = Use_rest{std::move($2), std::nullopt}; }
  | service_schema_selector
    { $$ = Use_rest{std::move($1.service), std::move($1.schema_path)}; }
  ;

/* SHOW statements ========================================================= */

show_rest_metadata_status_statement:
    SHOW_SYMBOL REST_SYMBOL METADATA_SYMBOL STATUS_SYMBOL opt_output_format
    { $$ = make_statement(Show_rest_metadata_status{$5}, @1); }
  | SHOW_SYMBOL REST_SYMBOL STATUS_SYMBOL opt_output_format
    { $$ = make_statement(Show_rest_metadata_status{$4}, @1); }
  ;

show_rest_services_statement:
    SHOW_SYMBOL REST_SYMBOL SERVICES_SYMBOL opt_for_auth_app
    { $$ = make_statement(Show_rest_services{std::move($4), std::nullopt}, @1); }
  | SHOW_SYMBOL REST_SYMBOL SERVICES_SYMBOL FOR_SYMBOL DAEMON_SYMBOL daemon_id
    { $$ = make_statement(Show_rest_services{std::nullopt, $6}, @1); }
  ;

show_rest_daemons_statement:
    SHOW_SYMBOL REST_SYMBOL DAEMONS_SYMBOL opt_output_format
    { $$ = make_statement(Show_rest_daemons{$4}, @1); }
  ;

drop_rest_daemon_statement:
    DROP_SYMBOL REST_SYMBOL DAEMON_SYMBOL opt_if_exists daemon_id
    { $$ = make_statement(Drop_rest_daemon{$4, $5}, @1); }
  ;

daemon_id:
    INT_NUMBER { $$ = std::stoll($1); }
  ;

opt_for_auth_app:
    %empty { $$ = std::nullopt; }
  | FOR_SYMBOL AUTH_SYMBOL APP_SYMBOL auth_app_name { $$ = std::move($4); }
  ;

show_rest_schemas_statement:
    SHOW_SYMBOL REST_SYMBOL DATABASES_SYMBOL opt_on_from_service
    { $$ = make_statement(Show_rest_schemas{std::move($4)}, @1); }
  ;

show_rest_views_statement:
    SHOW_SYMBOL REST_SYMBOL opt_data_mapping VIEWS_SYMBOL
    opt_on_from_service_schema_selector
    {
      $$ = make_statement(
          Show_rest_db_objects{Db_object_kind::view, std::move($5)}, @1);
    }
  ;

show_rest_procedures_statement:
    SHOW_SYMBOL REST_SYMBOL PROCEDURES_SYMBOL opt_on_from_service_schema_selector
    {
      $$ = make_statement(
          Show_rest_db_objects{Db_object_kind::procedure, std::move($4)}, @1);
    }
  ;

show_rest_functions_statement:
    SHOW_SYMBOL REST_SYMBOL FUNCTIONS_SYMBOL opt_on_from_service_schema_selector
    {
      $$ = make_statement(
          Show_rest_db_objects{Db_object_kind::function, std::move($4)}, @1);
    }
  ;

show_rest_content_sets_statement:
    SHOW_SYMBOL REST_SYMBOL CONTENT_SYMBOL SETS_SYMBOL opt_on_from_service
    { $$ = make_statement(Show_rest_content_sets{std::move($5)}, @1); }
  ;

show_rest_content_files_statement:
    SHOW_SYMBOL REST_SYMBOL CONTENT_SYMBOL FILES_SYMBOL on_or_from
    opt_service_request_path CONTENT_SYMBOL SET_SYMBOL content_set_request_path
    {
      $$ = make_statement(
          Show_rest_content_files{std::move($6), std::move($9)}, @1);
    }
  ;

on_or_from:
    ON_SYMBOL
  | FROM_SYMBOL
  ;

show_rest_auth_apps_statement:
    SHOW_SYMBOL REST_SYMBOL AUTH_SYMBOL APPS_SYMBOL opt_on_from_service
    { $$ = make_statement(Show_rest_auth_apps{std::move($5)}, @1); }
  ;

show_rest_auth_vendors_statement:
    SHOW_SYMBOL REST_SYMBOL AUTH_SYMBOL VENDORS_SYMBOL
    { $$ = make_statement(Show_rest_auth_vendors{}, @1); }
  ;

show_rest_users_statement:
    SHOW_SYMBOL REST_SYMBOL USERS_SYMBOL opt_on_from_service opt_for_auth_app
    {
      $$ = make_statement(Show_rest_users{std::move($4), std::move($5)}, @1);
    }
  ;

show_rest_columns_statement:
    SHOW_SYMBOL REST_SYMBOL COLUMNS_SYMBOL from_or_in opt_columns_source
    qualified_identifier opt_output_format
    {
      Show_rest_columns s;
      s.source = $5;
      s.object = std::move($6);
      s.format = $7;
      $$ = make_statement(std::move(s), @1);
    }
  ;

from_or_in:
    FROM_SYMBOL
  | IN_SYMBOL
  ;

opt_columns_source:
    %empty { $$ = Show_rest_columns::Source::any; }
  | TABLE_SYMBOL { $$ = Show_rest_columns::Source::table; }
  | VIEW_SYMBOL { $$ = Show_rest_columns::Source::view; }
  | PROCEDURE_SYMBOL { $$ = Show_rest_columns::Source::procedure; }
  | FUNCTION_SYMBOL { $$ = Show_rest_columns::Source::function; }
  ;

show_rest_roles_statement:
    SHOW_SYMBOL REST_SYMBOL ROLES_SYMBOL opt_on_from_role_service
    {
      Show_rest_roles s;
      s.on = std::move($4);
      $$ = make_statement(std::move(s), @1);
    }
  | SHOW_SYMBOL REST_SYMBOL ROLES_SYMBOL opt_on_from_role_service FOR_SYMBOL
    AT_SIGN_SYMBOL auth_app_name
    {
      Show_rest_roles s;
      s.on = std::move($4);
      s.auth_app = std::move($7);
      $$ = make_statement(std::move(s), @1);
    }
  | SHOW_SYMBOL REST_SYMBOL ROLES_SYMBOL opt_on_from_role_service FOR_SYMBOL
    user_name AT_SIGN_SYMBOL auth_app_name
    {
      Show_rest_roles s;
      s.on = std::move($4);
      s.user = std::move($6);
      s.auth_app = std::move($8);
      $$ = make_statement(std::move(s), @1);
    }
  ;

show_rest_grants_statement:
    SHOW_SYMBOL REST_SYMBOL GRANTS_SYMBOL FOR_SYMBOL role_name
    opt_on_from_role_service
    { $$ = make_statement(Show_rest_grants{std::move($5), std::move($6)}, @1); }
  ;

show_create_rest_service_statement:
    SHOW_SYMBOL CREATE_SYMBOL REST_SYMBOL SERVICE_SYMBOL
    opt_including_endpoints opt_output_format
    {
      $$ = make_statement(
          with_format(Show_create_rest_service{std::nullopt, $5}, $6), @1);
    }
  | SHOW_SYMBOL CREATE_SYMBOL REST_SYMBOL SERVICE_SYMBOL service_request_path
    opt_including_endpoints opt_output_format
    {
      $$ = make_statement(
          with_format(Show_create_rest_service{std::move($5), $6}, $7), @1);
    }
  ;

/* INCLUDING DATABASE [AND STATIC [AND DYNAMIC]] | ALL ENDPOINTS */
opt_including_endpoints:
    %empty { $$ = Endpoint_selection{}; }
  | INCLUDING_SYMBOL endpoint_selection ENDPOINTS_SYMBOL { $$ = $2; }
  ;

/* FORMAT=JSON | FORMAT=TRADITIONAL, as EXPLAIN FORMAT=JSON in the server */
opt_output_format:
    %empty { $$ = Output_format::traditional; }
  | FORMAT_SYMBOL EQUAL_OPERATOR JSON_SYMBOL { $$ = Output_format::json; }
  | FORMAT_SYMBOL EQUAL_OPERATOR text_or_identifier
    { $$ = output_format($3, @3); }
  ;

show_create_rest_schema_statement:
    SHOW_SYMBOL CREATE_SYMBOL REST_SYMBOL DATABASE_SYMBOL opt_schema_request_path
    opt_on_from_service opt_output_format
    {
      $$ = make_statement(
          with_format(Show_create_rest_schema{std::move($5), std::move($6)}, $7),
          @1);
    }
  ;

show_create_rest_view_statement:
    SHOW_SYMBOL CREATE_SYMBOL REST_SYMBOL opt_data_mapping VIEW_SYMBOL
    view_request_path opt_on_from_service_schema_selector opt_output_format
    {
      $$ = make_statement(
          with_format(Show_create_rest_db_object{Db_object_kind::view,
                                                 std::move($6), std::move($7)},
                      $8), @1);
    }
  ;

show_create_rest_procedure_statement:
    SHOW_SYMBOL CREATE_SYMBOL REST_SYMBOL PROCEDURE_SYMBOL procedure_request_path
    opt_on_from_service_schema_selector opt_output_format
    {
      $$ = make_statement(
          with_format(Show_create_rest_db_object{Db_object_kind::procedure,
                                                 std::move($5), std::move($6)},
                      $7), @1);
    }
  ;

show_create_rest_function_statement:
    SHOW_SYMBOL CREATE_SYMBOL REST_SYMBOL FUNCTION_SYMBOL function_request_path
    opt_on_from_service_schema_selector opt_output_format
    {
      $$ = make_statement(
          with_format(Show_create_rest_db_object{Db_object_kind::function,
                                                 std::move($5), std::move($6)},
                      $7), @1);
    }
  ;

show_create_rest_content_set_statement:
    SHOW_SYMBOL CREATE_SYMBOL REST_SYMBOL CONTENT_SYMBOL SET_SYMBOL
    content_set_request_path opt_on_from_service opt_output_format
    {
      $$ = make_statement(
          with_format(Show_create_rest_content_set{std::move($6), std::move($7)},
                      $8), @1);
    }
  ;

show_create_rest_content_file_statement:
    SHOW_SYMBOL CREATE_SYMBOL REST_SYMBOL CONTENT_SYMBOL FILE_SYMBOL
    content_file_request_path on_or_from opt_service_request_path
    CONTENT_SYMBOL SET_SYMBOL content_set_request_path opt_output_format
    {
      $$ = make_statement(
          with_format(Show_create_rest_content_file{std::move($6), std::move($8),
                                                    std::move($11)},
                      $12), @1);
    }
  ;

show_create_rest_auth_app_statement:
    SHOW_SYMBOL CREATE_SYMBOL REST_SYMBOL AUTH_SYMBOL APP_SYMBOL auth_app_name
    opt_output_format
    {
      $$ = make_statement(
          with_format(Show_create_rest_auth_app{std::move($6)}, $7), @1);
    }
  ;

show_create_rest_role_statement:
    SHOW_SYMBOL CREATE_SYMBOL REST_SYMBOL ROLE_SYMBOL role_name opt_role_service
    opt_output_format
    {
      $$ = make_statement(
          with_format(Show_create_rest_role{std::move($5), std::move($6)}, $7),
          @1);
    }
  ;

show_create_rest_user_statement:
    SHOW_SYMBOL CREATE_SYMBOL REST_SYMBOL USER_SYMBOL user_name AT_SIGN_SYMBOL
    auth_app_name opt_output_format
    {
      $$ = make_statement(
          with_format(Show_create_rest_user{std::move($5), std::move($7)}, $8),
          @1);
    }
  ;

/* Endpoint selection ======================================================= */

/* DATABASE (AND STATIC (AND DYNAMIC)?)? | ALL */
endpoint_selection:
    DATABASE_SYMBOL { $$ = Endpoint_selection{true, false, false}; }
  | DATABASE_SYMBOL AND_SYMBOL STATIC_SYMBOL
    { $$ = Endpoint_selection{true, true, false}; }
  | DATABASE_SYMBOL AND_SYMBOL STATIC_SYMBOL AND_SYMBOL DYNAMIC_SYMBOL
    { $$ = Endpoint_selection{true, true, true}; }
  | ALL_SYMBOL { $$ = Endpoint_selection{true, true, true}; }
  ;

/* Named identifiers ======================================================= */

service_request_path:
    request_path_identifier
    { $$ = Service_path{{}, std::move($1)}; }
  | service_developers_identifier request_path_identifier
    { $$ = Service_path{std::move($1), std::move($2)}; }
  ;

new_service_request_path:
    service_request_path { $$ = std::move($1); }
  ;

service_request_path_wildcard:
    request_path_identifier_with_wildcard { $$ = std::move($1); }
  ;

schema_request_path:
    request_path_identifier { $$ = std::move($1); }
  ;

schema_request_path_wildcard:
    request_path_identifier_with_wildcard { $$ = std::move($1); }
  ;

view_request_path:
    request_path_identifier { $$ = std::move($1); }
  ;

rest_object_name:
    identifier { $$ = std::move($1); }
  ;

rest_result_name:
    identifier { $$ = std::move($1); }
  ;

object_request_path_wildcard:
    request_path_identifier_with_wildcard { $$ = std::move($1); }
  ;

procedure_request_path:
    request_path_identifier { $$ = std::move($1); }
  ;

function_request_path:
    request_path_identifier { $$ = std::move($1); }
  ;

content_set_request_path:
    request_path_identifier { $$ = std::move($1); }
  ;

content_file_request_path:
    request_path_identifier { $$ = std::move($1); }
  ;

/* Common basic rules ====================================================== */

service_developer_identifier:
    text_or_identifier { $$ = std::move($1); }
  ;

service_developers_identifier:
    service_developer_list AT_SIGN_SYMBOL { $$ = std::move($1); }
  ;

service_developer_list:
    service_developer_identifier { $$ = String_list{std::move($1)}; }
  | service_developer_list COMMA_SYMBOL service_developer_identifier
    { $$ = std::move($1); $$.push_back(std::move($3)); }
  ;

request_path_identifier:
    REST_REQUEST_PATH { $$ = std::move($1); }
  | BACK_TICK_QUOTED_ID
    { validate_request_path($1, false, @1); $$ = std::move($1); }
  | DOUBLE_QUOTED_TEXT
    {
      require_ansi_quotes(driver, true, @1);
      validate_request_path($1.text, false, @1);
      $$ = std::move($1.text);
    }
  ;

request_path_identifier_with_wildcard:
    REST_REQUEST_PATH { $$ = std::move($1); }
  | BACK_TICK_QUOTED_ID
    { validate_request_path($1, true, @1); $$ = std::move($1); }
  | DOUBLE_QUOTED_TEXT
    {
      require_ansi_quotes(driver, true, @1);
      validate_request_path($1.text, true, @1);
      $$ = std::move($1.text);
    }
  ;

/* Json ==================================================================== */

json_obj:
    OPEN_CURLY_SYMBOL json_pairs CLOSE_CURLY_SYMBOL
    { $$ = "{" + $2 + "}"; }
  | OPEN_CURLY_SYMBOL CLOSE_CURLY_SYMBOL { $$ = "{}"; }
  ;

json_pairs:
    json_pair { $$ = std::move($1); }
  | json_pairs COMMA_SYMBOL json_pair { $$ = std::move($1) + "," + $3; }
  ;

json_pair:
    DOUBLE_QUOTED_TEXT COLON_SYMBOL json_value { $$ = $1.raw + ":" + $3; }
  ;

json_arr:
    OPEN_SQUARE_SYMBOL json_values CLOSE_SQUARE_SYMBOL
    { $$ = "[" + $2 + "]"; }
  | OPEN_SQUARE_SYMBOL CLOSE_SQUARE_SYMBOL { $$ = "[]"; }
  ;

json_values:
    json_value { $$ = std::move($1); }
  | json_values COMMA_SYMBOL json_value { $$ = std::move($1) + "," + $3; }
  ;

json_value:
    DOUBLE_QUOTED_TEXT { $$ = std::move($1.raw); }
  | json_number { $$ = std::move($1); }
  | MINUS_OPERATOR json_number { $$ = "-" + $2; }
  | PLUS_OPERATOR json_number { $$ = std::move($2); }
  | json_obj { $$ = std::move($1); }
  | json_arr { $$ = std::move($1); }
  | TRUE_SYMBOL { $$ = "true"; }
  | FALSE_SYMBOL { $$ = "false"; }
  | NULL_SYMBOL { $$ = "null"; }
  ;

/* JSON needs a digit before the decimal point: .5 is stored as 0.5 */
json_number:
    INT_NUMBER { $$ = std::move($1); }
  | DECIMAL_NUMBER { $$ = $1[0] == '.' ? "0" + $1 : std::move($1); }
  | FLOAT_NUMBER { $$ = $1[0] == '.' ? "0" + $1 : std::move($1); }
  ;

/* GraphQL (data mapping) ================================================== */

graphql_obj:
    OPEN_CURLY_SYMBOL graphql_pairs CLOSE_CURLY_SYMBOL { $$ = std::move($2); }
  | OPEN_CURLY_SYMBOL CLOSE_CURLY_SYMBOL { $$ = Graphql_object{}; }
  ;

opt_graphql_obj:
    %empty { $$ = std::nullopt; }
  | graphql_obj { $$ = std::move($1); }
  ;

graphql_pairs:
    graphql_pair { $$ = Graphql_object{}; $$.fields.push_back(std::move($1)); }
  | graphql_pairs COMMA_SYMBOL graphql_pair
    { $$ = std::move($1); $$.fields.push_back(std::move($3)); }
  ;

graphql_crud_options:
    AT_INSERT_SYMBOL { $$ = Crud_annotations{}; $$.insert = true; }
  | AT_NOINSERT_SYMBOL { $$ = Crud_annotations{}; $$.no_insert = true; }
  | AT_UPDATE_SYMBOL { $$ = Crud_annotations{}; $$.update = true; }
  | AT_NOUPDATE_SYMBOL { $$ = Crud_annotations{}; $$.no_update = true; }
  | AT_DELETE_SYMBOL { $$ = Crud_annotations{}; $$.del = true; }
  | AT_NODELETE_SYMBOL { $$ = Crud_annotations{}; $$.no_delete = true; }
  | AT_CHECK_SYMBOL { $$ = Crud_annotations{}; $$.check = true; }
  | AT_NOCHECK_SYMBOL { $$ = Crud_annotations{}; $$.no_check = true; }
  | graphql_crud_options AT_INSERT_SYMBOL { $$ = $1; $$.insert = true; }
  | graphql_crud_options AT_NOINSERT_SYMBOL { $$ = $1; $$.no_insert = true; }
  | graphql_crud_options AT_UPDATE_SYMBOL { $$ = $1; $$.update = true; }
  | graphql_crud_options AT_NOUPDATE_SYMBOL { $$ = $1; $$.no_update = true; }
  | graphql_crud_options AT_DELETE_SYMBOL { $$ = $1; $$.del = true; }
  | graphql_crud_options AT_NODELETE_SYMBOL { $$ = $1; $$.no_delete = true; }
  | graphql_crud_options AT_CHECK_SYMBOL { $$ = $1; $$.check = true; }
  | graphql_crud_options AT_NOCHECK_SYMBOL { $$ = $1; $$.no_check = true; }
  ;

opt_graphql_crud_options:
    %empty { $$ = Crud_annotations{}; }
  | graphql_crud_options { $$ = $1; }
  ;

/*
 * graphQlPairKey : graphQlPairValue (@IN|@OUT|@INOUT)? graphQlValueOptions?
 * (@DATATYPE(...))? graphQlCrudOptions? graphQlValueJsonSchema? graphQlObj?
 *
 * The value options rule carries the field being built so that the options
 * can be folded into it; the remaining suffixes are plain optional rules.
 */
graphql_pair:
    graphql_pair_key COLON_SYMBOL graphql_pair_value opt_graphql_mode
    graphql_value_options opt_graphql_datatype opt_graphql_crud_options
    opt_graphql_value_json_schema opt_graphql_obj
    {
      $$ = std::move($5);
      $$.name = std::move($1);
      $$.source = std::move($3);
      $$.mode = $4;
      $$.datatype = std::move($6);
      $$.crud = $7;
      $$.json_schema = std::move($8);
      if ($9) $$.nested.push_back(std::move(*$9));
    }
  ;

opt_graphql_mode:
    %empty { $$ = Graphql_field::Mode::none; }
  | AT_IN_SYMBOL { $$ = Graphql_field::Mode::in; }
  | AT_OUT_SYMBOL { $$ = Graphql_field::Mode::out; }
  | AT_INOUT_SYMBOL { $$ = Graphql_field::Mode::inout; }
  ;

graphql_value_options:
    %empty { $$ = Graphql_field{}; }
  | graphql_value_options AT_NOCHECK_SYMBOL { $$ = std::move($1); $$.no_check = true; }
  | graphql_value_options AT_SORTABLE_SYMBOL { $$ = std::move($1); $$.sortable = true; }
  | graphql_value_options AT_NOFILTERING_SYMBOL { $$ = std::move($1); $$.no_filtering = true; }
  | graphql_value_options AT_ROWOWNERSHIP_SYMBOL { $$ = std::move($1); $$.row_ownership = true; }
  | graphql_value_options AT_UNNEST_SYMBOL { $$ = std::move($1); $$.unnest = true; }
  | graphql_value_options AT_KEY_SYMBOL { $$ = std::move($1); $$.key = true; }
  ;

opt_graphql_datatype:
    %empty %prec PREC_BELOW_NOCHECK { $$ = std::nullopt; }
  | AT_DATATYPE_SYMBOL OPEN_PAR_SYMBOL graphql_datatype_text CLOSE_PAR_SYMBOL
    { $$ = std::move($3); }
  ;

graphql_datatype_text:
    DOUBLE_QUOTED_TEXT { $$ = std::move($1.text); }
  | SINGLE_QUOTED_TEXT { $$ = std::move($1); }
  | unquoted_identifier { $$ = std::move($1); }
  ;

opt_graphql_value_json_schema:
    %empty { $$ = std::nullopt; }
  | JSON_SYMBOL DATABASE_SYMBOL json_value { $$ = std::move($3); }
  ;

graphql_allowed_keyword:
    CREATE_SYMBOL { $$ = keyword_text(driver, @1); }
  | OR_SYMBOL { $$ = keyword_text(driver, @1); }
  | REPLACE_SYMBOL { $$ = keyword_text(driver, @1); }
  | ALTER_SYMBOL { $$ = keyword_text(driver, @1); }
  | SHOW_SYMBOL { $$ = keyword_text(driver, @1); }
  | STATUS_SYMBOL { $$ = keyword_text(driver, @1); }
  | NEW_SYMBOL { $$ = keyword_text(driver, @1); }
  | ON_SYMBOL { $$ = keyword_text(driver, @1); }
  | FROM_SYMBOL { $$ = keyword_text(driver, @1); }
  | IN_SYMBOL { $$ = keyword_text(driver, @1); }
  | DATABASES_SYMBOL { $$ = keyword_text(driver, @1); }
  | DATABASE_SYMBOL { $$ = keyword_text(driver, @1); }
  | JSON_SYMBOL { $$ = keyword_text(driver, @1); }
  | VIEW_SYMBOL { $$ = keyword_text(driver, @1); }
  | PROCEDURE_SYMBOL { $$ = keyword_text(driver, @1); }
  | FUNCTION_SYMBOL { $$ = keyword_text(driver, @1); }
  | DROP_SYMBOL { $$ = keyword_text(driver, @1); }
  | USE_SYMBOL { $$ = keyword_text(driver, @1); }
  | AS_SYMBOL { $$ = keyword_text(driver, @1); }
  | FILTER_SYMBOL { $$ = keyword_text(driver, @1); }
  | AUTHENTICATION_SYMBOL { $$ = keyword_text(driver, @1); }
  | PATH_SYMBOL { $$ = keyword_text(driver, @1); }
  | VALIDATION_SYMBOL { $$ = keyword_text(driver, @1); }
  | DEFAULT_SYMBOL { $$ = keyword_text(driver, @1); }
  | USER_SYMBOL { $$ = keyword_text(driver, @1); }
  | OPTIONS_SYMBOL { $$ = keyword_text(driver, @1); }
  | IF_SYMBOL { $$ = keyword_text(driver, @1); }
  | NOT_SYMBOL { $$ = keyword_text(driver, @1); }
  | EXISTS_SYMBOL { $$ = keyword_text(driver, @1); }
  | PAGE_SYMBOL { $$ = keyword_text(driver, @1); }
  | HOST_SYMBOL { $$ = keyword_text(driver, @1); }
  | TYPE_SYMBOL { $$ = keyword_text(driver, @1); }
  | FORMAT_SYMBOL { $$ = keyword_text(driver, @1); }
  | UPDATE_SYMBOL { $$ = keyword_text(driver, @1); }
  | NULL_SYMBOL { $$ = keyword_text(driver, @1); }
  | TRUE_SYMBOL { $$ = keyword_text(driver, @1); }
  | FALSE_SYMBOL { $$ = keyword_text(driver, @1); }
  | SET_SYMBOL { $$ = keyword_text(driver, @1); }
  | IDENTIFIED_SYMBOL { $$ = keyword_text(driver, @1); }
  | BY_SYMBOL { $$ = keyword_text(driver, @1); }
  | ROLE_SYMBOL { $$ = keyword_text(driver, @1); }
  | TO_SYMBOL { $$ = keyword_text(driver, @1); }
  | CLONE_SYMBOL { $$ = keyword_text(driver, @1); }
  | FILE_SYMBOL { $$ = keyword_text(driver, @1); }
  | BINARY_SYMBOL { $$ = keyword_text(driver, @1); }
  | DATA_SYMBOL { $$ = keyword_text(driver, @1); }
  | LOAD_SYMBOL { $$ = keyword_text(driver, @1); }
  | GRANT_SYMBOL { $$ = keyword_text(driver, @1); }
  | READ_SYMBOL { $$ = keyword_text(driver, @1); }
  | DELETE_SYMBOL { $$ = keyword_text(driver, @1); }
  | GROUP_SYMBOL { $$ = keyword_text(driver, @1); }
  | REVOKE_SYMBOL { $$ = keyword_text(driver, @1); }
  | ACCOUNT_SYMBOL { $$ = keyword_text(driver, @1); }
  | LOCK_SYMBOL { $$ = keyword_text(driver, @1); }
  | UNLOCK_SYMBOL { $$ = keyword_text(driver, @1); }
  | GRANTS_SYMBOL { $$ = keyword_text(driver, @1); }
  | FOR_SYMBOL { $$ = keyword_text(driver, @1); }
  | LEVEL_SYMBOL { $$ = keyword_text(driver, @1); }
  | ANY_SYMBOL { $$ = keyword_text(driver, @1); }
  | CLIENT_SYMBOL { $$ = keyword_text(driver, @1); }
  | URL_SYMBOL { $$ = keyword_text(driver, @1); }
  | NAME_SYMBOL { $$ = keyword_text(driver, @1); }
  | DO_SYMBOL { $$ = keyword_text(driver, @1); }
  | CONFIGURE_SYMBOL { $$ = keyword_text(driver, @1); }
  | REST_SYMBOL { $$ = keyword_text(driver, @1); }
  | METADATA_SYMBOL { $$ = keyword_text(driver, @1); }
  | SERVICES_SYMBOL { $$ = keyword_text(driver, @1); }
  | SERVICE_SYMBOL { $$ = keyword_text(driver, @1); }
  | VIEWS_SYMBOL { $$ = keyword_text(driver, @1); }
  | PROCEDURES_SYMBOL { $$ = keyword_text(driver, @1); }
  | PARAMETERS_SYMBOL { $$ = keyword_text(driver, @1); }
  | FUNCTIONS_SYMBOL { $$ = keyword_text(driver, @1); }
  | RESULT_SYMBOL { $$ = keyword_text(driver, @1); }
  | ENABLED_SYMBOL { $$ = keyword_text(driver, @1); }
  | PUBLISHED_SYMBOL { $$ = keyword_text(driver, @1); }
  | DISABLED_SYMBOL { $$ = keyword_text(driver, @1); }
  | PRIVATE_SYMBOL { $$ = keyword_text(driver, @1); }
  | UNPUBLISHED_SYMBOL { $$ = keyword_text(driver, @1); }
  | PROTOCOL_SYMBOL { $$ = keyword_text(driver, @1); }
  | HTTP_SYMBOL { $$ = keyword_text(driver, @1); }
  | HTTPS_SYMBOL { $$ = keyword_text(driver, @1); }
  | COMMENT_SYMBOL { $$ = keyword_text(driver, @1); }
  | REQUEST_SYMBOL { $$ = keyword_text(driver, @1); }
  | REDIRECTION_SYMBOL { $$ = keyword_text(driver, @1); }
  | MANAGEMENT_SYMBOL { $$ = keyword_text(driver, @1); }
  | AVAILABLE_SYMBOL { $$ = keyword_text(driver, @1); }
  | REQUIRED_SYMBOL { $$ = keyword_text(driver, @1); }
  | ITEMS_SYMBOL { $$ = keyword_text(driver, @1); }
  | PER_SYMBOL { $$ = keyword_text(driver, @1); }
  | CONTENT_SYMBOL { $$ = keyword_text(driver, @1); }
  | MEDIA_SYMBOL { $$ = keyword_text(driver, @1); }
  | AUTODETECT_SYMBOL { $$ = keyword_text(driver, @1); }
  | FEED_SYMBOL { $$ = keyword_text(driver, @1); }
  | ITEM_SYMBOL { $$ = keyword_text(driver, @1); }
  | SETS_SYMBOL { $$ = keyword_text(driver, @1); }
  | AUTH_SYMBOL { $$ = keyword_text(driver, @1); }
  | APPS_SYMBOL { $$ = keyword_text(driver, @1); }
  | APP_SYMBOL { $$ = keyword_text(driver, @1); }
  | ID_SYMBOL { $$ = keyword_text(driver, @1); }
  | SECRET_SYMBOL { $$ = keyword_text(driver, @1); }
  | VENDOR_SYMBOL { $$ = keyword_text(driver, @1); }
  | MRS_SYMBOL { $$ = keyword_text(driver, @1); }
  | MYSQL_SYMBOL { $$ = keyword_text(driver, @1); }
  | USERS_SYMBOL { $$ = keyword_text(driver, @1); }
  | ALLOW_SYMBOL { $$ = keyword_text(driver, @1); }
  | REGISTER_SYMBOL { $$ = keyword_text(driver, @1); }
  | CLASS_SYMBOL { $$ = keyword_text(driver, @1); }
  | DEVELOPMENT_SYMBOL { $$ = keyword_text(driver, @1); }
  | SCRIPTS_SYMBOL { $$ = keyword_text(driver, @1); }
  | MAPPING_SYMBOL { $$ = keyword_text(driver, @1); }
  | TYPESCRIPT_SYMBOL { $$ = keyword_text(driver, @1); }
  | ROLES_SYMBOL { $$ = keyword_text(driver, @1); }
  | EXTENDS_SYMBOL { $$ = keyword_text(driver, @1); }
  | OBJECT_SYMBOL { $$ = keyword_text(driver, @1); }
  | HIERARCHY_SYMBOL { $$ = keyword_text(driver, @1); }
  | TABLE_SYMBOL { $$ = keyword_text(driver, @1); }
  ;

graphql_pair_key:
    DOUBLE_QUOTED_TEXT { $$ = std::move($1.text); }
  | unquoted_identifier { $$ = std::move($1); }
  | graphql_allowed_keyword { $$ = std::move($1); }
  ;

graphql_pair_value:
    qualified_identifier { $$ = std::move($1); }
  | graphql_allowed_keyword { $$ = Qualified_name{std::nullopt, std::move($1)}; }
  ;

/* END OF MERGE PART */

schema_name:
    identifier { $$ = std::move($1); }
  ;

/* Identifiers excluding keywords (except if they are quoted). A double
   quoted string is one under ANSI_QUOTES only. */
pure_identifier:
    IDENTIFIER { $$ = std::move($1); }
  | BACK_TICK_QUOTED_ID { $$ = std::move($1); }
  | DOUBLE_QUOTED_TEXT
    { require_ansi_quotes(driver, true, @1); $$ = std::move($1.text); }
  ;

/* Identifiers including the keywords that are also allowed unquoted. */
identifier:
    pure_identifier { $$ = std::move($1); }
  | identifier_keyword { $$ = std::move($1); }
  ;

identifier_keyword:
    FILES_SYMBOL { $$ = keyword_text(driver, @1); }
  | VENDORS_SYMBOL { $$ = keyword_text(driver, @1); }
  | COLUMNS_SYMBOL { $$ = keyword_text(driver, @1); }
  | DAEMON_SYMBOL { $$ = keyword_text(driver, @1); }
  | DAEMONS_SYMBOL { $$ = keyword_text(driver, @1); }
  ;

/* An identifier where the rule using it accepts a double quoted string in
   every SQL mode. */
unquoted_identifier:
    IDENTIFIER { $$ = std::move($1); }
  | BACK_TICK_QUOTED_ID { $$ = std::move($1); }
  | identifier_keyword { $$ = std::move($1); }
  ;

qualified_identifier:
    identifier { $$ = Qualified_name{std::nullopt, std::move($1)}; }
  | identifier DOT_SYMBOL identifier
    { $$ = Qualified_name{std::move($1), std::move($3)}; }
  ;

/* A double quoted string is a text unless ANSI_QUOTES is set. */
text_string_literal:
    SINGLE_QUOTED_TEXT { $$ = std::move($1); }
  | DOUBLE_QUOTED_TEXT
    { require_ansi_quotes(driver, false, @1); $$ = std::move($1.text); }
  ;

/* A double quoted string is an identifier or a text, in every SQL mode. */
text_or_identifier:
    unquoted_identifier { $$ = std::move($1); }
  | SINGLE_QUOTED_TEXT { $$ = std::move($1); }
  | DOUBLE_QUOTED_TEXT { $$ = std::move($1.text); }
  ;

%%

namespace mrs {
namespace parser {

void Parser::error(const location_type &loc, const std::string &msg) {
  driver.set_error(msg, loc.begin.line, loc.begin.column);
}

std::string keyword_text(const Driver &driver,
                         const Parser::location_type &loc) {
  return driver.token_text(loc.begin.line, loc.begin.column);
}

Output_format output_format(const std::string &name,
                            const Parser::location_type &loc) {
  std::string upper = name;
  std::transform(upper.begin(), upper.end(), upper.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  if (upper == "JSON") return Output_format::json;
  if (upper == "TRADITIONAL") return Output_format::traditional;
  throw Parser::syntax_error(loc, "Unknown REST format name: '" + name + "'");
}

void require_ansi_quotes(const Driver &driver, bool ansi_quotes,
                         const Parser::location_type &loc) {
  if (driver.ansi_quotes() != ansi_quotes) {
    throw Parser::syntax_error(loc,
                               "syntax error, unexpected double quoted string");
  }
}

void validate_request_path(const std::string &path, bool allow_wildcards,
                           const Parser::location_type &loc) {
  if (path.empty() || path[0] == '/' ||
      (allow_wildcards && (path[0] == '*' || path[0] == '?'))) {
    return;
  }
  throw Parser::syntax_error(loc, "Invalid REST request path or wildcard");
}

}  // namespace parser
}  // namespace mrs
