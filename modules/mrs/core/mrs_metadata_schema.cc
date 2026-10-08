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

#include "modules/mrs/core/mrs_metadata_schema.h"

#include <stdexcept>

namespace mrs {

namespace embedded {
extern const unsigned char k_metadata_schema_script[];
extern const unsigned long k_metadata_schema_script_size;
}  // namespace embedded

namespace metadata {

std::string_view metadata_schema_script() {
  return std::string_view(
      reinterpret_cast<const char *>(embedded::k_metadata_schema_script),
      embedded::k_metadata_schema_script_size);
}

namespace {

bool view_exists(Db_session *session, std::string_view view) {
  const auto result = session->query(
      "SELECT COUNT(*) AS c FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_SCHEMA = " +
      sql::quote(k_metadata_schema) + " AND TABLE_NAME = " + sql::quote(view));
  return !result.empty() && result.first()["c"].as_int() > 0;
}

std::optional<Version> parse_version(const std::string &text) {
  Version v;
  if (std::sscanf(text.c_str(), "%d.%d.%d", &v.major, &v.minor, &v.patch) != 3) {
    return std::nullopt;
  }
  return v;
}

}  // namespace

Status get_status(Db_session *session) {
  Status status;
  if (!schema_exists(session)) return status;

  if (!view_exists(session, "msm_schema_version") &&
      !view_exists(session, "schema_version")) {
    status.service_being_upgraded = true;
    return status;
  }

  const auto version = schema_version(session);
  status.service_configured = true;
  status.service_upgradeable = version < k_schema_version;
  status.service_being_upgraded =
      version.major == 0 && version.minor == 0 && version.patch == 0;
  status.major_upgrade_required = version.major == 1;
  status.current_metadata_version = version.str();
  status.available_metadata_version = k_schema_version.str();
  status.required_router_version = k_required_router_version.str();

  if (status.service_being_upgraded) return status;

  const auto config = session->query(
      "SELECT service_enabled, JSON_VALUE(data, '$.ignore_service_upgrades_till') "
      "AS ignore_till FROM " +
      sql::metadata_table("config") + " WHERE id = 1");
  if (!config.empty()) {
    const auto &row = config.first();
    status.service_enabled = row["service_enabled"].as_int() == 1;
    if (!row["ignore_till"].is_null()) {
      if (const auto ignored = parse_version(row["ignore_till"].as_string())) {
        status.service_upgrade_ignored = !(*ignored < version);
      }
    }
  }

  const auto count = session->query("SELECT SUM(enabled) AS service_count FROM " +
                                    sql::metadata_table("service"));
  if (!count.empty() && !count.first()["service_count"].is_null()) {
    status.service_count = static_cast<int>(count.first()["service_count"].as_int());
  }
  return status;
}

Configure_result configure(Db_session *session,
                           const Configure_options &options) {
  Configure_result result;

  if (schema_exists(session)) {
    const auto current = schema_version(session);

    if (current.major > k_schema_version.major) {
      throw std::runtime_error(
          "This version of MariaDB Shell does not support the MRS metadata "
          "database schema version " +
          current.str() + ". Please update MariaDB Shell to work with this MRS version.");
    }

    if (current.major < k_supported_major_version && !options.update_if_available) {
      throw std::runtime_error(
          "The MRS metadata version " + current.str() +
          " is too old to be managed by this version of MariaDB Shell. Please "
          "update the MRS metadata version, e.g. run `CONFIGURE REST METADATA "
          "UPDATE IF AVAILABLE` to update.");
    }

    if (current < k_schema_version) {
      if (options.update_if_available) {
        throw std::runtime_error(
            "Updating the MRS metadata schema from version " + current.str() +
            " to " + k_schema_version.str() +
            " is not supported by this version of MariaDB Shell yet.");
      }
      result.info = "MRS metadata version update available, but update skipped.";
    } else {
      result.info = "No changes to the MRS metadata schema were needed.";
    }
  } else {
    session->execute_script(std::string(metadata_schema_script()));
    result.schema_changed = true;
    result.info = "The MRS metadata schema version " + k_schema_version.str() +
                  " was created.";
  }

  if (options.enabled) {
    session->execute(sql::Update("config")
                         .set("service_enabled", *options.enabled)
                         .where("id = 1")
                         .str());
    result.mrs_enabled = *options.enabled;
  } else {
    const auto row = session->query("SELECT service_enabled FROM " +
                                    sql::metadata_table("config") + " WHERE id = 1");
    result.mrs_enabled = !row.empty() && row.first()["service_enabled"].as_int() == 1;
  }

  if (options.options) {
    if (options.merge_options) {
      session->execute("UPDATE " + sql::metadata_table("config") +
                       " SET data = JSON_MERGE_PATCH(data, " +
                       sql::quote(*options.options) + ") WHERE id = 1");
    } else {
      session->execute(sql::Update("config")
                           .set("data", *options.options)
                           .where("id = 1")
                           .str());
    }
  }

  return result;
}

}  // namespace metadata
}  // namespace mrs
