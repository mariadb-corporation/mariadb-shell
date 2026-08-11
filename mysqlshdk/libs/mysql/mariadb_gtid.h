/*
 * Copyright (c) 2026, MariaDB Corporation.
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

#ifndef MYSQLSHDK_LIBS_MYSQL_MARIADB_GTID_H_
#define MYSQLSHDK_LIBS_MYSQL_MARIADB_GTID_H_

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace mysqlshdk {
namespace mysql {

/**
 * A MariaDB GTID position: the value of @@gtid_binlog_pos,
 * @@gtid_current_pos or @@gtid_slave_pos.
 *
 * MariaDB identifies a transaction by a domain-server-sequence triple
 * (`0-1-42`) instead of MySQL's `uuid:n`, and a *position* is a comma-separated
 * list carrying the last such triple seen per replication domain - it is
 * therefore at most one entry per domain, and `0-1-42` stands for every
 * transaction of domain 0 up to and including sequence 42. There is no
 * GTID_SUBSET() / GTID_SUBTRACT() pair to compare two of them with, so the
 * comparisons the loader needs are done here, client side.
 *
 * @@gtid_binlog_state is a superset of this format - it can list several
 * server_ids within one domain - and is deliberately not modelled: nothing in
 * dump & load reads or writes it. See MARIADB_DUMP_LOAD.md section 4.4.
 */
class Mariadb_gtid_position final {
 public:
  /**
   * Parses a position, ignoring surrounding and separating whitespace.
   *
   * If a domain appears more than once (which a well-formed position never
   * does), the highest sequence number wins.
   *
   * @throws std::invalid_argument if the string is not a GTID position.
   */
  static Mariadb_gtid_position parse(std::string_view position);

  bool empty() const noexcept { return m_domains.empty(); }

  /**
   * The position in canonical form: domains in ascending order, comma
   * separated, no spaces. An empty position is an empty string.
   */
  std::string str() const;

  /**
   * Whether this position implies every transaction in `other`, i.e. whether
   * `other` is a subset of it: each of its domains is present here, with a
   * sequence number at least as high.
   */
  bool contains(const Mariadb_gtid_position &other) const;

  /**
   * Whether the two positions have any transaction in common. Since a position
   * covers every sequence up to the one it names, that is true as soon as they
   * share a replication domain.
   */
  bool intersects(const Mariadb_gtid_position &other) const;

  /**
   * Adds `other` to this position, keeping the more advanced entry of the two
   * for every domain they share. This is the union of the two sets of
   * transactions, which is what `updateGtidSet: "append"` asks for.
   */
  Mariadb_gtid_position &merge(const Mariadb_gtid_position &other);

  bool operator==(const Mariadb_gtid_position &other) const {
    return m_domains == other.m_domains;
  }

 private:
  struct Entry {
    uint32_t server_id = 0;
    uint64_t sequence = 0;

    bool operator==(const Entry &other) const {
      return server_id == other.server_id && sequence == other.sequence;
    }
  };

  // keyed (and hence ordered) by domain id
  std::map<uint32_t, Entry> m_domains;
};

}  // namespace mysql
}  // namespace mysqlshdk

#endif  // MYSQLSHDK_LIBS_MYSQL_MARIADB_GTID_H_
