/*
 * Copyright (c) 2023, 2026, Oracle and/or its affiliates.
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

#ifndef MODULES_UTIL_COPY_COPY_OPERATION_H_
#define MODULES_UTIL_COPY_COPY_OPERATION_H_

#include <memory>
#include <string>
#include <utility>

#include "mysqlshdk/include/shellcore/shell_options.h"
#include "mysqlshdk/libs/db/connection_options.h"
#include "mysqlshdk/libs/storage/backend/in_memory/virtual_config.h"
#include "mysqlshdk/libs/storage/config.h"
#include "mysqlshdk/libs/storage/idirectory.h"
#include "mysqlshdk/libs/utils/debug.h"
#include "mysqlshdk/libs/utils/version.h"

#include "modules/mod_utils.h"
#include "modules/util/common/dump/server_features.h"
#include "modules/util/dump/ddl_dumper.h"
#include "modules/util/load/dump_loader.h"
#include "mysqlshdk/libs/utils/option_tracker.h"

namespace mysqlsh {
namespace copy {

std::pair<std::shared_ptr<mysqlshdk::storage::in_memory::Virtual_config>,
          std::unique_ptr<mysqlshdk::storage::IDirectory>>
setup_virtual_storage();

void copy(dump::Ddl_dumper *dumper, Dump_loader *loader,
          const std::shared_ptr<mysqlshdk::storage::in_memory::Virtual_config>
              &storage);

template <class Dumper, class Options>
void copy(const mysqlshdk::db::Connection_options &connection_options,
          Options *copy_options) {
  using shcore::option_tracker::Shell_feature;

  std::shared_ptr<mysqlshdk::db::ISession> load_session;

  try {
    load_session = establish_session(connection_options,
                                     current_shell_options()->get().wizards,
                                     false, true, Shell_feature::UTIL_COPY);
  } catch (const mysqlshdk::db::Error &e) {
    throw std::invalid_argument("Could not connect to the target instance: " +
                                e.format());
  }

  auto [storage, output] = setup_virtual_storage();

  copy_options->dump_options()->set_storage_config(
      storage, common::Storage_options::Storage_type::Memory);
  copy_options->dump_options()->set_url(output->full_path().real());

  using mysqlshdk::utils::Version;
  // everything below reasons about the target on MySQL's version scale;
  // MariaDB version numbers are not on it, so handing them to the MySQL
  // version policy yields a bogus "version is not supported" warning and bogus
  // feature detection - the same hole Load_dump_options used to have
  const auto target_is_maria_db = mysqlshdk::db::ServerVendor::MariaDB ==
                                  load_session->get_server_vendor();

  // Copy is the one place where the two ends are two live servers rather than
  // a dump on disk, so the cross-vendor check is a comparison between two
  // sessions. The in-memory dump is written in one vendor's dialect, so refuse
  // the mismatch here rather than partway through the DDL.
  // See MARIADB_DUMP_LOAD.md sections 6.4 and 7.1.
  const auto source_is_maria_db =
      copy_options->dump_options()->source_is_maria_db();

  if (dump::common::produces_maria_db_dialect(source_is_maria_db) !=
      target_is_maria_db) {
    const auto name = [](bool maria) { return maria ? "MariaDB" : "MySQL"; };

    throw std::invalid_argument(
        std::string{"The source instance is "} + name(source_is_maria_db) +
        " and the target instance is " + name(target_is_maria_db) +
        ". Copying across server vendors is not supported.");
  }

  // "newer than the tool" is measured against the Shell's own version for
  // MySQL and against the server version this Shell was built from for
  // MariaDB - see MARIADB_DUMP_LOAD.md section 7.4
  auto version = Version(
      load_session->query("SELECT @@version")->fetch_one()->get_string(0));
  auto is_mds = !target_is_maria_db && version.is_mds();
  DBUG_EXECUTE_IF("copy_utils_force_mds", { is_mds = true; });
  DBUG_EXECUTE_IF("copy_utils_unsupported_target_version", {
    const auto &reference = dump::common::reference_version(target_is_maria_db);
    version = Version(reference.get_major(), reference.get_minor() + 1,
                      reference.get_patch());
  });

  // if target is MDS, then we want to validate the version, so we won't copy
  // to an unsupported version
  // BUG#38107377 - but only if ignoreVersion is false
  copy_options->dump_options()->set_target_version(
      version, is_mds && !copy_options->load_options()->ignore_version());

  // enable MDS checks if target is an MDS instance
  if (is_mds) {
    copy_options->dump_options()->enable_mds_compatibility_checks();
  }

  // BUG#38852692 - automatically use 'target_has_mysql_native_password'
  // compatibility option
  if (!target_is_maria_db && 804 == version.numeric_version_series()) {
    const bool has_mysql_native_password =
        load_session
            ->query(
                "SELECT 1 FROM information_schema.plugins WHERE "
                "plugin_name='mysql_native_password' AND "
                "plugin_status='ACTIVE'")
            ->fetch_one();

    if (has_mysql_native_password) {
      copy_options->dump_options()->set_compatibility_option(
          dump::Compatibility_option::TARGET_HAS_MYSQL_NATIVE_PASSWORD);
    }
  }

  copy_options->dump_options()->validate_and_configure();

  copy_options->load_options()->set_storage_config(
      storage, common::Storage_options::Storage_type::Memory);
  copy_options->load_options()->set_url(output->full_path().real());
  copy_options->load_options()->set_session(load_session);
  copy_options->load_options()->validate_and_configure();

  const auto src = copy_options->dump_options()->canonical_address();
  const auto tgt = copy_options->load_options()->canonical_address();

  if (src == tgt) {
    throw std::invalid_argument(
        "The target instance is the same as the source instance");
  }

  current_console()->print_info(
      copy_options->load_options()->target_import_info(
          "Copying", ", source: " + src + ", target: " + tgt));

  // Both dumper and loader initialize their scoped consoles in constructors,
  // this order of initialization means that dumper has a Console_with_progress,
  // which uses the global console, loader has a Console_with_progress which
  // uses dumper's scoped console. Loader's console becomes the global console
  // for the main thread and it's also used by all subsequent threads. This
  // chain of consoles allows to synchronize console output with progress
  // threads of both dumper and loader.
  Dumper dumper{*copy_options->dump_options()};
  Dump_loader loader{*copy_options->load_options()};

  copy(&dumper, &loader, storage);
}

}  // namespace copy
}  // namespace mysqlsh

#endif  // MODULES_UTIL_COPY_COPY_OPERATION_H_
