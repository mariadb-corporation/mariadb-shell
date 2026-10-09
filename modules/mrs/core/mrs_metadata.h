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

#ifndef MODULES_MRS_CORE_MRS_METADATA_H_
#define MODULES_MRS_CORE_MRS_METADATA_H_

// Access to the MRS metadata schema (`mysql_rest_service_metadata`): the
// common pieces, the REST services and the REST schemas. The other object
// types are in mrs_metadata_db_objects.h, mrs_metadata_auth.h and
// mrs_metadata_content.h.
//
// The functions take the session explicitly and know nothing about the
// current service or schema; that is the executor's business.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "modules/mrs/core/mrs_db_session.h"
#include "modules/mrs/core/mrs_json.h"
#include "modules/mrs/core/mrs_sql.h"

namespace mrs {
namespace metadata {

// -- Common ---------------------------------------------------------------

struct Version {
  int major = 0;
  int minor = 0;
  int patch = 0;

  std::string str() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." +
           std::to_string(patch);
  }
  friend bool operator==(const Version &a, const Version &b) {
    return a.major == b.major && a.minor == b.minor && a.patch == b.patch;
  }
  friend bool operator<(const Version &a, const Version &b) {
    if (a.major != b.major) return a.major < b.major;
    if (a.minor != b.minor) return a.minor < b.minor;
    return a.patch < b.patch;
  }
  friend bool operator>(const Version &a, const Version &b) { return b < a; }
  friend bool operator!=(const Version &a, const Version &b) { return !(a == b); }
};

// The version of the metadata schema this module deploys and expects.
inline constexpr Version k_schema_version{5, 0, 0};
// The oldest major version this module can manage: ids are UUIDs since 5.0.0.
inline constexpr int k_supported_major_version = 5;

// Whether the metadata schema exists at all.
bool schema_exists(Db_session *session);

// The version of the deployed schema, from the msm_schema_version view (or
// the schema_version view of versions before 4.0.0). Throws when there is
// none.
Version schema_version(Db_session *session);

// Throws a descriptive error unless the metadata schema is present and of
// a version this module can work with.
void check_schema(Db_session *session);

// A new id from the get_sequence_id() function.
Id new_id(Db_session *session);

// Whether a metadata table has a row with the id.
bool row_exists(Db_session *session, std::string_view table, const Id &id);

// The ids and names used as "enabled" captions in SHOW output.
std::string enabled_caption(int enabled);

// The developer list of a service formatted as `a,'b c'@`, sorted, the way
// the metadata's sorted_developers column does it. Empty for no developers.
std::string format_developers(std::vector<std::string> developers);

// A request path (/a/b) is returned as is; anything else is backtick quoted.
std::string quote_request_path(std::string_view path);

// Appends `    KEY value` for a JSON option document in SHOW CREATE output
// (pretty printed, indented by 4). Returns the empty string for no value.
std::string format_json_entry(std::string_view key,
                              const std::optional<std::string> &json);

// Appends the SET of the options column of a metadata row to an UPDATE,
// honouring MERGE OPTIONS: merged into existing options, replaced when
// there are none yet.
void set_json_options(Db_session *session, sql::Update *update,
                      std::string_view table, const Id &id,
                      const std::string &options, bool merge);

// -- Services -------------------------------------------------------------

struct Service {
  Id id;
  Id url_host_id;
  std::optional<Id> parent_id;
  std::string url_host_name;
  std::string url_context_root;
  std::string url_protocol;  // HTTP, HTTPS or HTTP,HTTPS
  std::string name;
  int enabled = 1;
  bool published = false;
  std::optional<std::string> comments;
  std::optional<std::string> options;   // JSON text
  std::optional<std::string> metadata;  // JSON text
  std::string auth_path;
  std::optional<std::string> auth_completed_url;
  std::optional<std::string> auth_completed_url_validation;
  std::optional<std::string> auth_completed_page_content;
  std::optional<std::string> in_development;  // JSON text, {"developers": [...]}
  std::vector<std::string> developers;         // from in_development
  std::string host_ctx;                        // host + url_context_root
  std::string full_service_path;  // developers@ + host + url_context_root
  std::vector<std::string> auth_apps;  // names of the linked auth apps
};

// The service with the given id.
std::optional<Service> get_service(Db_session *session, const Id &id);

// The service with the given request path. With no developers only a
// service that is not in development matches; with developers the one in
// development for exactly those developers.
std::optional<Service> find_service(Db_session *session,
                                    std::string_view url_context_root,
                                    const std::vector<std::string> &developers);

// All services, ordered by request path.
std::vector<Service> get_services(Db_session *session);

// The services an auth app is linked to.
std::vector<Service> get_services_of_auth_app(Db_session *session,
                                              const Id &auth_app_id);

// -- Daemons --------------------------------------------------------------
//
// The MariaDB REST Daemon instances serving the REST services; each one
// registers itself in the router table of the metadata.

struct Daemon {
  int64_t id = 0;
  std::string name;  // router_name
  std::string address;
  std::string product_name;
  std::optional<std::string> version;
  std::optional<std::string> last_check_in;
  bool active = false;  // checked in within the last 10 seconds
  std::optional<std::string> developer;   // options.developer
  std::optional<std::string> attributes;  // JSON text
  std::optional<std::string> options;     // JSON text
};

// All daemons, ordered by id.
std::vector<Daemon> get_daemons(Db_session *session);
std::optional<Daemon> get_daemon(Db_session *session, int64_t id);

// The services a daemon serves (the router_services view).
std::vector<Service> get_services_of_daemon(Db_session *session, int64_t id);

// Deletes a daemon with its status reports and log entries.
void delete_daemon(Db_session *session, int64_t id);

// The values of a service to create. Unset fields take the column defaults.
struct Service_definition {
  std::string url_context_root;
  std::vector<std::string> developers;
  std::optional<bool> enabled;
  std::optional<bool> published;
  std::optional<std::string> url_protocol;
  std::optional<std::string> comments;
  std::optional<std::string> options;  // JSON; the default options when unset
  std::optional<std::string> metadata;
  std::optional<std::string> auth_path;
  std::optional<std::string> auth_completed_url;
  std::optional<std::string> auth_completed_url_validation;
  std::optional<std::string> auth_completed_page_content;
};

// Inserts a service, creating the (empty) url_host entry when needed.
// Returns its id. Throws for the reserved path /mrs.
Id add_service(Db_session *session, const Service_definition &definition);

// The changes ALTER REST SERVICE makes. An unset field is left alone; a
// field set to nullopt inside the outer optional is set to NULL.
struct Service_changes {
  std::optional<std::string> url_context_root;
  std::optional<std::vector<std::string>> developers;  // empty: not in development
  std::optional<bool> enabled;
  std::optional<bool> published;
  std::optional<std::string> url_protocol;
  std::optional<std::string> comments;
  std::optional<std::string> options;
  bool merge_options = false;
  std::optional<std::string> metadata;
  std::optional<std::optional<std::string>> auth_path;
  std::optional<std::optional<std::string>> auth_completed_url;
  std::optional<std::optional<std::string>> auth_completed_url_validation;
  std::optional<std::optional<std::string>> auth_completed_page_content;
};

void update_service(Db_session *session, const Id &id,
                    const Service_changes &changes);

void delete_service(Db_session *session, const Id &id);

// The CREATE OR REPLACE REST SERVICE statement of a service, optionally
// followed by the statements of its roles and schemas (database endpoints)
// and content sets (static and dynamic endpoints).
std::string service_create_statement(Db_session *session,
                                     const Service &service,
                                     bool include_database_endpoints,
                                     bool include_static_endpoints,
                                     bool include_dynamic_endpoints);

// Copies a service with all its schemas, objects, content sets and auth
// app links under a new request path. Returns the new id.
Id clone_service(Db_session *session, const Service &service,
                 const std::string &new_url_context_root,
                 const std::vector<std::string> &new_developers);

// -- Schemas --------------------------------------------------------------

struct Schema {
  Id id;
  Id service_id;
  std::string name;  // the database schema
  std::string schema_type;  // DATABASE_SCHEMA or SCRIPT_MODULE
  std::string request_path;
  bool requires_auth = false;
  int enabled = 1;
  bool internal = false;
  std::optional<int64_t> items_per_page;
  std::optional<std::string> comments;
  std::optional<std::string> options;
  std::optional<std::string> metadata;
  std::string host_ctx;  // of the service
};

std::optional<Schema> get_schema(Db_session *session, const Id &id);
std::optional<Schema> find_schema(Db_session *session, const Id &service_id,
                                  std::string_view request_path);
std::vector<Schema> get_schemas(Db_session *session, const Id &service_id);

struct Schema_definition {
  Id service_id;
  std::string name;
  std::optional<std::string> request_path;  // defaults to /<name>
  std::optional<bool> requires_auth;
  std::optional<int> enabled;
  std::optional<int64_t> items_per_page;
  std::optional<std::string> comments;
  std::optional<std::string> options;
  std::optional<std::string> metadata;
  std::string schema_type = "DATABASE_SCHEMA";
  bool internal = false;
  std::optional<Id> id;  // a fixed id, for cloning
};

// Inserts a schema. For a DATABASE_SCHEMA the database schema has to
// exist; its name is taken as the server reports it.
Id add_schema(Db_session *session, const Schema_definition &definition);

struct Schema_changes {
  std::optional<Id> service_id;
  std::optional<std::string> name;
  std::optional<std::string> request_path;
  std::optional<bool> requires_auth;
  std::optional<int> enabled;
  std::optional<int64_t> items_per_page;
  std::optional<std::string> comments;
  std::optional<std::string> options;
  bool merge_options = false;
  std::optional<std::string> metadata;
};

void update_schema(Db_session *session, const Id &id,
                   const Schema_changes &changes);

void delete_schema(Db_session *session, const Id &id);

// The CREATE OR REPLACE REST SCHEMA statement, optionally followed by the
// statements of its objects.
std::string schema_create_statement(Db_session *session, const Schema &schema,
                                    bool include_database_endpoints);

// Copies a schema with its objects into another service.
Id clone_schema(Db_session *session, const Schema &schema,
                const Id &new_service_id);

// -- Database schema (INFORMATION_SCHEMA) ---------------------------------

// The name of a database schema as the server reports it, or nullopt.
std::optional<std::string> database_schema_name(Db_session *session,
                                                std::string_view name);

}  // namespace metadata
}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_METADATA_H_
