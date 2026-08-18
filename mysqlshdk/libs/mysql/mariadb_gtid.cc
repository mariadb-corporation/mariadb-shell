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

#include "mysqlshdk/libs/mysql/mariadb_gtid.h"

#include <limits>
#include <stdexcept>

#include "mysqlshdk/libs/utils/utils_string.h"

namespace mysqlshdk {
namespace mysql {

namespace {

[[noreturn]] void invalid(std::string_view position) {
  throw std::invalid_argument("Invalid MariaDB GTID position: '" +
                              std::string{position} + "'");
}

/**
 * Parses one unsigned decimal component of a GTID. MariaDB accepts nothing but
 * digits here, so neither do we - a sign or a stray space means the whole
 * position is malformed, not that it is worth guessing at.
 */
uint64_t parse_number(std::string_view component, uint64_t max,
                      std::string_view position) {
  if (component.empty() ||
      component.find_first_not_of("0123456789") != std::string_view::npos) {
    invalid(position);
  }

  uint64_t value = 0;

  for (const auto c : component) {
    const uint64_t digit = c - '0';

    if (value > (max - digit) / 10) {
      invalid(position);
    }

    value = value * 10 + digit;
  }

  return value;
}

}  // namespace

Mariadb_gtid_position Mariadb_gtid_position::parse(std::string_view position) {
  Mariadb_gtid_position result;

  if (shcore::str_strip_view(position).empty()) {
    return result;
  }

  constexpr auto k_max_id = std::numeric_limits<uint32_t>::max();
  constexpr auto k_max_sequence = std::numeric_limits<uint64_t>::max();

  for (const auto &gtid : shcore::str_split(position, ",")) {
    const auto triple = shcore::str_split(shcore::str_strip_view(gtid), "-");

    if (3 != triple.size()) {
      invalid(position);
    }

    const auto domain =
        static_cast<uint32_t>(parse_number(triple[0], k_max_id, position));
    Entry entry;
    entry.server_id =
        static_cast<uint32_t>(parse_number(triple[1], k_max_id, position));
    entry.sequence = parse_number(triple[2], k_max_sequence, position);

    // a position holds a single entry per domain; if the string somehow
    // carries more than one, the most advanced of them is the position
    const auto [it, inserted] = result.m_domains.emplace(domain, entry);

    if (!inserted && it->second.sequence < entry.sequence) {
      it->second = entry;
    }
  }

  return result;
}

std::string Mariadb_gtid_position::str() const {
  std::string result;

  for (const auto &[domain, entry] : m_domains) {
    if (!result.empty()) {
      result += ',';
    }

    result += std::to_string(domain);
    result += '-';
    result += std::to_string(entry.server_id);
    result += '-';
    result += std::to_string(entry.sequence);
  }

  return result;
}

bool Mariadb_gtid_position::contains(const Mariadb_gtid_position &other) const {
  for (const auto &[domain, entry] : other.m_domains) {
    const auto it = m_domains.find(domain);

    if (m_domains.end() == it || it->second.sequence < entry.sequence) {
      return false;
    }
  }

  return true;
}

bool Mariadb_gtid_position::intersects(
    const Mariadb_gtid_position &other) const {
  for (const auto &[domain, entry] : other.m_domains) {
    const auto it = m_domains.find(domain);

    if (m_domains.end() != it && it->second.sequence && entry.sequence) {
      return true;
    }
  }

  return false;
}

Mariadb_gtid_position &Mariadb_gtid_position::merge(
    const Mariadb_gtid_position &other) {
  for (const auto &[domain, entry] : other.m_domains) {
    const auto [it, inserted] = m_domains.emplace(domain, entry);

    if (!inserted && it->second.sequence < entry.sequence) {
      it->second = entry;
    }
  }

  return *this;
}

}  // namespace mysql
}  // namespace mysqlshdk
