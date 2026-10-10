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

#include "modules/mrs/core/mrs_lexer.h"

namespace mrs {

namespace {

using Kind = Token::Kind;
using Keyword = Token::Keyword;
using Annotation = Token::Annotation;
using Punctuation = Token::Punctuation;

std::vector<Token> tokenize(std::string_view input, const Sql_mode &mode = {}) {
  Lexer lexer(input, mode);
  std::vector<Token> tokens;
  for (;;) {
    auto token = lexer.next();
    if (token.kind == Kind::end_of_input) break;
    tokens.push_back(std::move(token));
  }
  return tokens;
}

}  // namespace

TEST(Mrs_lexer, sql_mode_parsing) {
  const auto none = Sql_mode::from_string("");
  EXPECT_FALSE(none.ansi_quotes);
  EXPECT_FALSE(none.no_backslash_escapes);

  const auto both = Sql_mode::from_string(
      "ONLY_FULL_GROUP_BY, ansi_quotes ,NO_BACKSLASH_ESCAPES");
  EXPECT_TRUE(both.ansi_quotes);
  EXPECT_TRUE(both.no_backslash_escapes);

  // ANSI implies ANSI_QUOTES
  EXPECT_TRUE(Sql_mode::from_string("ANSI").ansi_quotes);
}

TEST(Mrs_lexer, keywords_are_case_insensitive) {
  const auto tokens = tokenize("create Rest SERVICE schema schemas");
  ASSERT_EQ(5u, tokens.size());
  EXPECT_EQ(Kind::keyword, tokens[0].kind);
  EXPECT_EQ(Keyword::CREATE_SYMBOL, tokens[0].keyword);
  EXPECT_EQ(Keyword::REST_SYMBOL, tokens[1].keyword);
  EXPECT_EQ(Keyword::SERVICE_SYMBOL, tokens[2].keyword);
  // SCHEMA(S) are aliases of DATABASE(S)
  EXPECT_EQ(Keyword::DATABASE_SYMBOL, tokens[3].keyword);
  EXPECT_EQ(Keyword::DATABASES_SYMBOL, tokens[4].keyword);
  EXPECT_EQ("Rest", tokens[1].text);
}

TEST(Mrs_lexer, identifiers_and_numbers) {
  const auto tokens = tokenize("my_table $x 1abc 42 3.14 .5 1e10 2.5E-3 ü");
  ASSERT_EQ(9u, tokens.size());
  EXPECT_EQ(Kind::identifier, tokens[0].kind);
  EXPECT_EQ("my_table", tokens[0].text);
  EXPECT_EQ(Kind::identifier, tokens[1].kind);
  // digits followed by letters form an identifier
  EXPECT_EQ(Kind::identifier, tokens[2].kind);
  EXPECT_EQ("1abc", tokens[2].text);
  EXPECT_EQ(Kind::int_number, tokens[3].kind);
  EXPECT_EQ("42", tokens[3].text);
  EXPECT_EQ(Kind::decimal_number, tokens[4].kind);
  EXPECT_EQ("3.14", tokens[4].text);
  EXPECT_EQ(Kind::decimal_number, tokens[5].kind);
  EXPECT_EQ(".5", tokens[5].text);
  EXPECT_EQ(Kind::float_number, tokens[6].kind);
  EXPECT_EQ("1e10", tokens[6].text);
  EXPECT_EQ(Kind::float_number, tokens[7].kind);
  EXPECT_EQ("2.5E-3", tokens[7].text);
  // non-ASCII characters are identifier characters
  EXPECT_EQ(Kind::identifier, tokens[8].kind);
  EXPECT_EQ("ü", tokens[8].text);
}

TEST(Mrs_lexer, strings_with_escapes) {
  const auto tokens = tokenize(R"('it''s' 'a\'b' "say \"hi\"\n" 'back\\slash')");
  ASSERT_EQ(4u, tokens.size());
  EXPECT_EQ(Kind::single_quoted_text, tokens[0].kind);
  EXPECT_EQ("it's", tokens[0].text);
  EXPECT_EQ("'it''s'", tokens[0].raw);
  EXPECT_EQ("a'b", tokens[1].text);
  EXPECT_EQ(Kind::double_quoted_text, tokens[2].kind);
  EXPECT_EQ("say \"hi\"\n", tokens[2].text);
  EXPECT_EQ("back\\slash", tokens[3].text);
}

TEST(Mrs_lexer, no_backslash_escapes) {
  Sql_mode mode;
  mode.no_backslash_escapes = true;
  const auto tokens = tokenize(R"('a\nb' "c\d")", mode);
  ASSERT_EQ(2u, tokens.size());
  EXPECT_EQ("a\\nb", tokens[0].text);
  EXPECT_EQ("c\\d", tokens[1].text);
}

TEST(Mrs_lexer, ansi_quotes) {
  // With ANSI_QUOTES a double quoted string is an identifier: the doubled
  // quote is the only escape.
  Sql_mode mode;
  mode.ansi_quotes = true;
  const auto tokens = tokenize(R"("a""b\n")", mode);
  ASSERT_EQ(1u, tokens.size());
  EXPECT_EQ(Kind::double_quoted_text, tokens[0].kind);
  EXPECT_EQ("a\"b\\n", tokens[0].text);
}

TEST(Mrs_lexer, back_tick_quoted_identifiers) {
  const auto tokens = tokenize("`sakila`.`my``table` `` `a\\`b");
  ASSERT_EQ(6u, tokens.size());
  EXPECT_EQ(Kind::back_tick_quoted_id, tokens[0].kind);
  EXPECT_EQ("sakila", tokens[0].text);
  EXPECT_EQ(Kind::punctuation, tokens[1].kind);
  EXPECT_EQ(Punctuation::DOT_SYMBOL, tokens[1].punctuation);
  EXPECT_EQ("my`table", tokens[2].text);
  EXPECT_EQ("", tokens[3].text);
  // no backslash escapes in identifiers: the backslash is part of the name
  EXPECT_EQ(Kind::back_tick_quoted_id, tokens[4].kind);
  EXPECT_EQ("a\\", tokens[4].text);
  EXPECT_EQ(Kind::identifier, tokens[5].kind);
  EXPECT_EQ("b", tokens[5].text);
}

TEST(Mrs_lexer, request_paths) {
  const auto tokens = tokenize("/myService /a/b/c /1x mike@/svc");
  ASSERT_EQ(6u, tokens.size());
  EXPECT_EQ(Kind::rest_request_path, tokens[0].kind);
  EXPECT_EQ("/myService", tokens[0].text);
  EXPECT_EQ("/a/b/c", tokens[1].text);
  EXPECT_EQ("/1x", tokens[2].text);
  EXPECT_EQ(Kind::identifier, tokens[3].kind);
  EXPECT_EQ("mike", tokens[3].text);
  EXPECT_EQ(Kind::punctuation, tokens[4].kind);
  EXPECT_EQ(Punctuation::AT_SIGN_SYMBOL, tokens[4].punctuation);
  EXPECT_EQ(Kind::rest_request_path, tokens[5].kind);
  EXPECT_EQ("/svc", tokens[5].text);
}

TEST(Mrs_lexer, annotations_and_at_sign) {
  const auto tokens = tokenize("@INSERT @nocheck @key \"mike\"@\"MRS\" @");
  ASSERT_EQ(7u, tokens.size());
  EXPECT_EQ(Kind::annotation, tokens[0].kind);
  EXPECT_EQ(Annotation::AT_INSERT_SYMBOL, tokens[0].annotation);
  EXPECT_EQ(Annotation::AT_NOCHECK_SYMBOL, tokens[1].annotation);
  EXPECT_EQ(Annotation::AT_KEY_SYMBOL, tokens[2].annotation);
  EXPECT_EQ(Kind::double_quoted_text, tokens[3].kind);
  EXPECT_EQ(Punctuation::AT_SIGN_SYMBOL, tokens[4].punctuation);
  EXPECT_EQ(Kind::double_quoted_text, tokens[5].kind);
  EXPECT_EQ(Punctuation::AT_SIGN_SYMBOL, tokens[6].punctuation);

  // An unknown word after @ is not an annotation: `user@app`
  const auto user_app = tokenize("mike@MRS");
  ASSERT_EQ(3u, user_app.size());
  EXPECT_EQ("mike", user_app[0].text);
  EXPECT_EQ(Punctuation::AT_SIGN_SYMBOL, user_app[1].punctuation);
  EXPECT_EQ(Kind::keyword, user_app[2].kind);
  EXPECT_EQ(Keyword::MRS_SYMBOL, user_app[2].keyword);
}

TEST(Mrs_lexer, comments_and_whitespace) {
  const auto tokens = tokenize(
      "# pound\n-- dash dash\nCREATE /* block\ncomment */ REST --notacomment\n"
      "/**/ SERVICE");
  ASSERT_EQ(6u, tokens.size());
  EXPECT_EQ(Keyword::CREATE_SYMBOL, tokens[0].keyword);
  EXPECT_EQ(Keyword::REST_SYMBOL, tokens[1].keyword);
  // `--` not followed by white space is two minus operators
  EXPECT_EQ(Punctuation::MINUS_OPERATOR, tokens[2].punctuation);
  EXPECT_EQ(Punctuation::MINUS_OPERATOR, tokens[3].punctuation);
  EXPECT_EQ(Kind::identifier, tokens[4].kind);
  EXPECT_EQ("notacomment", tokens[4].text);
  EXPECT_EQ(Keyword::SERVICE_SYMBOL, tokens[5].keyword);
}

TEST(Mrs_lexer, positions) {
  const auto tokens = tokenize("CREATE\n  REST 'x'");
  ASSERT_EQ(3u, tokens.size());
  EXPECT_EQ(1, tokens[0].line);
  EXPECT_EQ(0, tokens[0].column);
  EXPECT_EQ(2, tokens[1].line);
  EXPECT_EQ(2, tokens[1].column);
  EXPECT_EQ(2, tokens[2].line);
  EXPECT_EQ(7, tokens[2].column);
  EXPECT_EQ(14u, tokens[2].offset);
}

TEST(Mrs_lexer, punctuation_and_invalid_input) {
  const auto tokens = tokenize("{ } [ ] ( ) , : ; . - + = * / ~");
  ASSERT_EQ(16u, tokens.size());
  EXPECT_EQ(Punctuation::OPEN_CURLY_SYMBOL, tokens[0].punctuation);
  EXPECT_EQ(Punctuation::CLOSE_SQUARE_SYMBOL, tokens[3].punctuation);
  EXPECT_EQ(Punctuation::DIV_OPERATOR, tokens[14].punctuation);
  EXPECT_EQ(Kind::invalid, tokens[15].kind);
  EXPECT_EQ("~", tokens[15].text);

  // an unterminated string is invalid input
  const auto open = tokenize("'abc");
  ASSERT_EQ(1u, open.size());
  EXPECT_EQ(Kind::invalid, open[0].kind);
}

}  // namespace mrs
