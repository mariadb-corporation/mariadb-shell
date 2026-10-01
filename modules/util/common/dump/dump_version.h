/*
 * Copyright (c) 2024, Oracle and/or its affiliates.
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

#ifndef MODULES_UTIL_COMMON_DUMP_DUMP_VERSION_H_
#define MODULES_UTIL_COMMON_DUMP_DUMP_VERSION_H_

#include "mysqlshdk/libs/utils/version.h"

namespace mysqlsh {
namespace dump {
namespace common {

inline constexpr auto k_dumper_version = "2.0.1";

/**
 * Checks that this Shell can load a dump in the given format version.
 *
 * created_by_maria_db_shell tells which Shell produced the dump, so a note
 * about an older format names the right one - see Server_info::has_vendor.
 */
void validate_dumper_version(const mysqlshdk::utils::Version &version,
                             bool created_by_maria_db_shell = false);

}  // namespace common
}  // namespace dump
}  // namespace mysqlsh

#endif  // MODULES_UTIL_COMMON_DUMP_DUMP_VERSION_H_
