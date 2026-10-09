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

#ifndef MODULES_MRS_CORE_MRS_SCRIPTS_H_
#define MODULES_MRS_CORE_MRS_SCRIPTS_H_

// MRS scripts: TypeScript modules whose decorated classes and methods
// become REST endpoints. ALTER REST CONTENT SET ... LOAD [TYPESCRIPT]
// SCRIPTS analyses the files stored in a content set and registers them:
//
//   @Mrs.module({ name: "sales", requestPath: "/sales" })  -> a REST schema
//   class Sales {                                           of type
//     @Mrs.script({ requestPath: "/total" })                SCRIPT_MODULE
//     public static async total(year: number): Promise<number> { ... }
//   }                                                     -> a REST object
//                                                            of type SCRIPT
//
// The analysis reads the code with comments and strings blanked out and
// matches brackets, as the Python plugin's regular expressions did.

#include <array>
#include <cstddef>
#include <string_view>

#include "modules/mrs/core/mrs_db_session.h"
#include "modules/mrs/core/mrs_metadata_content.h"

namespace mrs {
namespace metadata {

// The content set options LOAD SCRIPTS generates. SHOW CREATE REST CONTENT
// SET leaves them out, LOAD SCRIPTS writes them again.
inline constexpr std::string_view k_contains_mrs_scripts = "contains_mrs_scripts";
inline constexpr std::string_view k_mrs_scripting_language = "mrs_scripting_language";
inline constexpr std::string_view k_script_module_files = "script_module_files";
inline constexpr std::string_view k_script_definitions = "script_definitions";
inline constexpr std::array<std::string_view, 4> k_generated_script_options{
    k_contains_mrs_scripts, k_mrs_scripting_language, k_script_module_files,
    k_script_definitions};

struct Registered_scripts {
  size_t modules = 0;
  size_t scripts = 0;
};

// Analyses the TypeScript files of a content set and registers their MRS
// scripts as REST endpoints, replacing the ones registered before. Throws
// with the analysis errors.
Registered_scripts register_scripts(Db_session *session, const Content_set &content_set);

}  // namespace metadata
}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_SCRIPTS_H_
