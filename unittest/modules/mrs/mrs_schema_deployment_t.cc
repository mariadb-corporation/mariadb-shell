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

// metadata::Script_deployer, the replica of msm's deploy_schema(), against a
// fake session: its checks, messages, log and the drop/restore rules.

#include <filesystem>
#include <functional>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "unittest/gtest_clean.h"

#include "modules/mrs/core/mrs_schema_deployment.h"

namespace mrs {
namespace metadata {
namespace {

// The metadata schema as far as the deployment looks at it.
class Fake_session : public Db_session {
 public:
  bool exists = false;
  bool managed = true;
  std::optional<Version> version;
  std::optional<std::string> script_error;
  std::vector<std::string> statements;
  int scripts_run = 0;

  Db_result query(const std::string &sql) override {
    statements.push_back(sql);
    if (sql.find("SCHEMATA") != std::string::npos) {
      return single("schema_exists", exists ? 1 : 0);
    }
    if (sql.find("TABLE_TYPE = 'VIEW'") != std::string::npos) {
      return single("table_count", exists && managed ? 1 : 0);
    }
    if (sql.find("SELECT major, minor, patch") != std::string::npos) {
      if (!version) throw Db_error("no view", 1146, "42S02");
      Db_result result;
      result.columns = std::make_shared<const std::vector<std::string>>(
          std::vector<std::string>{"major", "minor", "patch"});
      result.rows.emplace_back(
          result.columns,
          std::vector<Db_value>{Db_value(int64_t{version->major}),
                                Db_value(int64_t{version->minor}),
                                Db_value(int64_t{version->patch})});
      return result;
    }
    if (sql.find("GET_LOCK") != std::string::npos) return single("msm_lock", 1);
    return {};
  }

  uint64_t execute(const std::string &sql) override {
    statements.push_back(sql);
    if (sql.starts_with("DROP SCHEMA")) {
      exists = false;
      version.reset();
    }
    return 0;
  }

  void execute_script(const std::string &) override {
    ++scripts_run;
    if (script_error) throw Db_error(*script_error, 1064, "42000");
    exists = true;
    managed = true;
    version = k_schema_version;
  }

  bool is_mariadb() const override { return true; }
  uint64_t connection_id() const override { return 1; }
  std::string sql_mode() override { return ""; }

  bool ran(std::string_view prefix) const {
    for (const auto &s : statements) {
      if (s.starts_with(prefix)) return true;
    }
    return false;
  }

 private:
  static Db_result single(const std::string &column, int64_t value) {
    Db_result result;
    result.columns = std::make_shared<const std::vector<std::string>>(
        std::vector<std::string>{column});
    result.rows.emplace_back(result.columns,
                             std::vector<Db_value>{Db_value(value)});
    return result;
  }
};

class Fake_log : public Deployment_log {
 public:
  std::vector<std::string> lines;
  void write(std::string_view type, std::string_view message) override {
    lines.push_back(std::string(type) + " - " + std::string(message));
  }
  bool has(std::string_view text) const {
    for (const auto &line : lines) {
      if (line.find(text) != std::string::npos) return true;
    }
    return false;
  }
};

class Fake_backup : public Schema_backup {
 public:
  explicit Fake_backup(Fake_session *session) : m_session(session) {}

  bool created = false;
  bool restored = false;
  bool discarded = false;
  bool fail_restore = false;
  std::optional<Version> saved;

  void create(Db_session *, std::string_view, const Version &v) override {
    created = true;
    saved = v;
  }
  void restore(Db_session *) override {
    if (fail_restore) throw std::runtime_error("load failed");
    restored = true;
    m_session->exists = true;
    m_session->version = saved;
  }
  void discard() override { discarded = true; }

 private:
  Fake_session *m_session;
};

// The script names msm's update procedure, the source of the updatable
// versions.
constexpr const char *k_script = R"(
DELIMITER %%
DROP PROCEDURE IF EXISTS `msm_update_4.1.6_to_5.0.0`%%
CREATE PROCEDURE `msm_update_4.1.6_to_5.0.0`()
BEGIN
END%%
DELIMITER ;
)";

class Mrs_schema_deployment : public ::testing::Test {
 protected:
  void SetUp() override {
    std::random_device rd;
    m_dir = std::filesystem::temp_directory_path() /
            ("mrs_deployment_" + std::to_string(rd()));
    std::filesystem::create_directories(m_dir);
    std::ofstream(m_dir / deployment_script_name(k_schema_version)) << k_script;
  }
  void TearDown() override { std::filesystem::remove_all(m_dir); }

  std::string dir() const { return m_dir.string(); }

  std::filesystem::path m_dir;
};

std::string message_of(const std::function<void()> &f) {
  try {
    f();
  } catch (const std::exception &e) {
    return e.what();
  }
  return "";
}

TEST_F(Mrs_schema_deployment, updatable_versions_from_the_script) {
  const auto versions = updatable_versions(k_script);
  ASSERT_EQ(1u, versions.size());
  EXPECT_EQ((Version{4, 1, 6}), versions[0]);
  EXPECT_TRUE(updatable_versions("CREATE TABLE t (a INT);").empty());
}

TEST_F(Mrs_schema_deployment, released_versions_of_a_folder) {
  std::ofstream(m_dir / "mysql_rest_service_metadata_deployment_4.1.6.sql") << "";
  std::ofstream(m_dir / "notes.txt") << "";
  std::filesystem::create_directories(m_dir / "older_1.0.0.sql");
  const auto versions = released_versions(dir());
  ASSERT_EQ(2u, versions.size());
  EXPECT_EQ((Version{4, 1, 6}), versions[0]);
  EXPECT_EQ(k_schema_version, versions[1]);
  EXPECT_EQ(versions, Script_deployer(dir()).available_versions());
  EXPECT_TRUE(released_versions((m_dir / "nope").string()).empty());
}

TEST_F(Mrs_schema_deployment, fresh_deployment) {
  Fake_session session;
  Fake_log log;
  Script_deployer deployer(dir(), &log);

  EXPECT_EQ(
      "Deployment of `mysql_rest_service_metadata` version 5.0.0 completed "
      "successfully.",
      deployer.deploy(&session, true));
  EXPECT_EQ(1, session.scripts_run);
  EXPECT_TRUE(session.ran("SELECT GET_LOCK('MSM_METADATA_LOCK', 1)"));
  EXPECT_TRUE(session.ran("SELECT RELEASE_LOCK('MSM_METADATA_LOCK')"));
  EXPECT_TRUE(log.has(
      "INFO - Starting deployment of database schema "
      "`mysql_rest_service_metadata` using version 5.0.0 ..."));
  EXPECT_TRUE(log.has("INFO - Running SQL script `"));
}

TEST_F(Mrs_schema_deployment, already_on_the_version) {
  Fake_session session;
  session.exists = true;
  session.version = k_schema_version;
  Script_deployer deployer(dir());

  const auto info = deployer.deploy(&session, true);
  EXPECT_NE(std::string::npos, info.find("No changes performed."));
  EXPECT_EQ(0, session.scripts_run);
}

TEST_F(Mrs_schema_deployment, update_with_backup) {
  Fake_session session;
  session.exists = true;
  session.version = Version{4, 1, 6};
  Fake_backup backup(&session);
  Script_deployer deployer(dir(), nullptr, &backup);

  EXPECT_EQ(
      "Completed the update of `mysql_rest_service_metadata` version 4.1.6 "
      "to 5.0.0 successfully.",
      deployer.deploy(&session, true));
  EXPECT_TRUE(backup.created);
  EXPECT_TRUE(backup.discarded);
  EXPECT_FALSE(backup.restored);
}

TEST_F(Mrs_schema_deployment, no_backup_when_disabled) {
  Fake_session session;
  session.exists = true;
  session.version = Version{4, 1, 6};
  Fake_backup backup(&session);
  Script_deployer deployer(dir(), nullptr, &backup);

  deployer.deploy(&session, false);
  EXPECT_FALSE(backup.created);
}

TEST_F(Mrs_schema_deployment, versions_that_cannot_be_updated) {
  Fake_session session;
  session.exists = true;
  session.version = Version{4, 1, 5};
  Script_deployer deployer(dir());

  EXPECT_EQ(
      "Update of database schema `mysql_rest_service_metadata` to version "
      "5.0.0 requested but the version 4.1.5 cannot be updated.",
      message_of([&] { deployer.deploy(&session, true); }));

  // msm compares with the last updatable version, not the target
  session.version = Version{4, 2, 0};
  const auto info = deployer.deploy(&session, true);
  EXPECT_EQ(
      "The database schema `mysql_rest_service_metadata` is on a newer "
      "version 4.2.0 than shipped with this project (version 4.1.6). No "
      "changes performed.",
      info);
  EXPECT_EQ(0, session.scripts_run);
}

TEST_F(Mrs_schema_deployment, schema_not_managed_by_msm) {
  Fake_session session;
  session.exists = true;
  session.managed = false;
  Script_deployer deployer(dir());

  EXPECT_EQ(
      "Deployment or update of database schema `mysql_rest_service_metadata` "
      "using version 5.0.0 requested but the schema is not managed by MSM.",
      message_of([&] { deployer.deploy(&session, true); }));
}

TEST_F(Mrs_schema_deployment, missing_deployment_script) {
  Fake_session session;
  Fake_log log;
  Script_deployer deployer((m_dir / "nope").string(), &log);

  EXPECT_EQ(
      "Deployment or update of database schema `mysql_rest_service_metadata` "
      "using version 5.0.0 requested but there is no deployment script "
      "available for this version.",
      message_of([&] { deployer.deploy(&session, true); }));
  EXPECT_TRUE(log.has("ERROR - Deployment or update"));
}

TEST_F(Mrs_schema_deployment, failed_deployment_drops_the_new_schema) {
  Fake_session session;
  session.script_error = "boom";
  Script_deployer deployer(dir());

  const auto message = message_of([&] { deployer.deploy(&session, true); });
  EXPECT_TRUE(message.starts_with(
      "Deploying the database schema `mysql_rest_service_metadata` failed. "
      "Failed to run the SQL script.\nboom"))
      << message;
  EXPECT_TRUE(session.ran("DROP SCHEMA IF EXISTS `mysql_rest_service_metadata`"));
  EXPECT_TRUE(session.ran("SELECT RELEASE_LOCK"));
}

TEST_F(Mrs_schema_deployment, failed_update_is_restored_from_the_backup) {
  Fake_session session;
  session.exists = true;
  session.version = Version{4, 1, 6};
  session.script_error = "boom";
  Fake_backup backup(&session);
  Script_deployer deployer(dir(), nullptr, &backup);

  const auto message = message_of([&] { deployer.deploy(&session, true); });
  EXPECT_TRUE(message.starts_with(
      "An error occurred while updating the database schema "
      "`mysql_rest_service_metadata` to version 5.0.0. The schema has been "
      "restored back to version 4.1.6. Failed to run the SQL script.\nboom"))
      << message;
  EXPECT_TRUE(session.ran("DROP SCHEMA IF EXISTS"));
  EXPECT_TRUE(backup.restored);
  EXPECT_TRUE(backup.discarded);
  EXPECT_EQ((Version{4, 1, 6}), *session.version);
}

TEST_F(Mrs_schema_deployment, failed_restore_is_reported) {
  Fake_session session;
  session.exists = true;
  session.version = Version{4, 1, 6};
  session.script_error = "boom";
  Fake_backup backup(&session);
  backup.fail_restore = true;
  Script_deployer deployer(dir(), nullptr, &backup);

  const auto message = message_of([&] { deployer.deploy(&session, true); });
  EXPECT_NE(std::string::npos,
            message.find("The schema could not be restored back to version "
                         "4.1.6. Failed to run the SQL script.\nboom load failed"))
      << message;
}

TEST_F(Mrs_schema_deployment, failed_update_without_backup_keeps_the_schema) {
  Fake_session session;
  session.exists = true;
  session.version = Version{4, 1, 6};
  session.script_error = "boom";
  Script_deployer deployer(dir());

  const auto message = message_of([&] { deployer.deploy(&session, true); });
  EXPECT_TRUE(message.starts_with(
      "An error occurred while updating the database schema "
      "`mysql_rest_service_metadata` to version 5.0.0. Failed to run the SQL "
      "script.\nboom"))
      << message;
  EXPECT_FALSE(session.ran("DROP SCHEMA"));
}

}  // namespace
}  // namespace metadata
}  // namespace mrs
