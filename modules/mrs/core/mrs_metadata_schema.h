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

#ifndef MODULES_MRS_CORE_MRS_METADATA_SCHEMA_H_
#define MODULES_MRS_CORE_MRS_METADATA_SCHEMA_H_

// Deployment and status of the MRS metadata schema itself:
// CONFIGURE REST METADATA and SHOW REST METADATA STATUS.

#include <cstdint>
#include <optional>
#include <string>

#include "modules/mrs/core/mrs_db_session.h"
#include "modules/mrs/core/mrs_metadata.h"
#include "modules/mrs/core/mrs_schema_deployment.h"

namespace mrs {
namespace metadata {

// The version of the MariaDB REST Service router this module expects.
inline constexpr Version k_required_router_version{8, 1, 0};

struct Status {
  // The metadata schema the status is of (the session's)
  std::string metadata_schema;
  bool service_configured = false;
  bool service_enabled = false;
  int service_count = 0;
  bool service_upgradeable = false;
  bool service_upgrade_ignored = false;
  bool major_upgrade_required = false;
  bool service_being_upgraded = false;
  std::optional<std::string> current_metadata_version;
  std::optional<std::string> available_metadata_version;
  std::optional<std::string> required_router_version;
  // The id of the last audit log entry (0 without any): it changes whenever
  // the REST metadata changes, so clients can poll it to refresh.
  std::optional<int64_t> metadata_version;
  // The data document of the config table: the options CONFIGURE REST
  // METADATA OPTIONS sets (JSON text).
  std::optional<std::string> configuration_options;
};

Status get_status(Db_session *session);

struct Configure_options {
  std::optional<bool> enabled;
  std::optional<std::string> options;  // JSON
  bool merge_options = false;
  bool update_if_available = false;
};

struct Configure_result {
  bool schema_changed = false;
  std::string info;
  bool mrs_enabled = false;
};

// Deploys the metadata schema through the deployer (creating it, or
// updating an older version when update_if_available is set), then applies
// the options.
Configure_result configure(Db_session *session, const Configure_options &options,
                           Schema_deployer *deployer);

}  // namespace metadata
}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_METADATA_SCHEMA_H_
