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

#ifndef MODULES_MRS_CORE_MRS_METADATA_JSON_H_
#define MODULES_MRS_CORE_MRS_METADATA_JSON_H_

// The JSON documents of the REST objects that SHOW CREATE ... FORMAT=JSON
// and SHOW REST COLUMNS ... FORMAT=JSON return. Keys are the metadata
// column names; ids are UUID strings; option columns are embedded as JSON.
// Secrets (auth app secrets, password hashes) and file contents are never
// included.

#include <optional>
#include <string_view>
#include <vector>

#include "modules/mrs/core/mrs_db_session.h"
#include "modules/mrs/core/mrs_json.h"
#include "modules/mrs/core/mrs_metadata.h"
#include "modules/mrs/core/mrs_metadata_auth.h"
#include "modules/mrs/core/mrs_metadata_content.h"
#include "modules/mrs/core/mrs_metadata_db_objects.h"

namespace mrs {
namespace metadata {

// A service with the names of its auth apps; with database endpoints, its
// schemas with their objects and data mappings under "schemas".
json::Value service_json(Db_session *session, const Service &service,
                         bool include_database_endpoints);

json::Value schema_json(const Schema &schema);

// A view, procedure or function with its data mapping: "objects", each
// with its "fields" in the flat metadata order; a field representing a
// reference carries it as "object_reference".
json::Value db_object_json(Db_session *session, const Db_object &db_object);

json::Value content_set_json(const Content_set &content_set);
json::Value content_file_json(const Content_file &content_file);

// An auth app with the paths of the services it is linked to. The app
// secret (access_token) is left out.
json::Value auth_app_json(Db_session *session, const Auth_app &auth_app);

// A user with the roles granted to it.
json::Value user_json(Db_session *session, const User &user);

// A role with its privileges.
json::Value role_json(Db_session *session, const Role &role);

// The columns and references of a table or view, as the
// table_columns_with_references procedure returns them.
json::Value table_columns_json(std::string_view schema_name,
                               std::string_view name, std::string_view type,
                               const std::vector<Table_column> &columns);

// The parameters of a procedure or function, and the return type of a
// function.
json::Value routine_json(std::string_view schema_name, std::string_view name,
                         std::string_view type,
                         const std::vector<Routine_parameter> &parameters,
                         const std::optional<std::string> &return_type);

}  // namespace metadata
}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_METADATA_JSON_H_
