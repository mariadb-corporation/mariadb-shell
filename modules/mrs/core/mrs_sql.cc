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

#include "modules/mrs/core/mrs_sql.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace mrs {
namespace sql {

std::string quote(std::string_view text) {
  std::string result;
  result.reserve(text.size() + 2);
  result += '\'';
  for (const char c : text) {
    switch (c) {
      case '\0':
        result += "\\0";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\'':
        result += "''";
        break;
      case '\032':
        result += "\\Z";
        break;
      default:
        result += c;
    }
  }
  result += '\'';
  return result;
}

std::string quote_identifier(std::string_view name) {
  std::string result;
  result.reserve(name.size() + 2);
  result += '`';
  for (const char c : name) {
    if (c == '`') result += '`';
    result += c;
  }
  result += '`';
  return result;
}

std::string quote_qualified(std::string_view schema, std::string_view name) {
  return quote_identifier(schema) + "." + quote_identifier(name);
}

std::string hex(std::string_view binary) {
  static const char digits[] = "0123456789abcdef";
  std::string result = "0x";
  result.reserve(2 + binary.size() * 2);
  for (const unsigned char c : binary) {
    result += digits[c >> 4];
    result += digits[c & 0x0f];
  }
  return result;
}

std::string metadata_table(std::string_view table) {
  if (table.find('.') != std::string_view::npos) return std::string(table);
  return std::string(k_metadata_schema_marker) + "." + quote_identifier(table);
}

Quoting Quoting::from_sql_mode(std::string_view sql_mode) {
  Quoting quoting;
  size_t start = 0;
  while (start <= sql_mode.size()) {
    auto end = sql_mode.find(',', start);
    if (end == std::string_view::npos) end = sql_mode.size();
    if (sql_mode.substr(start, end - start) == "NO_BACKSLASH_ESCAPES") {
      quoting.no_backslash_escapes = true;
    }
    start = end + 1;
  }
  return quoting;
}

std::string literal(std::string_view text, const Quoting &quoting) {
  std::string result;
  result.reserve(text.size() + 2);
  result += '\'';
  for (const char c : text) {
    if (c == '\'') {
      result += "''";
    } else if (quoting.no_backslash_escapes) {
      result += c;
    } else if (c == '\\') {
      result += "\\\\";
    } else if (c == '\0') {
      result += "\\0";
    } else if (c == '\032') {
      result += "\\Z";
    } else {
      result += c;
    }
  }
  result += '\'';
  return result;
}

Value::Value(double v) : m_kind(Kind::raw) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.17g", v);
  m_data = buffer;
}

std::string Value::render(const Quoting &quoting) const {
  switch (m_kind) {
    case Kind::null:
      return "NULL";
    case Kind::raw:
      return m_data;
    case Kind::text:
      break;
  }
  return literal(m_data, quoting);
}

std::string Statement::render(const Quoting &quoting) const {
  std::string result;
  result.reserve(text.size() + params.size() * 40);
  size_t next = 0;

  const auto size = text.size();
  for (size_t i = 0; i < size; ++i) {
    const char c = text[i];
    if (c == '\'' || c == '"' || c == '`') {
      // A quoted string or identifier, copied as it is
      size_t j = i + 1;
      while (j < size) {
        if (c != '`' && text[j] == '\\' && !quoting.no_backslash_escapes) {
          j += 2;
          continue;
        }
        if (text[j] == c) {
          if (j + 1 < size && text[j + 1] == c) {
            j += 2;
            continue;
          }
          break;
        }
        ++j;
      }
      const size_t end = std::min(j + 1, size);
      result.append(text, i, end - i);
      i = end - 1;
    } else if (c == '#' || (c == '-' && i + 2 < size && text[i + 1] == '-' &&
                            (text[i + 2] == ' ' || text[i + 2] == '\t'))) {
      const auto end = std::min(text.find('\n', i), size);
      result.append(text, i, end - i);
      i = end - 1;
    } else if (c == '/' && i + 1 < size && text[i + 1] == '*') {
      const auto close = text.find("*/", i + 2);
      const size_t end = close == std::string::npos ? size : close + 2;
      result.append(text, i, end - i);
      i = end - 1;
    } else if (c == '?') {
      if (next >= params.size()) {
        throw std::logic_error("More placeholders than values in: " + text);
      }
      result += params[next++].render(quoting);
    } else {
      result += c;
    }
  }
  if (next != params.size()) {
    throw std::logic_error("More values than placeholders in: " + text);
  }
  return result;
}

void Where::add(std::string_view column, Value value) {
  m_conditions.emplace_back(quote_identifier(column) + " = ?",
                            std::vector<Value>{std::move(value)});
}

void Where::add_raw(std::string condition, std::vector<Value> params) {
  m_conditions.emplace_back(std::move(condition), std::move(params));
}

std::string Where::text(std::vector<Value> *params) const {
  std::string result;
  for (const auto &[condition, values] : m_conditions) {
    result += result.empty() ? " WHERE " : " AND ";
    result += condition;
    params->insert(params->end(), values.begin(), values.end());
  }
  return result;
}

bool Insert::empty() const {
  for (const auto &row : m_rows) {
    if (!row.empty()) return false;
  }
  return true;
}

Statement Insert::statement() const {
  // The columns of all rows, in the order they first appear
  std::vector<std::string> columns;
  for (const auto &row : m_rows) {
    for (const auto &[column, value] : row) {
      if (std::find(columns.begin(), columns.end(), column) == columns.end()) {
        columns.push_back(column);
      }
    }
  }

  Statement statement;
  statement.text = "INSERT INTO " + m_table + " (";
  for (size_t i = 0; i < columns.size(); ++i) {
    if (i > 0) statement.text += ", ";
    statement.text += quote_identifier(columns[i]);
  }
  statement.text += ") VALUES ";

  bool first_row = true;
  for (const auto &row : m_rows) {
    if (row.empty()) continue;
    statement.text += first_row ? "(" : ", (";
    first_row = false;
    for (size_t i = 0; i < columns.size(); ++i) {
      if (i > 0) statement.text += ", ";
      const auto it = std::find_if(row.begin(), row.end(), [&](const auto &cell) {
        return cell.first == columns[i];
      });
      if (it == row.end()) {
        statement.text += "DEFAULT";
      } else {
        statement.text += "?";
        statement.params.push_back(it->second);
      }
    }
    statement.text += ")";
  }
  return statement;
}

Statement Update::statement() const {
  Statement statement;
  statement.text = "UPDATE " + m_table + " SET ";
  bool first = true;
  for (const auto &[column, value] : m_sets) {
    if (!first) statement.text += ", ";
    first = false;
    statement.text += quote_identifier(column) + " = ?";
    statement.params.push_back(value);
  }
  for (const auto &[assignment, values] : m_raw_sets) {
    if (!first) statement.text += ", ";
    first = false;
    statement.text += assignment;
    statement.params.insert(statement.params.end(), values.begin(), values.end());
  }
  statement.text += m_where.text(&statement.params);
  return statement;
}

Statement Delete::statement() const {
  Statement statement;
  statement.text = "DELETE FROM " + m_table;
  statement.text += m_where.text(&statement.params);
  return statement;
}

}  // namespace sql
}  // namespace mrs
