/*
 * Copyright (c) 2026, MariaDB plc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335 USA
 */

#ifndef MYSQLSHDK_SHELLCORE_SECRET_OPTIONS_H_
#define MYSQLSHDK_SHELLCORE_SECRET_OPTIONS_H_

#include <string>

#include "mysqlshdk/include/scripting/types/option_pack.h"

namespace shcore {

/**
 * Name of the group used by the secret functions when no group is given.
 */
inline constexpr const char k_default_secret_group[] = "generic";

/**
 * Options of the secret functions (shell.storeSecret() and the others).
 */
struct Secret_options {
 public:
  static const Option_pack_def<Secret_options> &options();

  /**
   * Group of the secret: a lower-case UUID, or empty for the default group.
   */
  const std::string &group() const { return m_group; }

 protected:
  void set_group(const std::string &group);

 private:
  std::string m_group;
};

/**
 * Options of shell.listSecrets().
 */
struct List_secrets_options : public Secret_options {
 public:
  static const Option_pack_def<List_secrets_options> &options();

  /**
   * List the secrets of every group.
   */
  bool all_groups() const { return m_all_groups; }

 private:
  void set_list_group(const std::string &group);

  void on_unpacked_options();

  bool m_all_groups = false;
  bool m_has_group = false;
};

}  // namespace shcore

#endif  // MYSQLSHDK_SHELLCORE_SECRET_OPTIONS_H_
