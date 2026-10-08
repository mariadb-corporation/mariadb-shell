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

#ifndef MODULES_MRS_CORE_MRS_PARSER_DRIVER_H_
#define MODULES_MRS_CORE_MRS_PARSER_DRIVER_H_

// Glue between the bison generated parser (mrs_parser.yy) and the lexer. Only
// mrs_parser.cc and the generated parser include this header.

#include <deque>
#include <optional>
#include <string>
#include <string_view>

#include "modules/mrs/core/mrs_ast.h"
#include "modules/mrs/core/mrs_lexer.h"

namespace mrs {
namespace parser {

class Driver {
 public:
  Driver(std::string_view input, const Sql_mode &sql_mode)
      : m_lexer(input, sql_mode) {}

  Lexer &lexer() { return m_lexer; }
  ast::Script &script() { return m_script; }

  struct Error {
    std::string message;
    int line = 0;
    int column = 0;
  };

  void set_error(std::string message, int line, int column) {
    if (!m_error) m_error = Error{std::move(message), line, column};
  }
  const std::optional<Error> &error() const { return m_error; }

  // Keyword tokens carry no value. For the places where a keyword is used
  // as a name (data mapping fields), the text of the recently lexed tokens
  // is kept and looked up by position.
  void remember_token(int line, int column, std::string text) {
    if (m_recent_tokens.size() >= 8) m_recent_tokens.pop_front();
    m_recent_tokens.push_back({line, column, std::move(text)});
  }
  std::string token_text(int line, int column) const {
    for (const auto &token : m_recent_tokens) {
      if (token.line == line && token.column == column) return token.text;
    }
    return {};
  }

 private:
  struct Recent_token {
    int line;
    int column;
    std::string text;
  };

  Lexer m_lexer;
  ast::Script m_script;
  std::optional<Error> m_error;
  std::deque<Recent_token> m_recent_tokens;
};

}  // namespace parser
}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_PARSER_DRIVER_H_
