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
// schema. Callers do not write values into the statement text: a Statement
// holds the text with ? placeholders and the values, and the session binds
// them (Db_session::bind) in the quoting its sql_mode needs. quote() writes
// literals for REST SQL output (SHOW CREATE).

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

// The default name of the metadata schema. A deployment may add a prefix
// and a postfix to it (e.g. acme_mariadb_rest_service_eu), see
// metadata::schema_name_parts().
inline constexpr std::string_view k_default_metadata_schema = "mariadb_rest_service";

namespace sql {

// 'text' as a string literal for REST SQL output (SHOW CREATE), which has
// to read the same with and without the NO_BACKSLASH_ESCAPES SQL mode:
// quotes are doubled, line breaks stay as they are. Only a backslash, NUL
// and Ctrl-Z still need a backslash escape (they are read as written only
// without NO_BACKSLASH_ESCAPES).
std::string quote(std::string_view text);
// `name` with the backticks inside doubled.
std::string quote_identifier(std::string_view name);
// `schema`.`name`
std::string quote_qualified(std::string_view schema, std::string_view name);
// 0x... of binary data, e.g. the content of a file.
std::string hex(std::string_view binary);
// Stands for the metadata schema in statement text; Db_session::bind()
// replaces it with the session's metadata schema.
inline constexpr std::string_view k_metadata_schema_marker = "`{metadata_schema}`";

// <metadata schema marker>.`table`, or the name itself when it is already
// qualified.
std::string metadata_table(std::string_view table);

// How string literals are written for a session.
struct Quoting {
  // With the NO_BACKSLASH_ESCAPES SQL mode a backslash is an ordinary
  // character; only quotes are doubled.
  bool no_backslash_escapes = false;

  static Quoting from_sql_mode(std::string_view sql_mode);
};

// 'text' as a string literal the server reads back as the same text with
// the given quoting.
std::string literal(std::string_view text, const Quoting &quoting);

// A SQL value: NULL, a number, a text or raw SQL.
class Value {
 public:
  Value() = default;
  Value(std::nullptr_t) {}
  Value(bool v) : m_kind(Kind::raw), m_data(v ? "1" : "0") {}
  Value(int v) : m_kind(Kind::raw), m_data(std::to_string(v)) {}
  Value(int64_t v) : m_kind(Kind::raw), m_data(std::to_string(v)) {}
  Value(uint64_t v) : m_kind(Kind::raw), m_data(std::to_string(v)) {}
  Value(double v);
  Value(std::string_view v) : m_kind(Kind::text), m_data(v) {}
  Value(const std::string &v) : m_kind(Kind::text), m_data(v) {}
  Value(const char *v) : m_kind(Kind::text), m_data(v) {}

  template <typename T>
  Value(const std::optional<T> &v) : Value() {
    if (v) *this = Value(*v);
  }

  // Raw SQL, not quoted, e.g. DEFAULT or a hex literal.
  static Value raw(std::string sql) {
    Value v;
    v.m_kind = Kind::raw;
    v.m_data = std::move(sql);
    return v;
  }
  // An id (its UUID text); an empty or absent id is NULL.
  static Value id(const Id &uuid) { return uuid.empty() ? Value() : Value(uuid); }
  static Value id(const std::optional<Id> &uuid) {
    return uuid ? id(*uuid) : Value();
  }

  bool is_null() const { return m_kind == Kind::null; }
  // The SQL of the value.
  std::string render(const Quoting &quoting) const;

 private:
  enum class Kind { null, raw, text };
  Kind m_kind = Kind::null;
  std::string m_data;
};

// A statement text with ? placeholders and the values that go there.
struct Statement {
  Statement() = default;
  Statement(std::string text, std::vector<Value> params = {})
      : text(std::move(text)), params(std::move(params)) {}

  // The text with every placeholder replaced by its value. A ? inside a
  // quoted string, a quoted identifier or a comment is not a placeholder.
  // Throws std::logic_error when the counts differ.
  std::string render(const Quoting &quoting) const;

  std::string text;
  std::vector<Value> params;
};

// The conditions of a WHERE clause, joined with AND.
class Where {
 public:
  // `column` = value (a NULL value matches no row, as in SQL).
  void add(std::string_view column, Value value);
  // A condition of its own, e.g. "request_path LIKE ?", with the values of
  // its placeholders.
  void add_raw(std::string condition, std::vector<Value> params);
  // " WHERE ..." (empty without conditions); the values are appended to
  // `params`.
  std::string text(std::vector<Value> *params) const;

 private:
  std::vector<std::pair<std::string, std::vector<Value>>> m_conditions;
};

// INSERT INTO <table> (cols) VALUES (values)[, (values) ...]
class Insert {
 public:
  explicit Insert(std::string_view table) : m_table(metadata_table(table)) {}

  Insert &set(std::string_view column, Value value) {
    m_rows.back().emplace_back(column, std::move(value));
    return *this;
  }
  // Sets the column only when the optional holds a value; otherwise the
  // column keeps its default.
  template <typename T>
  Insert &set_if(std::string_view column, const std::optional<T> &value) {
    if (value) set(column, *value);
    return *this;
  }
  // Starts the next row of a multi-row insert; the set() calls that follow
  // fill it. A column a row leaves out gets DEFAULT in that row.
  Insert &next_row() {
    m_rows.emplace_back();
    return *this;
  }
  // Whether no row has a value.
  bool empty() const;

  Statement statement() const;
  operator Statement() const { return statement(); }

 private:
  std::string m_table;
  std::vector<std::vector<std::pair<std::string, Value>>> m_rows =
      std::vector<std::vector<std::pair<std::string, Value>>>(1);
};

// UPDATE <table> SET col = value, ... WHERE ...
class Update {
 public:
  explicit Update(std::string_view table) : m_table(metadata_table(table)) {}

  Update &set(std::string_view column, Value value) {
    m_sets.emplace_back(column, std::move(value));
    return *this;
  }
  // Sets the column only when the optional holds a value (a change).
  template <typename T>
  Update &set_if(std::string_view column, const std::optional<T> &value) {
    if (value) set(column, *value);
    return *this;
  }
  // An assignment of its own, e.g.
  // "options = JSON_MERGE_PATCH(options, ?)", with its values.
  Update &set_raw(std::string assignment, std::vector<Value> params = {}) {
    m_raw_sets.emplace_back(std::move(assignment), std::move(params));
    return *this;
  }
  Update &where(std::string_view column, Value value) {
    m_where.add(column, std::move(value));
    return *this;
  }
  Update &where_raw(std::string condition, std::vector<Value> params = {}) {
    m_where.add_raw(std::move(condition), std::move(params));
    return *this;
  }
  bool empty() const { return m_sets.empty() && m_raw_sets.empty(); }

  Statement statement() const;
  operator Statement() const { return statement(); }

 private:
  std::string m_table;
  std::vector<std::pair<std::string, Value>> m_sets;
  std::vector<std::pair<std::string, std::vector<Value>>> m_raw_sets;
  Where m_where;
};

// DELETE FROM <table> WHERE ...
class Delete {
 public:
  explicit Delete(std::string_view table) : m_table(metadata_table(table)) {}

  Delete &where(std::string_view column, Value value) {
    m_where.add(column, std::move(value));
    return *this;
  }
  Delete &where_raw(std::string condition, std::vector<Value> params = {}) {
    m_where.add_raw(std::move(condition), std::move(params));
    return *this;
  }

  Statement statement() const;
  operator Statement() const { return statement(); }

 private:
  std::string m_table;
  Where m_where;
};

}  // namespace sql
}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_SQL_H_
