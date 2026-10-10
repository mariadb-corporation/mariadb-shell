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

#ifndef MODULES_MRS_CORE_MRS_JSON_H_
#define MODULES_MRS_CORE_MRS_JSON_H_

// A small JSON document model for the MRS core: enough to read the option
// documents of the metadata and to print them the way SHOW CREATE does. It
// keeps the core free of a JSON library dependency.

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mrs {
namespace json {

class Parse_error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class Value {
 public:
  enum class Type { null, boolean, number, string, array, object };

  // Objects keep the order of their members.
  using Object = std::vector<std::pair<std::string, Value>>;
  using Array = std::vector<Value>;

  Value() = default;
  Value(std::nullptr_t) {}
  Value(bool b) : m_type(Type::boolean), m_bool(b) {}
  Value(int v) : Value(static_cast<int64_t>(v)) {}
  Value(int64_t v) : m_type(Type::number), m_text(std::to_string(v)) {}
  Value(double v);
  Value(const char *s) : m_type(Type::string), m_text(s) {}
  Value(std::string s) : m_type(Type::string), m_text(std::move(s)) {}
  Value(Array a) : m_type(Type::array), m_array(std::move(a)) {}
  Value(Object o) : m_type(Type::object), m_object(std::move(o)) {}

  // A number from its JSON text.
  static Value number(std::string text) {
    Value v;
    v.m_type = Type::number;
    v.m_text = std::move(text);
    return v;
  }
  static Value object() { return Value(Object{}); }
  static Value array() { return Value(Array{}); }

  Type type() const { return m_type; }
  bool is_null() const { return m_type == Type::null; }
  bool is_bool() const { return m_type == Type::boolean; }
  bool is_number() const { return m_type == Type::number; }
  bool is_string() const { return m_type == Type::string; }
  bool is_array() const { return m_type == Type::array; }
  bool is_object() const { return m_type == Type::object; }

  bool as_bool() const { return m_bool; }
  const std::string &as_string() const { return m_text; }
  int64_t as_int() const;
  double as_double() const;
  const Array &as_array() const { return m_array; }
  Array &as_array() { return m_array; }
  const Object &as_object() const { return m_object; }
  Object &as_object() { return m_object; }

  // Object access. get() returns nullptr for a missing key or a non-object.
  const Value *get(std::string_view key) const;
  Value *get(std::string_view key);
  bool has(std::string_view key) const { return get(key) != nullptr; }
  // Sets or replaces a member, keeping the position of an existing one.
  void set(std::string_view key, Value value);
  // Removes a member; returns it if it was present.
  std::optional<Value> remove(std::string_view key);

  // Convenience readers for option documents.
  bool get_bool(std::string_view key, bool default_value = false) const;
  std::string get_string(std::string_view key,
                         const std::string &default_value = {}) const;

  // Serialisation. compact: {"a":1,"b":[1,2]}
  // pretty (indent 4, as Python's json.dumps(indent=4)):
  // {
  //     "a": 1
  // }
  std::string dump(bool pretty = false, int indent_level = 0) const;

 private:
  Type m_type = Type::null;
  bool m_bool = false;
  std::string m_text;
  Array m_array;
  Object m_object;
};

// Parses a JSON document. Throws Parse_error.
Value parse(std::string_view text);

// Parses a document, or returns nullopt for an empty text or invalid JSON.
std::optional<Value> try_parse(std::string_view text);

// Writes a string as a JSON string literal with quotes.
std::string quote(std::string_view text);

// Reformats a JSON text in the pretty layout; invalid JSON is returned
// unchanged.
std::string pretty(std::string_view text);

}  // namespace json
}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_JSON_H_
