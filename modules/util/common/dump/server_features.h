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

#ifndef MODULES_UTIL_COMMON_DUMP_SERVER_FEATURES_H_
#define MODULES_UTIL_COMMON_DUMP_SERVER_FEATURES_H_

#include "mysqlshdk/libs/utils/version.h"

#include "modules/util/common/dump/server_info.h"

/**
 * Vendor-aware feature predicates for the dump & load code.
 *
 * Every gate here answers "does *this vendor* at *this version* support X",
 * never "is the version >= 8.0". MySQL and MariaDB version numbers are on
 * different scales, so a bare version comparison is only meaningful once the
 * vendor is known - which is why all of these take a Server_version rather than
 * a Version.
 *
 * MySQL thresholds are carried over unchanged from where these gates used to
 * live, so the MySQL build sees exactly the same answers it always has.
 *
 * See MARIADB_DUMP_LOAD.md section 7.3.
 */
namespace mysqlsh {
namespace dump {
namespace common {

/**
 * The version this Shell should be measured against when deciding whether a
 * server is "newer than the tool".
 *
 * The Shell versions on the same calendar scale MySQL Server uses, so for a
 * MySQL server the answer is the Shell's own version. MariaDB numbers its
 * releases differently, so a MariaDB server is measured against the MariaDB
 * version this Shell was built from instead. The semantics are identical in
 * both cases - "this tool understands servers up to the version it was built
 * for" - only the yardstick differs.
 *
 * Selected by the vendor of the *server*, never by the vendor of the build.
 *
 * See MARIADB_DUMP_LOAD.md section 7.4.
 */
const mysqlshdk::utils::Version &reference_version(bool is_maria_db);

inline const mysqlshdk::utils::Version &reference_version(
    const Server_version &v) {
  return reference_version(v.is_maria_db);
}

/**
 * Whether a dump produced from this server carries MariaDB's own SQL dialect.
 *
 * This is *not* the same question as "is the source MariaDB". A MySQL build
 * remaps a MariaDB source to 5.6 (see server_version() in server_info.cc),
 * which suppresses everything MariaDB-specific and makes the output
 * MySQL-shaped - that is upstream's supported MariaDB -> MySQL migration path,
 * and such a dump loads into MySQL. A MariaDB build applies no remap, so its
 * dumps are MariaDB-dialect and only load into MariaDB.
 *
 * Used to decide whether a load or a copy is crossing vendors - see
 * MARIADB_DUMP_LOAD.md sections 6.4 and 7.1.
 */
inline bool is_maria_db_dialect(const Server_version &v) {
  return v.is_maria_db && !v.is_5_6;
}

/**
 * Whether *this build* produces MariaDB-dialect dumps from a source of the
 * given vendor. The in-process counterpart of is_maria_db_dialect(), for
 * util.copy*() where there is no manifest to inspect.
 */
bool produces_maria_db_dialect(bool source_is_maria_db);

// --- privileges and locking ---------------------------------------------

/**
 * LOCK INSTANCE FOR BACKUP and the BACKUP_ADMIN privilege which guards it.
 *
 * MariaDB's analogue is BACKUP STAGE BLOCK_DDL, which needs RELOAD rather than
 * a dedicated privilege - see MARIADB_DUMP_LOAD.md section 4.1 (phase 3).
 */
bool supports_lock_instance_for_backup(const Server_version &v);

/**
 * The FLUSH_TABLES privilege, an alternative to RELOAD for executing FLUSH
 * TABLES WITH READ LOCK. MariaDB only has RELOAD.
 */
bool supports_flush_tables_privilege(const Server_version &v);

/**
 * Whether SELECT has to be checked explicitly before reading object metadata.
 *
 * On MySQL 8.0 the data dictionary reports what the account cannot see, so the
 * check is redundant. Everywhere else - older MySQL and every MariaDB - the
 * information_schema queries quietly return fewer rows instead, so the
 * privilege has to be verified up front.
 */
bool requires_explicit_select_privilege(const Server_version &v);

/**
 * Whether dumping accounts requires the SUPER privilege. MySQL 5.6 only;
 * MariaDB 10.5+ split SUPER into granular privileges and needs its own handling
 * (MARIADB_DUMP_LOAD.md section 4.2, phase 5).
 */
bool requires_super_to_dump_users(const Server_version &v);

/**
 * SHOW CREATE USER. MySQL grew it in 5.7, MariaDB in 10.2; before that the
 * account has to be reconstructed from the first SHOW GRANTS statement.
 */
bool supports_show_create_user(const Server_version &v);

/**
 * Whether SHOW CREATE USER implicitly commits the open transaction, which some
 * 8.0 releases do (and no MariaDB does).
 */
bool show_create_user_autocommits(const Server_version &v);

/**
 * Whether the partial_revokes system variable exists.
 */
bool supports_partial_revokes(const Server_version &v);

// --- metadata ------------------------------------------------------------

/**
 * information_schema.COLUMN_STATISTICS, the source of dumped histograms.
 *
 * MariaDB keeps engine-independent statistics in mysql.column_stats instead;
 * dumping those is MARIADB_DUMP_LOAD.md section 4.5 (phase 5).
 */
bool supports_column_statistics(const Server_version &v);

/**
 * Whether roles can be enumerated for dumping.
 *
 * MariaDB has roles since 10.0.5, but with a different model (hostless, one
 * active role, no activate_all_roles_on_login) - dumping them is
 * MARIADB_DUMP_LOAD.md section 4.5/4.6 (phase 5), so this is false for MariaDB
 * until then. Note this is *not* the same question as whether role-granted
 * privileges are resolved, which User_privileges already does for both vendors.
 */
bool supports_role_dumping(const Server_version &v);

/**
 * JavaScript libraries (SHOW CREATE LIBRARY, I_S.LIBRARIES). MySQL 9.2+ only.
 */
bool supports_library_ddl(const Server_version &v);

/**
 * information_schema.VIEW_TABLE_USAGE, which lists the tables a view reads.
 *
 * MySQL 8.0.13+ only - MariaDB has no such table, so the tables have to be
 * extracted by parsing VIEW_DEFINITION, which is the same fallback used for
 * older MySQL.
 */
bool supports_view_table_usage(const Server_version &v);

// --- SQL dialect ---------------------------------------------------------

/**
 * Whether the optimizer-hint comment syntax is available. Where it is not,
 * SQL_NO_CACHE is used instead - which is also what MariaDB wants, since it
 * kept SQL_NO_CACHE.
 */
bool supports_optimizer_hints(const Server_version &v);

/**
 * Whether BIT_XOR() accepts binary strings wider than 64 bits, as used by the
 * checksum expressions. MySQL 8.0 extended the bit functions; MariaDB did not,
 * so the checksum has to be computed in 64-bit slices there.
 */
bool supports_wide_bit_xor(const Server_version &v);

/**
 * GTID_SUBSET() / GTID_SUBTRACT() and the uuid:n-m GTID model.
 *
 * MariaDB's GTIDs are domain-based d-s-seq triples with no set-algebra
 * functions - MARIADB_DUMP_LOAD.md section 4.4 (phase 4).
 */
bool supports_gtid_set_functions(const Server_version &v);

/**
 * SET_ANY_DEFINER privilege.
 */
bool supports_set_any_definer_privilege(const Server_version &v);

// --- load target ---------------------------------------------------------

/**
 * ANALYZE TABLE ... UPDATE HISTOGRAM.
 */
bool supports_histograms(const Server_version &v);

/**
 * Whether indexes can be added in parallel (and hence whether
 * innodb_parallel_read_threads / innodb_ddl_threads exist).
 */
bool supports_parallel_index_creation(const Server_version &v);

/**
 * PS_CURRENT_THREAD_ID(). MySQL 8.0.16+; MariaDB has no such function, the
 * thread id has to come from performance_schema.threads.
 */
bool supports_ps_current_thread_id(const Server_version &v);

/**
 * Generated Invisible Primary Keys (sql_generate_invisible_primary_key).
 */
bool supports_gipks(const Server_version &v);

/**
 * Whether a dump loaded with generated invisible primary keys can be used for
 * inbound replication into a High Availability DB System.
 */
bool supports_invisible_pk_replication(const Server_version &v);

/**
 * Primary Key Equivalents standing in for a primary key when the server runs
 * with sql_require_primary_key enabled.
 */
bool supports_pke_as_pk(const Server_version &v);

/**
 * The sql_require_primary_key system variable. MariaDB has no equivalent, and
 * probing for one used to crash the loader (MARIADB_DUMP_LOAD.md section 4.10).
 */
bool supports_require_primary_key(const Server_version &v);

/**
 * restrict_fk_on_non_standard_key, and hence the force_non_standard_fks
 * compatibility option having any effect on the target.
 */
bool supports_non_standard_fk_restriction(const Server_version &v);

/**
 * The BULK LOAD statement.
 */
bool supports_bulk_load(const Server_version &v);

/**
 * Dynamic data masking, a MySQL 9.7 component.
 */
bool supports_dynamic_data_masking(const Server_version &v);

/**
 * Conversion of InnoDB-based vector store tables to Lakehouse.
 */
bool supports_vector_store_conversion(const Server_version &v);

/**
 * The MLE (JavaScript) component, and hence the mle.memory_max variable.
 */
bool supports_mle_component(const Server_version &v);

}  // namespace common
}  // namespace dump
}  // namespace mysqlsh

#endif  // MODULES_UTIL_COMMON_DUMP_SERVER_FEATURES_H_
