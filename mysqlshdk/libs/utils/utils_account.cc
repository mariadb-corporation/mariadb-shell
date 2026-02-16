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

#include "mysqlshdk/libs/utils/utils_account.h"

#include <stdexcept>

#include "mysqlshdk/libs/utils/utils_lexing.h"
#include "mysqlshdk/libs/utils/utils_sqlstring.h"

namespace shcore {

namespace {

using mysqlshdk::utils::span_quotable_sql_identifier;

std::size_t span_quotable_string_literal(
    std::string_view s, std::size_t p, std::string *out_string,
    bool allow_number_at_beginning = false) {
  if (s.size() <= p) return p;

  char quote = s[p];
  if (quote != '\'' && quote != '"') {
    // check if valid initial char
    if (!std::isalpha(quote) &&
        !(allow_number_at_beginning && std::isdigit(quote)) && quote != '_' &&
        quote != '$' && quote != '%')
      throw std::runtime_error("Invalid character in string literal");
    quote = 0;
  } else {
    p++;
  }

  if (quote == 0) {
    while (p < s.size()) {
      if (!std::isalnum(s[p]) && s[p] != '_' && s[p] != '$' && s[p] != '.' &&
          s[p] != '%')
        break;
      if (out_string) out_string->push_back(s[p]);
      ++p;
    }
  } else {
    int esc = 0;
    bool done = false;
    while (p < s.size() && !done) {
      if (esc == quote && s[p] != esc) {
        done = true;
        break;
      }
      switch (s[p]) {
        case '"':
        case '\'':
          if (quote == s[p]) {
            if (esc == quote || esc == '\\') {
              if (out_string) out_string->push_back(s[p]);
              esc = 0;
            } else {
              esc = s[p];
            }
          } else {
            if (out_string) out_string->push_back(s[p]);
            esc = 0;
          }
          break;
        case '\\':
          if (esc == '\\') {
            if (out_string) out_string->push_back(s[p]);
            esc = 0;
          } else if (esc == 0) {
            esc = '\\';
          } else {
            done = true;
          }
          break;
        case 'n':
          if (esc == '\\') {
            if (out_string) out_string->push_back('\n');
            esc = 0;
          } else if (esc == 0) {
            if (out_string) out_string->push_back(s[p]);
          } else {
            done = true;
          }
          break;
        case 't':
          if (esc == '\\') {
            if (out_string) out_string->push_back('\t');
            esc = 0;
          } else if (esc == 0) {
            if (out_string) out_string->push_back(s[p]);
          } else {
            done = true;
          }
          break;
        case 'b':
          if (esc == '\\') {
            if (out_string) out_string->push_back('\b');
            esc = 0;
          } else if (esc == 0) {
            if (out_string) out_string->push_back(s[p]);
          } else {
            done = true;
          }
          break;
        case 'r':
          if (esc == '\\') {
            if (out_string) out_string->push_back('\r');
            esc = 0;
          } else if (esc == 0) {
            if (out_string) out_string->push_back(s[p]);
          } else {
            done = true;
          }
          break;
        case '0':
          if (esc == '\\') {
            if (out_string) out_string->push_back('\0');
            esc = 0;
          } else if (esc == 0) {
            if (out_string) out_string->push_back(s[p]);
          } else {
            done = true;
          }
          break;
        case 'Z':
          if (esc == '\\') {
            if (out_string) {
              out_string->push_back(26);
            }
            esc = 0;
          } else if (esc == 0) {
            if (out_string) out_string->push_back(s[p]);
          } else {
            done = true;
          }
          break;
        default:
          if (esc == '\\') {
            if (out_string) out_string->push_back(s[p]);
            esc = 0;
          } else if (esc == 0) {
            if (out_string) out_string->push_back(s[p]);
          } else {
            done = true;
          }
          break;
      }
      ++p;
    }
    if (!done && esc == quote) {
      done = true;
    } else if (!done) {
      throw std::runtime_error("Invalid syntax in string literal");
    }
  }
  return p;
}

std::size_t span_account_hostname_relaxed(std::string_view s, std::size_t p,
                                          std::string *out_string,
                                          bool auto_quote_hosts) {
  if (s.size() <= p) return p;

  // Use the span_quotable_identifier, if an error occurs, try to see if quotes
  // would fix it, however first check for the existence of the '@' character
  // which is not allowed in hostnames
  std::size_t old_p = p, res = 0;

  if (s.find('@', p) != std::string::npos) {
    std::string err_msg = "Malformed hostname (illegal symbol: '@')";
    throw std::runtime_error(err_msg);
  }
  // Check if hostname starts with string literal or identifier depending on the
  // first character being a backtick or not.
  if (s[p] == '`') {
    res = span_quotable_sql_identifier(s, p, out_string);
  } else {
    bool quoted = false;
    // Do not allow quote characters unless they are surrounded by quotes
    if (s[p] == s[s.size() - 1] && (s[p] == '\'' || s[p] == '"')) {
      // hostname surrounded by quotes.
      quoted = true;
    } else {
      if ((s.find('\'', p) != std::string::npos) ||
          (s.find('"', p) != std::string::npos)) {
        throw std::runtime_error(
            "Malformed hostname. Cannot use \"'\" or '\"' "
            "characters on the hostname without quotes");
      }
    }
    bool try_quoting = false;
    try {
      res = span_quotable_string_literal(s, p, out_string, true);

      // If the complete string was not consumed could be a hostname that
      // requires quotes, they should be enabled only if not quoted already
      if (res < s.size() && auto_quote_hosts) {
        try_quoting = !quoted;
      }
    } catch (const std::runtime_error &) {
      // In case of error parsing, tries quoting
      try_quoting = auto_quote_hosts;
    }

    if (try_quoting) {
      std::string quoted_s;

      quoted_s += s.substr(0, old_p);
      quoted_s += '\'';
      quoted_s += escape_backticks(s.substr(old_p));
      quoted_s += '\'';

      // reset out_string
      if (out_string) *out_string = "";
      res = span_quotable_string_literal(quoted_s, old_p, out_string, true);
    }
  }
  return res;
}

}  // namespace

void split_account(std::string_view account, std::string *out_user,
                   std::string *out_host, Account::Auto_quote auto_quote) {
  std::size_t pos = 0;
  if (out_user) *out_user = "";
  if (out_host) *out_host = "";

  // Check if account starts with string literal or identifier depending on the
  // first character being a backtick or not.
  if (!account.empty()) {
    if (account[0] == '`') {
      pos = span_quotable_sql_identifier(account, 0, out_user);
    } else if (account[0] == '\'' || account[0] == '"') {
      pos = span_quotable_string_literal(account, 0, out_user);
    } else {
      pos = account.rfind('@');
      if (pos == 0) throw std::runtime_error("User name must not be empty.");

      if (Account::Auto_quote::USER_AND_HOST != auto_quote) {
        // don't allow @ on the username unless it is quoted
        if (account.rfind('@', pos - 1) != std::string::npos) {
          throw std::runtime_error("Invalid user name: " +
                                   std::string{account.substr(0, pos)});
        }
      }

      if (out_user != nullptr) out_user->assign(account, 0, pos);
    }
  } else {
    throw std::runtime_error("User name must not be empty.");
  }

  if (std::string::npos != pos && account[pos] == '@' &&
      ++pos < account.length()) {
    if (account.compare(pos, std::string::npos, "skip-grants host") == 0) {
      pos = account.length();
      if (out_host != nullptr) *out_host = "skip-grants host";
    } else {
      pos = span_account_hostname_relaxed(
          account, pos, out_host, Account::Auto_quote::NO != auto_quote);
    }
  }
  if (pos < account.size())
    throw std::runtime_error("Invalid syntax in account name '" +
                             std::string{account} + "'");
}

Account split_account(std::string_view account,
                      Account::Auto_quote auto_quote) {
  Account result;
  split_account(account, &result.user, &result.host, auto_quote);
  return result;
}

std::string make_account(std::string_view user, std::string_view host,
                         bool no_backslash_escapes) {
  return shcore::sqlstring("?@?", no_backslash_escapes ? NoBackslashEscapes : 0)
         << user << host;
}

std::string make_account(const Account &account) {
  return make_account(account.user, account.host);
}

std::ostream &operator<<(std::ostream &os, const Account &a) {
  return os << '`' << a.user << "`@`" << a.host << '`';
}

}  // namespace shcore
