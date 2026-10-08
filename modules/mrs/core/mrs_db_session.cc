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

#include "modules/mrs/core/mrs_db_session.h"

#include <cstdlib>

namespace mrs {

std::string Db_value::as_string() const {
  return std::visit(
      [](const auto &v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return {};
        } else if constexpr (std::is_same_v<T, std::string>) {
          return v;
        } else if constexpr (std::is_same_v<T, bool>) {
          return v ? "1" : "0";
        } else if constexpr (std::is_same_v<T, double>) {
          char buffer[64];
          std::snprintf(buffer, sizeof(buffer), "%.17g", v);
          return buffer;
        } else {
          return std::to_string(v);
        }
      },
      m_value);
}

int64_t Db_value::as_int() const {
  return std::visit(
      [](const auto &v) -> int64_t {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return 0;
        } else if constexpr (std::is_same_v<T, std::string>) {
          return std::strtoll(v.c_str(), nullptr, 10);
        } else {
          return static_cast<int64_t>(v);
        }
      },
      m_value);
}

uint64_t Db_value::as_uint() const {
  return std::visit(
      [](const auto &v) -> uint64_t {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return 0;
        } else if constexpr (std::is_same_v<T, std::string>) {
          return std::strtoull(v.c_str(), nullptr, 10);
        } else {
          return static_cast<uint64_t>(v);
        }
      },
      m_value);
}

double Db_value::as_double() const {
  return std::visit(
      [](const auto &v) -> double {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return 0.0;
        } else if constexpr (std::is_same_v<T, std::string>) {
          return std::strtod(v.c_str(), nullptr);
        } else {
          return static_cast<double>(v);
        }
      },
      m_value);
}

bool Db_value::as_bool() const {
  if (is_string()) {
    const auto &s = std::get<std::string>(m_value);
    // BIT(1) columns may arrive as a single byte
    if (s.size() == 1) return s[0] != 0 && s[0] != '0';
    return !s.empty() && s != "0";
  }
  return as_int() != 0;
}

std::optional<size_t> Db_row::column_index(std::string_view column) const {
  if (!m_columns) return std::nullopt;
  for (size_t i = 0; i < m_columns->size(); ++i) {
    if ((*m_columns)[i] == column) return i;
  }
  return std::nullopt;
}

const Db_value &Db_row::get(size_t index) const {
  if (index >= m_values.size()) {
    throw std::out_of_range("Column index out of range: " +
                            std::to_string(index));
  }
  return m_values[index];
}

const Db_value &Db_row::get(std::string_view column) const {
  const auto index = column_index(column);
  if (!index) {
    throw std::out_of_range("Unknown column: " + std::string(column));
  }
  return get(*index);
}

bool Db_row::has(std::string_view column) const {
  return column_index(column).has_value();
}

Db_transaction::Db_transaction(Db_session *session) : m_session(session) {
  m_session->execute("START TRANSACTION");
}

Db_transaction::~Db_transaction() {
  if (!m_done) {
    try {
      m_session->execute("ROLLBACK");
    } catch (...) {
      // nothing to be done about it in a destructor
    }
  }
}

void Db_transaction::commit() {
  m_session->execute("COMMIT");
  m_done = true;
}

}  // namespace mrs
