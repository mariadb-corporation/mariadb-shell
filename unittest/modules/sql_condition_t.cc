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

#include <set>
#include <stdexcept>
#include <string>

#include "modules/reports/utils.h"
#include "mysqlshdk/libs/utils/utils_sqlstring.h"
#include "unittest/gtest_clean.h"

namespace mysqlsh {
namespace reports {
namespace {

// the columns of the threads report, as far as these tests need them
std::string column(const std::string &name) {
  static const std::set<std::string> columns = {
      "tid",  "cid",  "name", "type", "time", "system.autocommit",
      "status.bytes_sent"};

  if (!columns.count(name)) {
    throw std::invalid_argument("Unknown column name: " + name);
  }

  return "`" + name + "`";
}

std::string parse(const std::string &condition) {
  return Sql_condition{column}.parse(condition);
}

std::string error_of(const std::string &condition) {
  try {
    parse(condition);
  } catch (const std::exception &e) {
    return e.what();
  }

  return "<no error>";
}

}  // namespace

// What both parsers do: the one built on the X protocol expression parser and
// the one of the builds without it.

TEST(Sql_condition, accepted_conditions) {
  for (const auto condition : {
           "tid = 1",
           "tid == 1",
           "tid != 1",
           "tid <> 1",
           "tid < 1",
           "tid <= 1",
           "tid > 1",
           "tid >= 1",
           "name LIKE '%main%'",
           "name like \"%main%\"",
           "name NOT LIKE '%main%'",
           "tid = 1 AND cid = 2",
           "tid = 1 and cid = 2",
           "tid = 1 OR cid = 2",
           "tid = 1 Or cid = 2",
           "NOT tid = 1",
           "not tid = 1",
           "(tid = 1 OR cid = 2) AND NOT (tid = 3 OR cid = 4)",
           "cid = NULL",
           "tid = TRUE",
           "tid = 1.5",
           "tid = cid",
           "system.autocommit = 1",
           "status.bytes_sent > 0",
       }) {
    SCOPED_TRACE(condition);
    EXPECT_NO_THROW(parse(condition));
  }
}

TEST(Sql_condition, disallowed_operators) {
  EXPECT_EQ("Disallowed operator: |", error_of("tid | 1"));
  EXPECT_EQ("Disallowed operator: is", error_of("tid = 1 IS cid = 2"));
  EXPECT_EQ("Disallowed operator: is", error_of("cid IS NULL"));
  EXPECT_EQ("Disallowed operator: in", error_of("tid IN (1, 2)"));
  EXPECT_EQ("Disallowed operator: between", error_of("tid BETWEEN 1 AND 2"));
  EXPECT_EQ("Disallowed operator: +", error_of("tid + 1 = 2"));
  EXPECT_EQ("Disallowed operator: -", error_of("tid - 1 = 2"));
}

TEST(Sql_condition, unknown_columns) {
  EXPECT_EQ("Unknown column name: invalidcolumn",
            error_of("invalidcolumn = 1"));
  EXPECT_EQ("Unknown column name: one", error_of("name = one"));
  EXPECT_EQ("Unknown column name: system.nosuchvar",
            error_of("tid = 1 AND system.nosuchvar = 1"));
}

TEST(Sql_condition, syntax_is_checked_before_names) {
  // "one" is not a column, but the condition is malformed first
  EXPECT_EQ(
      "Expected end of expression, at position 4,\n"
      "in: one two\n"
      "        ^^^",
      error_of("one two"));
}

#ifndef HAVE_X_PROTOCOL
// The parser of the builds without the X protocol: the SQL it writes and its
// own error messages.

TEST(Sql_condition, comparisons) {
  EXPECT_EQ("(`tid`) = (1)", parse("tid = 1"));
  EXPECT_EQ("(`tid`) = (1)", parse("tid == 1"));
  EXPECT_EQ("(`tid`) != (1)", parse("tid != 1"));
  EXPECT_EQ("(`tid`) != (1)", parse("tid <> 1"));
  EXPECT_EQ("(`tid`) < (1)", parse("tid < 1"));
  EXPECT_EQ("(`tid`) <= (1)", parse("tid <= 1"));
  EXPECT_EQ("(`tid`) > (1)", parse("tid > 1"));
  EXPECT_EQ("(`tid`) >= (1)", parse("tid >= 1"));
  EXPECT_EQ("(`name`) LIKE ('%main%')", parse("name LIKE '%main%'"));
  EXPECT_EQ("(`name`) LIKE ('%main%')", parse("name lIkE '%main%'"));
  EXPECT_EQ("NOT ((`name`) LIKE ('%main%'))", parse("name NOT LIKE '%main%'"));
  EXPECT_EQ("NOT ((`name`) LIKE ('%main%'))", parse("name not like '%main%'"));
  EXPECT_EQ("(`tid`) = (`cid`)", parse("tid = cid"));
}

TEST(Sql_condition, a_single_operand) {
  EXPECT_EQ("TRUE", parse("TRUE"));
  EXPECT_EQ("`tid`", parse("tid"));
}

TEST(Sql_condition, logical_operators) {
  // AND, OR and NOT whatever the spelling, never && or ||, which concatenates
  // strings in some SQL modes
  EXPECT_EQ("((`tid`) = (1)) AND ((`cid`) = (2))", parse("tid = 1 AND cid = 2"));
  EXPECT_EQ("((`tid`) = (1)) AND ((`cid`) = (2))", parse("tid = 1 aNd cid = 2"));
  EXPECT_EQ("((`tid`) = (1)) AND ((`cid`) = (2))", parse("tid = 1 && cid = 2"));
  EXPECT_EQ("((`tid`) = (1)) OR ((`cid`) = (2))", parse("tid = 1 OR cid = 2"));
  EXPECT_EQ("((`tid`) = (1)) OR ((`cid`) = (2))", parse("tid = 1 || cid = 2"));
  EXPECT_EQ("NOT ((`tid`) = (1))", parse("NOT tid = 1"));
  EXPECT_EQ("NOT ((`tid`) = (1))", parse("! tid = 1"));
  EXPECT_EQ("NOT (NOT ((`tid`) = (1)))", parse("NOT NOT tid = 1"));
}

TEST(Sql_condition, precedence) {
  // NOT, then AND, then OR, each applied to whole comparisons
  EXPECT_EQ("((`tid`) = (1)) OR (((`cid`) = (2)) AND ((`name`) = ('x')))",
            parse("tid = 1 OR cid = 2 AND name = 'x'"));
  EXPECT_EQ("(NOT ((`tid`) = (1))) AND ((`cid`) = (2))",
            parse("NOT tid = 1 AND cid = 2"));
  // left to right
  EXPECT_EQ("(((`tid`) = (1)) OR ((`cid`) = (2))) OR ((`name`) = ('x'))",
            parse("tid = 1 OR cid = 2 OR name = 'x'"));
}

TEST(Sql_condition, parentheses) {
  EXPECT_EQ("(((`tid`) = (1)) OR ((`cid`) = (2))) AND ((`name`) = ('x'))",
            parse("(tid = 1 OR cid = 2) AND name = 'x'"));
  EXPECT_EQ("(`tid`) = (1)", parse("((tid = 1))"));
  EXPECT_EQ("(`tid`) = (1)", parse("(tid) = (1)"));
}

TEST(Sql_condition, literals) {
  EXPECT_EQ("(`tid`) = (42)", parse("tid = 42"));
  EXPECT_EQ("(`tid`) = (-42)", parse("tid = -42"));
  EXPECT_EQ("(`tid`) = (1.5)", parse("tid = 1.5"));
  EXPECT_EQ("(`tid`) = (.5)", parse("tid = .5"));
  EXPECT_EQ("(`tid`) = (1.5e3)", parse("tid = 1.5e3"));
  EXPECT_EQ("(`tid`) = (2.0E-1)", parse("tid = 2.0E-1"));
  EXPECT_EQ("(`tid`) = (TRUE)", parse("tid = true"));
  EXPECT_EQ("(`tid`) = (FALSE)", parse("tid = False"));
  EXPECT_EQ("(`cid`) = (NULL)", parse("cid = null"));
  EXPECT_EQ("(`name`) = ('x')", parse("name = 'x'"));
  EXPECT_EQ("(`name`) = ('x')", parse("name = \"x\""));
  EXPECT_EQ("(`name`) = ('')", parse("name = ''"));
}

TEST(Sql_condition, strings_cannot_change_the_condition) {
  // whatever a string holds reaches the SQL quoted, as one value
  const auto value = [](const std::string &v) {
    return "(`name`) = (" + shcore::quote_sql_string(v) + ")";
  };

  EXPECT_EQ(value("it's"), parse("name = 'it''s'"));
  EXPECT_EQ(value("it's"), parse("name = 'it\\'s'"));
  EXPECT_EQ(value("say \"hi\""), parse("name = \"say \"\"hi\"\"\""));
  EXPECT_EQ(value("x' OR '1'='1"), parse("name = 'x'' OR ''1''=''1'"));
  EXPECT_EQ(value("x' OR 1=1 -- "), parse("name = 'x\\' OR 1=1 -- '"));
  EXPECT_EQ(value("a\\b"), parse("name = 'a\\\\b'"));
  EXPECT_EQ(value("; DROP TABLE t"), parse("name = '; DROP TABLE t'"));
}

TEST(Sql_condition, identifiers) {
  EXPECT_EQ("(`tid`) = (1)", parse("`tid` = 1"));
  EXPECT_EQ("(`system.autocommit`) = (1)", parse("system.autocommit = 1"));
  EXPECT_EQ("(`status.bytes_sent`) > (0)", parse("status . bytes_sent > 0"));
  // a keyword of the tokenizer that is no operator is a name, as in the X
  // protocol parser: the threads report has a "time" column
  EXPECT_EQ("(`time`) > (1)", parse("time > 1"));
  // a quoted name is still only a name: unknown unless it is a column
  EXPECT_EQ("Unknown column name: tid') OR ('1",
            error_of("`tid') OR ('1` = 1"));
  EXPECT_EQ("Unknown column name: a`b", error_of("`a``b` = 1"));
}

TEST(Sql_condition, malformed_conditions) {
  // the messages of the X protocol tokenizer and parser
  const auto at = [](const std::string &text, size_t position, size_t length,
                     const std::string &input) {
    return text + ", at position " + std::to_string(position) + ",\nin: " +
           input + "\n" + std::string(position + 4, ' ') +
           std::string(length, '^') +
           std::string(input.length() - position - length, ' ');
  };

  for (const auto condition : {"", "tid =", "(tid = 1", "NOT"}) {
    SCOPED_TRACE(condition);
    EXPECT_EQ("Unexpected end of expression.", error_of(condition));
  }

  EXPECT_EQ(at("Expected an operand", 6, 1, "tid = )"), error_of("tid = )"));
  EXPECT_EQ(at("Expected end of expression", 8, 1, "tid = 1 2"),
            error_of("tid = 1 2"));
  EXPECT_EQ(at("Expected end of expression", 8, 1, "tid = 1 )"),
            error_of("tid = 1 )"));
  // no exponent without a decimal point
  EXPECT_EQ(at("Expected end of expression", 7, 1, "tid = 1e"),
            error_of("tid = 1e"));
  EXPECT_EQ(at("Expected an identifier", 8, 1, "system. = 1"),
            error_of("system. = 1"));
  EXPECT_EQ("Unterminated quoted string starting at position 7",
            error_of("tid = 'abc"));
  EXPECT_EQ("Unterminated quoted string starting at position 1",
            error_of("`tid = 1"));
  EXPECT_EQ("Unknown character at position 7",
            error_of("tid = 1; DROP TABLE t"));
  EXPECT_EQ("Unknown character at position 8", error_of("tid = 1 # x"));
  // a comment cannot hide the rest of the condition either
  EXPECT_EQ("Disallowed operator: -", error_of("tid = 1 -- x"));
  EXPECT_EQ("Disallowed operator: /", error_of("tid = 1 /* x */"));
}

TEST(Sql_condition, operator_validator_decides) {
  // the parser hands every operator to the validator, which can narrow them
  const auto only_equality = [](const std::string &op) -> std::string {
    if ("==" != op) throw std::invalid_argument("Not this one: " + op);
    return "=";
  };

  const Sql_condition condition{column, only_equality};

  EXPECT_EQ("(`tid`) = (1)", condition.parse("tid = 1"));
  EXPECT_THROW(condition.parse("tid > 1"), std::invalid_argument);
  EXPECT_THROW(condition.parse("tid = 1 AND cid = 2"), std::invalid_argument);
  EXPECT_THROW(condition.parse("NOT tid = 1"), std::invalid_argument);
}
#endif  // !HAVE_X_PROTOCOL

}  // namespace reports
}  // namespace mysqlsh
