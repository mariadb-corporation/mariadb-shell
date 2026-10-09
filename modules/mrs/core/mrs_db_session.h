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

#ifndef MODULES_MRS_CORE_MRS_DB_SESSION_H_
#define MODULES_MRS_CORE_MRS_DB_SESSION_H_

// The database access the MRS core needs, as an abstract interface. The shell
// implements it on top of its classic session (mrs_shell_session.h); a server
// plugin provides its own implementation.

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "modules/mrs/core/mrs_sql.h"

namespace mrs {

// An error reported by the server.
class Db_error : public std::runtime_error {
 public:
  Db_error(const std::string &message, int code = 0,
           const std::string &sqlstate = {})
      : std::runtime_error(message), m_code(code), m_sqlstate(sqlstate) {}

  int code() const { return m_code; }
  const std::string &sqlstate() const { return m_sqlstate; }

 private:
  int m_code;
  std::string m_sqlstate;
};

// One cell of a result. Binary columns come back as strings holding the raw
// bytes; UUID ids and JSON columns as their text.
class Db_value {
 public:
  Db_value() = default;
  explicit Db_value(std::nullptr_t) {}
  explicit Db_value(int64_t v) : m_value(v) {}
  explicit Db_value(uint64_t v) : m_value(v) {}
  explicit Db_value(double v) : m_value(v) {}
  explicit Db_value(bool v) : m_value(v) {}
  explicit Db_value(std::string v, bool binary = false)
      : m_value(std::move(v)), m_binary(binary) {}
  explicit Db_value(const char *v) : m_value(std::string(v)) {}

  bool is_null() const { return std::holds_alternative<std::monostate>(m_value); }
  bool is_string() const { return std::holds_alternative<std::string>(m_value); }
  bool is_binary() const { return m_binary; }
  bool is_bool() const { return std::holds_alternative<bool>(m_value); }
  bool is_integer() const {
    return std::holds_alternative<int64_t>(m_value) ||
           std::holds_alternative<uint64_t>(m_value);
  }
  bool is_double() const { return std::holds_alternative<double>(m_value); }

  // Conversions; a NULL converts to the empty string, 0 or false.
  std::string as_string() const;
  int64_t as_int() const;
  uint64_t as_uint() const;
  double as_double() const;
  bool as_bool() const;

  // A NULL or an empty string yields no value.
  std::optional<std::string> as_optional_string() const {
    if (is_null()) return std::nullopt;
    return as_string();
  }

 private:
  std::variant<std::monostate, int64_t, uint64_t, double, bool, std::string>
      m_value;
  bool m_binary = false;
};

// A row with access to the cells by column name. The column names are
// shared with the result the row belongs to, so rows stay valid when the
// result is moved.
using Db_columns = std::shared_ptr<const std::vector<std::string>>;

class Db_row {
 public:
  Db_row(Db_columns columns, std::vector<Db_value> values)
      : m_columns(std::move(columns)), m_values(std::move(values)) {}

  const Db_value &get(size_t index) const;
  const Db_value &get(std::string_view column) const;
  const Db_value &operator[](std::string_view column) const {
    return get(column);
  }
  const Db_value &operator[](size_t index) const { return get(index); }
  bool has(std::string_view column) const;
  size_t size() const { return m_values.size(); }

 private:
  std::optional<size_t> column_index(std::string_view column) const;

  Db_columns m_columns;
  std::vector<Db_value> m_values;
};

// A buffered result.
struct Db_result {
  Db_columns columns = std::make_shared<const std::vector<std::string>>();
  std::vector<Db_row> rows;
  uint64_t affected_rows = 0;

  bool empty() const { return rows.empty(); }
  size_t size() const { return rows.size(); }
  const Db_row &first() const { return rows.front(); }
};

class Db_session {
 public:
  virtual ~Db_session() = default;

  // Runs a statement and buffers its result.
  virtual Db_result query(const std::string &sql) = 0;

  // Runs a statement without a result; returns the affected rows.
  virtual uint64_t execute(const std::string &sql) = 0;

  // Runs a script of statements, honouring DELIMITER commands.
  virtual void execute_script(const std::string &script) = 0;

  // The server's @@sql_mode.
  virtual std::string sql_mode() = 0;

  // Statements with ? placeholders: bind() writes the values in.
  Db_result query(const sql::Statement &statement) {
    return query(bind(statement));
  }
  Db_result query(std::string_view text, std::vector<sql::Value> params) {
    return query(sql::Statement(std::string(text), std::move(params)));
  }
  uint64_t execute(const sql::Statement &statement) {
    return execute(bind(statement));
  }
  uint64_t execute(std::string_view text, std::vector<sql::Value> params) {
    return execute(sql::Statement(std::string(text), std::move(params)));
  }

  // The SQL sent for a statement: its values written as literals in the
  // quoting the session's sql_mode needs. A server plugin may bind them on
  // the server instead.
  virtual std::string bind(const sql::Statement &statement);

  // The sql_mode statements are bound for, e.g. as read with the metadata
  // fingerprint. Without it, bind() reads the sql_mode first.
  void set_sql_mode(std::string_view sql_mode) {
    m_quoting = sql::Quoting::from_sql_mode(sql_mode);
  }

  // Ids fetched ahead by metadata::new_id(): a statement that creates many
  // rows needs a few round trips for them, not one per row. Each fetch
  // takes twice as many ids as the one before, up to 64.
  struct Id_pool {
    std::vector<Id> ids;  // the next one at the back
    size_t next_batch = 1;
  };
  Id_pool id_pool;

 private:
  std::optional<sql::Quoting> m_quoting;
};

// Runs a block inside a transaction: COMMIT at the end, ROLLBACK when the
// block throws.
class Db_transaction {
 public:
  explicit Db_transaction(Db_session *session);
  ~Db_transaction();

  Db_transaction(const Db_transaction &) = delete;
  Db_transaction &operator=(const Db_transaction &) = delete;

  void commit();

 private:
  Db_session *m_session;
  bool m_done = false;
};

}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_DB_SESSION_H_
