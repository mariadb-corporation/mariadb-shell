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
#include <optional>
#include <string>
#include <stdexcept>

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
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\'':
        result += "\\'";
        break;
      case '"':
        result += "\\\"";
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

namespace {

int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::optional<std::string> base64_decode(std::string_view text) {
  static const std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  uint32_t accumulator = 0;
  int bits = 0;
  for (const char c : text) {
    if (c == '=') break;
    const auto pos = alphabet.find(c);
    if (pos == std::string_view::npos) return std::nullopt;
    accumulator = (accumulator << 6) | static_cast<uint32_t>(pos);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      result += static_cast<char>((accumulator >> bits) & 0xff);
    }
  }
  return result;
}

}  // namespace

namespace {

// The canonical lower case UUID text of 16 bytes.
Id uuid_from_bytes(std::string_view bytes) {
  static const char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(36);
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) result += '-';
    const auto c = static_cast<unsigned char>(bytes[i]);
    result += digits[c >> 4];
    result += digits[c & 0x0f];
  }
  return result;
}

// The bytes of a string of hex digits, or nothing when it is not one.
std::optional<std::string> bytes_from_hex(std::string_view digits) {
  if (digits.size() % 2 != 0) return std::nullopt;
  std::string result;
  for (size_t i = 0; i < digits.size(); i += 2) {
    const int hi = hex_digit(digits[i]);
    const int lo = hex_digit(digits[i + 1]);
    if (hi < 0 || lo < 0) return std::nullopt;
    result += static_cast<char>((hi << 4) | lo);
  }
  return result;
}

}  // namespace

std::string id(const Id &uuid) { return quote(uuid); }

Id id_from_string(std::string_view text, std::string_view context) {
  const auto invalid = [&](std::string_view what) {
    return std::runtime_error("Invalid " + std::string(what) + " '" +
                              std::string(text) + "' for '" +
                              std::string(context) + "'.");
  };

  std::optional<std::string> bytes;
  if (text.size() == 36 && text[8] == '-' && text[13] == '-' &&
      text[18] == '-' && text[23] == '-') {
    // The UUID text itself
    std::string digits(text);
    std::erase(digits, '-');
    bytes = bytes_from_hex(digits);
    if (!bytes) throw invalid("UUID");
  } else if (text.size() > 2 && text[0] == '0' &&
             (text[1] == 'x' || text[1] == 'X')) {
    // The 0x... form the metadata schema versions before 5.0.0 used
    bytes = bytes_from_hex(text.substr(2));
    if (!bytes) throw invalid("hexadecimal string");
  } else if (text.size() > 2 && text.substr(text.size() - 2) == "==") {
    // base64 of the bytes, as in the SDK configuration
    bytes = base64_decode(text);
    if (!bytes) throw invalid("base64 string");
  } else {
    throw invalid("id format");
  }

  if (bytes->size() != 16) {
    throw std::runtime_error("The '" + std::string(context) +
                             "' has an invalid size.");
  }
  return uuid_from_bytes(*bytes);
}

std::string metadata_table(std::string_view table) {
  if (table.find('.') != std::string_view::npos) return std::string(table);
  return quote_identifier(k_metadata_schema) + "." + quote_identifier(table);
}

Value::Value(double v) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.17g", v);
  m_sql = buffer;
}

std::string Insert::str() const {
  std::string columns;
  std::string values;
  for (const auto &[column, value] : m_columns) {
    if (!columns.empty()) {
      columns += ", ";
      values += ", ";
    }
    columns += quote_identifier(column);
    values += value.str();
  }
  return "INSERT INTO " + m_table + " (" + columns + ") VALUES (" + values + ")";
}

std::string Update::str() const {
  std::string sets;
  for (const auto &[column, value] : m_sets) {
    if (!sets.empty()) sets += ", ";
    sets += quote_identifier(column) + " = " + value.str();
  }
  for (const auto &raw : m_raw_sets) {
    if (!sets.empty()) sets += ", ";
    sets += raw;
  }
  std::string result = "UPDATE " + m_table + " SET " + sets;
  if (!m_wheres.empty()) {
    result += " WHERE ";
    for (size_t i = 0; i < m_wheres.size(); ++i) {
      if (i > 0) result += " AND ";
      result += m_wheres[i];
    }
  }
  return result;
}

}  // namespace sql
}  // namespace mrs
