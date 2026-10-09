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

#ifndef MODULES_MRS_CORE_MRS_STRINGS_H_
#define MODULES_MRS_CORE_MRS_STRINGS_H_

// String helpers of the MRS core. The core does not depend on the shell's
// libraries (it is meant to be shared with a server plugin), so these are
// its own counterparts of shcore::str_lower, str_join, str_split and the
// base64 functions.

#include <string>
#include <string_view>
#include <vector>

namespace mrs {

// ASCII case conversion.
std::string to_lower(std::string_view text);
std::string to_upper(std::string_view text);

bool ends_with(std::string_view text, std::string_view suffix);

// The parts joined with the separator. An empty part adds no separator
// before the next one.
std::string join(const std::vector<std::string> &parts,
                 std::string_view separator);
// The parts between the separators; with skip_empty, empty parts are
// left out.
std::vector<std::string> split(std::string_view text, char separator,
                               bool skip_empty = false);

std::string base64_encode(std::string_view data);
// Decodes standard base64 (whitespace is skipped). Throws on bad input.
std::string base64_decode(std::string_view text);

}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_STRINGS_H_
