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

#include "modules/mrs/core/mrs_lexer.h"

#include <cctype>
#include <unordered_map>

#include "modules/mrs/core/mrs_strings.h"

namespace mrs {

namespace {

const std::unordered_map<std::string, Token::Keyword> &keyword_map() {
  static const std::unordered_map<std::string, Token::Keyword> map = {
#define MRS_KEYWORD_ENTRY(name, text) {text, Token::Keyword::name},
      MRS_KEYWORD_LIST(MRS_KEYWORD_ENTRY)
#undef MRS_KEYWORD_ENTRY
      // Aliases, as in the ANTLR lexer: SCHEMA(S) lexes as DATABASE(S).
      {"SCHEMA", Token::Keyword::DATABASE_SYMBOL},
      {"SCHEMAS", Token::Keyword::DATABASES_SYMBOL},
  };
  return map;
}

const std::unordered_map<std::string, Token::Annotation> &annotation_map() {
  static const std::unordered_map<std::string, Token::Annotation> map = {
#define MRS_ANNOTATION_ENTRY(name, text) {text, Token::Annotation::name},
      MRS_ANNOTATION_LIST(MRS_ANNOTATION_ENTRY)
#undef MRS_ANNOTATION_ENTRY
  };
  return map;
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

}  // namespace

bool is_identifier_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
         c == '$' || static_cast<unsigned char>(c) >= 0x80;
}

bool is_identifier_char(char c) { return is_identifier_start(c) || is_digit(c); }

Sql_mode Sql_mode::from_string(std::string_view sql_mode) {
  Sql_mode mode;
  size_t start = 0;
  while (start <= sql_mode.size()) {
    auto end = sql_mode.find(',', start);
    if (end == std::string_view::npos) end = sql_mode.size();
    auto item = to_upper(sql_mode.substr(start, end - start));
    // trim
    item.erase(0, item.find_first_not_of(" \t"));
    item.erase(item.find_last_not_of(" \t") + 1);
    if (item == "ANSI_QUOTES" || item == "ANSI") mode.ansi_quotes = true;
    if (item == "NO_BACKSLASH_ESCAPES") mode.no_backslash_escapes = true;
    start = end + 1;
  }
  return mode;
}

std::string_view Lexer::keyword_text(Token::Keyword keyword) {
  switch (keyword) {
#define MRS_KEYWORD_TEXT(name, text) \
  case Token::Keyword::name:         \
    return text;
    MRS_KEYWORD_LIST(MRS_KEYWORD_TEXT)
#undef MRS_KEYWORD_TEXT
  }
  return "";
}

Lexer::Lexer(std::string_view input, const Sql_mode &sql_mode)
    : m_input(input), m_sql_mode(sql_mode) {}

void Lexer::advance(size_t count) {
  for (size_t i = 0; i < count && m_pos < m_input.size(); ++i) {
    if (m_input[m_pos] == '\n') {
      ++m_line;
      m_column = 0;
    } else {
      ++m_column;
    }
    ++m_pos;
  }
}

void Lexer::skip_whitespace_and_comments() {
  while (!at_end()) {
    const char c = peek();
    if (is_space(c) || (c > 0 && c < ' ')) {
      // Also drops the control characters the ANTLR lexer flagged as invalid
      // input; they carry no meaning.
      advance();
    } else if (c == '#') {
      // A line comment ends at \n or \r
      while (!at_end() && peek() != '\n' && peek() != '\r') advance();
    } else if (c == '-' && peek(1) == '-' &&
               (peek(2) == ' ' || peek(2) == '\t' || peek(2) == '\n' ||
                peek(2) == '\r' || peek(2) == '\0')) {
      while (!at_end() && peek() != '\n' && peek() != '\r') advance();
    } else if (c == '/' && peek(1) == '*' && peek(2) != '!') {
      // An unterminated comment, and a /*! ... */ version comment, whose
      // content the server would run, are left to the parser as invalid
      // input
      const auto end = m_input.find("*/", m_pos + 2);
      if (end == std::string_view::npos) break;
      advance(end + 2 - m_pos);
    } else {
      break;
    }
  }
}

Token Lexer::make_token(Token::Kind kind, size_t start) const {
  Token token;
  token.kind = kind;
  token.raw = m_input.substr(start, m_pos - start);
  token.text = std::string(token.raw);
  token.offset = start;
  return token;
}

Token Lexer::next() {
  skip_whitespace_and_comments();

  const size_t start = m_pos;
  const int line = m_line;
  const int column = m_column;

  Token token;
  if (at_end()) {
    token = make_token(Token::Kind::end_of_input, start);
  } else {
    const char c = peek();
    if (is_digit(c) || (c == '.' && is_digit(peek(1)))) {
      token = lex_number(start);
    } else if (is_identifier_start(c)) {
      token = lex_identifier_or_keyword(start);
    } else if (c == '`' || c == '\'' || c == '"') {
      token = lex_quoted(start, c);
    } else if (c == '/' && is_identifier_char(peek(1))) {
      token = lex_request_path(start);
    } else if (c == '@') {
      token = lex_at(start);
    } else {
      advance();
      token = make_token(Token::Kind::invalid, start);
      switch (c) {
#define MRS_PUNCTUATION_CASE(name, ch)                 \
  case ch:                                             \
    token.kind = Token::Kind::punctuation;             \
    token.punctuation = Token::Punctuation::name;      \
    break;
        MRS_PUNCTUATION_LIST(MRS_PUNCTUATION_CASE)
#undef MRS_PUNCTUATION_CASE
        default:
          break;
      }
    }
  }

  token.line = line;
  token.column = column;
  return token;
}

Token Lexer::lex_number(size_t start) {
  // digits, a decimal part and an exponent: INT_NUMBER, DECIMAL_NUMBER,
  // FLOAT_NUMBER. Digits followed by identifier characters form an
  // identifier (`1abc`), as in the server.
  auto kind = Token::Kind::int_number;

  // 0x1F and 0b01 are hexadecimal and binary literals, which no rule takes,
  // not names; followed by more identifier characters they are a name
  if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X' || peek(1) == 'b' ||
                        peek(1) == 'B')) {
    const bool hex = peek(1) == 'x' || peek(1) == 'X';
    size_t length = 2;
    while (hex ? std::isxdigit(static_cast<unsigned char>(peek(length)))
               : (peek(length) == '0' || peek(length) == '1')) {
      ++length;
    }
    if (length > 2 && !is_identifier_char(peek(length))) {
      advance(length);
      return make_token(Token::Kind::invalid, start);
    }
  }

  while (is_digit(peek())) advance();

  if (peek() == '.' && is_digit(peek(1))) {
    kind = Token::Kind::decimal_number;
    advance();
    while (is_digit(peek())) advance();
  } else if (peek() == '.' && m_pos == start) {
    // cannot happen: a leading '.' is only lexed as a number with a digit
    advance();
  }

  if ((peek() == 'e' || peek() == 'E') &&
      (is_digit(peek(1)) ||
       ((peek(1) == '+' || peek(1) == '-') && is_digit(peek(2))))) {
    kind = Token::Kind::float_number;
    advance();
    if (peek() == '+' || peek() == '-') advance();
    while (is_digit(peek())) advance();
  }

  if (kind == Token::Kind::int_number && is_identifier_char(peek())) {
    while (is_identifier_char(peek())) advance();
    return make_token(Token::Kind::identifier, start);
  }

  return make_token(kind, start);
}

Token Lexer::lex_identifier_or_keyword(size_t start) {
  while (is_identifier_char(peek())) advance();

  auto token = make_token(Token::Kind::identifier, start);
  const auto &keywords = keyword_map();
  const auto it = keywords.find(to_upper(token.raw));
  if (it != keywords.end()) {
    token.kind = Token::Kind::keyword;
    token.keyword = it->second;
  }
  return token;
}

Token Lexer::lex_quoted(size_t start, char quote) {
  // Backslash escapes are honoured in strings unless NO_BACKSLASH_ESCAPES is
  // set, and never in identifiers (backtick quoted, or double quoted with
  // ANSI_QUOTES). A doubled quote stands for a single one in both cases.
  const bool is_identifier =
      quote == '`' || (quote == '"' && m_sql_mode.ansi_quotes);
  const bool backslash_escapes =
      !is_identifier && !m_sql_mode.no_backslash_escapes;

  std::string value;
  bool closed = false;

  // A doubled quote inside the literal is one quote character ('it''s');
  // separate literals ('a' 'b') do not concatenate, as in the ANTLR grammar.
  advance();  // the opening quote
  while (!at_end()) {
    const char c = peek();
    if (c == '\\' && backslash_escapes && m_pos + 1 < m_input.size()) {
      const char e = peek(1);
      advance(2);
      switch (e) {
        case 'n':
          value += '\n';
          break;
        case 't':
          value += '\t';
          break;
        case 'r':
          value += '\r';
          break;
        case 'b':
          value += '\b';
          break;
        case '0':
          value += '\0';
          break;
        case 'Z':
          value += '\032';
          break;
        case '%':
        case '_':
          value += '\\';
          value += e;
          break;
        default:
          value += e;
          break;
      }
    } else if (c == quote) {
      if (peek(1) == quote) {
        value += quote;
        advance(2);
      } else {
        advance();
        closed = true;
        break;
      }
    } else {
      value += c;
      advance();
    }
  }

  auto token = make_token(closed ? (quote == '`' ? Token::Kind::back_tick_quoted_id
                                    : quote == '\''
                                        ? Token::Kind::single_quoted_text
                                        : Token::Kind::double_quoted_text)
                                 : Token::Kind::invalid,
                          start);
  token.text = std::move(value);
  return token;
}

Token Lexer::lex_request_path(size_t start) {
  // (/identifier)+, where a segment of digits only, or a number with an
  // exponent like 1e5, is no identifier, as in the server
  bool valid = true;
  while (peek() == '/' && is_identifier_char(peek(1))) {
    advance();
    const auto segment_start = m_pos;
    while (is_identifier_char(peek())) advance();
    const auto segment = m_input.substr(segment_start, m_pos - segment_start);
    const auto digits = segment.find_first_not_of("0123456789");
    if (digits == std::string_view::npos) {
      valid = false;
    } else if (digits > 0 && (segment[digits] == 'e' || segment[digits] == 'E') &&
               digits + 1 < segment.size() &&
               segment.find_first_not_of("0123456789", digits + 1) ==
                   std::string_view::npos) {
      valid = false;
    }
  }
  return make_token(valid ? Token::Kind::rest_request_path
                          : Token::Kind::invalid,
                    start);
}

Token Lexer::lex_at(size_t start) {
  advance();  // '@'
  const size_t word_start = m_pos;
  const int line = m_line;
  const int column = m_column;
  while (is_identifier_char(peek())) advance();

  if (m_pos > word_start) {
    const auto word = to_upper(m_input.substr(word_start, m_pos - word_start));
    const auto &annotations = annotation_map();
    const auto it = annotations.find(word);
    if (it != annotations.end()) {
      auto token = make_token(Token::Kind::annotation, start);
      token.annotation = it->second;
      return token;
    }
    // Not an annotation: rewind to right after the '@'. The word is lexed
    // as the next token (`user@app`).
    m_pos = word_start;
    m_line = line;
    m_column = column;
  }

  auto token = make_token(Token::Kind::punctuation, start);
  token.punctuation = Token::Punctuation::AT_SIGN_SYMBOL;
  return token;
}

}  // namespace mrs
