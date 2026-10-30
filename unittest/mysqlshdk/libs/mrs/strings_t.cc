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
#include <string>

#include "unittest/gtest_clean.h"

#include "mysqlshdk/libs/mrs/strings.h"

namespace mrs {

// ASCII letters change case, every other byte stays: a UTF-8 character
// is left whole, whatever the locale (the shell sets en_US.UTF-8, and a
// Latin-1 locale would have std::toupper turn the bytes of ä into others)
TEST(Mrs_strings, case_conversion_is_ascii_only) {
  const auto with_locale = [](const char *name, const auto &check) {
    const std::string previous = std::setlocale(LC_ALL, nullptr);
    if (std::setlocale(LC_ALL, name)) check();
    std::setlocale(LC_ALL, previous.c_str());
  };

  const auto check = []() {
    EXPECT_EQ("abc_1", to_lower("ABC_1"));
    EXPECT_EQ("ABC_1", to_upper("abc_1"));
    EXPECT_EQ("grÖße", to_lower("GRÖßE"));
    EXPECT_EQ("GRößE", to_upper("größe"));
    EXPECT_EQ("", to_lower(""));
    EXPECT_EQ("", to_upper(""));
  };

  check();
  with_locale("en_US.UTF-8", check);
  with_locale("en_US.ISO8859-1", check);
  with_locale("C", check);
}

}  // namespace mrs
