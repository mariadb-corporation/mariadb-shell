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

#include <string>
#include <vector>

#include "unittest/gtest_clean.h"

#include "mysqlshdk/libs/utils/utils_file.h"
#include "mysqlshdk/libs/utils/utils_mysql_parsing.h"
#include "mysqlshdk/libs/utils/utils_path.h"
#include "mysqlshdk/libs/utils/utils_string.h"

#include "modules/mrs/core/mrs_parser.h"

extern "C" const char *g_test_home;

namespace mrs {

using namespace ast;

namespace {

template <typename T>
const T &parse_as(const std::string &sql, const Sql_mode &mode = {}) {
  static Statement statement;
  statement = parse_statement(sql, mode);
  const T *value = statement.as<T>();
  if (!value) {
    throw std::logic_error("Unexpected statement type for: " + sql);
  }
  return *value;
}

void expect_parse_error(const std::string &sql, const std::string &message,
                        int line = -1, int column = -1,
                        const Sql_mode &mode = {}) {
  try {
    parse_script(sql, mode);
    FAIL() << "No syntax error for: " << sql;
  } catch (const Parse_error &e) {
    EXPECT_NE(std::string::npos, std::string(e.what()).find(message))
        << sql << "\n" << e.what();
    if (line >= 0) EXPECT_EQ(line, e.line()) << sql;
    if (column >= 0) EXPECT_EQ(column, e.column()) << sql;
  }
}

}  // namespace

TEST(Mrs_parser, prefixes) {
  const auto &prefixes = rest_sql_prefixes();
  EXPECT_EQ(13u, prefixes.size());
  EXPECT_EQ("CONFIGURE REST ", prefixes[0]);
  EXPECT_EQ("LOAD REST ", prefixes.back());
}

TEST(Mrs_parser, empty_script_and_separators) {
  EXPECT_TRUE(parse_script("").empty());
  EXPECT_TRUE(parse_script("  ;; -- comment\n").empty());

  const auto script = parse_script(
      "SHOW REST SERVICES;; SHOW REST METADATA STATUS\n;\nSHOW REST STATUS");
  ASSERT_EQ(3u, script.size());
  EXPECT_TRUE(script[0].is<Show_rest_services>());
  EXPECT_TRUE(script[1].is<Show_rest_metadata_status>());
  EXPECT_TRUE(script[2].is<Show_rest_metadata_status>());
  EXPECT_EQ(1, script[0].line);
  EXPECT_EQ(1, script[1].line);
  EXPECT_EQ(21, script[1].column);
  EXPECT_EQ(3, script[2].line);

  EXPECT_THROW(parse_statement("SHOW REST SERVICES; SHOW REST SERVICES"),
               Parse_error);
}

TEST(Mrs_parser, syntax_errors) {
  expect_parse_error("CREATE REST", "Syntax Error: Syntax error, unexpected",
                     1, 11);
  expect_parse_error("CREATE REST SERVICE", "unexpected end of input", 1, 19);
  expect_parse_error("SHOW REST SERVICES\nFOO", "unexpected identifier", 2, 0);
  expect_parse_error("CREATE REST SERVICE /a ~", "Unexpected input '~'", 1,
                     23);
  // a quoted request path has to start with /
  expect_parse_error("CREATE REST SERVICE `abc`",
                     "Invalid REST request path or wildcard", 1, 20);
  // a string is taken as a developer name awaiting the @
  expect_parse_error("CREATE REST SERVICE 'abc'",
                     "unexpected end of input, expecting \",\" or @", 1, 25);
  // the position is the start of the offending token
  expect_parse_error("CREATE REST SERVICE /a\n   ENABLED FOO", "unexpected",
                     2, 11);
}

TEST(Mrs_parser, configure_rest_metadata) {
  {
    const auto &s = parse_as<Configure_rest_metadata>("CONFIGURE REST METADATA");
    EXPECT_FALSE(s.enabled.has_value());
    EXPECT_FALSE(s.options.has_value());
    EXPECT_FALSE(s.update_if_available);
  }
  {
    const auto &s = parse_as<Configure_rest_metadata>(
        "CONFIGURE REST METADATA ENABLED UPDATE IF AVAILABLE "
        "MERGE OPTIONS {\"a\": [1, 2.5, -3, true, null, \"x\"], \"b\": {}}");
    EXPECT_TRUE(*s.enabled);
    EXPECT_TRUE(s.update_if_available);
    EXPECT_TRUE(s.options->merge);
    EXPECT_EQ("{\"a\":[1,2.5,-3,true,null,\"x\"],\"b\":{}}", s.options->value);
  }
  {
    const auto &s =
        parse_as<Configure_rest_metadata>("CONFIGURE REST METADATA DISABLED UPDATE");
    EXPECT_FALSE(*s.enabled);
    EXPECT_TRUE(s.update_if_available);
  }
}

TEST(Mrs_parser, create_rest_service) {
  {
    const auto &s = parse_as<Create_rest_service>("CREATE REST SERVICE /myService");
    EXPECT_FALSE(s.flags.or_replace);
    EXPECT_FALSE(s.flags.if_not_exists);
    EXPECT_EQ("/myService", s.path.path);
    EXPECT_TRUE(s.path.developers.empty());
    EXPECT_FALSE(s.options.enabled.has_value());
  }
  {
    const auto &s = parse_as<Create_rest_service>(
        "CREATE OR REPLACE REST SERVICE miguel,'alfredo@oracle.com'@/myTestService");
    EXPECT_TRUE(s.flags.or_replace);
    ASSERT_EQ(2u, s.path.developers.size());
    EXPECT_EQ("miguel", s.path.developers[0]);
    EXPECT_EQ("alfredo@oracle.com", s.path.developers[1]);
    EXPECT_EQ("/myTestService", s.path.path);
  }
  {
    const auto &s = parse_as<Create_rest_service>(
        "CREATE REST SERVICE IF NOT EXISTS /svc ENABLED PUBLISHED PROTOCOL HTTPS "
        "COMMENT \"A simple REST service\" "
        "AUTHENTICATION PATH \"/authentication\" REDIRECTION DEFAULT "
        "VALIDATION DEFAULT PAGE CONTENT 'content' "
        "OPTIONS {\"logging\": {\"exceptions\": true}} METADATA {\"position\": 1} "
        "ADD AUTH APP \"MRS\" IF EXISTS REMOVE AUTH APP `MySQL`");
    EXPECT_TRUE(s.flags.if_not_exists);
    EXPECT_TRUE(*s.options.enabled);
    EXPECT_TRUE(*s.options.published);
    EXPECT_EQ("HTTPS", *s.options.protocol);
    EXPECT_EQ("A simple REST service", *s.options.comments);
    EXPECT_FALSE(s.options.auth_path->is_default);
    EXPECT_EQ("/authentication", s.options.auth_path->text);
    EXPECT_TRUE(s.options.auth_redirection->is_default);
    EXPECT_TRUE(s.options.auth_validation->is_default);
    EXPECT_EQ("content", s.options.auth_page_content->text);
    EXPECT_FALSE(s.options.options->merge);
    EXPECT_EQ("{\"logging\":{\"exceptions\":true}}", s.options.options->value);
    EXPECT_EQ("{\"position\":1}", *s.options.metadata);
    ASSERT_EQ(1u, s.options.add_auth_apps.size());
    EXPECT_EQ("MRS", s.options.add_auth_apps[0].name);
    EXPECT_TRUE(s.options.add_auth_apps[0].if_exists);
    ASSERT_EQ(1u, s.options.remove_auth_apps.size());
    EXPECT_EQ("MySQL", s.options.remove_auth_apps[0].name);
    EXPECT_FALSE(s.options.remove_auth_apps[0].if_exists);
  }
}

TEST(Mrs_parser, json_values_keep_their_source) {
  // JSON strings keep escapes as written, they are not SQL-unescaped.
  const auto &s = parse_as<Create_rest_service>(
      R"(CREATE REST SERVICE /svc OPTIONS {"a": "x\nyé", "b": [{"c": 1.5e3}]})");
  EXPECT_EQ(R"({"a":"x\nyé","b":[{"c":1.5e3}]})", s.options.options->value);
}

TEST(Mrs_parser, alter_clone_drop_rest_service) {
  {
    const auto &s = parse_as<Alter_rest_service>(
        "ALTER REST SERVICE mike@/myTestService NEW REQUEST PATH "
        "mike,alfredo@/myTestService2 DISABLED MERGE OPTIONS {\"test\": null}");
    ASSERT_EQ(1u, s.path.developers.size());
    EXPECT_EQ("mike", s.path.developers[0]);
    ASSERT_TRUE(s.new_path.has_value());
    EXPECT_EQ("/myTestService2", s.new_path->path);
    ASSERT_EQ(2u, s.new_path->developers.size());
    EXPECT_FALSE(*s.options.enabled);
    EXPECT_TRUE(s.options.options->merge);
    EXPECT_EQ("{\"test\":null}", s.options.options->value);
  }
  {
    const auto &s = parse_as<Alter_rest_service>(
        "ALTER REST SERVICE /svc AUTHENTICATION PATH DEFAULT REDIRECTION '/r'");
    EXPECT_FALSE(s.new_path.has_value());
    EXPECT_TRUE(s.options.auth_path->is_default);
    EXPECT_EQ("/r", s.options.auth_redirection->text);
  }
  {
    const auto &s = parse_as<Clone_rest_service>(
        "CLONE REST SERVICE mike@/myTestService NEW REQUEST PATH /myClonedService");
    EXPECT_EQ("/myTestService", s.path.path);
    EXPECT_EQ("/myClonedService", s.new_path.path);
  }
  {
    const auto &s = parse_as<Drop_rest_service>("DROP REST SERVICE IF EXISTS /svc");
    EXPECT_TRUE(s.if_exists);
    EXPECT_EQ("/svc", s.path.path);
  }
}

TEST(Mrs_parser, rest_schema_statements) {
  {
    const auto &s = parse_as<Create_rest_schema>(
        "CREATE REST SCHEMA ON SERVICE /myTestService FROM `sakila` ENABLED "
        "ITEMS PER PAGE 25 COMMENT \"The sakila schema\" METADATA {\"position\": 1}");
    EXPECT_FALSE(s.schema_path.has_value());
    EXPECT_EQ("/myTestService", s.service->path);
    EXPECT_EQ("sakila", s.schema_name);
    EXPECT_EQ(Enabled_state::enabled, *s.options.enabled);
    EXPECT_EQ(25, *s.options.items_per_page);
    EXPECT_EQ("The sakila schema", *s.options.comments);
  }
  {
    const auto &s = parse_as<Create_rest_schema>(
        "CREATE OR REPLACE REST DATABASE /sakila FROM sakila PRIVATE "
        "AUTHENTICATION NOT REQUIRED");
    EXPECT_TRUE(s.flags.or_replace);
    EXPECT_EQ("/sakila", *s.schema_path);
    EXPECT_FALSE(s.service.has_value());
    EXPECT_EQ(Enabled_state::private_, *s.options.enabled);
    EXPECT_FALSE(*s.options.requires_auth);
  }
  {
    const auto &s = parse_as<Alter_rest_schema>(
        "ALTER REST SCHEMA /sakila ON SERVICE /svc NEW REQUEST PATH /sakila2 "
        "FROM `other` DISABLED AUTHENTICATION REQUIRED");
    EXPECT_EQ("/sakila", *s.schema_path);
    EXPECT_EQ("/svc", s.service->path);
    EXPECT_EQ("/sakila2", *s.new_path);
    EXPECT_EQ("other", *s.schema_name);
    EXPECT_EQ(Enabled_state::disabled, *s.options.enabled);
    EXPECT_TRUE(*s.options.requires_auth);
  }
  {
    const auto &s = parse_as<Drop_rest_schema>(
        "DROP REST SCHEMA IF EXISTS /sakila FROM /myTestService");
    EXPECT_TRUE(s.if_exists);
    EXPECT_EQ("/sakila", s.schema_path);
    EXPECT_EQ("/myTestService", s.service->path);
  }
  {
    const auto &s = parse_as<Show_rest_schemas>("SHOW REST SCHEMAS FROM SERVICE /svc");
    EXPECT_EQ("/svc", s.service->path);
  }
  {
    const auto &s = parse_as<Show_create_rest_schema>(
        "SHOW CREATE REST SCHEMA /test ON SERVICE /myTest");
    EXPECT_EQ("/test", *s.schema_path);
    EXPECT_EQ("/myTest", s.service->path);
  }
}

TEST(Mrs_parser, create_rest_view_with_data_mapping) {
  const auto &s = parse_as<Create_rest_view>(R"sql(
    CREATE OR REPLACE REST DATA MAPPING VIEW /city
    ON SERVICE /myTestService SCHEMA /sakila
    AS sakila.city CLASS MyServiceSakilaCity @INSERT @UPDATE @DELETE @NOCHECK {
        cityId: city_id @KEY @SORTABLE @NOCHECK,
        city: city,
        address: sakila.address @UNNEST {
            address: address
        },
        country: sakila.country @INSERT @NOUPDATE {
            countryId: country_id @SORTABLE,
            country: country
        },
        info: info JSON SCHEMA {"type": "object"},
        name: name @DATATYPE("varchar(20)")
    }
    ENABLED AUTHENTICATION REQUIRED ITEMS PER PAGE 25
    COMMENT "The sakila.city table" MEDIA TYPE "Test" FORMAT FEED
    AUTHENTICATION PROCEDURE sakila.auth_proc)sql");
  EXPECT_TRUE(s.flags.or_replace);
  EXPECT_EQ("/city", s.path);
  EXPECT_EQ("/myTestService", s.on->service->path);
  EXPECT_EQ("/sakila", s.on->schema_path);
  EXPECT_EQ("sakila", *s.object.schema);
  EXPECT_EQ("city", s.object.name);
  EXPECT_EQ("MyServiceSakilaCity", *s.class_name);
  EXPECT_TRUE(s.crud.allow_insert());
  EXPECT_TRUE(s.crud.allow_update());
  EXPECT_TRUE(s.crud.allow_delete());
  EXPECT_TRUE(s.crud.is_no_check());

  ASSERT_TRUE(s.mapping.has_value());
  const auto &fields = s.mapping->fields;
  ASSERT_EQ(6u, fields.size());

  EXPECT_EQ("cityId", fields[0].name);
  EXPECT_EQ("city_id", fields[0].source.name);
  EXPECT_FALSE(fields[0].source.schema.has_value());
  EXPECT_TRUE(fields[0].key);
  EXPECT_TRUE(fields[0].sortable);
  EXPECT_TRUE(fields[0].no_check);
  EXPECT_FALSE(fields[0].crud.any());

  EXPECT_EQ("city", fields[1].name);
  EXPECT_FALSE(fields[1].key);

  EXPECT_EQ("address", fields[2].name);
  EXPECT_EQ("sakila", *fields[2].source.schema);
  EXPECT_EQ("address", fields[2].source.name);
  EXPECT_TRUE(fields[2].unnest);
  ASSERT_EQ(1u, fields[2].nested.size());
  ASSERT_EQ(1u, fields[2].nested[0].fields.size());
  EXPECT_EQ("address", fields[2].nested[0].fields[0].name);

  EXPECT_EQ("country", fields[3].name);
  EXPECT_TRUE(fields[3].crud.insert);
  EXPECT_TRUE(fields[3].crud.no_update);
  EXPECT_FALSE(fields[3].crud.allow_update());
  ASSERT_EQ(1u, fields[3].nested.size());
  EXPECT_EQ(2u, fields[3].nested[0].fields.size());
  EXPECT_TRUE(fields[3].nested[0].fields[0].sortable);

  EXPECT_EQ("info", fields[4].name);
  EXPECT_EQ("{\"type\":\"object\"}", *fields[4].json_schema);

  EXPECT_EQ("name", fields[5].name);
  EXPECT_EQ("name", fields[5].source.name);
  EXPECT_EQ("varchar(20)", *fields[5].datatype);

  EXPECT_EQ(Enabled_state::enabled, *s.options.enabled);
  EXPECT_TRUE(*s.options.requires_auth);
  EXPECT_EQ(25, *s.options.items_per_page);
  EXPECT_EQ("The sakila.city table", *s.options.comments);
  EXPECT_EQ("Test", *s.options.media_type);
  EXPECT_FALSE(s.options.media_type_autodetect);
  EXPECT_EQ(Result_format::feed, *s.options.format);
  EXPECT_EQ("sakila", *s.options.auth_procedure->schema);
  EXPECT_EQ("auth_proc", s.options.auth_procedure->name);
}

TEST(Mrs_parser, create_rest_view_minimal_and_keywords_as_field_names) {
  {
    const auto &s = parse_as<Create_rest_view>(
        "CREATE REST VIEW IF NOT EXISTS /actor AS `sakila`.`actor`");
    EXPECT_TRUE(s.flags.if_not_exists);
    EXPECT_FALSE(s.on.has_value());
    EXPECT_FALSE(s.class_name.has_value());
    EXPECT_FALSE(s.crud.any());
    EXPECT_FALSE(s.mapping.has_value());
    EXPECT_FALSE(s.options.enabled.has_value());
  }
  {
    // keywords are accepted as field names and column names
    const auto &s = parse_as<Create_rest_view>(
        "CREATE REST DATA MAPPING VIEW /t AS t { name: name, type: type, "
        "\"quoted key\": data, user: `user` } MEDIA TYPE AUTODETECT FORMAT MEDIA");
    ASSERT_TRUE(s.mapping.has_value());
    ASSERT_EQ(4u, s.mapping->fields.size());
    EXPECT_EQ("name", s.mapping->fields[0].name);
    EXPECT_EQ("name", s.mapping->fields[0].source.name);
    EXPECT_EQ("type", s.mapping->fields[1].source.name);
    EXPECT_EQ("quoted key", s.mapping->fields[2].name);
    EXPECT_EQ("data", s.mapping->fields[2].source.name);
    EXPECT_EQ("user", s.mapping->fields[3].source.name);
    EXPECT_TRUE(s.options.media_type_autodetect);
    EXPECT_EQ(Result_format::media, *s.options.format);
  }
  {
    // an empty mapping object
    const auto &s = parse_as<Create_rest_view>("CREATE REST VIEW /t AS t CLASS C {}");
    ASSERT_TRUE(s.mapping.has_value());
    EXPECT_TRUE(s.mapping->fields.empty());
  }
}

TEST(Mrs_parser, alter_and_drop_rest_view) {
  {
    const auto &s = parse_as<Alter_rest_view>(
        "ALTER REST DATA MAPPING VIEW /actorInfo ON SERVICE /svc SCHEMA /sakila "
        "NEW REQUEST PATH /actorInfo2 CLASS MyClass @UPDATE { actorId: actor_id } "
        "DISABLED FORMAT ITEM");
    EXPECT_EQ("/actorInfo", s.path);
    EXPECT_EQ("/svc", s.on->service->path);
    EXPECT_EQ("/actorInfo2", *s.new_path);
    ASSERT_TRUE(s.class_def.has_value());
    EXPECT_EQ("MyClass", s.class_def->name);
    EXPECT_TRUE(s.class_def->crud.allow_update());
    ASSERT_TRUE(s.class_def->mapping.has_value());
    EXPECT_EQ(1u, s.class_def->mapping->fields.size());
    EXPECT_EQ(Enabled_state::disabled, *s.options.enabled);
    EXPECT_EQ(Result_format::item, *s.options.format);
  }
  {
    const auto &s = parse_as<Alter_rest_view>("ALTER REST VIEW /city CLASS C2");
    ASSERT_TRUE(s.class_def.has_value());
    EXPECT_EQ("C2", s.class_def->name);
    EXPECT_FALSE(s.class_def->mapping.has_value());
    EXPECT_FALSE(s.new_path.has_value());
  }
  {
    const auto &s = parse_as<Drop_rest_db_object>(
        "DROP REST DATA MAPPING VIEW IF EXISTS /country FROM SERVICE /svc SCHEMA /sakila");
    EXPECT_EQ(Db_object_kind::view, s.kind);
    EXPECT_TRUE(s.if_exists);
    EXPECT_EQ("/country", s.path);
    EXPECT_EQ("/sakila", s.from->schema_path);
  }
  {
    const auto &s = parse_as<Drop_rest_db_object>("DROP REST VIEW /actor FROM SCHEMA /s");
    EXPECT_FALSE(s.if_exists);
    EXPECT_FALSE(s.from->service.has_value());
    EXPECT_EQ("/s", s.from->schema_path);
  }
}

TEST(Mrs_parser, rest_procedure_and_function) {
  {
    const auto &s = parse_as<Create_rest_routine>(R"sql(
      CREATE OR REPLACE REST PROCEDURE /filmInStock
      AS sakila.film_in_stock FORCE
      PARAMETERS MyServiceSakilaFilmInStockParams {
          pFilmId: p_film_id @IN,
          pStoreId: p_store_id @INOUT,
          pFilmCount: p_film_count @OUT
      }
      RESULT MyServiceSakilaFilmInStock {
          inventoryId: inventory_id @DATATYPE("int")
      }
      RESULT {
          inventoryId2: inventory_id @DATATYPE(int)
      })sql");
    EXPECT_EQ(Create_rest_routine::Kind::procedure, s.kind);
    EXPECT_TRUE(s.force);
    EXPECT_EQ("/filmInStock", s.path);
    EXPECT_EQ("film_in_stock", s.object.name);
    ASSERT_TRUE(s.parameters.has_value());
    EXPECT_EQ("MyServiceSakilaFilmInStockParams", *s.parameters->name);
    ASSERT_EQ(3u, s.parameters->object.fields.size());
    EXPECT_EQ(Graphql_field::Mode::in, s.parameters->object.fields[0].mode);
    EXPECT_EQ(Graphql_field::Mode::inout, s.parameters->object.fields[1].mode);
    EXPECT_EQ(Graphql_field::Mode::out, s.parameters->object.fields[2].mode);
    ASSERT_EQ(2u, s.results.size());
    EXPECT_EQ("MyServiceSakilaFilmInStock", *s.results[0].name);
    EXPECT_EQ("int", *s.results[0].object.fields[0].datatype);
    EXPECT_FALSE(s.results[1].name.has_value());
    EXPECT_EQ("int", *s.results[1].object.fields[0].datatype);
  }
  {
    const auto &s = parse_as<Create_rest_routine>(
        "CREATE REST FUNCTION IF NOT EXISTS /actorFunc ON SERVICE /svc SCHEMA /s "
        "AS `sakila`.`actor` PARAMETERS { s: s @IN } RESULT R { result: result } "
        "DISABLED");
    EXPECT_EQ(Create_rest_routine::Kind::function, s.kind);
    EXPECT_TRUE(s.flags.if_not_exists);
    EXPECT_FALSE(s.force);
    EXPECT_FALSE(s.parameters->name.has_value());
    ASSERT_EQ(1u, s.results.size());
    EXPECT_EQ("R", *s.results[0].name);
    EXPECT_EQ(Enabled_state::disabled, *s.options.enabled);
  }
  {
    // a function has at most one result
    expect_parse_error(
        "CREATE REST FUNCTION /f AS f RESULT {a: a} RESULT {b: b}", "unexpected");
  }
  {
    const auto &s = parse_as<Alter_rest_routine>(
        "ALTER REST PROCEDURE /filmInStock ON SERVICE /svc SCHEMA /sakila "
        "NEW REQUEST PATH /filmInStockUpdated PARAMETERS P { pFilmId: p_film_id @IN } "
        "RESULT R1 { a: a } RESULT R2 { b: b } MERGE OPTIONS {\"test\": 1}");
    EXPECT_EQ(Create_rest_routine::Kind::procedure, s.kind);
    EXPECT_EQ("/filmInStockUpdated", *s.new_path);
    EXPECT_EQ("P", *s.parameters->name);
    EXPECT_EQ(2u, s.results.size());
    EXPECT_TRUE(s.options.options->merge);
  }
  {
    const auto &s = parse_as<Alter_rest_routine>(
        "ALTER REST FUNCTION /actorFunc NEW REQUEST PATH /actorFuncNew");
    EXPECT_EQ(Create_rest_routine::Kind::function, s.kind);
    EXPECT_FALSE(s.parameters.has_value());
    EXPECT_TRUE(s.results.empty());
  }
  {
    const auto &s = parse_as<Drop_rest_db_object>("DROP REST PROCEDURE IF EXISTS /p");
    EXPECT_EQ(Db_object_kind::procedure, s.kind);
    EXPECT_TRUE(s.if_exists);
  }
  {
    const auto &s = parse_as<Drop_rest_db_object>("DROP REST FUNCTION /f");
    EXPECT_EQ(Db_object_kind::function, s.kind);
  }
  {
    const auto &s = parse_as<Show_create_rest_db_object>(
        "SHOW CREATE REST FUNCTION /actorFunc ON SERVICE /myTest SCHEMA /test");
    EXPECT_EQ(Db_object_kind::function, s.kind);
    EXPECT_EQ("/actorFunc", s.path);
    EXPECT_EQ("/myTest", s.on->service->path);
  }
  {
    const auto &s = parse_as<Show_rest_db_objects>("SHOW REST PROCEDURES");
    EXPECT_EQ(Db_object_kind::procedure, s.kind);
    EXPECT_FALSE(s.on.has_value());
  }
  {
    const auto &s = parse_as<Show_rest_db_objects>(
        "SHOW REST DATA MAPPING VIEWS FROM SERVICE /myTestService SCHEMA /sakila");
    EXPECT_EQ(Db_object_kind::view, s.kind);
    EXPECT_EQ("/sakila", s.on->schema_path);
  }
}

TEST(Mrs_parser, content_sets_and_files) {
  {
    const auto &s = parse_as<Create_rest_content_set>(
        "CREATE REST CONTENT SET /testContent ON SERVICE /myTestService "
        "FROM \"./grammar/test\" IGNORE \"*.txt\" LOAD TYPESCRIPT SCRIPTS PRIVATE");
    EXPECT_EQ("/testContent", s.path);
    EXPECT_EQ("/myTestService", s.service->path);
    EXPECT_EQ("./grammar/test", *s.directory);
    EXPECT_EQ("*.txt", *s.options.ignore_list);
    EXPECT_TRUE(s.options.load_scripts);
    EXPECT_TRUE(s.options.typescript);
    EXPECT_EQ(Enabled_state::private_, *s.options.enabled);
  }
  {
    const auto &s = parse_as<Create_rest_content_set>(
        "CREATE OR REPLACE REST CONTENT SET /mySet ON /svc LOAD SCRIPTS");
    EXPECT_TRUE(s.flags.or_replace);
    EXPECT_FALSE(s.directory.has_value());
    EXPECT_TRUE(s.options.load_scripts);
    EXPECT_FALSE(s.options.typescript);
  }
  {
    const auto &s = parse_as<Alter_rest_content_set>(
        "ALTER REST CONTENT SET /mySet ON SERVICE /svc NEW REQUEST PATH /mySet2 "
        "AUTHENTICATION REQUIRED COMMENT 'c'");
    EXPECT_EQ("/mySet2", *s.new_path);
    EXPECT_TRUE(*s.options.requires_auth);
    EXPECT_EQ("c", *s.options.comments);
  }
  {
    const auto &s = parse_as<Create_rest_content_file>(
        "CREATE REST CONTENT FILE `/binaryFile1` ON SERVICE "
        "miguel,'alfredo@oracle.com'@/myTestService CONTENT SET /mySet "
        "BINARY CONTENT \"AAEC\" ENABLED");
    EXPECT_EQ("/binaryFile1", s.path);
    EXPECT_EQ(2u, s.service->developers.size());
    EXPECT_EQ("/mySet", s.content_set_path);
    EXPECT_TRUE(s.binary);
    EXPECT_EQ("AAEC", *s.content);
    EXPECT_FALSE(s.from_file.has_value());
    EXPECT_EQ(Enabled_state::enabled, *s.options.enabled);
  }
  {
    const auto &s = parse_as<Create_rest_content_file>(
        "CREATE OR REPLACE REST CONTENT FILE /f ON CONTENT SET /mySet "
        "FROM \"grammar/test/binary_test_file\"");
    EXPECT_FALSE(s.service.has_value());
    EXPECT_EQ("grammar/test/binary_test_file", *s.from_file);
    EXPECT_FALSE(s.binary);
    EXPECT_FALSE(s.content.has_value());
  }
  {
    const auto &s = parse_as<Create_rest_content_file>(
        "CREATE REST CONTENT FILE /f ON /svc CONTENT SET /mySet CONTENT 'text'");
    EXPECT_EQ("/svc", s.service->path);
    EXPECT_EQ("text", *s.content);
  }
  {
    const auto &s = parse_as<Drop_rest_content_file>(
        "DROP REST CONTENT FILE IF EXISTS `/textFile` FROM SERVICE /svc CONTENT SET /mySet");
    EXPECT_TRUE(s.if_exists);
    EXPECT_EQ("/textFile", s.path);
    EXPECT_EQ("/svc", s.service->path);
    EXPECT_EQ("/mySet", s.content_set_path);
  }
  {
    const auto &s = parse_as<Drop_rest_content_set>(
        "DROP REST CONTENT SET /mySet FROM SERVICE /svc");
    EXPECT_EQ("/svc", s.service->path);
  }
  {
    const auto &s = parse_as<Show_rest_content_files>(
        "SHOW REST CONTENT FILES ON SERVICE /svc CONTENT SET /mySet");
    EXPECT_EQ("/svc", s.service->path);
    EXPECT_EQ("/mySet", s.content_set_path);
  }
  {
    const auto &s = parse_as<Show_rest_content_sets>("SHOW REST CONTENT SETS");
    EXPECT_FALSE(s.service.has_value());
  }
  {
    const auto &s = parse_as<Show_create_rest_content_file>(
        "SHOW CREATE REST CONTENT FILE /f FROM CONTENT SET /cs");
    EXPECT_EQ("/f", s.path);
    EXPECT_FALSE(s.service.has_value());
    EXPECT_EQ("/cs", s.content_set_path);
  }
  {
    const auto &s = parse_as<Show_create_rest_content_set>(
        "SHOW CREATE REST CONTENT SET /cs ON SERVICE /svc");
    EXPECT_EQ("/svc", s.service->path);
  }
}

TEST(Mrs_parser, auth_apps) {
  {
    const auto &s = parse_as<Create_rest_auth_app>(
        "CREATE OR REPLACE REST AUTH APP \"MRS\" VENDOR MRS");
    EXPECT_EQ("MRS", s.name);
    EXPECT_EQ("MRS", s.vendor);
  }
  {
    const auto &s = parse_as<Create_rest_auth_app>(
        "CREATE REST AUTHENTICATION APP IF NOT EXISTS 'MySQL' VENDOR MySQL "
        "ALLOW NEW USERS TO REGISTER DEFAULT ROLE \"Full Access\" DISABLED "
        "COMMENT 'c' APP ID 'id' CLIENT SECRET 'secret' URL 'https://x'");
    EXPECT_TRUE(s.flags.if_not_exists);
    EXPECT_EQ("MySQL", s.name);
    EXPECT_EQ("MySQL Internal", s.vendor);
    EXPECT_TRUE(*s.options.allow_new_users);
    EXPECT_EQ("Full Access", *s.options.default_role);
    EXPECT_FALSE(*s.options.enabled);
    EXPECT_EQ("c", *s.options.comments);
    EXPECT_EQ("id", *s.options.app_id);
    EXPECT_EQ("secret", *s.options.app_secret);
    EXPECT_EQ("https://x", *s.options.url);
  }
  {
    const auto &s = parse_as<Create_rest_auth_app>(
        "CREATE REST AUTH APP oauth VENDOR 'Facebook' DO NOT ALLOW NEW USERS");
    EXPECT_EQ("Facebook", s.vendor);
    EXPECT_FALSE(*s.options.allow_new_users);
  }
  {
    const auto &s = parse_as<Alter_rest_auth_app>(
        "ALTER REST AUTH APP \"MRS\" NEW NAME \"MRS2\" ENABLED");
    EXPECT_EQ("MRS", s.name);
    EXPECT_EQ("MRS2", *s.new_name);
    EXPECT_TRUE(*s.options.enabled);
  }
  {
    const auto &s = parse_as<Drop_rest_auth_app>("DROP REST AUTH APP IF EXISTS \"MRS\"");
    EXPECT_TRUE(s.if_exists);
    EXPECT_EQ("MRS", s.name);
  }
  {
    const auto &s = parse_as<Show_rest_auth_apps>(
        "SHOW REST AUTH APPS FROM SERVICE /myTestService");
    EXPECT_EQ("/myTestService", s.service->path);
  }
  {
    const auto &s = parse_as<Show_create_rest_auth_app>("SHOW CREATE REST AUTH APP `MRS`");
    EXPECT_EQ("MRS", s.name);
  }
  EXPECT_TRUE(
      parse_statement("SHOW REST AUTH VENDORS").is<Show_rest_auth_vendors>());
  {
    const auto &s = parse_as<Show_rest_services>(
        "SHOW REST SERVICES FOR AUTH APP \"MRS\"");
    EXPECT_EQ("MRS", *s.auth_app);
  }
  EXPECT_FALSE(
      parse_as<Show_rest_services>("SHOW REST SERVICES").auth_app.has_value());
}

// Keywords such as MRS or MYSQL have to be quoted when used as names, as in
// the ANTLR grammar of the Python plugin.
TEST(Mrs_parser, keywords_as_names_need_quotes) {
  expect_parse_error("CREATE REST USER mike@MRS", "unexpected MRS");
  expect_parse_error("CREATE REST AUTH APP MySQL VENDOR MRS", "unexpected MYSQL");
  EXPECT_NO_THROW(parse_statement("CREATE REST USER mike@`MRS`"));
  EXPECT_NO_THROW(parse_statement("CREATE REST AUTH APP 'MySQL' VENDOR MRS"));
}

TEST(Mrs_parser, users) {
  {
    const auto &s = parse_as<Show_rest_users>("SHOW REST USERS");
    EXPECT_FALSE(s.service.has_value());
    EXPECT_FALSE(s.auth_app.has_value());
  }
  {
    const auto &s = parse_as<Show_rest_users>(
        "SHOW REST USERS ON SERVICE /svc FOR AUTH APP 'MRS'");
    EXPECT_EQ("/svc", s.service->path);
    EXPECT_EQ("MRS", *s.auth_app);
  }
  {
    const auto &s =
        parse_as<Show_rest_users>("SHOW REST USERS FROM /svc");
    EXPECT_EQ("/svc", s.service->path);
    EXPECT_FALSE(s.auth_app.has_value());
  }
  EXPECT_EQ("myApp", *parse_as<Show_rest_users>(
                          "SHOW REST USERS FOR AUTH APP myApp").auth_app);
  {
    const auto &s = parse_as<Create_rest_user>(
        "CREATE REST USER \"boss\"@\"MRS\" IDENTIFIED BY \"MySQLR0cks!\" ACCOUNT LOCK "
        "OPTIONS {\"email\": \"boss@example.com\"} APP OPTIONS {\"myoption\": 12345}");
    EXPECT_EQ("boss", s.name);
    EXPECT_EQ("MRS", s.auth_app);
    EXPECT_EQ("MySQLR0cks!", *s.password);
    EXPECT_TRUE(*s.options.account_locked);
    EXPECT_EQ("{\"email\":\"boss@example.com\"}", s.options.options->value);
    EXPECT_EQ("{\"myoption\":12345}", *s.options.app_options);
  }
  {
    const auto &s = parse_as<Create_rest_user>(
        "CREATE OR REPLACE REST USER mike@`MRS` ACCOUNT UNLOCK");
    EXPECT_TRUE(s.flags.or_replace);
    EXPECT_EQ("mike", s.name);
    EXPECT_EQ("MRS", s.auth_app);
    EXPECT_FALSE(s.password.has_value());
    EXPECT_FALSE(*s.options.account_locked);
  }
  {
    const auto &s = parse_as<Alter_rest_user>(
        "ALTER REST USER \"mike\"@\"MRS\" IDENTIFIED BY 'new'");
    EXPECT_EQ("new", *s.password);
    EXPECT_FALSE(s.options.account_locked.has_value());
  }
  {
    const auto &s = parse_as<Drop_rest_user>("DROP REST USER IF EXISTS \"mike\"@\"MRS\"");
    EXPECT_TRUE(s.if_exists);
    EXPECT_EQ("mike", s.name);
    EXPECT_EQ("MRS", s.auth_app);
  }
  {
    const auto &s = parse_as<Show_create_rest_user>("SHOW CREATE REST USER mike@'MRS'");
    EXPECT_EQ("mike", s.name);
    EXPECT_EQ("MRS", s.auth_app);
  }
}

TEST(Mrs_parser, roles_grants_and_revokes) {
  {
    const auto &s = parse_as<Create_rest_role>(
        "CREATE REST ROLE \"role3\" EXTENDS \"role1\" ON SERVICE /myTestService "
        "COMMENT \"A comment here\" OPTIONS {\"option1\":1}");
    EXPECT_EQ("role3", s.name);
    EXPECT_EQ("role1", *s.extends);
    ASSERT_TRUE(s.on.has_value());
    EXPECT_FALSE(s.on->any_service);
    EXPECT_EQ("/myTestService", s.on->service->path);
    EXPECT_EQ("A comment here", *s.options.comments);
    EXPECT_EQ("{\"option1\":1}", s.options.options->value);
  }
  {
    const auto &s = parse_as<Create_rest_role>("CREATE OR REPLACE REST ROLE r ON ANY SERVICE");
    EXPECT_TRUE(s.on->any_service);
    EXPECT_FALSE(s.on->service.has_value());
  }
  {
    const auto &s = parse_as<Create_rest_role>("CREATE REST ROLE IF NOT EXISTS r ON /svc");
    EXPECT_TRUE(s.flags.if_not_exists);
    EXPECT_EQ("/svc", s.on->service->path);
  }
  {
    const auto &s = parse_as<Drop_rest_role>(
        "DROP REST ROLE IF EXISTS \"role3\" ON SERVICE /myTestService");
    EXPECT_TRUE(s.if_exists);
    EXPECT_EQ("role3", s.name);
    EXPECT_EQ("/myTestService", s.on->service->path);
  }
  {
    const auto &s = parse_as<Rest_privilege_statement>(
        "GRANT REST CREATE, READ, UPDATE, DELETE ON SERVICE /myTestService "
        "SCHEMA /sakila OBJECT /country TO \"role2\"");
    EXPECT_FALSE(s.revoke);
    ASSERT_EQ(4u, s.privileges.size());
    EXPECT_EQ(Privilege::create, s.privileges[0]);
    EXPECT_EQ(Privilege::del, s.privileges[3]);
    EXPECT_EQ("/myTestService", *s.service_pattern);
    EXPECT_EQ("/sakila", *s.schema_pattern);
    EXPECT_EQ("/country", *s.object_pattern);
    EXPECT_EQ("role2", s.role);
    EXPECT_FALSE(s.role_service.has_value());
  }
  {
    const auto &s = parse_as<Rest_privilege_statement>(
        "GRANT REST READ ON SCHEMA /sakila OBJECT /actor TO \"role2\" ON ANY SERVICE");
    EXPECT_FALSE(s.service_pattern.has_value());
    EXPECT_EQ("/sakila", *s.schema_pattern);
    EXPECT_EQ("/actor", *s.object_pattern);
    EXPECT_TRUE(s.role_service->any_service);
  }
  {
    const auto &s = parse_as<Rest_privilege_statement>(
        "GRANT REST READ ON SERVICE `*` TO 'Role1'");
    EXPECT_EQ("*", *s.service_pattern);
    EXPECT_FALSE(s.schema_pattern.has_value());
  }
  {
    const auto &s = parse_as<Rest_privilege_statement>(
        "GRANT REST READ ON SERVICE /svc SCHEMA `` OBJECT `` TO 'Role1'");
    EXPECT_EQ("/svc", *s.service_pattern);
    EXPECT_EQ("", *s.schema_pattern);
    EXPECT_EQ("", *s.object_pattern);
  }
  {
    const auto &s = parse_as<Rest_privilege_statement>("GRANT REST UPDATE TO r");
    EXPECT_FALSE(s.service_pattern.has_value());
    EXPECT_FALSE(s.schema_pattern.has_value());
    EXPECT_FALSE(s.object_pattern.has_value());
  }
  {
    const auto &s = parse_as<Rest_privilege_statement>(
        "REVOKE REST CREATE ON SERVICE /myTestService SCHEMA /sakila FROM \"role1\" ON /svc");
    EXPECT_TRUE(s.revoke);
    EXPECT_EQ(1u, s.privileges.size());
    EXPECT_EQ("/svc", s.role_service->service->path);
  }
  {
    const auto &s = parse_as<Rest_role_statement>(
        "GRANT REST ROLE \"role1\" TO \"mike\"@\"MRS\" COMMENT \"Hello World!?\"");
    EXPECT_FALSE(s.revoke);
    EXPECT_EQ("role1", s.role);
    EXPECT_EQ("mike", s.user);
    EXPECT_EQ("MRS", s.auth_app);
    EXPECT_EQ("Hello World!?", *s.comments);
  }
  {
    const auto &s = parse_as<Rest_role_statement>(
        "REVOKE REST ROLE \"role1\" ON SERVICE /svc FROM \"mike\"@\"MRS\"");
    EXPECT_TRUE(s.revoke);
    EXPECT_EQ("/svc", s.role_service->service->path);
    EXPECT_FALSE(s.comments.has_value());
  }
  {
    const auto &s = parse_as<Show_rest_grants>("SHOW REST GRANTS FOR \"role1\" ON ANY SERVICE");
    EXPECT_EQ("role1", s.role);
    EXPECT_TRUE(s.on->any_service);
  }
  {
    const auto &s = parse_as<Show_rest_roles>("SHOW REST ROLES");
    EXPECT_FALSE(s.on.has_value());
    EXPECT_FALSE(s.user.has_value());
    EXPECT_FALSE(s.auth_app.has_value());
  }
  {
    const auto &s = parse_as<Show_rest_roles>(
        "SHOW REST ROLES FROM SERVICE /svc FOR \"mike\"@\"MRS\"");
    EXPECT_EQ("/svc", s.on->service->path);
    EXPECT_EQ("mike", *s.user);
    EXPECT_EQ("MRS", *s.auth_app);
  }
  {
    const auto &s = parse_as<Show_rest_roles>("SHOW REST ROLES ON ANY SERVICE FOR @'MRS'");
    EXPECT_TRUE(s.on->any_service);
    EXPECT_FALSE(s.user.has_value());
    EXPECT_EQ("MRS", *s.auth_app);
  }
  {
    const auto &s = parse_as<Show_create_rest_role>("SHOW CREATE REST ROLE r ON SERVICE /svc");
    EXPECT_EQ("r", s.name);
    EXPECT_EQ("/svc", s.on->service->path);
  }
}

TEST(Mrs_parser, use_and_show_statements) {
  {
    const auto &s = parse_as<Use_rest>("USE REST SERVICE /myTestService");
    EXPECT_EQ("/myTestService", s.service->path);
    EXPECT_FALSE(s.schema_path.has_value());
  }
  {
    const auto &s = parse_as<Use_rest>("USE REST SCHEMA /sakila");
    EXPECT_FALSE(s.service.has_value());
    EXPECT_EQ("/sakila", *s.schema_path);
  }
  {
    const auto &s = parse_as<Use_rest>("USE REST SERVICE mike@/svc SCHEMA /sakila");
    EXPECT_EQ("/svc", s.service->path);
    EXPECT_EQ(1u, s.service->developers.size());
    EXPECT_EQ("/sakila", *s.schema_path);
  }
  {
    const auto &s = parse_as<Show_create_rest_service>(
        "SHOW CREATE REST SERVICE /myTestService INCLUDING DATABASE ENDPOINTS");
    EXPECT_EQ("/myTestService", s.path->path);
    EXPECT_TRUE(s.include_database_endpoints);
  }
  {
    const auto &s = parse_as<Show_create_rest_service>("SHOW CREATE REST SERVICE");
    EXPECT_FALSE(s.path.has_value());
    EXPECT_FALSE(s.include_database_endpoints);
  }
}

TEST(Mrs_parser, dump_and_load_statements) {
  {
    const auto &s = parse_as<Dump_rest_service>(
        "DUMP REST SERVICE /svc AS SQL SCRIPT INCLUDING DATABASE AND STATIC ENDPOINTS "
        "TO ZIP '/tmp/out.zip'");
    EXPECT_EQ("/svc", s.path.path);
    EXPECT_TRUE(s.endpoints.database);
    EXPECT_TRUE(s.endpoints.static_);
    EXPECT_FALSE(s.endpoints.dynamic);
    EXPECT_TRUE(s.zip);
    EXPECT_EQ("/tmp/out.zip", s.directory);
  }
  {
    const auto &s = parse_as<Dump_rest_service>(
        "DUMP REST SERVICE /svc AS SCRIPT INCLUDING ALL ENDPOINTS TO '/tmp/out'");
    EXPECT_TRUE(s.endpoints.database);
    EXPECT_TRUE(s.endpoints.static_);
    EXPECT_TRUE(s.endpoints.dynamic);
    EXPECT_FALSE(s.zip);
  }
  {
    const auto &s = parse_as<Load_rest_service>(
        "LOAD REST SERVICE AS /newSvc FROM '/tmp/svc.mrs.sql'");
    EXPECT_EQ("/newSvc", s.as_path->path);
    EXPECT_EQ("/tmp/svc.mrs.sql", s.directory);
  }
  {
    const auto &s = parse_as<Load_rest_service>("LOAD REST SERVICE FROM 'x'");
    EXPECT_FALSE(s.as_path.has_value());
  }
  // Projects are dumped and loaded by the mrs_plugin, not by REST SQL
  expect_parse_error(
      "DUMP REST PROJECT 'proj' VERSION '1.0.0' "
      "SERVICE /svc INCLUDING DATABASE ENDPOINTS TO '/tmp/p'",
      "unexpected identifier");
  expect_parse_error("LOAD REST PROJECT FROM '/tmp/p'", "unexpected identifier");
}

TEST(Mrs_parser, output_format) {
  // FORMAT=JSON closes every SHOW CREATE statement, as in EXPLAIN FORMAT=JSON
  // the value is a name or a text in any case
  EXPECT_EQ(Output_format::traditional,
            parse_as<Show_create_rest_service>("SHOW CREATE REST SERVICE /s").format);
  EXPECT_EQ(Output_format::json,
            parse_as<Show_create_rest_service>(
                "SHOW CREATE REST SERVICE /s INCLUDING DATABASE ENDPOINTS FORMAT=JSON")
                .format);
  EXPECT_EQ(Output_format::json,
            parse_as<Show_create_rest_service>("SHOW CREATE REST SERVICE FORMAT = json")
                .format);
  EXPECT_EQ(Output_format::json,
            parse_as<Show_create_rest_schema>(
                "SHOW CREATE REST SCHEMA /db ON SERVICE /s FORMAT='Json'")
                .format);
  EXPECT_EQ(Output_format::traditional,
            parse_as<Show_create_rest_schema>(
                "SHOW CREATE REST SCHEMA /db FORMAT=TRADITIONAL")
                .format);
  for (const char *sql :
       {"SHOW CREATE REST VIEW /v ON SERVICE /s SCHEMA /db FORMAT=JSON",
        "SHOW CREATE REST DATA MAPPING VIEW /v FORMAT=JSON",
        "SHOW CREATE REST PROCEDURE /p FORMAT=JSON",
        "SHOW CREATE REST FUNCTION /f ON SERVICE /s SCHEMA /db FORMAT=JSON"}) {
    EXPECT_EQ(Output_format::json, parse_as<Show_create_rest_db_object>(sql).format)
        << sql;
  }
  EXPECT_EQ(Output_format::json,
            parse_as<Show_create_rest_content_set>(
                "SHOW CREATE REST CONTENT SET /cs ON SERVICE /s FORMAT=JSON")
                .format);
  EXPECT_EQ(Output_format::json,
            parse_as<Show_create_rest_content_file>(
                "SHOW CREATE REST CONTENT FILE /f FROM CONTENT SET /cs FORMAT=JSON")
                .format);
  EXPECT_EQ(Output_format::json,
            parse_as<Show_create_rest_auth_app>(
                "SHOW CREATE REST AUTH APP 'MRS' FORMAT=JSON")
                .format);
  EXPECT_EQ(Output_format::json,
            parse_as<Show_create_rest_role>(
                "SHOW CREATE REST ROLE r ON ANY SERVICE FORMAT=JSON")
                .format);
  EXPECT_EQ(Output_format::json,
            parse_as<Show_create_rest_user>(
                "SHOW CREATE REST USER mike@`MRS` FORMAT=JSON")
                .format);
  expect_parse_error("SHOW CREATE REST SERVICE /s FORMAT=XML",
                     "Unknown REST format name: 'XML'", 1, 35);
  expect_parse_error("SHOW CREATE REST SERVICE /s FORMAT JSON", "unexpected JSON");
  expect_parse_error("SHOW REST SERVICES FORMAT=JSON", "unexpected FORMAT");
}

TEST(Mrs_parser, show_rest_columns) {
  {
    const auto &s = parse_as<Show_rest_columns>("SHOW REST COLUMNS FROM sakila.city");
    EXPECT_EQ(Show_rest_columns::Source::any, s.source);
    EXPECT_EQ("sakila", *s.object.schema);
    EXPECT_EQ("city", s.object.name);
    EXPECT_EQ(Output_format::traditional, s.format);
  }
  {
    const auto &s = parse_as<Show_rest_columns>("SHOW REST COLUMNS IN TABLE city FORMAT=JSON");
    EXPECT_EQ(Show_rest_columns::Source::table, s.source);
    EXPECT_FALSE(s.object.schema.has_value());
    EXPECT_EQ(Output_format::json, s.format);
  }
  EXPECT_EQ(Show_rest_columns::Source::view,
            parse_as<Show_rest_columns>("SHOW REST COLUMNS FROM VIEW db.v").source);
  EXPECT_EQ(Show_rest_columns::Source::procedure,
            parse_as<Show_rest_columns>("SHOW REST COLUMNS FROM PROCEDURE db.p").source);
  EXPECT_EQ(Show_rest_columns::Source::function,
            parse_as<Show_rest_columns>("SHOW REST COLUMNS FROM FUNCTION `db`.`f`").source);
  expect_parse_error("SHOW REST COLUMNS sakila.city", "unexpected identifier");

  // COLUMNS is also a name; TABLE only as a data mapping key
  EXPECT_EQ("columns", parse_as<Create_rest_view>(
                           "CREATE REST VIEW /v ON SERVICE /s SCHEMA /d AS db.columns")
                           .object.name);
  EXPECT_NO_THROW(parse_statement(
      "CREATE REST VIEW /v ON SERVICE /s SCHEMA /d AS db.t { table: table, columns: columns }"));
  expect_parse_error("CREATE REST VIEW /v ON SERVICE /s SCHEMA /d AS db.table",
                     "unexpected TABLE");
}

TEST(Mrs_parser, metadata_status_format) {
  EXPECT_EQ(Output_format::traditional,
            parse_as<Show_rest_metadata_status>("SHOW REST METADATA STATUS").format);
  EXPECT_EQ(Output_format::json,
            parse_as<Show_rest_metadata_status>("SHOW REST METADATA STATUS FORMAT=JSON")
                .format);
  EXPECT_EQ(Output_format::json,
            parse_as<Show_rest_metadata_status>("SHOW REST STATUS FORMAT=JSON").format);
}

TEST(Mrs_parser, daemons) {
  EXPECT_EQ(Output_format::traditional,
            parse_as<Show_rest_daemons>("SHOW REST DAEMONS").format);
  EXPECT_EQ(Output_format::json,
            parse_as<Show_rest_daemons>("SHOW REST DAEMONS FORMAT=JSON").format);
  {
    const auto &s = parse_as<Show_rest_services>("SHOW REST SERVICES FOR DAEMON 3");
    EXPECT_EQ(3, *s.daemon);
    EXPECT_FALSE(s.auth_app.has_value());
  }
  {
    const auto &s = parse_as<Drop_rest_daemon>("DROP REST DAEMON 12");
    EXPECT_EQ(12, s.id);
    EXPECT_FALSE(s.if_exists);
  }
  EXPECT_TRUE(parse_as<Drop_rest_daemon>("DROP REST DAEMON IF EXISTS 1").if_exists);
  expect_parse_error("DROP REST DAEMON myDaemon", "unexpected identifier");
  expect_parse_error("SHOW REST SERVICES FOR DAEMON", "unexpected end of input");
  // DAEMON and DAEMONS are also names
  EXPECT_EQ("daemons", parse_as<Create_rest_view>(
                           "CREATE REST VIEW /v ON SERVICE /s SCHEMA /d AS db.daemons")
                           .object.name);
}

TEST(Mrs_parser, sql_modes) {
  // ANSI_QUOTES: double quoted strings are identifiers, the doubled quote
  // is the escape
  Sql_mode ansi;
  ansi.ansi_quotes = true;
  {
    const auto &s = parse_as<Create_rest_user>(
        R"(CREATE REST USER "a""b"@"MRS" IDENTIFIED BY 'p\'q')", ansi);
    EXPECT_EQ("a\"b", s.name);
    EXPECT_EQ("p'q", *s.password);
  }
  {
    // JSON is unaffected by the SQL mode
    const auto &s = parse_as<Create_rest_service>(
        R"(CREATE REST SERVICE "/svc" OPTIONS {"a": "b"})", ansi);
    EXPECT_EQ("/svc", s.path.path);
    EXPECT_EQ(R"({"a":"b"})", s.options.options->value);
  }

  Sql_mode no_backslash;
  no_backslash.no_backslash_escapes = true;
  {
    const auto &s = parse_as<Create_rest_service>(
        R"(CREATE REST SERVICE /svc COMMENT 'a\nb')", no_backslash);
    EXPECT_EQ("a\\nb", *s.options.comments);
  }
}

// Lexical and quoting rules that follow the ANTLR grammar (MRSLexer.g4 /
// MRSParser.g4) and the server.
TEST(Mrs_parser, antlr_compatible_lexical_rules) {
  Sql_mode ansi;
  ansi.ansi_quotes = true;

  // A double quoted string is an identifier under ANSI_QUOTES ...
  {
    const auto &s = parse_as<Create_rest_view>(
        R"(CREATE REST VIEW /v ON SERVICE /svc SCHEMA /db AS "sakila"."actor")",
        ansi);
    EXPECT_EQ("sakila", *s.object.schema);
    EXPECT_EQ("actor", s.object.name);
  }
  {
    const auto &s = parse_as<Create_rest_schema>(
        R"(CREATE REST SCHEMA /db FROM "sakila")", ansi);
    EXPECT_EQ("sakila", s.schema_name);
  }
  expect_parse_error(R"(CREATE REST SERVICE /svc COMMENT "hello")",
                     "unexpected double quoted string", 1, 33, ansi);
  // ... and a text otherwise
  EXPECT_EQ("hello", *parse_as<Create_rest_service>(
                          R"(CREATE REST SERVICE /svc COMMENT "hello")")
                          .options.comments);
  expect_parse_error(R"(CREATE REST SERVICE "/svc")",
                     "unexpected double quoted string", 1, 20);
  expect_parse_error(R"(CREATE REST SCHEMA /db FROM "sakila")",
                     "unexpected double quoted string");
  // Names that take both accept it in every mode
  EXPECT_EQ("r", parse_as<Create_rest_role>(R"(CREATE REST ROLE "r")").name);
  EXPECT_EQ("r",
            parse_as<Create_rest_role>(R"(CREATE REST ROLE "r")", ansi).name);

  // FILES is a keyword that is also allowed as a name
  EXPECT_TRUE(parse_statement(
                  "SHOW REST CONTENT FILES ON SERVICE /svc CONTENT SET /cs")
                  .is<Show_rest_content_files>());
  EXPECT_EQ("files", parse_as<Create_rest_role>("CREATE REST ROLE files").name);
  EXPECT_EQ("files", parse_as<Create_rest_view>(
                         "CREATE REST VIEW /f ON SERVICE /s SCHEMA /d AS db.files")
                         .object.name);

  // So is VENDORS
  EXPECT_EQ("vendors", parse_as<Create_rest_view>(
                           "CREATE REST VIEW /v ON SERVICE /s SCHEMA /d AS db.vendors")
                           .object.name);

  // A request path segment is an identifier: not digits only, no number
  EXPECT_EQ("/v1/x2", parse_as<Create_rest_service>("CREATE REST SERVICE /v1/x2")
                          .path.path);
  expect_parse_error("CREATE REST SERVICE /1", "Unexpected input '/1'");
  expect_parse_error("CREATE REST SERVICE /v1/2", "Unexpected input '/v1/2'");
  expect_parse_error("CREATE REST SERVICE /1e5", "Unexpected input '/1e5'");

  // Hexadecimal and binary literals are no names
  expect_parse_error("CREATE REST ROLE 0x1F", "Unexpected input '0x1F'");
  expect_parse_error("CREATE REST ROLE 0b01", "Unexpected input '0b01'");
  EXPECT_EQ("0x1Fz", parse_as<Create_rest_role>("CREATE REST ROLE 0x1Fz").name);

  // GRANT and REVOKE need SERVICE when a schema follows
  expect_parse_error("GRANT REST READ ON /svc SCHEMA /db TO r",
                     "unexpected SCHEMA, expecting TO");
  expect_parse_error("REVOKE REST READ ON /svc SCHEMA /db FROM r",
                     "unexpected SCHEMA, expecting FROM");
  EXPECT_EQ("/svc", *parse_as<Rest_privilege_statement>(
                         "GRANT REST READ ON /svc TO r")
                         .service_pattern);

  // Comments: line comments end at \r as well, an unterminated comment and
  // a /*! version comment are errors
  EXPECT_EQ(2u,
            parse_script("SHOW REST SERVICES # c\r;SHOW REST SERVICES").size());
  EXPECT_EQ(2u,
            parse_script("SHOW REST SERVICES -- c\r;SHOW REST SERVICES").size());
  expect_parse_error("SHOW REST SERVICES /* unterminated",
                     "unexpected /, expecting end of input");
  expect_parse_error("SHOW REST SERVICES /*!50000 x */",
                     "unexpected /, expecting end of input");
  EXPECT_EQ(1u, parse_script("SHOW REST SERVICES /* closed */").size());

  // JSON numbers may carry a sign and a decimal part; they are stored as
  // valid JSON
  EXPECT_EQ(R"({"a":0.5,"b":1,"c":-1,"d":-1.5e3})",
            parse_as<Create_rest_service>(
                R"(CREATE REST SERVICE /s OPTIONS {"a": .5, "b": +1, "c": -1, "d": -1.5e3})")
                .options.options->value);

  // Any number of semicolons around the statements
  EXPECT_EQ(1u, parse_script(";SHOW REST SERVICES;;").size());
  EXPECT_TRUE(parse_script(";;").empty());
}

// Every REST statement of the grammar test of the Python plugin parses.
TEST(Mrs_parser, grammar_test_file) {
  const auto path = shcore::path::join_path(g_test_home, "data", "mrs",
                                            "grammar_test.sql");
  ASSERT_TRUE(shcore::is_file(path)) << path;

  const auto statements =
      mysqlshdk::utils::split_sql(shcore::get_text_file(path));
  ASSERT_GT(statements.size(), 150u);

  size_t rest_statements = 0;
  for (const auto &statement : statements) {
    const auto trimmed = shcore::str_strip(statement);
    if (trimmed.empty()) continue;

    bool is_rest = false;
    for (const auto &prefix : rest_sql_prefixes()) {
      if (shcore::str_ibeginswith(trimmed, prefix)) {
        is_rest = true;
        break;
      }
    }
    if (!is_rest) continue;  // plain SQL in the test, e.g. a SELECT

    ++rest_statements;
    try {
      parse_statement(trimmed);
    } catch (const Parse_error &e) {
      ADD_FAILURE() << e.what() << "\n" << trimmed;
    }
  }

  EXPECT_GT(rest_statements, 150u);
}

}  // namespace mrs
