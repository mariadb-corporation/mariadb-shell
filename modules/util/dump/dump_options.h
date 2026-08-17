/*
 * Copyright (c) 2020, 2026, Oracle and/or its affiliates.
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

#ifndef MODULES_UTIL_DUMP_DUMP_OPTIONS_H_
#define MODULES_UTIL_DUMP_DUMP_OPTIONS_H_

#include <cassert>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "mysqlshdk/include/scripting/types.h"
#include "mysqlshdk/include/scripting/types/option_pack.h"
#include "mysqlshdk/libs/db/filtering_options.h"
#include "mysqlshdk/libs/storage/compressed_file.h"
#include "mysqlshdk/libs/utils/version.h"

#include "modules/util/common/common_options.h"
#include "modules/util/common/dump/server_info.h"
#include "modules/util/dump/compatibility_option.h"
#include "modules/util/dump/instance_cache.h"
#include "modules/util/dump/lakehouse_target_option.h"
#include "modules/util/import_table/dialect.h"

namespace mysqlsh {
namespace dump {

enum class Dry_run {
  DISABLED,
  WRITE_EMPTY_DATA_FILES,
  DONT_WRITE_ANY_FILES,
};

class Dump_options : public mysqlsh::common::Common_options {
 public:
  using Filtering_options = mysqlshdk::db::Filtering_options;

  Dump_options(const Dump_options &) = default;
  Dump_options(Dump_options &&) = default;

  Dump_options &operator=(const Dump_options &) = default;
  Dump_options &operator=(Dump_options &&) = default;

  ~Dump_options() override = default;

  static const shcore::Option_pack_def<Dump_options> &options();

  /**
   * Default value of the targetVersion option: the newest server this Shell
   * knows about, on the scale used by the vendor being dumped.
   */
  const mysqlshdk::utils::Version &current_version() const;

  // setters
  void set_compression(mysqlshdk::storage::Compression compression) {
    m_compression = compression;
  }

  void set_dry_run_mode(Dry_run dry_run) { m_dry_run_mode = dry_run; }

  void disable_index_files() { m_write_index_files = false; }

  void dont_rename_data_files() { m_rename_data_files = false; }

  void disable_innodb_vector_store_tables_handling() {
    m_handle_innodb_vector_store_tables = false;
  }

  // getters
  const std::string &output_url() const noexcept { return url(); }

  const shcore::Dictionary_t &original_options() const { return m_options; }

  bool use_base64() const { return m_use_base64; }

  int64_t max_rate() const { return m_max_rate; }

  mysqlshdk::storage::Compression compression() const { return m_compression; }

  const mysqlshdk::storage::Compression_options &compression_options() const {
    return m_compression_options;
  }

  bool has_lakehouse_target() const noexcept {
    return m_lakehouse_target.has_value();
  }

  const Lakehouse_target_option &lakehouse_target() const noexcept {
    assert(has_lakehouse_target());
    return *m_lakehouse_target;
  }

  const import_table::Dialect &dialect() const { return m_dialect; }

  const std::string &character_set() const { return m_character_set; }

  bool mds_compatibility() const { return m_is_mds; }

  const Compatibility_options &compatibility_options() const {
    return m_compatibility_options;
  }

  Filtering_options &filters() { return m_filtering_options; }
  const Filtering_options &filters() const { return m_filtering_options; }

  const mysqlshdk::utils::Version &target_version() const {
    if (m_target_version.has_value()) {
      return *m_target_version;
    } else {
      return current_version();
    }
  }

  bool implicit_target_version() const { return !m_target_version.has_value(); }

  /**
   * The target version together with the vendor it belongs to.
   *
   * Under the vendor -> vendor scope the target is always the same vendor as
   * the source, so the vendor comes from the session being dumped. Use this,
   * not target_version(), whenever the question is "does the target support
   * feature X" - see MARIADB_DUMP_LOAD.md section 7.3.
   */
  common::Server_version target_server_version() const {
    return common::server_version(target_version(), m_source_is_maria_db);
  }

  /**
   * Vendor of the server being dumped, as reported by the session handshake.
   */
  bool source_is_maria_db() const noexcept { return m_source_is_maria_db; }

  const Instance_cache_builder::Partition_filters &included_partitions() const {
    return m_partitions;
  }

  const std::string &where(const std::string &schema,
                           const std::string &table) const;

  Dry_run dry_run_mode() const { return m_dry_run_mode; }

  bool is_dry_run() const { return Dry_run::DISABLED != m_dry_run_mode; }

  bool write_index_files() const { return m_write_index_files; }

  bool rename_data_files() const { return m_rename_data_files; }

  bool handle_innodb_vector_store_tables() const {
    return m_handle_innodb_vector_store_tables;
  }

  bool report_dump_option() const { return m_report_dump_option; }

  void set_report_dump_option(bool value) { m_report_dump_option = value; }

  virtual bool split() const = 0;

  virtual uint64_t bytes_per_chunk() const = 0;

  virtual std::size_t threads() const = 0;

  virtual std::size_t worker_threads() const { return threads(); }

  virtual bool is_export_only() const = 0;

  virtual bool use_single_file() const = 0;

  virtual bool dump_ddl() const = 0;

  virtual bool dump_data() const = 0;

  virtual bool consistent_dump() const = 0;

  virtual bool skip_consistency_checks() const = 0;

  virtual bool skip_upgrade_checks() const = 0;

  virtual bool dump_events() const = 0;

  virtual bool dump_routines() const = 0;

  virtual bool dump_libraries() const = 0;

  /**
   * MariaDB sequences. Not a user-facing option: sequences live in the table
   * namespace and are selected with the table filters (as in mysqldump), this
   * only says whether the utility dumps whole schemas or just a set of tables.
   */
  virtual bool dump_sequences() const = 0;

  virtual bool dump_triggers() const = 0;

  virtual bool dump_users() const = 0;

  virtual bool dump_data_masking_policies() const = 0;

  bool allow_data_masking() const noexcept { return m_allow_data_masking; }

  virtual bool use_timezone_utc() const = 0;

  virtual bool dump_binlog_info() const = 0;

  virtual bool checksum() const = 0;

 protected:
  explicit Dump_options(const char *name, bool url_is_directory = true);

  void on_set_session(
      const std::shared_ptr<mysqlshdk::db::ISession> &session) override;

  void on_validate() const override;

  void enable_mds_compatibility() { m_is_mds = true; }

  void set_compatibility_option(Compatibility_option c) {
    m_compatibility_options |= c;
  }

  void set_target_version(const mysqlshdk::utils::Version &version,
                          bool fatal = true);

  void validate_target_version() const;

  void set_where_clause(const std::map<std::string, std::string> &where);

  void set_where_clause(const std::string &schema, const std::string &table,
                        const std::string &where);

  void set_partitions(
      const std::map<std::string, std::unordered_set<std::string>> &partitions);

  void set_partitions(const std::string &schema, const std::string &table,
                      const std::unordered_set<std::string> &partitions);

  void set_lakehouse_target(Lakehouse_target_option &&storage) {
    m_lakehouse_target = std::move(storage);
  }

  bool exists(const std::string &schema) const;

  bool exists(const std::string &schema, const std::string &table) const;

  std::set<std::string> find_missing(
      const std::unordered_set<std::string> &schemas) const;

  std::set<std::string> find_missing(
      const std::string &schema,
      const std::unordered_set<std::string> &tables) const;

  // currently used by dumpTables(), dumpSchemas() and dumpInstance()
  Filtering_options m_filtering_options;

  mutable bool m_filter_conflicts = false;

 protected:
  void on_start_unpack(const shcore::Dictionary_t &options);

 private:
  void on_unpacked_options();

  void set_string_option(const std::string &option, const std::string &value);

  std::set<std::string> find_missing_impl(
      const std::string &subquery,
      const std::unordered_set<std::string> &objects) const;

  void validate_partitions() const;

  // input arguments
  shcore::Dictionary_t m_options;

  // not configurable
  bool m_use_base64 = true;

  // common options
  int64_t m_max_rate = 0;
  mysqlshdk::storage::Compression m_compression =
      mysqlshdk::storage::Compression::ZSTD;
  mysqlshdk::storage::Compression_options m_compression_options;

  std::string m_character_set = "utf8mb4";

  import_table::Dialect m_dialect;
  import_table::Dialect m_dialect_unpacker;

  Dry_run m_dry_run_mode = Dry_run::DISABLED;

  bool m_write_index_files = true;

  bool m_rename_data_files = true;

  bool m_handle_innodb_vector_store_tables = true;

  bool m_report_dump_option = true;

  // schema -> table -> condition
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      m_where;

  // schema -> table -> partitions
  Instance_cache_builder::Partition_filters m_partitions;

  // WL17279-FR1.2.1: default value of `allowDataMasking` is false
  bool m_allow_data_masking = false;

  // these options are unpacked elsewhere, but are here 'cause we're returning
  // a reference

  // currently used by dumpTables(), dumpSchemas() and dumpInstance()
  bool m_is_mds = false;
  Compatibility_options m_compatibility_options;
  std::optional<mysqlshdk::utils::Version> m_target_version;
  bool m_target_version_is_fatal = true;
  // vendor of the source, which under the vendor -> vendor scope is also the
  // vendor of the target
  bool m_source_is_maria_db = false;
  std::optional<Lakehouse_target_option> m_lakehouse_target;
};

}  // namespace dump
}  // namespace mysqlsh

#endif  // MODULES_UTIL_DUMP_DUMP_OPTIONS_H_
