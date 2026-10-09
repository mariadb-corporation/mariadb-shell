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

#include "modules/mrs/core/mrs_schema_deployment.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "modules/mrs/core/mrs_sql.h"

namespace mrs {
namespace metadata {

namespace {

constexpr std::string_view k_lock_name = "MSM_METADATA_LOCK";

std::optional<std::string> read_file(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::nullopt;
  std::stringstream content;
  content << file.rdbuf();
  return content.str();
}

// msm's get_schema_is_managed(): the schema has an msm_schema_version view.
bool schema_is_managed(Db_session *session) {
  const auto result = session->query(
      "SELECT COUNT(*) AS table_count FROM information_schema.TABLES "
      "WHERE TABLE_SCHEMA = ? AND TABLE_NAME = 'msm_schema_version' AND "
      "TABLE_TYPE = 'VIEW'",
      {session->metadata_schema()});
  return !result.empty() && result.first()["table_count"].as_int() == 1;
}

// The lock msm's execute_msm_sql_script() takes for the time of the script:
// one per schema, MSM_METADATA_LOCK_<MD5 of the schema name>, so schemas
// deployed from the same project (one per customer) can be updated at the
// same time.
constexpr std::string_view k_lock_expression = "CONCAT(?, '_', MD5(?))";

class Msm_lock {
 public:
  explicit Msm_lock(Db_session *session) : m_session(session) {
    const auto result = session->query(
        "SELECT GET_LOCK(" + std::string(k_lock_expression) + ", 1) AS msm_lock",
        {k_lock_name, session->metadata_schema()});
    m_locked = !result.empty() && result.first()["msm_lock"].as_int() == 1;
    if (!m_locked) {
      throw std::runtime_error(
          "Failed to acquire MSM schema update lock. Please ensure no other "
          "MSM schema update is running, then try again.");
    }
  }
  ~Msm_lock() {
    try {
      m_session->query(
          "SELECT RELEASE_LOCK(" + std::string(k_lock_expression) + ")",
          {k_lock_name, m_session->metadata_schema()});
    } catch (...) {
      // The lock goes with the connection anyway
    }
  }
  Msm_lock(const Msm_lock &) = delete;
  Msm_lock &operator=(const Msm_lock &) = delete;

 private:
  Db_session *m_session;
  bool m_locked = false;
};

}  // namespace

std::vector<Version> released_versions(const std::string &dir) {
  std::vector<Version> versions;
  std::error_code error;
  const std::regex pattern(R"(.*?(\d+)\.(\d+)\.(\d+)\.sql)");
  for (const auto &entry : std::filesystem::directory_iterator(dir, error)) {
    std::smatch match;
    const auto name = entry.path().filename().string();
    if (entry.is_regular_file(error) && std::regex_match(name, match, pattern)) {
      versions.push_back(Version{std::stoi(match[1]), std::stoi(match[2]),
                                 std::stoi(match[3])});
    }
  }
  std::sort(versions.begin(), versions.end());
  return versions;
}

std::string deployment_script_name(const Version &version) {
  return std::string(k_schema_file_name) + "_deployment_" + version.str() +
         ".sql";
}

std::string apply_schema_substitutions(std::string_view script,
                                       std::string_view schema_name) {
  check_metadata_schema_name(schema_name);
  const auto parts = *schema_name_parts(schema_name);

  static const std::regex k_placeholder(R"(/\*<msm:([A-Za-z_][A-Za-z0-9_]*)>\*/)");
  std::string result;
  result.reserve(script.size());
  const std::string text(script);
  size_t last = 0;
  for (auto it = std::sregex_iterator(text.begin(), text.end(), k_placeholder);
       it != std::sregex_iterator(); ++it) {
    const auto name = (*it)[1].str();
    result.append(text, last, it->position() - last);
    if (name == "schema_prefix") {
      result += parts.prefix;
    } else if (name == "schema_postfix") {
      result += parts.postfix;
    } else {
      throw std::runtime_error("The deployment script uses the unknown "
                               "substitution /*<msm:" + name + ">*/.");
    }
    last = it->position() + it->length();
  }
  result.append(text, last, std::string::npos);
  return result;
}

std::vector<Version> updatable_versions(std::string_view script) {
  static const std::regex k_update_procedure(
      R"(PROCEDURE `msm_update_(\d+)\.(\d+)\.(\d+)_to_\d+\.\d+\.\d+`\()");

  std::vector<Version> versions;
  const std::string text(script);
  for (auto it = std::sregex_iterator(text.begin(), text.end(),
                                      k_update_procedure);
       it != std::sregex_iterator(); ++it) {
    const Version v{std::stoi((*it)[1]), std::stoi((*it)[2]),
                    std::stoi((*it)[3])};
    if (std::find(versions.begin(), versions.end(), v) == versions.end()) {
      versions.push_back(v);
    }
  }
  std::sort(versions.begin(), versions.end());
  return versions;
}

Script_deployer::Script_deployer(std::string deployment_dir,
                                 Deployment_log *log, Schema_backup *backup)
    : m_deployment_dir(std::move(deployment_dir)),
      m_log(log),
      m_backup(backup) {}

void Script_deployer::log(std::string_view type, std::string_view message) {
  if (m_log) m_log->write(type, message);
}

void Script_deployer::fail(const std::string &message) {
  log("ERROR", message);
  throw std::runtime_error(message);
}

std::string Script_deployer::deploy(Db_session *session, bool backup) {
  const auto name = session->metadata_schema();
  const auto version = k_schema_version.str();

  // The deployment script of the version this module expects
  const auto script_path =
      std::filesystem::path(m_deployment_dir) /
      deployment_script_name(k_schema_version);
  const auto script = read_file(script_path);
  if (!script) {
    fail("Deployment or update of database schema `" + name +
         "` using version " + version +
         " requested but there is no deployment script available for this "
         "version.");
  }

  // The script for this schema name (its prefix and postfix filled in)
  std::string deploy_script;
  try {
    deploy_script = apply_schema_substitutions(*script, name);
  } catch (const std::exception &e) {
    fail(e.what());
  }

  // Whether the schema exists, is managed by MSM, and on which version
  const bool exists = schema_exists(session);
  std::optional<Version> current;
  if (exists) {
    if (!schema_is_managed(session)) {
      fail("Deployment or update of database schema `" + name +
           "` using version " + version +
           " requested but the schema is not managed by MSM.");
    }
    current = schema_version(session);
  }

  if (current && *current == k_schema_version) {
    const auto info = "Deployment or update of database schema `" + name +
                      "` using version " + version +
                      " requested but the schema is already on the requested "
                      "version. No changes performed.";
    log("INFO", info);
    return info;
  }

  if (current) {
    const auto updatable = updatable_versions(*script);
    if (!updatable.empty() && *current > updatable.back()) {
      const auto info = "The database schema `" + name +
                        "` is on a newer version " + current->str() +
                        " than shipped with this project (version " +
                        updatable.back().str() + "). No changes performed.";
      log("INFO", info);
      return info;
    }
    if (std::find(updatable.begin(), updatable.end(), *current) ==
        updatable.end()) {
      fail("Update of database schema `" + name + "` to version " + version +
           " requested but the version " + current->str() +
           " cannot be updated.");
    }
  }

  if (!exists) {
    log("INFO", "Starting deployment of database schema `" + name +
                    "` using version " + version + " ...");
  } else {
    log("INFO", "Starting update of database schema `" + name +
                    "` version " + current->str() + " to version " + version +
                    " ...");
  }

  // A copy of the existing schema to go back to if the update fails
  bool backup_available = false;
  if (exists && backup && m_backup) {
    log("INFO", "Preparing dump of `" + name + "` version " + current->str() +
                    " in order to be roll back in case of an error.");
    try {
      m_backup->create(session, name, *current);
    } catch (const std::exception &e) {
      fail(e.what());
    }
    backup_available = true;
  }

  try {
    log("INFO", "Running SQL script `" + script_path.string() + "` ...");
    {
      Msm_lock lock(session);
      try {
        session->execute_script(deploy_script);
      } catch (const std::exception &e) {
        log("ERROR", "Failed to run the the SQL script `" +
                         script_path.string() + "`.\n" + e.what());
        throw std::runtime_error(std::string("Failed to run the SQL script.\n") +
                                 e.what());
      }
    }
    log("INFO", "SQL script " + script_path.string() + " executed successfully.");

    const auto info =
        !exists ? "Deployment of `" + name + "` version " + version +
                      " completed successfully."
                : "Completed the update of `" + name + "` version " +
                      current->str() + " to " + version + " successfully.";
    log("INFO", info);
    if (backup_available) m_backup->discard();
    return info;
  } catch (const std::exception &e) {
    // Drop the schema only when it can be restored from the copy, or when
    // this deployment created it; otherwise the data the update was applied
    // to would be lost.
    if (backup_available || !exists) {
      try {
        session->execute("DROP SCHEMA IF EXISTS " +
                         sql::quote_identifier(name));
      } catch (...) {
        // The original error is the one to report
      }
    }

    if (backup_available) {
      try {
        m_backup->restore(session);
        m_backup->discard();
      } catch (const std::exception &restore_error) {
        fail("An error occurred while updating the database schema `" + name +
             "` to version " + version +
             ". The schema could not be restored back to version " +
             current->str() + ". " + e.what() + " " + restore_error.what());
      }
      fail("An error occurred while updating the database schema `" + name +
           "` to version " + version +
           ". The schema has been restored back to version " + current->str() +
           ". " + e.what());
    }

    if (!exists) {
      fail("Deploying the database schema `" + name + "` failed. " + e.what());
    }
    fail("An error occurred while updating the database schema `" + name +
         "` to version " + version + ". " + e.what());
  }
}

}  // namespace metadata
}  // namespace mrs
