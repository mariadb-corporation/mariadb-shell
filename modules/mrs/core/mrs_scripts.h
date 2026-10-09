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

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "modules/mrs/core/mrs_db_session.h"
#include "modules/mrs/core/mrs_json.h"
#include "modules/mrs/core/mrs_metadata_content.h"

namespace mrs {
namespace scripts {

struct Code_file {
  std::string path;  // the request path of the file in its content set
  std::string code;
  std::string last_modification;
};

using Properties = std::vector<std::pair<std::string, json::Value>>;

struct Position {
  int line_start = 0;
  int line_end = 0;
  size_t character_start = 0;
  size_t character_end = 0;
};

struct Parameter {
  std::string name;
  std::string type;
  bool optional = false;
  bool is_array = false;
  std::optional<json::Value> default_value;
};

struct Script {
  std::string function_name;
  Position position;
  std::vector<Parameter> parameters;
  std::string return_type;
  bool returns_array = false;
  Properties properties;
};

struct Module {
  const Code_file *file = nullptr;
  std::string class_name;
  std::string schema_type;  // SCRIPT_MODULE or DATABASE_SCHEMA
  Position position;
  Properties properties;
  std::vector<Script> scripts;
  std::vector<Script> triggers;
};

struct Interface_property {
  std::string name;
  std::string type;
  bool optional = false;
  bool read_only = false;
  std::optional<std::string> index_signature_type;
};

struct Interface {
  const Code_file *file = nullptr;
  std::string name;
  std::optional<std::string> extends;
  Position position;
  std::vector<Interface_property> properties;
};

struct Definitions {
  std::vector<Module> modules;
  std::vector<Interface> interfaces;  // the ones the scripts use
  std::vector<std::string> errors;

  // The script definitions in the layout of the Python plugin's
  // get_folder_mrs_script_definitions(), stored in the content set options.
  json::Value to_json() const;
};

// TypeScript files that can hold MRS scripts: .ts and .mts, but no test
// (.spec.ts) or declaration (.d.ts) files.
bool is_script_file(std::string_view path);

// The code with the contents of comments and string literals replaced by
// spaces (line breaks are kept), so brackets and keywords in them do not
// count. Positions stay the same.
std::string blank_comments_and_strings(std::string_view code);

// Whether a file defines an @Mrs.module or @Mrs.schema class.
bool defines_mrs_module(std::string_view code);

// The modules, scripts and used interfaces of the given TypeScript files.
// The files have to outlive the result.
Definitions analyze_typescript(const std::vector<Code_file> &files);

}  // namespace scripts

namespace metadata {

struct Registered_scripts {
  size_t modules = 0;
  size_t scripts = 0;
};

// Analyses the files of a content set and registers their MRS scripts as
// REST endpoints, replacing the ones registered before. Without a language
// it is detected from the files. Throws with the analysis errors.
Registered_scripts register_scripts(Db_session *session,
                                    const Content_set &content_set,
                                    const std::optional<std::string> &language);

}  // namespace metadata
}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_SCRIPTS_H_
