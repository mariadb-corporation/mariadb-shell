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

#include <stdexcept>
#include <string>

#include "unittest/gtest_clean.h"

#include "modules/mrs/core/mrs_sql.h"

namespace mrs {
namespace sql {

namespace {
const Quoting k_default{};
const Quoting k_no_backslash_escapes{true};
}  // namespace

TEST(Mrs_sql, quoting_from_sql_mode) {
  EXPECT_FALSE(Quoting::from_sql_mode("").no_backslash_escapes);
  EXPECT_FALSE(Quoting::from_sql_mode("STRICT_TRANS_TABLES").no_backslash_escapes);
  EXPECT_TRUE(Quoting::from_sql_mode("NO_BACKSLASH_ESCAPES").no_backslash_escapes);
  EXPECT_TRUE(Quoting::from_sql_mode("ANSI_QUOTES,NO_BACKSLASH_ESCAPES,STRICT")
                  .no_backslash_escapes);
  EXPECT_FALSE(Quoting::from_sql_mode("NO_BACKSLASH_ESCAPESX").no_backslash_escapes);
}

TEST(Mrs_sql, literal) {
  EXPECT_EQ("'it''s'", literal("it's", k_default));
  EXPECT_EQ("'it''s'", literal("it's", k_no_backslash_escapes));
  EXPECT_EQ("'a\\\\b'", literal("a\\b", k_default));
  EXPECT_EQ("'a\\b'", literal("a\\b", k_no_backslash_escapes));
  EXPECT_EQ(std::string("'\\0\\Z'"),
            literal(std::string_view("\0\032", 2), k_default));
  EXPECT_EQ(std::string("'\0\032'", 4),
            literal(std::string_view("\0\032", 2), k_no_backslash_escapes));
  EXPECT_EQ("'\"\nx'", literal("\"\nx", k_default));
}

TEST(Mrs_sql, values) {
  EXPECT_EQ("NULL", Value().render(k_default));
  EXPECT_EQ("NULL", Value(std::optional<int>()).render(k_default));
  EXPECT_EQ("7", Value(7).render(k_default));
  EXPECT_EQ("1", Value(true).render(k_default));
  EXPECT_EQ("'x'", Value("x").render(k_default));
  EXPECT_EQ("DEFAULT", Value::raw("DEFAULT").render(k_default));
  EXPECT_EQ("NULL", Value::id(Id{}).render(k_default));
  EXPECT_EQ("'31000000-0000-0000-0000-000000000000'",
            Value::id(Id{"31000000-0000-0000-0000-000000000000"}).render(k_default));
}

TEST(Mrs_sql, placeholders) {
  EXPECT_EQ("SELECT 1 FROM t WHERE a = 'x' AND b = 2",
            Statement("SELECT 1 FROM t WHERE a = ? AND b = ?", {"x", 2})
                .render(k_default));
  // A ? inside quotes, back ticks or comments is not a placeholder
  EXPECT_EQ("SELECT '?', \"?\", `?`, 'x' -- ?\n/* ? */ # ?",
            Statement("SELECT '?', \"?\", `?`, ? -- ?\n/* ? */ # ?", {"x"})
                .render(k_default));
  // Escaped and doubled quotes do not end a literal early
  EXPECT_EQ("SELECT 'a''?', 'b\\'?', 1",
            Statement("SELECT 'a''?', 'b\\'?', ?", {1}).render(k_default));
  // Without backslash escapes, the backslash ends nothing: 'b\' is a literal
  EXPECT_EQ("SELECT 'b\\', 1",
            Statement("SELECT 'b\\', ?", {1}).render(k_no_backslash_escapes));
  EXPECT_THROW(Statement("SELECT ?, ?", {1}).render(k_default), std::logic_error);
  EXPECT_THROW(Statement("SELECT ?", {1, 2}).render(k_default), std::logic_error);
  EXPECT_EQ("SELECT 1", Statement("SELECT 1").render(k_default));
}

TEST(Mrs_sql, builders) {
  auto insert = Insert("t").set("a", 1).set("b", "x").statement();
  EXPECT_EQ(
      "INSERT INTO `mysql_rest_service_metadata`.`t` (`a`, `b`) VALUES (1, 'x')",
      insert.render(k_default));

  auto rows = Insert("t");
  rows.set("a", 1).set("b", "x");
  rows.next_row().set("a", 2);
  rows.next_row().set("b", "y");
  EXPECT_EQ(
      "INSERT INTO `mysql_rest_service_metadata`.`t` (`a`, `b`) VALUES "
      "(1, 'x'), (2, DEFAULT), (DEFAULT, 'y')",
      rows.statement().render(k_default));

  EXPECT_EQ(
      "UPDATE `mysql_rest_service_metadata`.`t` SET `a` = 'it''s', "
      "o = JSON_MERGE_PATCH(o, '{}') WHERE `id` = 'i' AND x IN (1, 2)",
      Update("t")
          .set("a", "it's")
          .set_raw("o = JSON_MERGE_PATCH(o, ?)", {"{}"})
          .where("id", "i")
          .where_raw("x IN (?, ?)", {1, 2})
          .statement()
          .render(k_default));

  EXPECT_EQ("DELETE FROM `mysql_rest_service_metadata`.`t` WHERE `id` = 'a\\b'",
            Delete("t").where("id", "a\\b").statement().render(
                k_no_backslash_escapes));
}

}  // namespace sql
}  // namespace mrs
