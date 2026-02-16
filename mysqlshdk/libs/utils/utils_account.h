/*
 * Copyright (c) 2020, 2026, Oracle and/or its affiliates.
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

#ifndef MYSQLSHDK_LIBS_UTILS_UTILS_ACCOUNT_H_
#define MYSQLSHDK_LIBS_UTILS_UTILS_ACCOUNT_H_

#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace shcore {

struct Account {
  enum class Auto_quote {
    /**
     * No auto-quotes, string must be a valid account name.
     */
    NO,
    /**
     * Host will be auto-quoted, multiple unqouted '@' characters are NOT
     * allowed.
     */
    HOST,
    /**
     * String is a result of i.e. CURRENT_USER() function and is not quoted at
     * all. Host will be auto-quoted, multiple unqouted '@' characters are
     * allowed, the last one marks the beginning of the host name.
     */
    USER_AND_HOST,
  };

  std::string user;
  std::string host;

  bool operator<(const Account &a) const {
    return std::tie(user, host) < std::tie(a.user, a.host);
  }

  bool operator==(const Account &a) const {
    return user == a.user && host == a.host;
  }

  // helper used by gtest
  friend std::ostream &operator<<(std::ostream &os, const Account &a);
};

/**
 * Split a MySQL account string (in the form user@host) into its username and
 * hostname components. The returned strings will be unquoted.
 *
 * The supported format is the <a
 * href="https://dev.mysql.com/doc/refman/en/account-names.html">standard MySQL
 * account name format</a>. This means it supports both identifiers and string
 * literals for username and hostname.
 */
void split_account(std::string_view account, std::string *out_user,
                   std::string *out_host,
                   Account::Auto_quote auto_quote = Account::Auto_quote::NO);

Account split_account(std::string_view account,
                      Account::Auto_quote auto_quote = Account::Auto_quote::NO);

template <typename C>
std::vector<Account> to_accounts(
    const C &c, Account::Auto_quote auto_quote = Account::Auto_quote::NO) {
  std::vector<Account> result;

  for (const auto &i : c) {
    result.emplace_back(split_account(i, auto_quote));
  }

  return result;
}

/**
 * Join MySQL account components into a string suitable for use with GRANT and
 * similar.
 */
std::string make_account(std::string_view user, std::string_view host,
                         bool no_backslash_escapes = false);

std::string make_account(const Account &account);

}  // namespace shcore

#endif  // MYSQLSHDK_LIBS_UTILS_UTILS_ACCOUNT_H_
