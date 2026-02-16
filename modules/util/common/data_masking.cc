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

#include "modules/util/common/data_masking.h"

#include <cassert>
#include <stdexcept>
#include <string_view>

#include "mysqlshdk/libs/db/result.h"
#include "mysqlshdk/libs/utils/utils_general.h"
#include "mysqlshdk/libs/utils/utils_lexing.h"
#include "mysqlshdk/libs/utils/utils_sqlstring.h"
#include "mysqlshdk/libs/utils/utils_string.h"

namespace mysqlsh {
namespace common {

Data_masking::Data_masking(
    const std::shared_ptr<mysqlshdk::db::ISession> &session)
    : m_session(session) {}

bool Data_masking::is_component_installed() const {
  return m_session
      ->query(
          "SELECT 1 FROM information_schema.tables WHERE "
          "TABLE_SCHEMA='performance_schema' AND "
          "TABLE_NAME='column_masking_policy'")
      ->fetch_one();
}

std::unordered_set<std::string> Data_masking::fetch_policies() const {
  try {
    return fetch_policies("performance_schema.column_masking_policy");
  } catch (const mysqlshdk::db::Error &outer) {
    try {
      return fetch_policies("mysql.column_masking_policies");
    } catch (const mysqlshdk::db::Error &inner) {
      throw std::runtime_error(
          shcore::str_format("Could not fetch data masking policies from "
                             "neither performance_schema.column_masking_policy "
                             "(%s) nor mysql.column_masking_policies (%s)",
                             outer.what(), inner.what()));
    }
  }
}

std::string Data_masking::fetch_policy(std::string_view policy_name) const {
  return m_session
      ->query(shcore::sqlformat("SHOW CREATE MASKING POLICY !", policy_name))
      ->fetch_one_or_throw()
      ->get_string(1);
}

void Data_masking::drop_policy(std::string_view policy_name) const {
  return m_session->execute(
      shcore::sqlformat("DROP MASKING POLICY IF EXISTS !", policy_name));
}

Data_masking::Policy Data_masking::parse_policy(
    std::string_view policy_name) const {
  return common::parse_policy(fetch_policy(policy_name));
}

std::unordered_set<std::string> Data_masking::fetch_policies(
    std::string_view from_table) const {
  const auto result = m_session->query(shcore::str_format(
      "SELECT LOWER(policy_name) FROM %.*s",
      static_cast<int>(from_table.size()), from_table.data()));
  std::unordered_set<std::string> names;

  while (const auto row = result->fetch_one()) {
    names.emplace(row->get_string(0));
  }

  return names;
}

Data_masking::Policy parse_policy(std::string_view create_policy) {
  Data_masking::Policy policy;
  mysqlshdk::utils::SQL_iterator it(create_policy, 0, false);

  if (!it.consume_tokens("CREATE", "MASKING", "POLICY")) {
    throw std::runtime_error{"Expected CREATE MASKING POLICY statement"};
  }

  // return next token skipping any parentheses
  const auto next_token = [&it]() {
    std::string_view token;

    do {
      token = it.next_token();
    } while (shcore::str_caseeq(token, "(", ")"));

    return token;
  };

  // unquote identifier and convert it to lower case
  const auto as_identifier = [](std::string_view id) {
    if (id.empty()) [[unlikely]] {
      return std::string{};
    } else if ('`' == id.front()) {
      return shcore::utf8_lower(shcore::unquote_identifier(id));
    } else {
      return shcore::utf8_lower(id);
    }
  };

  {
    auto policy_name = next_token();

    if (shcore::str_caseeq(policy_name, "IF")) {
      // NOT
      next_token();
      // EXISTS
      next_token();
      // policy name follows
      policy_name = next_token();
    }

    policy.name = as_identifier(policy_name);
  }

  // argument name is case insensitive
  const auto arg = as_identifier(next_token());

  // CASE
  next_token();
  // WHEN
  next_token();

  {
    const auto gatekeeper = next_token();

    if (shcore::str_caseeq(gatekeeper, "CURRENT_ROLE_IN")) {
      policy.authorization_type =
          Data_masking::Policy::Authorization_type::ROLE;
    } else if (shcore::str_caseeq(gatekeeper, "CURRENT_USER_IN")) {
      policy.authorization_type =
          Data_masking::Policy::Authorization_type::USER;
    }

    policy.gatekeeper += gatekeeper;

    for (auto token = it.next_token(); !shcore::str_caseeq(token, "THEN");
         token = it.next_token()) {
      policy.gatekeeper += token;
    }
  }

  bool allow = true;

  for (auto token = next_token(); !shcore::str_caseeq(token, "ELSE");
       token = next_token()) {
    // if this is anything else than '(', arg ,')' then data is being masked for
    // the listed authorization IDs
    if (arg != as_identifier(token)) {
      allow = false;
      break;
    }
  }

  policy.allow = allow;

  return policy;
}

}  // namespace common
}  // namespace mysqlsh
