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

// The REST AUTH APP, USER, ROLE, GRANT and REVOKE statements.

#include <stdexcept>

#include "modules/mrs/core/mrs_ddl_executor.h"
#include "modules/mrs/core/mrs_metadata_auth.h"

namespace mrs {

using namespace ast;

namespace {

// `user`@`app`, as the messages name a user.
std::string user_target(const std::string &user, const std::string &auth_app) {
  return "`" + user + "`@`" + auth_app + "`";
}

// :"user"@"app", the form the Python plugin used in the USER messages.
std::string user_full_path(const std::string &user, const std::string &auth_app) {
  return ":\"" + user + "\"@\"" + auth_app + "\"";
}

Db_value text_or_null(const std::optional<std::string> &text) {
  return text ? Db_value(*text) : Db_value(nullptr);
}

Db_value text_or_empty(const std::optional<std::string> &text) {
  return Db_value(text ? *text : std::string());
}

std::string privilege_name(Privilege privilege) {
  switch (privilege) {
    case Privilege::create:
      return "CREATE";
    case Privilege::read:
      return "READ";
    case Privilege::update:
      return "UPDATE";
    case Privilege::del:
      return "DELETE";
  }
  return "READ";
}

std::vector<std::string> privilege_names(const std::vector<Privilege> &privileges) {
  std::vector<std::string> names;
  for (const auto privilege : privileges) {
    names.push_back(privilege_name(privilege));
  }
  return names;
}

// A privilege path pattern: "*" when not given.
std::string pattern_of(const std::optional<std::string> &pattern) {
  return pattern ? *pattern : "*";
}

void check_pattern(const std::string &pattern, const char *what) {
  if (pattern == "*" || pattern.empty() || pattern[0] == '/') return;
  throw std::runtime_error(std::string(what) +
                           " must be \"\", \"*\" or start with a /");
}

// The columns a user's OPTIONS document fills besides the JSON itself.
struct User_option_columns {
  std::optional<std::string> email;
  std::optional<std::string> vendor_user_id;
  std::optional<std::string> mapped_user_id;
  std::optional<std::string> options;  // the remaining document
};

// Takes the email, vendor_user_id and mapped_user_id keys out of the
// OPTIONS document; they live in columns of their own.
User_option_columns split_user_options(const std::optional<Json> &options) {
  User_option_columns result;
  if (!options) return result;

  auto doc = json::parse(*options);
  if (!doc.is_object()) {
    result.options = doc.dump();
    return result;
  }
  const auto take = [&doc](std::string_view key) -> std::optional<std::string> {
    const auto value = doc.remove(key);
    if (!value || value->is_null()) return std::nullopt;
    return value->is_string() ? value->as_string() : value->dump();
  };
  result.email = take("email");
  result.vendor_user_id = take("vendor_user_id");
  result.mapped_user_id = take("mapped_user_id");
  result.options = doc.dump();
  return result;
}

}  // namespace

// -- REST AUTH APP --------------------------------------------------------

void Ddl_executor::do_execute(const Create_rest_auth_app &s, Statement_result *r) {
  set_failure_context("Failed to create the REST AUTH APP `" + s.name + "`.");

  Db_transaction transaction(m_session);

  if (s.flags.or_replace || s.flags.if_not_exists) {
    if (const auto existing = metadata::find_auth_app(m_session, s.name)) {
      if (s.flags.if_not_exists) {
        r->message = "REST AUTH APP `" + s.name + "` created successfully.";
        r->id = sql::hex(existing->id);
        transaction.commit();
        return;
      }
      metadata::delete_auth_app(m_session, existing->id);
    }
  }

  // Auth apps are not specific to a service, so only a global role can be
  // the default role; 'Full Access' is the default
  Id default_role_id = metadata::full_access_role_id();
  if (s.options.default_role) {
    const auto role =
        metadata::find_role(m_session, *s.options.default_role, std::nullopt);
    if (!role) {
      throw std::runtime_error("Given role \"" + *s.options.default_role +
                               "\" not found.");
    }
    default_role_id = role->id;
  }

  const auto vendor = metadata::find_auth_vendor(m_session, s.vendor);
  if (!vendor) {
    throw std::runtime_error("The vendor `" + s.vendor + "` was not found.");
  }
  if (vendor->is_oauth2()) {
    if (!s.options.url) {
      throw std::runtime_error("The OAuth2 vendor `" + s.vendor +
                               "` requires the URL option to be specified.");
    }
    if (!s.options.app_id) {
      throw std::runtime_error(
          "The OAuth2 vendor `" + s.vendor +
          "` requires the APP/CLIENT ID option to be specified.");
    }
    if (!s.options.app_secret) {
      throw std::runtime_error(
          "The OAuth2 vendor `" + s.vendor +
          "` requires the APP/CLIENT SECRET option to be specified.");
    }
  }

  metadata::Auth_app_definition definition;
  definition.auth_vendor_id = vendor->id;
  definition.name = s.name;
  definition.description = s.options.comments;
  definition.url = s.options.url;
  definition.access_token = s.options.app_secret;
  definition.app_id = s.options.app_id;
  definition.enabled = s.options.enabled.value_or(true);
  definition.limit_to_registered_users = !s.options.allow_new_users.value_or(false);
  definition.default_role_id = default_role_id;

  const Id id = metadata::add_auth_app(m_session, definition);
  transaction.commit();

  r->message = "REST AUTH APP `" + s.name + "` created successfully.";
  r->id = sql::hex(id);
}

void Ddl_executor::do_execute(const Alter_rest_auth_app &s, Statement_result *r) {
  set_failure_context("Failed to update the REST AUTH APP `" + s.name + "`.");

  Db_transaction transaction(m_session);

  const auto auth_app = metadata::find_auth_app(m_session, s.name);
  if (!auth_app) {
    throw std::runtime_error("The given REST AUTH APP `" + s.name +
                             "` could not be found.");
  }

  metadata::Auth_app_changes changes;
  changes.name = s.new_name;
  changes.description = s.options.comments;
  changes.enabled = s.options.enabled;
  if (s.options.allow_new_users) {
    changes.limit_to_registered_users = !*s.options.allow_new_users;
  }
  if (s.options.default_role) {
    const auto role =
        metadata::find_role(m_session, *s.options.default_role, std::nullopt);
    if (!role) {
      throw std::runtime_error("Given role \"" + *s.options.default_role +
                               "\" not found.");
    }
    changes.default_role_id = role->id;
  }
  changes.url = s.options.url;
  changes.access_token = s.options.app_secret;
  changes.app_id = s.options.app_id;

  metadata::update_auth_app(m_session, auth_app->id, changes);
  transaction.commit();

  r->message = "REST AUTH APP `" + s.name + "` updated successfully.";
  r->affected_items_count = 1;
  r->id = sql::hex(auth_app->id);
}

void Ddl_executor::do_execute(const Drop_rest_auth_app &s, Statement_result *r) {
  set_failure_context("Failed to drop the REST AUTH APP `" + s.name + "`.");

  Db_transaction transaction(m_session);

  const auto auth_app = metadata::find_auth_app(m_session, s.name);
  if (!auth_app && !s.if_exists) {
    throw std::runtime_error("The given REST AUTH APP `" + s.name +
                             "` could not be found.");
  }
  if (auth_app) {
    metadata::delete_auth_app(m_session, auth_app->id);
    r->id = sql::hex(auth_app->id);
  }

  transaction.commit();
  r->message = "REST AUTH APP `" + s.name + "` dropped successfully.";
}

void Ddl_executor::do_execute(const Show_rest_auth_apps &s, Statement_result *r) {
  set_failure_context("Cannot SHOW the REST auth apps.");

  // Without a service, all auth apps are listed
  std::optional<Id> service_id;
  if (const auto service = resolve_service(s.service)) service_id = service->id;

  r->columns = {"REST AUTH APP name", "vendor", "comments", "enabled"};
  for (const auto &auth_app : metadata::get_auth_apps(m_session, service_id)) {
    auto &row = r->add_row();
    row.emplace_back(auth_app.name);
    row.emplace_back(auth_app.auth_vendor);
    row.push_back(text_or_null(auth_app.description));
    row.emplace_back(metadata::enabled_caption(auth_app.enabled ? 1 : 0));
  }
}

void Ddl_executor::do_execute(const Show_create_rest_auth_app &s,
                              Statement_result *r) {
  set_failure_context("Failed to get the REST AUTH APP `" + s.name + "`.");

  const auto auth_app = metadata::find_auth_app(m_session, s.name);
  if (!auth_app) {
    throw std::runtime_error("The given REST AUTH APP `" + s.name +
                             "` could not be found.");
  }

  r->columns = {"CREATE REST AUTH APP"};
  r->add_row().emplace_back(
      metadata::auth_app_create_statement(m_session, *auth_app, false));
  r->id = sql::hex(auth_app->id);
}

// -- REST USER ------------------------------------------------------------

void Ddl_executor::do_execute(const Create_rest_user &s, Statement_result *r) {
  const auto full_path = user_full_path(s.name, s.auth_app);
  set_failure_context("Failed to create the REST USER `" + full_path + "`.");

  Db_transaction transaction(m_session);

  if (s.password && s.password->empty()) {
    throw std::runtime_error("The password must not be empty.");
  }

  const auto auth_app = metadata::find_auth_app(m_session, s.auth_app);
  if (!auth_app) {
    throw std::runtime_error("The given REST AUTH APP for " + full_path +
                             " was not found.");
  }

  if (s.flags.or_replace || s.flags.if_not_exists) {
    if (const auto existing = metadata::find_user(m_session, auth_app->id, s.name)) {
      if (s.flags.if_not_exists) {
        r->message = "REST USER `" + full_path + "` created successfully.";
        r->id = sql::hex(existing->id);
        transaction.commit();
        return;
      }
      metadata::delete_user(m_session, existing->id);
    }
  }

  const auto option_columns = split_user_options(
      s.options.options ? std::optional<Json>(s.options.options->value)
                        : std::nullopt);

  metadata::User_definition definition;
  definition.auth_app_id = auth_app->id;
  definition.name = s.name;
  definition.email = option_columns.email;
  definition.vendor_user_id = option_columns.vendor_user_id;
  definition.mapped_user_id = option_columns.mapped_user_id;
  definition.login_permitted = !s.options.account_locked.value_or(false);
  definition.options = option_columns.options;
  definition.app_options = s.options.app_options;
  definition.password = s.password;

  const Id id = metadata::add_user(m_session, definition);
  transaction.commit();

  r->message = "REST USER `" + full_path + "` created successfully.";
  r->id = sql::hex(id);
}

void Ddl_executor::do_execute(const Alter_rest_user &s, Statement_result *r) {
  const auto full_path = user_full_path(s.name, s.auth_app);
  set_failure_context("Failed to update the REST USER `" + full_path + "`.");

  Db_transaction transaction(m_session);

  if (s.password && s.password->empty()) {
    throw std::runtime_error("The password must not be empty.");
  }

  const auto auth_app = metadata::find_auth_app(m_session, s.auth_app);
  if (!auth_app) {
    throw std::runtime_error("The given REST AUTH APP for " + full_path +
                             " was not found.");
  }
  const auto user = metadata::find_user(m_session, auth_app->id, s.name);
  if (!user) {
    throw std::runtime_error("Invalid REST user \"" + s.name + "\"@\"" +
                             s.auth_app + "\"");
  }

  const auto option_columns = split_user_options(
      s.options.options ? std::optional<Json>(s.options.options->value)
                        : std::nullopt);

  metadata::User_changes changes;
  if (s.options.account_locked) {
    changes.login_permitted = !*s.options.account_locked;
  }
  changes.email = option_columns.email;
  changes.vendor_user_id = option_columns.vendor_user_id;
  changes.mapped_user_id = option_columns.mapped_user_id;
  changes.password = s.password;
  changes.app_options = s.options.app_options;
  // An OPTIONS document that held nothing but the column keys leaves the
  // stored options alone
  if (option_columns.options && *option_columns.options != "{}") {
    changes.options = option_columns.options;
    changes.merge_options = s.options.options->merge;
  }

  metadata::update_user(m_session, *user, changes);
  transaction.commit();

  r->message = "REST USER `" + full_path + "` updated successfully.";
  r->affected_items_count = 1;
  r->id = sql::hex(user->id);
}

void Ddl_executor::do_execute(const Drop_rest_user &s, Statement_result *r) {
  const auto full_path = user_full_path(s.name, s.auth_app);
  set_failure_context("Failed to drop the REST USER `" + full_path + "`.");

  Db_transaction transaction(m_session);

  const auto auth_app = metadata::find_auth_app(m_session, s.auth_app);
  if (!auth_app && !s.if_exists) {
    throw std::runtime_error("The given REST AUTH APP for " + full_path +
                             " was not found.");
  }
  if (auth_app) {
    const auto user = metadata::find_user(m_session, auth_app->id, s.name);
    if (!user && !s.if_exists) throw std::runtime_error("User was not found.");
    if (user) {
      metadata::delete_user(m_session, user->id);
      r->id = sql::hex(user->id);
    }
  }

  transaction.commit();
  r->message = "REST USER `" + full_path + "` dropped successfully.";
}

void Ddl_executor::do_execute(const Show_create_rest_user &s,
                              Statement_result *r) {
  const auto full_name = user_target(s.name, s.auth_app);
  set_failure_context("Failed to get the REST USER `" + full_name + "`.");

  std::optional<metadata::User> user;
  if (const auto auth_app = metadata::find_auth_app(m_session, s.auth_app)) {
    user = metadata::find_user(m_session, auth_app->id, s.name);
  }
  if (!user) {
    throw std::runtime_error("User `" + full_name + "` was not found.");
  }

  r->columns = {"CREATE REST USER"};
  r->add_row().emplace_back(metadata::user_create_statement(m_session, *user, false));
  r->id = sql::hex(user->id);
}

// -- REST ROLE ------------------------------------------------------------

void Ddl_executor::do_execute(const Create_rest_role &s, Statement_result *r) {
  set_failure_context("Failed to create the REST ROLE `" + s.name + "`.");

  const auto service_id = resolve_role_service(s.on);

  Db_transaction transaction(m_session);

  std::optional<Id> parent_role_id;
  if (s.extends) {
    const auto parent = metadata::find_role(m_session, *s.extends, service_id);
    if (!parent) {
      throw std::runtime_error("Invalid parent role '" + *s.extends + "'");
    }
    parent_role_id = parent->id;
  }

  if (s.flags.or_replace || s.flags.if_not_exists) {
    if (const auto existing = metadata::find_role(m_session, s.name, service_id)) {
      if (s.flags.if_not_exists) {
        r->message = "REST ROLE `" + s.name + "` created successfully.";
        r->id = sql::hex(existing->id);
        transaction.commit();
        return;
      }
      metadata::delete_role(m_session, existing->id);
    }
  }

  metadata::Role_definition definition;
  definition.derived_from_role_id = parent_role_id;
  definition.specific_to_service_id = service_id;
  definition.caption = s.name;
  definition.description = s.options.comments;
  if (s.options.options) definition.options = s.options.options->value;

  const Id id = metadata::add_role(m_session, definition);
  transaction.commit();

  r->message = "REST ROLE `" + s.name + "` created successfully.";
  r->id = sql::hex(id);
}

void Ddl_executor::do_execute(const Drop_rest_role &s, Statement_result *r) {
  set_failure_context("Failed to drop the REST ROLE `" + s.name + "`.");

  const auto service_id = resolve_role_service(s.on);

  Db_transaction transaction(m_session);

  const auto role = metadata::find_role(m_session, s.name, service_id);
  if (!role && !s.if_exists) {
    throw std::runtime_error("Role `" + s.name + "` was not found.");
  }
  if (role) {
    metadata::delete_role(m_session, role->id);
    r->id = sql::hex(role->id);
  }

  transaction.commit();
  r->message = "REST ROLE `" + s.name + "` dropped successfully.";
}

void Ddl_executor::do_execute(const Show_rest_roles &s, Statement_result *r) {
  set_failure_context("Cannot SHOW REST ROLES.");

  // Without a service (given or current) the roles of any service are shown
  bool any_service = s.on && s.on->any_service;
  std::optional<Id> service_id;
  if (!any_service) {
    if (const auto service = resolve_service(s.on ? s.on->service : std::nullopt)) {
      service_id = service->id;
    } else {
      any_service = true;
    }
  }

  const bool for_user = s.user && s.auth_app;
  std::string first_column = "REST role";
  if (for_user) {
    first_column = "REST roles for " + sql::quote_identifier(*s.user) + "@" +
                   sql::quote_identifier(*s.auth_app);
  } else if (s.auth_app) {
    first_column = "REST roles for @" + *s.auth_app;
  } else if (s.user) {
    first_column = "REST roles for " + *s.user;
  }
  r->columns = {first_column, "derived_from_role", "description", "options"};

  const auto add_role_row = [r](const metadata::Role &role) -> std::vector<Db_value> & {
    auto &row = r->add_row();
    row.emplace_back(role.caption);
    row.push_back(text_or_empty(role.derived_from_role_caption));
    row.push_back(text_or_empty(role.description));
    row.push_back(text_or_null(role.options));
    return row;
  };

  if (for_user && !any_service) {
    // The roles granted to one user, with the comments of the grants
    std::optional<metadata::User> user;
    if (const auto auth_app = metadata::find_auth_app(m_session, *s.auth_app)) {
      user = metadata::find_user(m_session, auth_app->id, *s.user);
    }
    if (!user) {
      throw std::runtime_error("User " + sql::quote_identifier(*s.user) + "@" +
                               sql::quote_identifier(*s.auth_app) + " not found");
    }
    r->add_column("comments");
    for (const auto &user_role : metadata::get_user_roles(m_session, user->id)) {
      auto &row = add_role_row(user_role.role);
      row.push_back(text_or_empty(user_role.comments));
    }
    return;
  }

  if (s.user || s.auth_app) {
    // The granted roles matching the user and/or auth app, with the users
    // holding them
    r->add_column("specific_to_service");
    if (for_user) r->add_column("comments");
    r->add_column("users");
    for (const auto &granted : metadata::get_granted_roles(
             m_session, service_id, s.user, s.auth_app, true)) {
      auto &row = add_role_row(granted.role);
      row.emplace_back(granted.role.specific_to_service_request_path);
      if (for_user) row.emplace_back("");
      row.push_back(text_or_empty(granted.users));
    }
    return;
  }

  r->add_column("specific_to_service");
  for (const auto &role : metadata::get_roles(m_session, service_id, true)) {
    auto &row = add_role_row(role);
    row.emplace_back(role.specific_to_service_request_path);
  }
}

void Ddl_executor::do_execute(const Show_create_rest_role &s,
                              Statement_result *r) {
  set_failure_context("Failed to get the REST ROLE `" + s.name + "`.");

  const auto service_id = resolve_role_service(s.on);
  const auto role = metadata::find_role(m_session, s.name, service_id);
  if (!role) throw std::runtime_error("Role `" + s.name + "` was not found.");

  r->columns = {"CREATE REST ROLE"};
  r->add_row().emplace_back(metadata::role_create_statement(m_session, *role));
  r->id = sql::hex(role->id);
}

// -- GRANT / REVOKE -------------------------------------------------------

void Ddl_executor::do_execute(const Rest_privilege_statement &s,
                              Statement_result *r) {
  set_failure_context(s.revoke
                          ? "Failed to revoke privileges for REST role `" +
                                s.role + "`."
                          : "Failed to grant privileges for REST role `" +
                                s.role + "`.");

  const auto service_id = resolve_role_service(s.role_service);

  const auto service_path = pattern_of(s.service_pattern);
  const auto schema_path = pattern_of(s.schema_pattern);
  const auto object_path = pattern_of(s.object_pattern);
  if (!s.revoke) {
    check_pattern(service_path, "service_path");
    check_pattern(schema_path, "schema_path");
    check_pattern(object_path, "object_path");
  }

  Db_transaction transaction(m_session);

  const auto role = metadata::find_role(m_session, s.role, service_id);
  if (!role) throw std::runtime_error("Role `" + s.role + "` was not found.");

  const auto operations = privilege_names(s.privileges);
  if (s.revoke) {
    if (!metadata::delete_role_privilege(m_session, role->id, operations,
                                         service_path, schema_path,
                                         object_path)) {
      throw std::runtime_error("There is no such grant for role " + s.role);
    }
    r->message = "REVOKE from `" + s.role + "` executed successfully.";
  } else {
    const Id id = metadata::add_role_privilege(
        m_session, role->id, operations, service_path, schema_path, object_path);
    r->message = "GRANT to `" + s.role + "` added successfully.";
    r->id = sql::hex(id);
  }

  transaction.commit();
}

void Ddl_executor::do_execute(const Rest_role_statement &s, Statement_result *r) {
  const auto target = user_target(s.user, s.auth_app);
  set_failure_context(s.revoke ? "Failed to REVOKE REST role `" + s.role +
                                     "` from " + target + "."
                               : "Failed to grant REST role `" + s.role +
                                     "` to " + target + ".");

  const auto service_id = resolve_role_service(s.role_service);

  Db_transaction transaction(m_session);

  const auto role = metadata::find_role(m_session, s.role, service_id);
  if (!role) throw std::runtime_error("Role `" + s.role + "` was not found.");

  std::optional<metadata::User> user;
  if (const auto auth_app = metadata::find_auth_app(m_session, s.auth_app)) {
    user = metadata::find_user(m_session, auth_app->id, s.user);
  }
  if (!user) {
    if (s.revoke) {
      throw std::runtime_error("The given user `" + s.user + "` was not found.");
    }
    throw std::runtime_error("User \"" + s.user + "\"@\"" + s.auth_app +
                             "\" was not found.");
  }

  if (s.revoke) {
    metadata::delete_user_role(m_session, user->id, role->id);
    r->message = "REVOKE ROLE from " + target + " executed successfully.";
  } else {
    metadata::add_user_role(m_session, user->id, role->id, s.comments);
    r->message = "GRANT ROLE to " + target + " added successfully.";
  }

  transaction.commit();
}

void Ddl_executor::do_execute(const Show_rest_grants &s, Statement_result *r) {
  set_failure_context("Cannot SHOW REST GRANTs.");

  const auto service_id = resolve_role_service(s.on);
  const auto role = metadata::find_role(m_session, s.role, service_id);
  if (!role) throw std::runtime_error("No such role " + s.role);

  r->columns = {"REST grants for " + role->caption};
  for (const auto &privilege : metadata::get_role_privileges(m_session, role->id)) {
    r->add_row().emplace_back(
        metadata::privilege_grant_statement(privilege, *role));
  }
  r->id = sql::hex(role->id);
}

}  // namespace mrs
