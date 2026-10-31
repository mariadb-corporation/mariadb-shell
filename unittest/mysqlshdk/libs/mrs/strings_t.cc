/*
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

#include <clocale>
#include <initializer_list>
#include <iostream>
#include <string>

#include "unittest/gtest_clean.h"

#include "mysqlshdk/libs/mrs/strings.h"

namespace mrs {

namespace {

// Puts the process locale back when leaving the scope, whatever the way out
struct Locale_guard {
  std::string previous = std::setlocale(LC_ALL, nullptr);
  ~Locale_guard() { std::setlocale(LC_ALL, previous.c_str()); }
};

// Runs check under the first of the locales setlocale() accepts: the same
// locale is spelled differently by each libc, and not every machine has it
// installed. Says so when none is, as the check then does not run at all.
template <typename Check>
void with_locale(std::initializer_list<const char *> names,
                 const Check &check) {
  const Locale_guard guard;
  for (const auto name : names) {
    if (std::setlocale(LC_ALL, name)) return check();
  }
  std::cout << "[   NOTE   ] no " << *names.begin()
            << " locale is installed, not checked under it\n";
}

}  // namespace

// ASCII letters change case, every other byte stays: a UTF-8 character
// is left whole, whatever the locale (the shell sets en_US.UTF-8, and a
// single-byte locale would have std::tolower()/toupper() turn the bytes of
// a UTF-8 character into other letters of its own)
TEST(Mrs_strings, case_conversion_is_ascii_only) {
  const auto check = []() {
    EXPECT_EQ("abc_1", to_lower("ABC_1"));
    EXPECT_EQ("ABC_1", to_upper("abc_1"));
    EXPECT_EQ("", to_lower(""));
    EXPECT_EQ("", to_upper(""));

    // Ö is C3 96: a Latin-1 tolower() turns C3 (Ã) into E3 (ã)
    EXPECT_EQ("grÖße", to_lower("GRÖßE"));
    // € is E2 82 AC, 日 is E6 97 A5: a Latin-1 toupper() turns E2 (â) into
    // C2 (Â) and E6 (æ) into C6 (Æ). The bytes of ö and ß (C3 B6, C3 9F)
    // are not lowercase letters there, so they alone would not tell.
    EXPECT_EQ("€ 日本", to_upper("€ 日本"));
    EXPECT_EQ("GRößE", to_upper("größe"));
    EXPECT_EQ("€ 日本", to_lower("€ 日本"));
  };

  check();
  with_locale({"en_US.UTF-8", "en_US.utf8", "C.UTF-8"}, check);
  // Latin-1, or Windows-1252 whose letters have the same case mapping
  with_locale({"en_US.ISO8859-1", "en_US.ISO-8859-1", "en_US.iso88591",
               "en_US.1252", ".1252"},
              check);
  with_locale({"C"}, check);
}

}  // namespace mrs
