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

#ifndef MODULES_MRS_CORE_MRS_METADATA_CONTENT_H_
#define MODULES_MRS_CORE_MRS_METADATA_CONTENT_H_

// Access to the content sets and content files of the metadata schema, and
// the file system helpers that load static content into them.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "modules/mrs/core/mrs_db_session.h"
#include "modules/mrs/core/mrs_metadata.h"

namespace mrs {
namespace metadata {

// -- Content sets ---------------------------------------------------------

struct Content_set {
  Id id;
  Id service_id;
  std::string content_type;  // STATIC or SCRIPTS
  std::string request_path;
  bool requires_auth = false;
  int enabled = 1;
  bool internal = false;
  std::optional<std::string> comments;
  std::optional<std::string> options;
  std::string host_ctx;
};

std::optional<Content_set> get_content_set(Db_session *session, const Id &id);
std::optional<Content_set> find_content_set(Db_session *session,
                                            const Id &service_id,
                                            std::string_view request_path);
std::vector<Content_set> get_content_sets(Db_session *session,
                                          const Id &service_id);

// The values of a content set to create. When a directory is given, its
// files are loaded (recursively) as the content files of the set; the
// ignore list is a comma separated list of file patterns (`*` and `?`
// wildcards) and defaults to "*node_modules/*, */.*".
struct Content_set_definition {
  Id service_id;
  std::string request_path;
  std::optional<bool> requires_auth;
  std::optional<int> enabled;
  std::optional<std::string> comments;
  std::optional<std::string> options;  // JSON
  std::string content_type = "STATIC";
  std::optional<std::string> directory;
  std::optional<std::string> ignore_list;
  std::optional<Id> id;  // a fixed id, for cloning
};

struct Added_content_set {
  Id id;
  size_t files_added = 0;
};

Added_content_set add_content_set(Db_session *session,
                                  const Content_set_definition &definition);

// The changes ALTER REST CONTENT SET makes; an unset field is left alone.
struct Content_set_changes {
  std::optional<std::string> request_path;
  std::optional<bool> requires_auth;
  std::optional<int> enabled;
  std::optional<std::string> comments;
  std::optional<std::string> options;
  bool merge_options = false;
  std::optional<std::string> content_type;
};

void update_content_set(Db_session *session, const Id &id,
                        const Content_set_changes &changes);

// Deletes a content set; its files go with it (BEFORE DELETE trigger).
void delete_content_set(Db_session *session, const Id &id);

// -- Content files --------------------------------------------------------

struct Content_file {
  Id id;
  Id content_set_id;
  std::string request_path;
  bool requires_auth = false;
  int enabled = 1;
  int64_t size = 0;
  std::optional<std::string> options;
  std::string content_set_request_path;
  std::string host_ctx;  // of the service
  // The raw bytes; only present when the file was fetched with content.
  std::optional<std::string> content;
};

std::optional<Content_file> get_content_file(Db_session *session, const Id &id,
                                             bool include_content);
std::optional<Content_file> find_content_file(Db_session *session,
                                              const Id &content_set_id,
                                              std::string_view request_path,
                                              bool include_content);
// The files of a content set, ordered by request path.
std::vector<Content_file> get_content_files(Db_session *session,
                                            const Id &content_set_id,
                                            bool include_content);

struct Content_file_definition {
  Id content_set_id;
  std::string request_path;
  std::string content;  // raw bytes
  std::optional<bool> requires_auth;
  std::optional<int> enabled;
  std::optional<std::string> options;  // JSON
};

Id add_content_file(Db_session *session,
                    const Content_file_definition &definition);

void delete_content_file(Db_session *session, const Id &id);

// Loads the files below a directory into a content set, honouring the
// ignore list (see Content_set_definition). The request path of a file is
// its path relative to the directory. Returns the number of files added;
// throws when the directory holds no file.
size_t add_content_directory(Db_session *session, const Id &content_set_id,
                             const std::string &directory, bool requires_auth,
                             std::string_view ignore_list);

// -- SHOW CREATE ----------------------------------------------------------

// The CREATE OR REPLACE REST CONTENT SET statement of a set, without its
// files. The LOAD SCRIPTS clause of a script set is only written when asked.
std::string content_set_create_statement(Db_session *session,
                                         const Content_set &content_set,
                                         bool allow_load_scripts);

// The CREATE OR REPLACE REST CONTENT FILE statement of a file; the content
// is fetched when the struct does not carry it. Text content is written as
// CONTENT '...', anything else as BINARY CONTENT '<base64>'.
std::string content_file_create_statement(Db_session *session,
                                          const Content_file &content_file);

// The statements of one content set: the set followed by its files.
std::vector<std::string> content_set_statements(Db_session *session,
                                                const Content_set &content_set,
                                                bool allow_load_scripts);

// The CREATE OR REPLACE REST CONTENT SET statements of a service's content
// sets, with their files; dynamic (script) content sets only when asked.
std::vector<std::string> content_set_create_statements(Db_session *session,
                                                       const Id &service_id,
                                                       bool include_dynamic);

// Copies a content set with its files into another service.
Id clone_content_set(Db_session *session, const Content_set &content_set,
                     const Id &new_service_id);

// -- Helpers --------------------------------------------------------------

// Whether the bytes look like text: no NUL byte and less than 30% of
// bytes outside printable ASCII (an empty content counts as binary).
bool is_text(std::string_view data);

std::string base64_encode(std::string_view data);
// Decodes standard base64 (whitespace is skipped). Throws on bad input.
std::string base64_decode(std::string_view text);

}  // namespace metadata
}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_METADATA_CONTENT_H_
