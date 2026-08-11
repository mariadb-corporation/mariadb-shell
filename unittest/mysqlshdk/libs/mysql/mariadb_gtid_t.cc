/*
 * Copyright (c) 2026, MariaDB Corporation.
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

#include "mysqlshdk/libs/mysql/mariadb_gtid.h"

#include <stdexcept>
#include <string>

#include "unittest/gtest_clean.h"

namespace mysqlshdk {
namespace mysql {

namespace {

Mariadb_gtid_position parse(const std::string &position) {
  return Mariadb_gtid_position::parse(position);
}

}  // namespace

TEST(Mariadb_gtid_position_test, parse_and_normalize) {
  EXPECT_TRUE(parse("").empty());
  EXPECT_TRUE(parse("   ").empty());
  EXPECT_EQ("", parse("").str());

  EXPECT_EQ("0-1-42", parse("0-1-42").str());
  EXPECT_FALSE(parse("0-1-42").empty());

  // domains are ordered, whitespace around the entries is not significant
  EXPECT_EQ("0-1-42,1-2-7", parse("1-2-7, 0-1-42").str());
  EXPECT_EQ("0-1-42,1-2-7", parse(" 0-1-42 ,\t1-2-7\n").str());

  // a position holds one entry per domain, the most advanced one wins
  EXPECT_EQ("0-2-99", parse("0-1-42,0-2-99").str());
  EXPECT_EQ("0-2-99", parse("0-2-99,0-1-42").str());

  // sequence numbers are 64-bit
  EXPECT_EQ("0-1-18446744073709551615", parse("0-1-18446744073709551615").str());
}

TEST(Mariadb_gtid_position_test, parse_rejects_garbage) {
  for (const auto *position : {"0", "0-1", "0-1-2-3", "0--1", "-1-2", "0-1-",
                               "x-1-2", "0-1-x", "0-1-2,", ",0-1-2", "0 -1-2",
                               "0-1-+2", "0-1--2", "4294967296-1-2",
                               "0-4294967296-2", "0-1-18446744073709551616"}) {
    EXPECT_THROW(parse(position), std::invalid_argument) << position;
  }
}

TEST(Mariadb_gtid_position_test, contains) {
  const auto empty = parse("");
  const auto one = parse("0-1-42");
  const auto ahead = parse("0-1-43");
  const auto two_domains = parse("0-1-42,1-1-10");

  EXPECT_TRUE(empty.contains(empty));
  EXPECT_TRUE(one.contains(empty));
  EXPECT_FALSE(empty.contains(one));

  EXPECT_TRUE(one.contains(one));
  EXPECT_TRUE(ahead.contains(one));
  EXPECT_FALSE(one.contains(ahead));

  // the server_id is not part of the comparison, the domain and the sequence
  // are what say which transactions a position covers
  EXPECT_TRUE(one.contains(parse("0-7-42")));

  EXPECT_TRUE(two_domains.contains(one));
  EXPECT_FALSE(one.contains(two_domains));
}

TEST(Mariadb_gtid_position_test, intersects) {
  EXPECT_FALSE(parse("").intersects(parse("0-1-1")));
  EXPECT_FALSE(parse("0-1-1").intersects(parse("")));

  // sharing a domain means sharing transactions, whatever the sequences are
  EXPECT_TRUE(parse("0-1-42").intersects(parse("0-1-1")));
  EXPECT_TRUE(parse("0-1-1").intersects(parse("0-9-42")));

  EXPECT_FALSE(parse("0-1-42").intersects(parse("1-1-42")));
  EXPECT_TRUE(parse("0-1-42,1-1-7").intersects(parse("1-1-42")));

  // sequence 0 means the domain has nothing in it
  EXPECT_FALSE(parse("0-1-0").intersects(parse("0-1-42")));
}

TEST(Mariadb_gtid_position_test, merge) {
  auto position = parse("");

  EXPECT_EQ("", position.merge(parse("")).str());
  EXPECT_EQ("0-1-42", position.merge(parse("0-1-42")).str());

  // the more advanced entry of a shared domain wins, new domains are added
  EXPECT_EQ("0-1-42,1-3-7", position.merge(parse("0-2-11,1-3-7")).str());
  EXPECT_EQ("0-2-99,1-3-7", position.merge(parse("0-2-99")).str());

  EXPECT_EQ("0-2-99,1-3-7", position.merge(parse("")).str());
}

}  // namespace mysql
}  // namespace mysqlshdk
