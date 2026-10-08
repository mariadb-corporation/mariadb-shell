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

#ifndef MODULES_MRS_CORE_MRS_SQL_H_
#define MODULES_MRS_CORE_MRS_SQL_H_

// Helpers to build the SQL statements the MRS core runs against the metadata
// schema. Values are quoted into the statement text, no placeholders are
// used, so a complete statement is handed to the session.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mrs {

// A metadata id: the canonical lower case text of a UUID column
// (metadata schema 5.0.0), e.g. "31000000-0000-0000-0000-000000000000".
using Id = std::string;

// The name of the metadata schema.
inline constexpr std::string_view k_metadata_schema = "mysql_rest_service_metadata";

namespace sql {

// 'text' with the MySQL escapes applied.
std::string quote(std::string_view text);
// `name` with the backticks inside doubled.
std::string quote_identifier(std::string_view name);
// `schema`.`name`
std::string quote_qualified(std::string_view schema, std::string_view name);
// 0x... of binary data, e.g. the content of a file.
std::string hex(std::string_view binary);
// The SQL literal of an id: the quoted UUID text.
std::string id(const Id &uuid);
// The id of a UUID text, of the 0x... hex form ids had before metadata
// schema 5.0.0, or of base64 (ending in ==) of the 16 bytes. Throws on bad
// input.
Id id_from_string(std::string_view text, std::string_view context);
// `mysql_rest_service_metadata`.`table`, or the name itself when it is
// already qualified.
std::string metadata_table(std::string_view table);

// A SQL value: NULL, a number, a quoted string or raw SQL.
class Value {
 public:
  Value() : m_sql("NULL") {}
  Value(std::nullptr_t) : m_sql("NULL") {}
  Value(bool v) : m_sql(v ? "1" : "0") {}
  Value(int v) : m_sql(std::to_string(v)) {}
  Value(int64_t v) : m_sql(std::to_string(v)) {}
  Value(uint64_t v) : m_sql(std::to_string(v)) {}
  Value(double v);
  Value(std::string_view v) : m_sql(quote(v)) {}
  Value(const std::string &v) : m_sql(quote(v)) {}
  Value(const char *v) : m_sql(quote(v)) {}

  template <typename T>
  Value(const std::optional<T> &v) : Value() {
    if (v) *this = Value(*v);
  }

  // Raw SQL, not quoted.
  static Value raw(std::string sql) {
    Value v;
    v.m_sql = std::move(sql);
    return v;
  }
  // An id as quoted UUID text; an empty or absent id is NULL.
  static Value id(const Id &uuid) {
    return uuid.empty() ? Value() : raw(sql::id(uuid));
  }
  static Value id(const std::optional<Id> &uuid) {
    return uuid ? id(*uuid) : Value();
  }

  const std::string &str() const { return m_sql; }

 private:
  std::string m_sql;
};

// INSERT INTO <table> (cols) VALUES (values)
class Insert {
 public:
  explicit Insert(std::string_view table) : m_table(metadata_table(table)) {}

  Insert &set(std::string_view column, Value value) {
    m_columns.emplace_back(column, std::move(value));
    return *this;
  }
  std::string str() const;

 private:
  std::string m_table;
  std::vector<std::pair<std::string, Value>> m_columns;
};

// UPDATE <table> SET col = value, ... WHERE ...
class Update {
 public:
  explicit Update(std::string_view table) : m_table(metadata_table(table)) {}

  Update &set(std::string_view column, Value value) {
    m_sets.emplace_back(column, std::move(value));
    return *this;
  }
  // A raw assignment, e.g. "options = JSON_MERGE_PATCH(options, ...)".
  Update &set_raw(std::string assignment) {
    m_raw_sets.push_back(std::move(assignment));
    return *this;
  }
  Update &where(std::string condition) {
    m_wheres.push_back(std::move(condition));
    return *this;
  }
  bool empty() const { return m_sets.empty() && m_raw_sets.empty(); }
  std::string str() const;

 private:
  std::string m_table;
  std::vector<std::pair<std::string, Value>> m_sets;
  std::vector<std::string> m_raw_sets;
  std::vector<std::string> m_wheres;
};

// "column = value" conditions.
inline std::string eq(std::string_view column, const Value &value) {
  return std::string(column) + " = " + value.str();
}

}  // namespace sql
}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_SQL_H_
