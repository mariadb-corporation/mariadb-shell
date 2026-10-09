/*
 * Copyright (c) 2026, MariaDB plc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is designed to work with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms, as
 * designated in a particular file or component or in included license
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

#ifndef MODULES_MRS_CORE_MRS_SCHEMA_DEPLOYMENT_H_
#define MODULES_MRS_CORE_MRS_SCHEMA_DEPLOYMENT_H_

// Deployment of the MRS metadata schema, the way the MSM (MySQL Schema
// Management) plugin's deploy_schema() does it.
//
// The schema is an MSM project (modules/mrs/db_schema). Where the msm plugin
// is available, the host deploys through it (see Schema_deployer). Where it
// is not, e.g. in a server plugin, Script_deployer replicates deploy_schema:
// the same checks and messages, the MSM_METADATA_LOCK, the update log and
// the drop/restore rules on failure, running the project's deployment
// script, which creates the schema or updates an older version in place.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "modules/mrs/core/mrs_db_session.h"
#include "modules/mrs/core/mrs_metadata.h"

namespace mrs {
namespace metadata {

// The MSM project of the metadata schema.
inline constexpr std::string_view k_msm_project_name =
    "mysql_rest_service_metadata.msm.project";
// The project's schemaFileName, the prefix of its release scripts.
inline constexpr std::string_view k_schema_file_name =
    "mysql_rest_service_metadata";

// The file name of the deployment script of a version, e.g.
// mysql_rest_service_metadata_deployment_5.0.0.sql.
std::string deployment_script_name(const Version &version);

// The versions a deployment script can update from: the X of each
// msm_update_X_to_Y procedure it creates, sorted.
std::vector<Version> updatable_versions(std::string_view script);

// The versions of the release scripts in a folder (files ending in
// X.Y.Z.sql, as msm's get_released_versions() reads releases/versions),
// sorted. A missing folder has none.
std::vector<Version> released_versions(const std::string &dir);

// Deploys (creates or updates) the metadata schema to k_schema_version.
// Returns msm's information message; a message containing "No changes"
// means the schema was left as it was. Throws with msm's error messages.
class Schema_deployer {
 public:
  virtual ~Schema_deployer() = default;
  virtual std::string deploy(Db_session *session, bool backup) = 0;
  // The released versions of the metadata schema the deployer has scripts
  // for.
  virtual std::vector<Version> available_versions() const { return {}; }
};

// Where Script_deployer writes msm's update log, e.g. the
// msm_schema_update_log.txt of the msm plugin's data folder.
class Deployment_log {
 public:
  virtual ~Deployment_log() = default;
  // type is "INFO" or "ERROR"
  virtual void write(std::string_view type, std::string_view message) = 0;
};

// A copy of the schema taken before an update, loaded back when the update
// fails (msm dumps the schema with util.dumpSchemas()).
class Schema_backup {
 public:
  virtual ~Schema_backup() = default;
  // Takes the copy; throws when it cannot.
  virtual void create(Db_session *session, std::string_view schema_name,
                      const Version &version) = 0;
  // Loads the copy back after the schema was dropped; throws on failure.
  virtual void restore(Db_session *session) = 0;
  // Removes the copy once it is no longer needed.
  virtual void discard() = 0;
};

// msm's deploy_schema() on a folder of deployment scripts, e.g. the
// releases/deployment folder of the MSM project. The log and the backup
// are optional; without a backup an update that fails leaves the schema as
// the script left it, as msm does with backups disabled.
class Script_deployer : public Schema_deployer {
 public:
  explicit Script_deployer(std::string deployment_dir,
                           Deployment_log *log = nullptr,
                           Schema_backup *backup = nullptr);

  std::string deploy(Db_session *session, bool backup) override;
  std::vector<Version> available_versions() const override {
    return released_versions(m_deployment_dir);
  }

 private:
  void log(std::string_view type, std::string_view message);
  [[noreturn]] void fail(const std::string &message);

  std::string m_deployment_dir;
  Deployment_log *m_log;
  Schema_backup *m_backup;
};

}  // namespace metadata
}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_SCHEMA_DEPLOYMENT_H_
