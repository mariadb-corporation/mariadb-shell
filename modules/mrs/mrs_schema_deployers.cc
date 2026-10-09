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

#include "modules/mrs/mrs_schema_deployers.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <stdexcept>
#include <utility>

#include "modules/mrs/core/mrs_sql.h"
#include "modules/util/dump/dump_schemas.h"
#include "modules/util/dump/dump_schemas_options.h"
#include "modules/util/load/dump_loader.h"
#include "modules/util/load/load_dump_options.h"
#include "mysqlshdk/libs/utils/utils_file.h"
#include "mysqlshdk/libs/utils/utils_general.h"
#include "mysqlshdk/libs/utils/utils_path.h"

namespace mysqlsh {
namespace mrs {

namespace {

using ::mrs::Db_session;
using ::mrs::metadata::Version;

// The msm plugin's data folder; its update log and backups live there, so
// both ways of deploying leave the same trail.
std::string msm_plugin_data_path() {
  const auto path = shcore::path::join_path(shcore::get_user_config_path(),
                                            "plugin_data", "msm_plugin");
  shcore::create_directory(path, true);
  return path;
}

// "2026-10-08 16:07:01.123456", the format of Python's datetime.now().
std::string log_timestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::system_clock::to_time_t(now);
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                          now.time_since_epoch())
                          .count() %
                      1000000;
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &seconds);
#else
  localtime_r(&seconds, &local);
#endif
  char text[40];
  std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local);
  char fraction[16];
  std::snprintf(fraction, sizeof(fraction), ".%06lld",
                static_cast<long long>(micros));
  return std::string(text) + fraction;
}

// msm's msm_schema_update_log.txt
class Msm_log_file : public ::mrs::metadata::Deployment_log {
 public:
  void write(std::string_view type, std::string_view message) override {
    try {
      std::ofstream file(
          shcore::path::join_path(msm_plugin_data_path(),
                                  "msm_schema_update_log.txt"),
          std::ios::app);
      file << log_timestamp() << " - " << type << " - " << message << "\n";
    } catch (...) {
      // The log must not make the deployment fail
    }
  }
};

shcore::Object_bridge_ref global_object(shcore::IShell_core *shell_core,
                                        const std::string &name) {
  if (!shell_core->is_global(name)) return {};
  const auto value = shell_core->get_global(name);
  if (value.get_type() != shcore::Object) return {};
  return value.as_object();
}

// The schema dumped into the msm plugin's backup folder and loaded back,
// as msm's deploy_schema() does with util.dumpSchemas() and util.loadDump().
// The dumper and loader run on the session being deployed to, which need
// not be the global one.
class Dump_schema_backup : public ::mrs::metadata::Schema_backup {
 public:
  Dump_schema_backup(std::shared_ptr<mysqlshdk::db::ISession> session,
                     ::mrs::metadata::Deployment_log *log)
      : m_target(std::move(session)), m_log(log) {}

  void create(Db_session *session, std::string_view schema_name,
              const Version &version) override {
    // A new folder per backup: <file name>_backup_<version>[_<n>]
    const auto base = shcore::path::join_path(
        msm_plugin_data_path(), "backups",
        std::string(::mrs::metadata::k_schema_file_name) + "_backup_" +
            version.str());
    m_directory = base;
    for (int i = 2; shcore::path::exists(m_directory); ++i) {
      m_directory = base + "_" + std::to_string(i);
    }
    shcore::create_directory(m_directory, true);

    // The dump is loaded back with LOAD DATA LOCAL INFILE
    m_session = session;
    const auto row = session->query("SELECT @@local_infile AS local_infile");
    m_original_local_infile =
        !row.empty() && row.first()["local_infile"].as_int() == 1;
    if (!m_original_local_infile) {
      try {
        m_log->write("INFO",
                     "Enabling local_infile option in order to be able to "
                     "load back the schema dump in case of an update error...");
        session->execute("SET GLOBAL local_infile=1");
      } catch (const std::exception &) {
        throw std::runtime_error(
            "Failed to enable the local_infile option. Please execute SET "
            "PERSIST GLOBAL local_infile=1; on the MariaDB Server.");
      }
    }

    m_log->write("INFO", "Creating dump of `" + std::string(schema_name) +
                             "` version " + version.str() + " ...");
    dump::Dump_schemas_options options;
    dump::Dump_schemas_options::options().unpack(
        shcore::make_dict("skipUpgradeChecks", true, "showProgress", false),
        &options);
    options.set_schemas({std::string(schema_name)});
    options.set_url(m_directory);
    options.set_session(m_target);
    options.validate_and_configure();
    dump::Dump_schemas(options).run();
  }

  void restore(Db_session *) override {
    Load_dump_options options;
    Load_dump_options::options().unpack(
        shcore::make_dict("showMetadata", false, "showProgress", false,
                          "ignoreVersion", true),
        &options);
    options.set_url(m_directory);
    options.set_session(m_target);
    options.validate_and_configure();
    Dump_loader(options).run();
  }

  void discard() override {
    if (!m_original_local_infile && m_session) {
      try {
        m_session->execute("SET GLOBAL local_infile=0");
        m_log->write("INFO", "Restored local_infile option.");
      } catch (...) {
        // Leaving it enabled is no reason to fail the deployment
      }
      m_original_local_infile = true;
    }
    if (!m_directory.empty() && shcore::path::exists(m_directory)) {
      shcore::remove_directory(m_directory, true);
    }
  }

 private:
  std::shared_ptr<mysqlshdk::db::ISession> m_target;
  ::mrs::metadata::Deployment_log *m_log;
  Db_session *m_session = nullptr;
  std::string m_directory;
  bool m_original_local_infile = true;
};

// The core's replica of deploy_schema(), with msm's log and backup.
class Shell_script_deployer : public ::mrs::metadata::Schema_deployer {
 public:
  std::vector<Version> available_versions() const override {
    return ::mrs::metadata::released_versions(
        shcore::path::join_path(msm_project_path(), "releases", "versions"));
  }

  explicit Shell_script_deployer(
      std::shared_ptr<mysqlshdk::db::ISession> session)
      : m_backup(std::move(session), &m_log) {}

  std::string deploy(Db_session *session, bool backup) override {
    // The share folder is looked up only when deploying
    ::mrs::metadata::Script_deployer deployer(
        shcore::path::join_path(msm_project_path(), "releases", "deployment"),
        &m_log, &m_backup);
    return deployer.deploy(session, backup);
  }

 private:
  Msm_log_file m_log;
  Dump_schema_backup m_backup;
};

// msm.deploySchema() of the loaded msm plugin, on the bundled project.
class Msm_plugin_deployer : public ::mrs::metadata::Schema_deployer {
 public:
  std::vector<Version> available_versions() const override {
    return ::mrs::metadata::released_versions(
        shcore::path::join_path(msm_project_path(), "releases", "versions"));
  }

  Msm_plugin_deployer(shcore::Object_bridge_ref msm,
                      std::shared_ptr<ShellBaseSession> session)
      : m_msm(std::move(msm)), m_session(std::move(session)) {}

  std::string deploy(Db_session *, bool backup) override {
    shcore::Argument_list args;
    args.emplace_back(shcore::make_dict(
        "schema_project_path", msm_project_path(), "backup", backup, "session",
        shcore::Value(std::static_pointer_cast<shcore::Object_bridge>(
            m_session))));
    try {
      const auto result = m_msm->call("deploySchema", args);
      return result.get_type() == shcore::String ? result.get_string()
                                                 : result.descr();
    } catch (const shcore::Exception &e) {
      throw std::runtime_error(e.what());
    }
  }

 private:
  shcore::Object_bridge_ref m_msm;
  std::shared_ptr<ShellBaseSession> m_session;
};

}  // namespace

std::string msm_project_path() {
  return shcore::path::join_path(
      shcore::get_share_folder(), "mrs",
      std::string(::mrs::metadata::k_msm_project_name));
}

std::unique_ptr<::mrs::metadata::Schema_deployer> make_schema_deployer(
    shcore::IShell_core *shell_core,
    const std::shared_ptr<ShellBaseSession> &session) {
  if (auto msm = global_object(shell_core, "msm")) {
    return std::make_unique<Msm_plugin_deployer>(std::move(msm), session);
  }
  return std::make_unique<Shell_script_deployer>(session->get_core_session());
}

}  // namespace mrs
}  // namespace mysqlsh
