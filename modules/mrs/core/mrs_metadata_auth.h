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

#ifndef MODULES_MRS_CORE_MRS_METADATA_AUTH_H_
#define MODULES_MRS_CORE_MRS_METADATA_AUTH_H_

// Access to the authentication part of the metadata schema: auth vendors,
// auth apps, users, roles and privileges.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "modules/mrs/core/mrs_db_session.h"
#include "modules/mrs/core/mrs_metadata.h"

namespace mrs {
namespace metadata {

// -- Well-known ids -------------------------------------------------------

// The rows the metadata schema ships with have well-known ids: one byte
// followed by 15 zero bytes (0x30..., 0x31... before 5.0.0).

// The MRS vendor (built-in user management, passwords stored by MRS).
inline Id mrs_vendor_id() { return "30000000-0000-0000-0000-000000000000"; }
// The MySQL Internal vendor (authentication against server accounts).
inline Id mysql_vendor_id() { return "31000000-0000-0000-0000-000000000000"; }
// The global 'Full Access' role.
inline Id full_access_role_id() {
  return "31000000-0000-0000-0000-000000000000";
}

// -- Auth vendors ---------------------------------------------------------

struct Auth_vendor {
  Id id;
  std::string name;
  bool enabled = true;
  std::optional<std::string> comments;

  // Every vendor other than MRS and MySQL Internal is an OAuth2 vendor
  // that needs an URL, an app id and an app secret.
  bool is_oauth2() const {
    return id != mrs_vendor_id() && id != mysql_vendor_id();
  }
};

// The vendor with the given name (case-insensitive).
std::optional<Auth_vendor> find_auth_vendor(Db_session *session,
                                            std::string_view name);

// All vendors, ordered by name.
std::vector<Auth_vendor> get_auth_vendors(Db_session *session);

// -- Roles ----------------------------------------------------------------

struct Role {
  Id id;
  std::optional<Id> derived_from_role_id;
  std::optional<std::string> derived_from_role_caption;
  std::optional<Id> specific_to_service_id;  // nullopt: a global role
  std::optional<std::string> specific_to_service;  // its url_context_root
  // host + url_context_root of the service; empty for a global role
  std::string specific_to_service_request_path;
  std::string caption;
  std::optional<std::string> description;
  std::optional<std::string> options;  // JSON text
};

std::optional<Role> get_role(Db_session *session, const Id &id);

// The role with the caption that is specific to the service, or the global
// role of that caption when no service is given.
std::optional<Role> find_role(Db_session *session, std::string_view caption,
                              const std::optional<Id> &service_id);

// With a service: its roles, plus the global ones when include_global is
// set. Without a service: all roles, or only the service specific ones.
std::vector<Role> get_roles(Db_session *session,
                            const std::optional<Id> &service_id,
                            bool include_global);

// A role as granted to users, for SHOW REST ROLES FOR ...
struct Granted_role {
  Role role;
  std::optional<std::string> users;  // "user@app, ..." when requested
};

// The roles granted to any user, limited to a service (and the global
// roles), a user name and/or an auth app name.
std::vector<Granted_role> get_granted_roles(
    Db_session *session, const std::optional<Id> &service_id,
    const std::optional<std::string> &user_name,
    const std::optional<std::string> &auth_app_name, bool include_users);

struct Role_definition {
  std::optional<Id> derived_from_role_id;
  std::optional<Id> specific_to_service_id;
  std::string caption;
  std::optional<std::string> description;
  std::string options = "{}";  // JSON
};

// Inserts a role. Throws a DUPLICATION ERROR for a caption that is already
// used for the same service.
Id add_role(Db_session *session, const Role_definition &definition);

// Deletes a role with its privileges and user grants. Throws a REFERENCE
// ERROR when other roles extend it.
void delete_role(Db_session *session, const Id &id);

// The CREATE REST ROLE statement of a role. With on_current_service, the
// statement names no service (ON SERVICE ...) and acts on the current one,
// as in the script of SHOW CREATE REST SERVICE ... INCLUDING ... ENDPOINTS.
std::string role_create_statement(Db_session *session, const Role &role,
                                  bool on_current_service = false);

// The CREATE REST ROLE statements of the roles specific to a service, for
// SHOW CREATE REST SERVICE ... INCLUDING DATABASE ENDPOINTS (on the current
// service).
std::vector<std::string> role_create_statements(Db_session *session,
                                                const Id &service_id);

// -- Privileges -----------------------------------------------------------

struct Privilege {
  Id id;
  Id role_id;
  std::string role_caption;
  std::vector<std::string> crud_operations;  // CREATE, READ, UPDATE, DELETE
  std::string service_path;  // patterns, * for all
  std::string schema_path;
  std::string object_path;
};

std::vector<Privilege> get_role_privileges(Db_session *session,
                                           const Id &role_id);

// Grants the operations on the given paths; extends an existing privilege
// on the same paths. Returns the id of the privilege.
Id add_role_privilege(Db_session *session, const Id &role_id,
                      const std::vector<std::string> &operations,
                      const std::string &service_path,
                      const std::string &schema_path,
                      const std::string &object_path);

// Revokes the operations from the privilege on the given paths; the
// privilege is deleted when nothing remains. Returns false when the role
// has no privilege on the paths.
bool delete_role_privilege(Db_session *session, const Id &role_id,
                           const std::vector<std::string> &operations,
                           const std::string &service_path,
                           const std::string &schema_path,
                           const std::string &object_path);

// The GRANT REST ... ON ... TO role statement of a privilege.
std::string privilege_grant_statement(const Privilege &privilege,
                                      const Role &role);

// -- Auth apps ------------------------------------------------------------

struct Auth_app {
  Id id;
  Id auth_vendor_id;
  std::string auth_vendor;  // the vendor's name
  std::string name;
  std::optional<std::string> description;
  std::optional<std::string> url;
  std::optional<std::string> url_direct_auth;
  std::optional<std::string> access_token;
  std::optional<std::string> app_id;
  bool enabled = true;
  bool limit_to_registered_users = true;
  std::optional<Id> default_role_id;
  std::optional<std::string> options;
};

std::optional<Auth_app> get_auth_app(Db_session *session, const Id &id);

// The auth app with the given name (case-insensitive).
std::optional<Auth_app> find_auth_app(Db_session *session, std::string_view name);

// The auth apps linked to a service, or all of them; ordered by name.
std::vector<Auth_app> get_auth_apps(Db_session *session,
                                    const std::optional<Id> &service_id);

struct Auth_app_definition {
  Id auth_vendor_id;
  std::string name;
  std::optional<std::string> description;
  std::optional<std::string> url;
  std::optional<std::string> url_direct_auth;
  std::optional<std::string> access_token;
  std::optional<std::string> app_id;
  bool enabled = true;
  bool limit_to_registered_users = true;
  std::optional<Id> default_role_id;
  std::optional<std::string> options;
};

Id add_auth_app(Db_session *session, const Auth_app_definition &definition);

// The changes ALTER REST AUTH APP makes; an unset field is left alone.
struct Auth_app_changes {
  std::optional<std::string> name;
  std::optional<std::string> description;
  std::optional<bool> enabled;
  std::optional<bool> limit_to_registered_users;
  std::optional<Id> default_role_id;
  std::optional<std::string> url;
  std::optional<std::string> access_token;
  std::optional<std::string> app_id;
};

void update_auth_app(Db_session *session, const Id &id,
                     const Auth_app_changes &changes);

// Deletes an auth app with its service links; its users go with it.
void delete_auth_app(Db_session *session, const Id &id);

// Links an auth app to a service / removes the link.
void link_auth_app(Db_session *session, const Id &auth_app_id,
                   const Id &service_id);
void unlink_auth_app(Db_session *session, const Id &auth_app_id,
                     const Id &service_id);

// The CREATE OR REPLACE REST AUTH APP statement, optionally followed by
// the statements of its users and their role grants.
std::string auth_app_create_statement(Db_session *session,
                                      const Auth_app &auth_app,
                                      bool include_users);

// -- Users ----------------------------------------------------------------

struct User {
  Id id;
  Id auth_app_id;
  std::string auth_app_name;
  std::string name;
  std::optional<std::string> email;
  std::optional<std::string> vendor_user_id;
  std::optional<std::string> mapped_user_id;
  bool login_permitted = true;
  std::optional<std::string> app_options;  // JSON text
  std::optional<std::string> options;      // JSON text
  bool has_auth_string = false;  // the hash itself is never read back
};

// The user with the given name of an auth app.
std::optional<User> find_user(Db_session *session, const Id &auth_app_id,
                              std::string_view name);

// The users of an auth app.
std::vector<User> get_users(Db_session *session, const Id &auth_app_id);

// The users of the auth apps linked to a service and/or of one auth app,
// or all users; ordered by auth app and user name.
std::vector<User> get_users(Db_session *session,
                            const std::optional<Id> &service_id,
                            const std::optional<Id> &auth_app_id);

struct User_definition {
  Id auth_app_id;
  std::string name;
  std::optional<std::string> email;
  std::optional<std::string> vendor_user_id;
  std::optional<std::string> mapped_user_id;
  bool login_permitted = true;
  std::optional<std::string> options;      // JSON
  std::optional<std::string> app_options;  // JSON
  std::optional<std::string> password;
};

// Inserts a user. An MRS auth app requires a password, which is checked
// for strength and stored hashed; the other auth apps reject one.
Id add_user(Db_session *session, const User_definition &definition);

struct User_changes {
  std::optional<std::string> email;
  std::optional<std::string> vendor_user_id;
  std::optional<std::string> mapped_user_id;
  std::optional<bool> login_permitted;
  std::optional<std::string> options;
  bool merge_options = false;
  std::optional<std::string> app_options;
  std::optional<std::string> password;
};

void update_user(Db_session *session, const User &user,
                 const User_changes &changes);

void delete_user(Db_session *session, const Id &id);

// The auth_string MRS stores for a password: a salted PBKDF2-HMAC-SHA256
// derived key in the "$A$005$<salt>$<key>" form the router verifies.
std::string hash_password(std::string_view password);

// A role granted to a user.
struct User_role {
  Role role;
  std::optional<std::string> comments;
  std::optional<std::string> options;  // JSON text of the grant
};

std::vector<User_role> get_user_roles(Db_session *session, const Id &user_id);

void add_user_role(Db_session *session, const Id &user_id, const Id &role_id,
                   const std::optional<std::string> &comments);
void delete_user_role(Db_session *session, const Id &user_id,
                      const Id &role_id);

// The GRANT REST ROLE ... TO user@app statement of a granted role.
std::string user_role_grant_statement(const User &user,
                                      const User_role &user_role);

// The CREATE OR REPLACE REST USER statement of a user, optionally followed
// by the GRANT REST ROLE statements of its roles. The password is never
// shown; a stored one appears as '[Stored Password]'.
std::string user_create_statement(Db_session *session, const User &user,
                                  bool include_grants);

}  // namespace metadata
}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_METADATA_AUTH_H_
