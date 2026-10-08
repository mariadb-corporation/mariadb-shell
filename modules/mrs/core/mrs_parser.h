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

#ifndef MODULES_MRS_CORE_MRS_PARSER_H_
#define MODULES_MRS_CORE_MRS_PARSER_H_

// The public entry point of the MRS SQL extension parser.

#include <stdexcept>
#include <string>
#include <string_view>

#include "modules/mrs/core/mrs_ast.h"
#include "modules/mrs/core/mrs_lexer.h"

namespace mrs {

// A syntax error. The message has the form the Python plugin used:
// "Syntax Error: <description> [Ln <line>: Col <column>]".
class Parse_error : public std::runtime_error {
 public:
  Parse_error(const std::string &description, int line, int column);

  // The description without the position suffix.
  const std::string &description() const { return m_description; }
  int line() const { return m_line; }
  int column() const { return m_column; }

 private:
  std::string m_description;
  int m_line;
  int m_column;
};

// Parses a script of REST SQL statements separated by semicolons. Throws
// Parse_error for the first syntax error.
ast::Script parse_script(std::string_view script, const Sql_mode &sql_mode = {});

// Parses a script that must consist of exactly one statement.
ast::Statement parse_statement(std::string_view statement,
                               const Sql_mode &sql_mode = {});

// The prefixes of the SQL statements the MRS extension handles. They match
// the beginning of a statement case-insensitively, after leading white space.
const std::vector<std::string> &rest_sql_prefixes();

}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_PARSER_H_
