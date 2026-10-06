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

#include "mysqlshdk/shellcore/secret_options.h"

#include <stdexcept>

#include "mysqlshdk/libs/utils/utils_string.h"
#include "mysqlshdk/libs/utils/utils_uuid.h"

namespace shcore {

namespace {

constexpr auto k_group = "group";
constexpr auto k_all_groups = "allGroups";

}  // namespace

const Option_pack_def<Secret_options> &Secret_options::options() {
  static const auto opts = Option_pack_def<Secret_options>().optional(
      k_group, &Secret_options::set_group);

  return opts;
}

void Secret_options::set_group(const std::string &group) {
  if (k_default_secret_group == group) {
    m_group.clear();
  } else if (is_uuid(group)) {
    // Windows compares credential names case-insensitively, a single case
    // keeps a group the same everywhere
    m_group = str_lower(group);
  } else {
    throw std::invalid_argument{
        str_format("Option '%s' must be '%s' or a UUID, got: '%s'.", k_group,
                   k_default_secret_group, group.c_str())};
  }
}

const Option_pack_def<List_secrets_options> &List_secrets_options::options() {
  static const auto opts =
      Option_pack_def<List_secrets_options>()
          .optional(k_group, &List_secrets_options::set_list_group)
          .optional(k_all_groups, &List_secrets_options::m_all_groups)
          .on_done(&List_secrets_options::on_unpacked_options);

  return opts;
}

void List_secrets_options::set_list_group(const std::string &group) {
  set_group(group);
  m_has_group = true;
}

void List_secrets_options::on_unpacked_options() {
  if (m_all_groups && m_has_group) {
    throw std::invalid_argument{
        str_format("The '%s' and '%s' options cannot be used together.",
                   k_group, k_all_groups)};
  }
}

}  // namespace shcore
