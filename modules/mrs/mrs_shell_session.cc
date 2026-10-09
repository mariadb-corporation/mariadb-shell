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

#include "modules/mrs/mrs_shell_session.h"

#include <stdexcept>
#include <utility>

#include "mysqlshdk/libs/db/mysql/session.h"
#include "mysqlshdk/libs/mysql/instance.h"
#include "mysqlshdk/libs/mysql/script.h"

namespace mysqlsh {
namespace mrs {

namespace {

// MariaDB sends UUID columns with the BINARY flag but a character set
// collation; only the binary collation means raw bytes.
bool is_binary(const mysqlshdk::db::Column &column) {
  constexpr uint32_t k_binary_collation = 63;
  return column.is_binary() && column.get_collation() == k_binary_collation;
}

::mrs::Db_value to_value(const mysqlshdk::db::IRow *row, uint32_t index,
                         const mysqlshdk::db::Column &column) {
  using mysqlshdk::db::Type;

  if (row->is_null(index)) return ::mrs::Db_value(nullptr);

  switch (row->get_type(index)) {
    case Type::Null:
      return ::mrs::Db_value(nullptr);
    case Type::Integer:
      return ::mrs::Db_value(row->get_int(index));
    case Type::UInteger:
      return ::mrs::Db_value(row->get_uint(index));
    case Type::Float:
    case Type::Double:
      return ::mrs::Db_value(row->get_double(index));
    case Type::Bit: {
      const auto [value, bits] = row->get_bit(index);
      if (bits == 1) return ::mrs::Db_value(value != 0);
      return ::mrs::Db_value(value);
    }
    case Type::Bytes:
      return ::mrs::Db_value(row->get_string(index), true);
    case Type::String:
      return ::mrs::Db_value(row->get_string(index), is_binary(column));
    default:
      // DECIMAL, dates, ENUM, SET, JSON, ...: their text form
      return ::mrs::Db_value(row->get_as_string(index), is_binary(column));
  }
}

[[noreturn]] void rethrow(const mysqlshdk::db::Error &e) {
  throw ::mrs::Db_error(e.what(), e.code(), e.sqlstate());
}

}  // namespace

Shell_db_session::Shell_db_session(
    std::shared_ptr<mysqlshdk::db::ISession> session)
    : m_session(std::move(session)) {}

::mrs::Db_result Shell_db_session::do_query(const std::string &sql) {
  ::mrs::Db_result result;
  try {
    const auto res = m_session->query(sql, true);
    result.affected_rows = res->get_affected_row_count();

    const auto &metadata = res->get_metadata();
    auto columns = std::make_shared<std::vector<std::string>>();
    for (const auto &column : metadata) {
      columns->push_back(column.get_column_label());
    }
    result.columns = columns;

    while (const auto *row = res->fetch_one()) {
      std::vector<::mrs::Db_value> values;
      values.reserve(metadata.size());
      for (uint32_t i = 0; i < metadata.size(); ++i) {
        values.push_back(to_value(row, i, metadata[i]));
      }
      result.rows.emplace_back(result.columns, std::move(values));
    }
  } catch (const mysqlshdk::db::Error &e) {
    rethrow(e);
  }
  return result;
}

uint64_t Shell_db_session::do_execute(const std::string &sql) {
  try {
    const auto res = m_session->query(sql, true);
    return res->get_affected_row_count();
  } catch (const mysqlshdk::db::Error &e) {
    rethrow(e);
  }
}

void Shell_db_session::execute_script(const std::string &script) {
  // The script changes the current schema (USE), so it runs on its own
  // connection, through the shell's script runner (DELIMITER aware).
  auto session = mysqlshdk::db::mysql::Session::create();
  try {
    session->connect(m_session->get_connection_options());
    mysqlshdk::mysql::execute_sql_script(
        mysqlshdk::mysql::Instance(session), script, [](std::string_view error) {
          throw std::runtime_error("Error splitting the script: " + std::string(error));
        });
  } catch (const mysqlshdk::db::Error &e) {
    session->close();
    rethrow(e);
  } catch (...) {
    session->close();
    throw;
  }
  session->close();
}

std::string Shell_db_session::sql_mode() {
  try {
    const auto res = m_session->query("SELECT @@session.sql_mode", true);
    if (const auto *row = res->fetch_one()) {
      if (!row->is_null(0)) return row->get_string(0);
    }
  } catch (const mysqlshdk::db::Error &e) {
    rethrow(e);
  }
  return {};
}

}  // namespace mrs
}  // namespace mysqlsh
