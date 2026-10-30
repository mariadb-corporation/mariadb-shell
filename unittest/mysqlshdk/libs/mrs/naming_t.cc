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
  EXPECT_EQ("", name_to_camel_case(""));
  // UTF-8 is kept; the extension folds accents to ASCII (grosse), the shell
  // does not
  EXPECT_EQ("größe", name_to_camel_case("größe"));
}

}  // namespace metadata
}  // namespace mrs
