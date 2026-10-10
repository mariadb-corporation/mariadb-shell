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

#include "modules/mrs/core/mrs_json.h"

#include <cstdio>
#include <cstdlib>

namespace mrs {
namespace json {

Value::Value(double v) : m_type(Type::number) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.17g", v);
  m_text = buffer;
}

int64_t Value::as_int() const {
  if (m_type == Type::number) return std::strtoll(m_text.c_str(), nullptr, 10);
  if (m_type == Type::boolean) return m_bool ? 1 : 0;
  if (m_type == Type::string) return std::strtoll(m_text.c_str(), nullptr, 10);
  return 0;
}

double Value::as_double() const {
  if (m_type == Type::number || m_type == Type::string) {
    return std::strtod(m_text.c_str(), nullptr);
  }
  if (m_type == Type::boolean) return m_bool ? 1.0 : 0.0;
  return 0.0;
}

const Value *Value::get(std::string_view key) const {
  if (m_type != Type::object) return nullptr;
  for (const auto &[k, v] : m_object) {
    if (k == key) return &v;
  }
  return nullptr;
}

Value *Value::get(std::string_view key) {
  if (m_type != Type::object) return nullptr;
  for (auto &[k, v] : m_object) {
    if (k == key) return &v;
  }
  return nullptr;
}

void Value::set(std::string_view key, Value value) {
  if (m_type != Type::object) {
    m_type = Type::object;
    m_object.clear();
  }
  if (auto *existing = get(key)) {
    *existing = std::move(value);
  } else {
    m_object.emplace_back(std::string(key), std::move(value));
  }
}

std::optional<Value> Value::remove(std::string_view key) {
  if (m_type != Type::object) return std::nullopt;
  for (auto it = m_object.begin(); it != m_object.end(); ++it) {
    if (it->first == key) {
      Value v = std::move(it->second);
      m_object.erase(it);
      return v;
    }
  }
  return std::nullopt;
}

bool Value::get_bool(std::string_view key, bool default_value) const {
  const auto *v = get(key);
  if (!v) return default_value;
  if (v->is_bool()) return v->as_bool();
  if (v->is_number()) return v->as_int() != 0;
  return default_value;
}

std::string Value::get_string(std::string_view key,
                              const std::string &default_value) const {
  const auto *v = get(key);
  if (!v || !v->is_string()) return default_value;
  return v->as_string();
}

std::string quote(std::string_view text) {
  std::string result = "\"";
  for (const unsigned char c : text) {
    switch (c) {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      case '\b':
        result += "\\b";
        break;
      case '\f':
        result += "\\f";
        break;
      default:
        if (c < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
          result += buffer;
        } else {
          result += static_cast<char>(c);
        }
    }
  }
  result += '"';
  return result;
}

std::string Value::dump(bool pretty, int indent_level) const {
  switch (m_type) {
    case Type::null:
      return "null";
    case Type::boolean:
      return m_bool ? "true" : "false";
    case Type::number:
      return m_text;
    case Type::string:
      return quote(m_text);
    case Type::array:
    case Type::object:
      break;
  }

  const bool is_object = m_type == Type::object;
  const size_t count = is_object ? m_object.size() : m_array.size();
  if (count == 0) return is_object ? "{}" : "[]";

  const std::string indent = pretty ? std::string((indent_level + 1) * 4, ' ') : "";
  const std::string closing_indent = pretty ? std::string(indent_level * 4, ' ') : "";
  const std::string separator = pretty ? ",\n" : ",";

  std::string result = is_object ? "{" : "[";
  if (pretty) result += "\n";
  for (size_t i = 0; i < count; ++i) {
    if (i > 0) result += separator;
    result += indent;
    if (is_object) {
      result += quote(m_object[i].first);
      result += pretty ? ": " : ":";
      result += m_object[i].second.dump(pretty, indent_level + 1);
    } else {
      result += m_array[i].dump(pretty, indent_level + 1);
    }
  }
  if (pretty) {
    result += "\n";
    result += closing_indent;
  }
  result += is_object ? "}" : "]";
  return result;
}

namespace {

class Parser {
 public:
  explicit Parser(std::string_view text) : m_text(text) {}

  Value parse_document() {
    skip_whitespace();
    Value value = parse_value();
    skip_whitespace();
    if (m_pos != m_text.size()) fail("Unexpected trailing characters");
    return value;
  }

 private:
  [[noreturn]] void fail(const std::string &message) const {
    throw Parse_error(message + " at offset " + std::to_string(m_pos));
  }

  char peek() const { return m_pos < m_text.size() ? m_text[m_pos] : '\0'; }

  void skip_whitespace() {
    while (m_pos < m_text.size() &&
           (m_text[m_pos] == ' ' || m_text[m_pos] == '\t' ||
            m_text[m_pos] == '\n' || m_text[m_pos] == '\r')) {
      ++m_pos;
    }
  }

  void expect(char c) {
    if (peek() != c) fail(std::string("Expected '") + c + "'");
    ++m_pos;
  }

  bool consume_literal(std::string_view literal) {
    if (m_text.substr(m_pos, literal.size()) == literal) {
      m_pos += literal.size();
      return true;
    }
    return false;
  }

  Value parse_value() {
    switch (peek()) {
      case '{':
        return parse_object();
      case '[':
        return parse_array();
      case '"':
        return Value(parse_string());
      case 't':
        if (consume_literal("true")) return Value(true);
        break;
      case 'f':
        if (consume_literal("false")) return Value(false);
        break;
      case 'n':
        if (consume_literal("null")) return Value(nullptr);
        break;
      default:
        if (peek() == '-' || (peek() >= '0' && peek() <= '9')) {
          return parse_number();
        }
        break;
    }
    fail("Unexpected character");
  }

  Value parse_object() {
    expect('{');
    Value::Object members;
    skip_whitespace();
    if (peek() == '}') {
      ++m_pos;
      return Value(std::move(members));
    }
    for (;;) {
      skip_whitespace();
      if (peek() != '"') fail("Expected a string key");
      std::string key = parse_string();
      skip_whitespace();
      expect(':');
      skip_whitespace();
      members.emplace_back(std::move(key), parse_value());
      skip_whitespace();
      if (peek() == ',') {
        ++m_pos;
        continue;
      }
      expect('}');
      return Value(std::move(members));
    }
  }

  Value parse_array() {
    expect('[');
    Value::Array items;
    skip_whitespace();
    if (peek() == ']') {
      ++m_pos;
      return Value(std::move(items));
    }
    for (;;) {
      skip_whitespace();
      items.push_back(parse_value());
      skip_whitespace();
      if (peek() == ',') {
        ++m_pos;
        continue;
      }
      expect(']');
      return Value(std::move(items));
    }
  }

  static void append_utf8(std::string *out, uint32_t cp) {
    if (cp < 0x80) {
      *out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      *out += static_cast<char>(0xc0 | (cp >> 6));
      *out += static_cast<char>(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
      *out += static_cast<char>(0xe0 | (cp >> 12));
      *out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
      *out += static_cast<char>(0x80 | (cp & 0x3f));
    } else {
      *out += static_cast<char>(0xf0 | (cp >> 18));
      *out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
      *out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
      *out += static_cast<char>(0x80 | (cp & 0x3f));
    }
  }

  uint32_t parse_hex4() {
    if (m_pos + 4 > m_text.size()) fail("Truncated \\u escape");
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = m_text[m_pos++];
      value <<= 4;
      if (c >= '0' && c <= '9') {
        value |= static_cast<uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value |= static_cast<uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        value |= static_cast<uint32_t>(c - 'A' + 10);
      } else {
        fail("Invalid \\u escape");
      }
    }
    return value;
  }

  std::string parse_string() {
    expect('"');
    std::string result;
    for (;;) {
      if (m_pos >= m_text.size()) fail("Unterminated string");
      const char c = m_text[m_pos++];
      if (c == '"') return result;
      if (c != '\\') {
        result += c;
        continue;
      }
      if (m_pos >= m_text.size()) fail("Unterminated escape");
      const char e = m_text[m_pos++];
      switch (e) {
        case '"':
        case '\\':
        case '/':
          result += e;
          break;
        case 'b':
          result += '\b';
          break;
        case 'f':
          result += '\f';
          break;
        case 'n':
          result += '\n';
          break;
        case 'r':
          result += '\r';
          break;
        case 't':
          result += '\t';
          break;
        case 'u': {
          uint32_t cp = parse_hex4();
          if (cp >= 0xd800 && cp <= 0xdbff) {
            // A high surrogate needs its low one; anything else is no pair
            const size_t before = m_pos;
            if (consume_literal("\\u")) {
              const uint32_t low = parse_hex4();
              if (low >= 0xdc00 && low <= 0xdfff) {
                cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
              } else {
                m_pos = before;
              }
            }
          }
          if (cp >= 0xd800 && cp <= 0xdfff) fail("Invalid \\u surrogate");
          append_utf8(&result, cp);
          break;
        }
        default:
          fail("Invalid escape");
      }
    }
  }

  Value parse_number() {
    const size_t start = m_pos;
    if (peek() == '-') ++m_pos;
    while (peek() >= '0' && peek() <= '9') ++m_pos;
    if (peek() == '.') {
      ++m_pos;
      while (peek() >= '0' && peek() <= '9') ++m_pos;
    }
    if (peek() == 'e' || peek() == 'E') {
      ++m_pos;
      if (peek() == '+' || peek() == '-') ++m_pos;
      while (peek() >= '0' && peek() <= '9') ++m_pos;
    }
    if (m_pos == start) fail("Invalid number");
    return Value::number(std::string(m_text.substr(start, m_pos - start)));
  }

  std::string_view m_text;
  size_t m_pos = 0;
};

}  // namespace

Value parse(std::string_view text) { return Parser(text).parse_document(); }

std::optional<Value> try_parse(std::string_view text) {
  if (text.empty()) return std::nullopt;
  try {
    return parse(text);
  } catch (const Parse_error &) {
    return std::nullopt;
  }
}

std::string pretty(std::string_view text) {
  const auto value = try_parse(text);
  if (!value) return std::string(text);
  return value->dump(true);
}

}  // namespace json
}  // namespace mrs
