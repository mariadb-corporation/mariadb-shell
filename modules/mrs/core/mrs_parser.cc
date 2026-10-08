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

#include "modules/mrs/core/mrs_parser.h"

#include <cctype>

#include "modules/mrs/core/mrs_parser_driver.h"
#include "mrs_parser_gen.h"

namespace mrs {

namespace parser {

// Converts the lexer's tokens into the parser's symbols.
Parser::symbol_type yylex(Driver &driver) {
  const auto token = driver.lexer().next();

  Parser::location_type loc;
  loc.begin.line = token.line;
  loc.begin.column = token.column;
  loc.end.line = token.line;
  loc.end.column = token.column + static_cast<int>(token.raw.size());

  switch (token.kind) {
    case Token::Kind::end_of_input:
      return Parser::make_END(loc);

    case Token::Kind::identifier:
      return Parser::make_IDENTIFIER(token.text, loc);

    case Token::Kind::back_tick_quoted_id:
      return Parser::make_BACK_TICK_QUOTED_ID(token.text, loc);

    case Token::Kind::single_quoted_text:
      return Parser::make_SINGLE_QUOTED_TEXT(token.text, loc);

    case Token::Kind::double_quoted_text:
      return Parser::make_DOUBLE_QUOTED_TEXT(
          Quoted{token.text, std::string(token.raw)}, loc);

    case Token::Kind::int_number:
      return Parser::make_INT_NUMBER(token.text, loc);

    case Token::Kind::decimal_number:
      return Parser::make_DECIMAL_NUMBER(token.text, loc);

    case Token::Kind::float_number:
      return Parser::make_FLOAT_NUMBER(token.text, loc);

    case Token::Kind::rest_request_path:
      return Parser::make_REST_REQUEST_PATH(token.text, loc);

    case Token::Kind::keyword:
      driver.remember_token(token.line, token.column, std::string(token.raw));
      switch (token.keyword) {
#define MRS_KEYWORD_SYMBOL(name, text) \
  case Token::Keyword::name:           \
    return Parser::make_##name(loc);
        MRS_KEYWORD_LIST(MRS_KEYWORD_SYMBOL)
#undef MRS_KEYWORD_SYMBOL
      }
      break;

    case Token::Kind::annotation:
      switch (token.annotation) {
#define MRS_ANNOTATION_SYMBOL(name, text) \
  case Token::Annotation::name:           \
    return Parser::make_##name(loc);
        MRS_ANNOTATION_LIST(MRS_ANNOTATION_SYMBOL)
#undef MRS_ANNOTATION_SYMBOL
      }
      break;

    case Token::Kind::punctuation:
      switch (token.punctuation) {
#define MRS_PUNCTUATION_SYMBOL(name, ch) \
  case Token::Punctuation::name:         \
    return Parser::make_##name(loc);
        MRS_PUNCTUATION_LIST(MRS_PUNCTUATION_SYMBOL)
#undef MRS_PUNCTUATION_SYMBOL
      }
      break;

    case Token::Kind::invalid:
      break;
  }

  driver.set_error(
      "Unexpected input '" + std::string(token.raw.substr(0, 20)) + "'",
      token.line, token.column);
  return Parser::make_YYUNDEF(loc);
}

}  // namespace parser

namespace {

std::string capitalize(std::string s) {
  if (!s.empty()) s[0] = static_cast<char>(std::toupper(s[0]));
  return s;
}

}  // namespace

Parse_error::Parse_error(const std::string &description, int line, int column)
    : std::runtime_error("Syntax Error: " + capitalize(description) + " [Ln " +
                         std::to_string(line) + ": Col " +
                         std::to_string(column) + "]"),
      m_description(capitalize(description)),
      m_line(line),
      m_column(column) {}

ast::Script parse_script(std::string_view script, const Sql_mode &sql_mode) {
  parser::Driver driver(script, sql_mode);
  parser::Parser parser(driver);

  const int rc = parser.parse();

  if (driver.error()) {
    const auto &error = *driver.error();
    throw Parse_error(error.message, error.line, error.column);
  }
  if (rc != 0) {
    throw Parse_error("Parsing failed", 0, 0);
  }

  return std::move(driver.script());
}

ast::Statement parse_statement(std::string_view statement,
                               const Sql_mode &sql_mode) {
  auto script = parse_script(statement, sql_mode);
  if (script.size() != 1) {
    throw Parse_error("Exactly one statement is expected", 1, 0);
  }
  return std::move(script.front());
}

const std::vector<std::string> &rest_sql_prefixes() {
  static const std::vector<std::string> prefixes = {
      "CONFIGURE REST ", "CREATE REST ",  "CREATE OR REPLACE REST ",
      "ALTER REST ",     "DROP REST ",    "USE REST ",
      "SHOW REST ",      "SHOW CREATE REST ", "GRANT REST ",
      "REVOKE REST ",    "CLONE REST ",   "DUMP REST ",
      "LOAD REST ",
  };
  return prefixes;
}

}  // namespace mrs
