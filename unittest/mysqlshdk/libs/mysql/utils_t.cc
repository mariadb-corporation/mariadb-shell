/*
 * Copyright (c) 2018, 2026, Oracle and/or its affiliates.
 * Copyright (c) 2026, MariaDB plc.
 *
 * SPDX-License-Identifier: GPL-2.0-only
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include "mysqlshdk/libs/mysql/utils.h"
#include "unittest/gtest_clean.h"
#include "unittest/test_utils.h"

#include "unittest/test_utils/mocks/mysqlshdk/libs/db/mock_result.h"
#include "unittest/test_utils/mocks/mysqlshdk/libs/mysql/mock_instance.h"

namespace mysqlshdk {
namespace mysql {

class Mysql_utils : public tests::Shell_test_env {};

class Mysql_utils_mocked : public ::testing::Test {};

#define COMPARE_ACCOUNTS(user1, host1, user2, host2)                          \
  EXPECT_EQ(                                                                  \
      1,                                                                      \
      session                                                                 \
          ->queryf(                                                           \
              "select "                                                       \
              "coalesce((select group_concat(concat(table_catalog, "          \
              "privilege_type, "                                              \
              "is_grantable)) from information_schema.user_privileges where " \
              "grantee = ? order by privilege_type), '')"                     \
              " = coalesce((select group_concat(concat(table_catalog, "       \
              "privilege_type, "                                              \
              "is_grantable)) from information_schema.user_privileges where " \
              "grantee = ? order by privilege_type), '')",                    \
              "'" user1 "'@'" host1 "'", "'" user2 "'@'" host2 "'")           \
          ->fetch_one()                                                       \
          ->get_int(0));                                                      \
  EXPECT_EQ(                                                                  \
      1,                                                                      \
      session                                                                 \
          ->queryf("select "                                                  \
                   "coalesce((select group_concat(concat(table_catalog, "     \
                   "table_schema, "                                           \
                   "privilege_type, is_grantable)) from "                     \
                   "information_schema.schema_privileges where grantee = ? "  \
                   "order by privilege_type), '') "                           \
                   " = coalesce((select group_concat(concat(table_catalog, "  \
                   "table_schema, privilege_type, is_grantable)) from "       \
                   "information_schema.schema_privileges where grantee = ? "  \
                   "order by privilege_type), '')",                           \
                   "'" user1 "'@'" host1 "'", "'" user2 "'@'" host2 "'")      \
          ->fetch_one()                                                       \
          ->get_int(0));                                                      \
  EXPECT_EQ(                                                                  \
      1,                                                                      \
      session                                                                 \
          ->queryf("select "                                                  \
                   "coalesce((select group_concat(concat(table_catalog, "     \
                   "table_schema, table_name, "                               \
                   "privilege_type, is_grantable)) from "                     \
                   "information_schema.table_privileges "                     \
                   "where grantee = ? order by privilege_type), '') "         \
                   " = coalesce((select group_concat(concat(table_catalog, "  \
                   "table_schema, table_name, "                               \
                   "privilege_type, is_grantable)) from "                     \
                   "information_schema.table_privileges "                     \
                   "where grantee = ? order by privilege_type), '')",         \
                   "'" user1 "'@'" host1 "'", "'" user2 "'@'" host2 "'")      \
          ->fetch_one()                                                       \
          ->get_int(0));                                                      \
  EXPECT_EQ(                                                                  \
      1,                                                                      \
      session                                                                 \
          ->queryf(                                                           \
              "select "                                                       \
              "coalesce((select group_concat(concat(table_catalog, "          \
              "table_schema, "                                                \
              "table_name, column_name, privilege_type, is_grantable)) from " \
              "information_schema.column_privileges where grantee = ? order " \
              "by privilege_type), '') "                                      \
              " = coalesce((select group_concat(concat(table_catalog, "       \
              "table_schema, "                                                \
              "table_name, "                                                  \
              "column_name, privilege_type, is_grantable)) from "             \
              "information_schema.column_privileges where grantee = ? order " \
              "by privilege_type), '')",                                      \
              "'" user1 "'@'" host1 "'", "'" user2 "'@'" host2 "'")           \
          ->fetch_one()                                                       \
          ->get_int(0));

TEST_F(Mysql_utils, clone_user) {
  auto session = create_mysql_session();
  mysqlshdk::mysql::Instance instance(session);

  session->execute("drop user if exists rootey@localhost");
  session->execute("drop user if exists testusr@localhost");

  session->execute("create user testusr@localhost");
  session->execute(
      "grant shutdown on *.* to testusr@localhost with grant option");
  session->execute(
      "grant select on performance_schema.* to testusr@localhost with "
      "grant "
      "option");
  session->execute("grant select,insert on mysql.user to testusr@localhost");
  session->execute(
      "grant select (user), update (user) on mysql.db to "
      "testusr@localhost");

  // clone the current user (presumably root@localhost)
  clone_user(instance, "root", "localhost", "rootey", "localhost", "foo");
  // compare grants through the I_S table
  COMPARE_ACCOUNTS("root", "localhost", "rootey", "localhost");
  session->execute("drop user rootey@localhost");

  clone_user(instance, "testusr", "localhost", "rootey", "localhost", "foo");
  COMPARE_ACCOUNTS("testusr", "localhost", "rootey", "localhost");

  // drop the account
  session->execute("drop user testusr@localhost");
  session->execute("drop user rootey@localhost");
  session->close();
}

namespace detail {
std::string replace_grantee(const std::string &grant,
                            const std::string &replacement);
}

TEST_F(Mysql_utils, clone_user_replace_grantee) {
  EXPECT_EQ("GRANT PROXY ON ''@'' TO 'foo'@'bar' WITH GRANT OPTION",
            detail::replace_grantee(
                "GRANT PROXY ON ''@'' TO 'root'@'localhost' WITH GRANT OPTION",
                "'foo'@'bar'"));

  EXPECT_EQ("GRANT PROXY ON ``@`` TO 'foo'@'bar' WITH GRANT OPTION",
            detail::replace_grantee(
                "GRANT PROXY ON ``@`` TO `root`@`localhost` WITH GRANT OPTION",
                "'foo'@'bar'"));

  EXPECT_EQ(
      "GRANT ALL PRIVILEGES ON *.* TO 'foo'@'bar' WITH GRANT OPTION",
      detail::replace_grantee(
          "GRANT ALL PRIVILEGES ON *.* TO 'root'@'localhost' WITH GRANT OPTION",
          "'foo'@'bar'"));

  EXPECT_EQ(
      "GRANT ALL PRIVILEGES ON *.* TO 'foo'@'bar' WITH GRANT OPTION",
      detail::replace_grantee(
          "GRANT ALL PRIVILEGES ON *.* TO `root`@`localhost` WITH GRANT OPTION",
          "'foo'@'bar'"));

  EXPECT_EQ(
      "GRANT ALL PRIVILEGES ON ` TO 'bla'@'bla' `.* TO 'foo'@'bar'",
      detail::replace_grantee(
          "GRANT ALL PRIVILEGES ON ` TO 'bla'@'bla' `.* TO 'root'@'localhost'",
          "'foo'@'bar'"));
}

#ifndef MARIADB_BUILD
TEST_F(Mysql_utils, drop_table_or_view) {
  auto session = create_mysql_session();
  mysqlshdk::mysql::Instance instance(session);

  instance.execute("DROP SCHEMA IF EXISTS drop_table_or_view");
  instance.execute("CREATE SCHEMA drop_table_or_view");
  instance.execute("USE drop_table_or_view");

  instance.execute("CREATE TABLE tsample (id INT, name VARCHAR(30))");
  instance.execute("CREATE VIEW vsample AS SELECT * FROM tsample");

  EXPECT_THROW_LIKE(
      drop_table_or_view(instance, "drop_table_or_view", "unexisting", false),
      mysqlshdk::db::Error, "Unknown table 'drop_table_or_view.unexisting'");

  EXPECT_NO_THROW(
      drop_table_or_view(instance, "drop_table_or_view", "unexisting", true));

  EXPECT_NO_THROW(
      drop_table_or_view(instance, "drop_table_or_view", "vsample", true));

  EXPECT_NO_THROW(
      drop_table_or_view(instance, "drop_table_or_view", "tsample", true));

  instance.execute("DROP SCHEMA drop_table_or_view");
}

TEST_F(Mysql_utils, drop_view_or_table) {
  auto session = create_mysql_session();
  mysqlshdk::mysql::Instance instance(session);

  instance.execute("DROP SCHEMA IF EXISTS drop_view_or_table");
  instance.execute("CREATE SCHEMA drop_view_or_table");
  instance.execute("USE drop_view_or_table");

  instance.execute("CREATE TABLE tsample (id INT, name VARCHAR(30))");
  instance.execute("CREATE VIEW vsample AS SELECT * FROM tsample");

  EXPECT_THROW_LIKE(
      drop_view_or_table(instance, "drop_view_or_table", "unexisting", false),
      mysqlshdk::db::Error, "Unknown table 'drop_view_or_table.unexisting'");

  EXPECT_NO_THROW(
      drop_view_or_table(instance, "drop_view_or_table", "unexisting", true));

  EXPECT_NO_THROW(
      drop_view_or_table(instance, "drop_view_or_table", "vsample", true));

  EXPECT_NO_THROW(
      drop_view_or_table(instance, "drop_view_or_table", "tsample", true));

  instance.execute("DROP SCHEMA drop_view_or_table");
}
#endif

TEST_F(Mysql_utils_mocked,
       create_user_with_random_password_uses_server_random) {
  mysqlshdk::mysql::Mock_instance instance;
  mysqlshdk::mysql::IInstance::Create_user_options options;

  EXPECT_CALL(instance, get_version())
      .WillRepeatedly(testing::Return(mysqlshdk::utils::Version(8, 0, 18)));
  EXPECT_CALL(instance, descr()).WillRepeatedly(testing::Return("mock"));

  auto rs = std::make_shared<testing::Mock_result>();
  rs->add_result({"user", "host", "generated password", "auth_factor"},
                 {mysqlshdk::db::Type::String, mysqlshdk::db::Type::String,
                  mysqlshdk::db::Type::String, mysqlshdk::db::Type::Integer})
      .add_row({"mysqlsh.test", "%", "exampleRandomPass", "1"});
  EXPECT_CALL(*rs, has_resultset()).WillRepeatedly(testing::Return(true));

  EXPECT_CALL(instance, query(testing::_, true))
      .WillOnce(testing::Invoke([&](const std::string &sql, bool) {
        EXPECT_THAT(
            sql,
            testing::HasSubstr(
                "IDENTIFIED WITH 'caching_sha2_password' BY RANDOM PASSWORD"));
        rs->rewind();
        return rs;
      }));
  EXPECT_CALL(instance, create_user("mysqlsh.test", "%", testing::_)).Times(0);

  auto password = mysqlshdk::mysql::create_user_with_random_password(
      instance, "mysqlsh.test", {"%"}, options);

  ASSERT_TRUE(password.has_value());
  EXPECT_EQ("exampleRandomPass", *password);
}

TEST_F(Mysql_utils_mocked, create_user_with_random_password_applies_grants) {
  mysqlshdk::mysql::Mock_instance instance;
  mysqlshdk::mysql::IInstance::Create_user_options options;
  options.grants.push_back(
      mysqlshdk::mysql::IInstance::Create_user_options::Grant{"SELECT", "*.*",
                                                              false});

  EXPECT_CALL(instance, get_version())
      .WillRepeatedly(testing::Return(mysqlshdk::utils::Version(8, 0, 18)));
  EXPECT_CALL(instance, descr()).WillRepeatedly(testing::Return("mock"));

  auto rs = std::make_shared<testing::Mock_result>();
  rs->add_result({"user", "host", "generated password", "auth_factor"},
                 {mysqlshdk::db::Type::String, mysqlshdk::db::Type::String,
                  mysqlshdk::db::Type::String, mysqlshdk::db::Type::Integer})
      .add_row({"mysqlsh.test", "%", "exampleRandomPass", "1"});
  EXPECT_CALL(*rs, has_resultset()).WillRepeatedly(testing::Return(true));

  EXPECT_CALL(instance, query(testing::_, true)).WillOnce(testing::Return(rs));
  EXPECT_CALL(
      instance,
      execute(testing::HasSubstr("GRANT SELECT ON *.* TO 'mysqlsh.test'@'%'")))
      .Times(1);
  EXPECT_CALL(instance, create_user(testing::_, testing::_, testing::_))
      .Times(0);

  auto password = mysqlshdk::mysql::create_user_with_random_password(
      instance, "mysqlsh.test", {"%"}, options);

  ASSERT_TRUE(password.has_value());
  EXPECT_EQ("exampleRandomPass", *password);
}

TEST_F(Mysql_utils_mocked,
       create_user_with_random_password_multi_host_reuses_password) {
  mysqlshdk::mysql::Mock_instance instance;
  mysqlshdk::mysql::IInstance::Create_user_options options;

  EXPECT_CALL(instance, get_version())
      .WillRepeatedly(testing::Return(mysqlshdk::utils::Version(8, 0, 18)));
  EXPECT_CALL(instance, descr()).WillRepeatedly(testing::Return("mock"));

  auto rs = std::make_shared<testing::Mock_result>();
  rs->add_result({"user", "host", "generated password", "auth_factor"},
                 {mysqlshdk::db::Type::String, mysqlshdk::db::Type::String,
                  mysqlshdk::db::Type::String, mysqlshdk::db::Type::Integer})
      .add_row({"mysqlsh.test", "%", "exampleRandomPass", "1"});
  EXPECT_CALL(*rs, has_resultset()).WillRepeatedly(testing::Return(true));

  EXPECT_CALL(instance, query(testing::_, true)).WillOnce(testing::Return(rs));
  EXPECT_CALL(instance, create_user("mysqlsh.test", "localhost", testing::_))
      .WillOnce(testing::Invoke(
          [&](std::string_view, std::string_view,
              const mysqlshdk::mysql::IInstance::Create_user_options &opts) {
            ASSERT_TRUE(opts.password.has_value());
            EXPECT_EQ("exampleRandomPass", *opts.password);
            EXPECT_FALSE(opts.random_password);
          }));

  auto password = mysqlshdk::mysql::create_user_with_random_password(
      instance, "mysqlsh.test", {"%", "localhost"}, options);

  ASSERT_TRUE(password.has_value());
  EXPECT_EQ("exampleRandomPass", *password);
}

TEST_F(Mysql_utils_mocked, create_user_with_random_password_missing_password) {
  mysqlshdk::mysql::Mock_instance instance;
  mysqlshdk::mysql::IInstance::Create_user_options options;

  EXPECT_CALL(instance, get_version())
      .WillRepeatedly(testing::Return(mysqlshdk::utils::Version(8, 0, 18)));
  EXPECT_CALL(instance, descr()).WillRepeatedly(testing::Return("mock"));

  auto empty_result = std::make_shared<testing::Mock_result>();
  empty_result
      ->add_result({"user", "host"},
                   {mysqlshdk::db::Type::String, mysqlshdk::db::Type::String})
      .add_row({"mysqlsh.test", "%"});
  EXPECT_CALL(*empty_result, has_resultset())
      .WillRepeatedly(testing::Return(true));

  EXPECT_CALL(instance, query(testing::_, true))
      .WillOnce(testing::Return(empty_result));
  EXPECT_CALL(instance, create_user("mysqlsh.test", "%", testing::_)).Times(0);

  EXPECT_THROW(mysqlshdk::mysql::create_user_with_random_password(
                   instance, "mysqlsh.test", {"%"}, options),
               std::runtime_error);
}

TEST_F(Mysql_utils_mocked, create_user_with_random_password_respects_length) {
  mysqlshdk::mysql::Mock_instance instance;
  mysqlshdk::mysql::IInstance::Create_user_options options;

  EXPECT_CALL(instance, get_version())
      .WillRepeatedly(testing::Return(mysqlshdk::utils::Version(8, 0, 17)));
  EXPECT_CALL(instance, descr()).WillRepeatedly(testing::Return("mock"));
  EXPECT_CALL(instance, get_sysvar_int("validate_password.length", testing::_))
      .WillOnce(testing::Return(std::optional<int64_t>(64)));

  EXPECT_CALL(instance, create_user("mysqlsh.test", "%", testing::_))
      .WillOnce(testing::Invoke(
          [&](std::string_view, std::string_view,
              const mysqlshdk::mysql::IInstance::Create_user_options &opts) {
            ASSERT_TRUE(opts.password.has_value());
            EXPECT_GE(opts.password->size(), static_cast<size_t>(64));
          }));

  auto password = mysqlshdk::mysql::create_user_with_random_password(
      instance, "mysqlsh.test", {"%"}, options);

  ASSERT_TRUE(password.has_value());
  EXPECT_GE(password->size(), static_cast<size_t>(64));
}

TEST_F(Mysql_utils_mocked,
       create_user_with_random_password_uses_session_length) {
  mysqlshdk::mysql::Mock_instance instance;
  mysqlshdk::mysql::IInstance::Create_user_options options;

  EXPECT_CALL(instance, get_version())
      .WillRepeatedly(testing::Return(mysqlshdk::utils::Version(8, 0, 17)));
  EXPECT_CALL(instance, descr()).WillRepeatedly(testing::Return("mock"));
  EXPECT_CALL(instance, get_sysvar_int("validate_password.length", testing::_))
      .WillOnce(testing::Return(std::optional<int64_t>(32)));

  EXPECT_CALL(instance, create_user("mysqlsh.test", "%", testing::_))
      .WillOnce(testing::Invoke(
          [&](std::string_view, std::string_view,
              const mysqlshdk::mysql::IInstance::Create_user_options &opts) {
            ASSERT_TRUE(opts.password.has_value());
            EXPECT_GE(opts.password->size(), static_cast<size_t>(32));
          }));

  auto password = mysqlshdk::mysql::create_user_with_random_password(
      instance, "mysqlsh.test", {"%"}, options);

  ASSERT_TRUE(password.has_value());
  EXPECT_GE(password->size(), static_cast<size_t>(32));
}

TEST_F(Mysql_utils_mocked, create_user_with_random_password_length_fallback) {
  mysqlshdk::mysql::Mock_instance instance;
  mysqlshdk::mysql::IInstance::Create_user_options options;

  EXPECT_CALL(instance, get_version())
      .WillRepeatedly(testing::Return(mysqlshdk::utils::Version(8, 0, 17)));
  EXPECT_CALL(instance, descr()).WillRepeatedly(testing::Return("mock"));
  EXPECT_CALL(instance, get_sysvar_int("validate_password.length", testing::_))
      .WillOnce(testing::Return(std::optional<int64_t>()));

  EXPECT_CALL(instance, create_user("mysqlsh.test", "%", testing::_))
      .WillOnce(testing::Invoke(
          [&](std::string_view, std::string_view,
              const mysqlshdk::mysql::IInstance::Create_user_options &opts) {
            ASSERT_TRUE(opts.password.has_value());
            EXPECT_GE(opts.password->size(),
                      static_cast<size_t>(kPASSWORD_LENGTH));
          }));

  auto password = mysqlshdk::mysql::create_user_with_random_password(
      instance, "mysqlsh.test", {"%"}, options);

  ASSERT_TRUE(password.has_value());
  EXPECT_GE(password->size(), static_cast<size_t>(kPASSWORD_LENGTH));
}

TEST_F(Mysql_utils_mocked, create_user_with_random_password_retries_exhausted) {
  mysqlshdk::mysql::Mock_instance instance;
  mysqlshdk::mysql::IInstance::Create_user_options options;

  EXPECT_CALL(instance, get_version())
      .WillRepeatedly(testing::Return(mysqlshdk::utils::Version(8, 0, 17)));
  EXPECT_CALL(instance, descr()).WillRepeatedly(testing::Return("mock"));
  EXPECT_CALL(instance, get_sysvar_int("validate_password.length", testing::_))
      .WillOnce(testing::Return(std::optional<int64_t>(kPASSWORD_LENGTH)));

  EXPECT_CALL(instance, create_user("mysqlsh.test", "%", testing::_))
      .WillRepeatedly(testing::Invoke(
          [](std::string_view, std::string_view,
             const mysqlshdk::mysql::IInstance::Create_user_options &) {
            throw mysqlshdk::db::Error("weak password", ER_NOT_VALID_PASSWORD,
                                       "HY000");
          }));

  EXPECT_THROW(mysqlshdk::mysql::create_user_with_random_password(
                   instance, "mysqlsh.test", {"%"}, options),
               std::runtime_error);
}

void verify_schema_structure(const mysqlshdk::mysql::Instance &instance,
                             const std::string &schema,
                             bool with_tables = true) {
  std::set<std::string> objects;
  auto result = instance.queryf("SHOW TABLES FROM !", schema.c_str());
  auto row = result->fetch_one();
  while (row) {
    objects.insert(row->get_string(0));
    row = result->fetch_one();
  }

  if (with_tables) {
    EXPECT_NE(objects.end(), objects.find("table1"));
    EXPECT_NE(objects.end(), objects.find("table2"));
  } else {
    EXPECT_EQ(objects.end(), objects.find("table1"));
    EXPECT_EQ(objects.end(), objects.find("table2"));
  }

  EXPECT_EQ(objects.end(), objects.find("table3"));

  EXPECT_NE(objects.end(), objects.find("view1"));
  EXPECT_NE(objects.end(), objects.find("view2"));
  EXPECT_EQ(objects.end(), objects.find("view3"));

  // We can not query the views if the tables no longer exist
  if (with_tables) {
    result = instance.queryf("SELECT * FROM !.view1", schema.c_str());
    row = result->fetch_one();
    EXPECT_EQ(1, row->get_int(0));
    EXPECT_EQ(0, row->get_int(1));
    EXPECT_EQ(1, row->get_int(2));
    EXPECT_EQ(nullptr, result->fetch_one());

    result = instance.queryf("SELECT * FROM !.view2", schema.c_str());
    row = result->fetch_one();
    EXPECT_EQ(1, row->get_int(0));
    EXPECT_EQ("John Doe", row->get_string(1));
    EXPECT_EQ(nullptr, result->fetch_one());
  }
}

#ifndef MARIADB_BUILD
TEST_F(Mysql_utils, copy_schema) {
  auto session = create_mysql_session();
  mysqlshdk::mysql::Instance instance(session);

  // BASE DATA FOR TESTS
  instance.execute("DROP SCHEMA IF EXISTS copy_schema_demo1");
  instance.execute("DROP SCHEMA IF EXISTS copy_schema_demo2");
  instance.execute("DROP SCHEMA IF EXISTS copy_schema_demo3");
  instance.execute("DROP SCHEMA IF EXISTS copy_schema_demo4");

  instance.execute("CREATE SCHEMA copy_schema_demo1");
  instance.execute("USE copy_schema_demo1");
  instance.execute("CREATE TABLE table1 (id INT, name VARCHAR(30))");
  instance.execute("INSERT INTO table1 VALUES (1, 'John Doe')");
  instance.execute("CREATE TABLE table2 (id INT, name VARCHAR(30))");
  instance.execute(
      "CREATE VIEW view1 (major, minor, patch) AS SELECT "
      "1, 0, 1");
  instance.execute("CREATE VIEW view2 AS SELECT * FROM table1");

  instance.execute("CREATE SCHEMA copy_schema_demo2");
  instance.execute("USE copy_schema_demo2");
  instance.execute("CREATE TABLE table1 (id INT, name VARCHAR(30))");
  instance.execute("INSERT INTO table1 VALUES (2, 'Jane Doe')");
  instance.execute("CREATE TABLE table3 (id INT, name VARCHAR(30))");
  instance.execute(
      "CREATE VIEW view1 (major, minor, patch) AS SELECT "
      "2, 0, 2");
  instance.execute("CREATE VIEW view3 AS SELECT * FROM table1");

  // ERROR BACKING UP TO EXISTING SCHEMA
  EXPECT_THROW(copy_schema(instance, "copy_schema_demo1", "copy_schema_demo2",
                           false, false),
               std::runtime_error);

  // OK BACKING UP TO EXISTING SCHEMA
  EXPECT_NO_THROW(copy_schema(instance, "copy_schema_demo1",
                              "copy_schema_demo2", true, false));

  verify_schema_structure(instance, "copy_schema_demo2");

  // OK BACKING UP TO NON EXISTING SCHEMA
  EXPECT_NO_THROW(copy_schema(instance, "copy_schema_demo1",
                              "copy_schema_demo3", true, false));

  verify_schema_structure(instance, "copy_schema_demo3");

  // So far all the tests have been real data copy (not moves) so the source
  // schema should continue containing the data
  verify_schema_structure(instance, "copy_schema_demo1");

  // Now backups to another schema but MOVING the tables
  EXPECT_NO_THROW(copy_schema(instance, "copy_schema_demo1",
                              "copy_schema_demo4", true, true));

  verify_schema_structure(instance, "copy_schema_demo1", false);
  verify_schema_structure(instance, "copy_schema_demo4");

  instance.execute("DROP SCHEMA copy_schema_demo1");
  instance.execute("DROP SCHEMA copy_schema_demo2");
  instance.execute("DROP SCHEMA copy_schema_demo3");
  instance.execute("DROP SCHEMA copy_schema_demo4");
}
#endif

}  // namespace mysql
}  // namespace mysqlshdk
