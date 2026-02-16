/*
 * Copyright (c) 2026, Oracle and/or its affiliates.
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

#ifndef MODULES_UTIL_COMMON_DATA_MASKING_H_
#define MODULES_UTIL_COMMON_DATA_MASKING_H_

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>

#include "mysqlshdk/libs/db/session.h"

namespace mysqlsh {
namespace common {

/*
 * NOTE: policy names are case-insensitive, we're using lower case values here.
 */

class Data_masking final {
 public:
  /**
   * A data masking policy.
   */
  struct Policy {
    /**
     * Types of authorization ID.
     */
    enum class Authorization_type {
      ROLE,
      USER,
      UNKNOWN,
    };

    /**
     * Name of the policy (lower case).
     */
    std::string name;

    /**
     * Type of authorization IDs used by this policy.
     */
    Authorization_type authorization_type = Authorization_type::UNKNOWN;

    /**
     * Whether policy allows to view data unmasked.
     */
    bool allow = false;

    /**
     * Gatekeeper function used by this policy (full expression).
     */
    std::string gatekeeper;
  };

  explicit Data_masking(
      const std::shared_ptr<mysqlshdk::db::ISession> &session);

  Data_masking(const Data_masking &) = default;
  Data_masking(Data_masking &&) = default;

  Data_masking &operator=(const Data_masking &) = default;
  Data_masking &operator=(Data_masking &&) = default;

  ~Data_masking() = default;

  /**
   * Checks if the `component_object_policy` component is installed.
   *
   * @returns true if component is installed
   */
  bool is_component_installed() const;

  /**
   * Fetches names of data masking policies (lower case).
   *
   * @returns List of names of data masking policies.
   */
  std::unordered_set<std::string> fetch_policies() const;

  /**
   * Fetches CREATE statement of the given policy.
   *
   * @param policy_name Name of the policy to fetch.
   *
   * @returns CREATE MASKING POLICY statement of the given policy.
   */
  std::string fetch_policy(std::string_view policy_name) const;

  /**
   * Drops the given policy.
   *
   * * @param policy_name Name of the policy to drop.
   */
  void drop_policy(std::string_view policy_name) const;

  /**
   * Parses the CREATE MASKING POLICY statement of the given policy.
   *
   * @param policy_name Name of the policy to parse.
   *
   * @returns Parsed policy.
   */
  Policy parse_policy(std::string_view policy_name) const;

 private:
  std::unordered_set<std::string> fetch_policies(
      std::string_view from_table) const;

  std::shared_ptr<mysqlshdk::db::ISession> m_session;
};

/**
 * Parses the given CREATE MASKING POLICY statement.
 *
 * @param create_policy CREATE MASKING POLICY statement to parse.
 *
 * @returns Parsed policy.
 */
Data_masking::Policy parse_policy(std::string_view create_policy);

}  // namespace common
}  // namespace mysqlsh

#endif  // MODULES_UTIL_COMMON_DATA_MASKING_H_
