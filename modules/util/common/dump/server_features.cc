/*
 * Copyright (c) 2026, Oracle and/or its affiliates.
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

#include "modules/util/common/dump/server_features.h"

namespace mysqlsh {
namespace dump {
namespace common {

namespace {

using mysqlshdk::utils::Version;

/**
 * A feature MySQL has from the given version on, and MariaDB does not have at
 * all. This is the shape of most of the gates in the dump & load code.
 */
inline bool mysql_only(const Server_version &v, uint32_t since) {
  return !v.is_maria_db && v.number.numeric() >= since;
}

}  // namespace

const Version &reference_version(bool is_maria_db) {
  return is_maria_db ? mysqlshdk::utils::k_build_server_version
                     : mysqlshdk::utils::k_shell_version;
}

bool produces_maria_db_dialect(bool source_is_maria_db) {
#ifdef MARIADB_BUILD
  return source_is_maria_db;
#else   // !MARIADB_BUILD
  // mirrors the 5.6 remap in server_version() - a MySQL build always writes
  // MySQL-shaped dumps, whatever it read them from
  (void)source_is_maria_db;
  return false;
#endif  // !MARIADB_BUILD
}

bool supports_lock_instance_for_backup(const Server_version &v) {
  return mysql_only(v, 80000);
}

bool supports_flush_tables_privilege(const Server_version &v) {
  return mysql_only(v, 80023);
}

bool requires_explicit_select_privilege(const Server_version &v) {
  // the MySQL 8.0 data dictionary filters by privilege and reports what is
  // missing; everything else quietly returns fewer rows
  return !v.is_8_0;
}

bool requires_super_to_dump_users(const Server_version &v) { return v.is_5_6; }

bool supports_show_create_user(const Server_version &v) {
  return v.is_maria_db ? v.number.numeric() >= 100200 : !v.is_5_6;
}

bool show_create_user_autocommits(const Server_version &v) {
  // BUG in some 8.0 releases: SHOW CREATE USER commits the open transaction
  return !v.is_maria_db && v.number >= Version(8, 0, 21) &&
         v.number <= Version(8, 0, 23);
}

bool supports_partial_revokes(const Server_version &v) {
  return mysql_only(v, 80016);
}

bool supports_column_statistics(const Server_version &v) { return v.is_8_0; }

bool supports_role_dumping(const Server_version &v) { return v.is_8_0; }

bool supports_library_ddl(const Server_version &v) {
  return mysql_only(v, 90200);
}

bool supports_view_table_usage(const Server_version &v) {
  return mysql_only(v, 80013);
}

bool supports_optimizer_hints(const Server_version &v) { return v.is_8_0; }

bool supports_wide_bit_xor(const Server_version &v) { return v.is_8_0; }

bool supports_gtid_set_functions(const Server_version &v) {
  return mysql_only(v, 80000);
}

bool supports_set_any_definer_privilege(const Server_version &v) {
  return mysql_only(v, 80200);
}

bool supports_histograms(const Server_version &v) {
  return !v.is_maria_db && v.number > Version(8, 0, 0);
}

bool supports_parallel_index_creation(const Server_version &v) {
  return mysql_only(v, 80027);
}

bool supports_ps_current_thread_id(const Server_version &v) {
  return mysql_only(v, 80016);
}

bool supports_gipks(const Server_version &v) { return mysql_only(v, 80030); }

bool supports_invisible_pk_replication(const Server_version &v) {
  return mysql_only(v, 80032);
}

bool supports_pke_as_pk(const Server_version &v) {
  return mysql_only(v, 90700);
}

bool supports_require_primary_key(const Server_version &v) {
  return mysql_only(v, 80013);
}

bool supports_non_standard_fk_restriction(const Server_version &v) {
  return mysql_only(v, 80400);
}

bool supports_bulk_load(const Server_version &v) {
  // minimum version which supports the required syntax
  return mysql_only(v, 80400);
}

bool supports_dynamic_data_masking(const Server_version &v) {
  return mysql_only(v, 90700);
}

bool supports_vector_store_conversion(const Server_version &v) {
  return mysql_only(v, 90401);
}

bool supports_mle_component(const Server_version &v) { return !v.is_maria_db; }

}  // namespace common
}  // namespace dump
}  // namespace mysqlsh
