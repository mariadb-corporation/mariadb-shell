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

#include "mysqlshdk/libs/mrs/json.h"

namespace mrs {
namespace json {

TEST(Mrs_json, unicode_escapes_and_surrogate_pairs) {
  EXPECT_EQ("\xC3\xA9", parse("\"\\u00e9\"").as_string());
  // A surrogate pair is one code point
  EXPECT_EQ("\xF0\x9F\x98\x80", parse("\"\\ud83d\\ude00\"").as_string());
  EXPECT_EQ("\"\xF0\x9F\x98\x80\"", parse("\"\\ud83d\\ude00\"").dump());

  // A high surrogate without a low one, and a lone low surrogate, are not
  // code points: they must not be emitted as invalid UTF-8
  EXPECT_THROW(parse("\"\\ud83d\\u0041\""), Parse_error);
  EXPECT_THROW(parse("\"\\ud83d\""), Parse_error);
  EXPECT_THROW(parse("\"\\ud83dx\""), Parse_error);
  EXPECT_THROW(parse("\"\\ude00\""), Parse_error);
  EXPECT_FALSE(try_parse("{\"a\": \"\\ud83d\\u0041\"}").has_value());
}

TEST(Mrs_json, nesting_depth_is_limited) {
  const auto nested = [](size_t depth, char open, char close) {
    return std::string(depth, open) + std::string(depth, close);
  };

  // 512 levels parse and dump
  EXPECT_EQ(nested(512, '[', ']'), parse(nested(512, '[', ']')).dump());
  std::string objects = "1";
  for (int i = 0; i < 512; ++i) objects = "{\"a\":" + objects + "}";
  EXPECT_NO_THROW(parse(objects));

  // Deeper input fails instead of overflowing the stack, however deep
  EXPECT_THROW(parse(nested(513, '[', ']')), Parse_error);
  EXPECT_THROW(parse(std::string(100000, '[')), Parse_error);
  EXPECT_FALSE(try_parse(std::string(100000, '[')).has_value());
  EXPECT_THROW(parse("{\"a\":" + objects + "}"), Parse_error);

  // Siblings do not add up: the depth is that of the deepest branch
  EXPECT_NO_THROW(parse("[" + nested(511, '[', ']') + "," +
                        nested(511, '[', ']') + "]"));
}

}  // namespace json
}  // namespace mrs
