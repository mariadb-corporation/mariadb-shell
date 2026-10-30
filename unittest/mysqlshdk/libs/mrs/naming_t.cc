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

#include "unittest/gtest_clean.h"

#include "mysqlshdk/libs/mrs/metadata_rest_objects.h"

namespace mrs {
namespace metadata {

// the field names the VS Code extension's REST object dialog gives the same
// columns and parameters
TEST(Mrs_naming, name_to_camel_case) {
  EXPECT_EQ("firstName", name_to_camel_case("first_name"));
  EXPECT_EQ("firstName", name_to_camel_case("First Name"));
  EXPECT_EQ("firstName", name_to_camel_case("first-name"));
  EXPECT_EQ("kebabCaseX", name_to_camel_case("kebab-case-x"));
  EXPECT_EQ("aB", name_to_camel_case("__a__b__"));
  EXPECT_EQ("city", name_to_camel_case("city"));
  EXPECT_EQ("lastUpdate", name_to_camel_case("lastUpdate"));
  EXPECT_EQ("lastUpdate", name_to_camel_case("Last_Update"));
  EXPECT_EQ("user", name_to_camel_case("USER"));
  EXPECT_EQ("currentConnections", name_to_camel_case("CURRENT_CONNECTIONS"));
  EXPECT_EQ("col1", name_to_camel_case("COL1"));
  EXPECT_EQ("cityId", name_to_camel_case("city_ID"));
  EXPECT_EQ("1stPlace", name_to_camel_case("1st_place"));
  // nothing left: the caller names such a field
  EXPECT_EQ("", name_to_camel_case(""));
  EXPECT_EQ("", name_to_camel_case("$"));
  EXPECT_EQ("", name_to_camel_case("--"));
  // UTF-8 is kept; the extension folds accents to ASCII (grosse), the shell
  // does not, and folds only the ASCII letters of a word in capitals
  EXPECT_EQ("größe", name_to_camel_case("größe"));
  EXPECT_EQ("grÖsse", name_to_camel_case("GRÖSSE"));
}

// the names SHOW CREATE writes in `name: column`, so that REST SQL reads
// them back
TEST(Mrs_naming, pair_name_text) {
  EXPECT_EQ("city", pair_name_text("city"));
  EXPECT_EQ("_x$1", pair_name_text("_x$1"));
  EXPECT_EQ("größe", pair_name_text("größe"));
  EXPECT_EQ("`First Name`", pair_name_text("First Name"));
  EXPECT_EQ("`last-name`", pair_name_text("last-name"));
  EXPECT_EQ("`a``b`", pair_name_text("a`b"));
  EXPECT_EQ("``", pair_name_text(""));
  // a number is no identifier; digits followed by letters are
  EXPECT_EQ("`123`", pair_name_text("123"));
  EXPECT_EQ("`1st`", pair_name_text("1st"));
  // most keywords are accepted as a field or column name, the annotation
  // words and a few others are not
  EXPECT_EQ("name", pair_name_text("name"));
  EXPECT_EQ("user", pair_name_text("user"));
  EXPECT_EQ("`key`", pair_name_text("key"));
  EXPECT_EQ("`INOUT`", pair_name_text("INOUT"));
  EXPECT_EQ("`Select`", pair_name_text("Select"));
  EXPECT_EQ("in", pair_name_text("in"));
  EXPECT_EQ("update", pair_name_text("update"));
}

TEST(Mrs_naming, identifier_text) {
  EXPECT_EQ("sakila", identifier_text("sakila"));
  EXPECT_EQ("`my db`", identifier_text("my db"));
  // a schema or table name that is a keyword is quoted, whichever it is
  EXPECT_EQ("`user`", identifier_text("user"));
  EXPECT_EQ("`files`", identifier_text("files"));
}

}  // namespace metadata
}  // namespace mrs
