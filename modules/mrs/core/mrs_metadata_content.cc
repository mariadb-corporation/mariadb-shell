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

#include "modules/mrs/core/mrs_metadata_content.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace mrs {
namespace metadata {

using sql::Value;

namespace {

namespace fs = std::filesystem;

constexpr std::string_view k_default_ignore_list = "*node_modules/*, */.*";

std::optional<std::string> optional_text(const Db_value &value) {
  return value.as_optional_string();
}

// -- Content set rows -----------------------------------------------------

const char *k_content_set_select = R"(
SELECT cs.id, cs.service_id, cs.request_path, cs.requires_auth,
    cs.enabled, cs.internal, cs.comments, cs.options,
    CONCAT(h.name, se.url_context_root) AS host_ctx,
    cs.content_type
FROM `mysql_rest_service_metadata`.`content_set` cs
    LEFT OUTER JOIN `mysql_rest_service_metadata`.`service` se
        ON se.id = cs.service_id
    LEFT JOIN `mysql_rest_service_metadata`.`url_host` h
        ON se.url_host_id = h.id
)";

Content_set content_set_from_row(const Db_row &row) {
  Content_set cs;
  cs.id = row["id"].as_string();
  cs.service_id = row["service_id"].as_string();
  cs.content_type = row["content_type"].as_string();
  cs.request_path = row["request_path"].as_string();
  cs.requires_auth = row["requires_auth"].as_bool();
  cs.enabled = static_cast<int>(row["enabled"].as_int());
  cs.internal = row["internal"].as_bool();
  cs.comments = optional_text(row["comments"]);
  cs.options = optional_text(row["options"]);
  cs.host_ctx = row["host_ctx"].as_string();
  return cs;
}

std::vector<Content_set> query_content_sets(Db_session *session,
                                            const std::string &where) {
  std::string sql = k_content_set_select;
  if (!where.empty()) sql += " WHERE " + where;
  sql += " ORDER BY cs.request_path";

  std::vector<Content_set> content_sets;
  for (const auto &row : session->query(sql).rows) {
    content_sets.push_back(content_set_from_row(row));
  }
  return content_sets;
}

// -- Content file rows ----------------------------------------------------

std::string content_file_select(bool include_content) {
  return std::string(R"(
SELECT f.id, f.content_set_id, f.request_path, f.requires_auth, f.enabled,
    f.size, f.options,
    cs.request_path AS content_set_request_path,
    CONCAT(h.name, se.url_context_root) AS host_ctx)") +
         (include_content ? ", f.content" : "") + R"(
FROM `mysql_rest_service_metadata`.`content_file` f
    LEFT OUTER JOIN `mysql_rest_service_metadata`.`content_set` cs
        ON cs.id = f.content_set_id
    LEFT OUTER JOIN `mysql_rest_service_metadata`.`service` se
        ON se.id = cs.service_id
    LEFT JOIN `mysql_rest_service_metadata`.`url_host` h
        ON se.url_host_id = h.id
)";
}

Content_file content_file_from_row(const Db_row &row) {
  Content_file f;
  f.id = row["id"].as_string();
  f.content_set_id = row["content_set_id"].as_string();
  f.request_path = row["request_path"].as_string();
  f.requires_auth = row["requires_auth"].as_bool();
  f.enabled = static_cast<int>(row["enabled"].as_int());
  f.size = row["size"].as_int();
  f.options = optional_text(row["options"]);
  f.content_set_request_path = row["content_set_request_path"].as_string();
  f.host_ctx = row["host_ctx"].as_string();
  if (row.has("content")) f.content = row["content"].as_string();
  return f;
}

std::vector<Content_file> query_content_files(Db_session *session,
                                              const std::string &where,
                                              bool include_content) {
  std::string sql = content_file_select(include_content);
  if (!where.empty()) sql += " WHERE " + where;
  sql += " ORDER BY f.request_path";

  std::vector<Content_file> files;
  for (const auto &row : session->query(sql).rows) {
    files.push_back(content_file_from_row(row));
  }
  return files;
}

// A LONGBLOB value: a hex literal keeps arbitrary bytes out of the
// connection character set.
Value blob_value(const std::string &content) {
  return content.empty() ? Value("") : Value::raw(sql::hex(content));
}

// A text literal quoted the way the Python plugin did it in its SHOW
// CREATE output: only backslashes and quotes are escaped, newlines stay.
std::string quote_content(std::string_view text) {
  std::string result = "'";
  for (const char c : text) {
    if (c == '\\' || c == '\'' || c == '"') result += '\\';
    result += c;
  }
  return result + "'";
}

// Appends the SET of the options column honouring MERGE OPTIONS: merged
// into existing options, replaced when there are none yet.
void set_json_options(Db_session *session, sql::Update *update, const Id &id,
                      const std::string &options, bool merge) {
  if (merge) {
    const auto row = session->query(
        "SELECT options IS NULL AS options_is_null FROM " +
        sql::metadata_table("content_set") + " WHERE id = " + sql::hex(id));
    if (!row.empty() && !row.first()["options_is_null"].as_bool()) {
      update->set_raw("options = JSON_MERGE_PATCH(options, " +
                      sql::quote(options) + ")");
      return;
    }
  }
  update->set("options", options);
}

// -- File system ----------------------------------------------------------

std::string expand_user_path(const std::string &path) {
  if (path.size() >= 2 && path[0] == '~' && (path[1] == '/' || path[1] == '\\')) {
    const char *home = std::getenv("HOME");
    if (!home) home = std::getenv("USERPROFILE");
    if (home) return std::string(home) + path.substr(1);
  }
  return path;
}

// The ignore list as one regular expression matched against the start of
// a file's full path, with `*` and `?` as wildcards (the way the Python
// plugin converted it). Empty entries are skipped.
std::optional<std::regex> ignore_pattern(std::string_view ignore_list) {
  std::string pattern;
  std::stringstream entries{std::string(ignore_list)};
  std::string entry;
  while (std::getline(entries, entry, ',')) {
    const auto first = entry.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) continue;
    const auto last = entry.find_last_not_of(" \t\r\n");

    std::string converted;
    for (const char c : entry.substr(first, last - first + 1)) {
      switch (c) {
        case '\\':
          converted += '/';
          break;
        case '.':
          converted += "\\.";
          break;
        case '*':
          converted += ".*";
          break;
        case '?':
          converted += '.';
          break;
        default:
          converted += c;
      }
    }
    pattern += pattern.empty() ? "^(?:(" : ")|(";
    pattern += converted;
  }
  if (pattern.empty()) return std::nullopt;

  try {
    return std::regex(pattern + "))");
  } catch (const std::regex_error &) {
    throw std::runtime_error("The IGNORE list '" + std::string(ignore_list) +
                             "' is not a valid pattern list.");
  }
}

std::string read_file(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("The file '" + path.string() +
                             "' could not be read.");
  }
  std::stringstream content;
  content << file.rdbuf();
  return content.str();
}

// "YYYY-MM-DD HH:MM:SS.mmm" in UTC, as the Python plugin stored it.
std::string last_modification(const fs::path &path) {
  using namespace std::chrono;

  // file_clock and system_clock may differ in their epoch; the difference
  // of their "now" values converts between them portably.
  const auto file_time = fs::last_write_time(path);
  const auto system_time = time_point_cast<system_clock::duration>(
      system_clock::now() + (file_time - fs::file_time_type::clock::now()));

  const std::time_t seconds = system_clock::to_time_t(system_time);
  auto millis = static_cast<int>(
      duration_cast<milliseconds>(system_time.time_since_epoch()).count() % 1000);
  if (millis < 0) millis += 1000;

  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &seconds);
#else
  gmtime_r(&seconds, &tm);
#endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);

  char fraction[8];
  std::snprintf(fraction, sizeof(fraction), ".%03d", millis);
  return std::string(buffer) + fraction;
}

// The regular files below a directory, sorted by path for a predictable
// load order.
std::vector<fs::path> files_below(const fs::path &directory) {
  std::vector<fs::path> files;
  try {
    for (const auto &entry : fs::recursive_directory_iterator(
             directory, fs::directory_options::skip_permission_denied)) {
      if (entry.is_regular_file()) files.push_back(entry.path());
    }
  } catch (const fs::filesystem_error &e) {
    throw std::runtime_error("The directory '" + directory.string() +
                             "' could not be read: " + e.what());
  }
  std::sort(files.begin(), files.end());
  return files;
}

// -- SHOW CREATE pieces ---------------------------------------------------

std::string enabled_clause(int enabled) {
  if (enabled == 2) return "\n    PRIVATE";
  if (enabled == 0) return "\n    DISABLED";
  return {};
}

std::string authentication_clause(bool requires_auth) {
  return requires_auth ? "\n    AUTHENTICATION REQUIRED"
                       : "\n    AUTHENTICATION NOT REQUIRED";
}

std::string load_scripts_clause(const Content_set &content_set) {
  std::string language;
  if (content_set.options) {
    if (const auto options = json::try_parse(*content_set.options)) {
      language = options->get_string("mrs_scripting_language");
    }
  }
  return language == "TypeScript" ? "\n    LOAD TYPESCRIPT SCRIPTS"
                                  : "\n    LOAD SCRIPTS";
}

}  // namespace

// -- Content sets ---------------------------------------------------------

std::optional<Content_set> get_content_set(Db_session *session, const Id &id) {
  auto sets = query_content_sets(session, "cs.id = " + sql::hex(id));
  if (sets.empty()) return std::nullopt;
  return std::move(sets.front());
}

std::optional<Content_set> find_content_set(Db_session *session,
                                            const Id &service_id,
                                            std::string_view request_path) {
  if (request_path.empty() || request_path[0] != '/') {
    throw std::runtime_error("The request_path has to start with '/'.");
  }
  auto sets = query_content_sets(
      session, "cs.service_id = " + sql::hex(service_id) +
                   " AND cs.request_path = " + sql::quote(request_path));
  if (sets.empty()) return std::nullopt;
  return std::move(sets.front());
}

std::vector<Content_set> get_content_sets(Db_session *session,
                                          const Id &service_id) {
  return query_content_sets(session, "cs.service_id = " + sql::hex(service_id));
}

Added_content_set add_content_set(Db_session *session,
                                  const Content_set_definition &definition) {
  if (definition.request_path.empty() || definition.request_path[0] != '/') {
    throw std::runtime_error("The request_path has to start with '/'.");
  }

  // The directory is checked before anything is written
  std::optional<fs::path> directory;
  if (definition.directory) {
    directory = fs::path(expand_user_path(*definition.directory));
    if (!fs::is_directory(*directory)) {
      throw std::runtime_error("The given path " + *definition.directory +
                               " does not exist.");
    }
  }

  const bool requires_auth = definition.requires_auth.value_or(false);

  const Id id = definition.id ? *definition.id : new_id(session);
  sql::Insert insert("content_set");
  insert.set("id", Value::id(id));
  insert.set("service_id", Value::id(definition.service_id));
  insert.set("request_path", definition.request_path);
  insert.set("requires_auth", requires_auth);
  insert.set("enabled", definition.enabled.value_or(1));
  insert.set("comments", definition.comments.value_or(""));
  insert.set("options", definition.options);
  insert.set("content_type", definition.content_type);
  session->execute(insert.str());

  Added_content_set result{id, 0};
  if (directory) {
    result.files_added = add_content_directory(
        session, id, directory->string(), requires_auth,
        definition.ignore_list.value_or(std::string(k_default_ignore_list)));
  }
  return result;
}

void update_content_set(Db_session *session, const Id &id,
                        const Content_set_changes &changes) {
  sql::Update update("content_set");
  if (changes.request_path) update.set("request_path", *changes.request_path);
  if (changes.requires_auth) update.set("requires_auth", *changes.requires_auth);
  if (changes.enabled) update.set("enabled", *changes.enabled);
  if (changes.comments) update.set("comments", *changes.comments);
  if (changes.content_type) update.set("content_type", *changes.content_type);
  if (changes.options) {
    set_json_options(session, &update, id, *changes.options,
                     changes.merge_options);
  }
  if (update.empty()) return;
  update.where("id = " + sql::hex(id));
  session->execute(update.str());
}

void delete_content_set(Db_session *session, const Id &id) {
  const auto content_set = get_content_set(session, id);

  if (session->execute("DELETE FROM " + sql::metadata_table("content_set") +
                       " WHERE id = " + sql::hex(id)) == 0) {
    throw std::runtime_error("The specified content_set with id " +
                             sql::hex(id) + " was not found.");
  }

  // A script set may have left behind a script module without objects
  if (content_set && content_set->content_type == "SCRIPTS") {
    session->execute(
        "DELETE FROM " + sql::metadata_table("db_schema") +
        " WHERE schema_type = 'SCRIPT_MODULE' AND id NOT IN (SELECT "
        "db_schema_id FROM " +
        sql::metadata_table("db_object") + ")");
  }
}

// -- Content files --------------------------------------------------------

std::optional<Content_file> get_content_file(Db_session *session, const Id &id,
                                             bool include_content) {
  auto files =
      query_content_files(session, "f.id = " + sql::hex(id), include_content);
  if (files.empty()) return std::nullopt;
  return std::move(files.front());
}

std::optional<Content_file> find_content_file(Db_session *session,
                                              const Id &content_set_id,
                                              std::string_view request_path,
                                              bool include_content) {
  auto files = query_content_files(
      session,
      "f.content_set_id = " + sql::hex(content_set_id) +
          " AND f.request_path = " + sql::quote(request_path),
      include_content);
  if (files.empty()) return std::nullopt;
  return std::move(files.front());
}

std::vector<Content_file> get_content_files(Db_session *session,
                                            const Id &content_set_id,
                                            bool include_content) {
  return query_content_files(
      session, "f.content_set_id = " + sql::hex(content_set_id),
      include_content);
}

Id add_content_file(Db_session *session,
                    const Content_file_definition &definition) {
  const Id id = new_id(session);
  sql::Insert insert("content_file");
  insert.set("id", Value::id(id));
  insert.set("content_set_id", Value::id(definition.content_set_id));
  insert.set("request_path", definition.request_path);
  insert.set("requires_auth", definition.requires_auth.value_or(false));
  insert.set("enabled", definition.enabled.value_or(1));
  insert.set("content", blob_value(definition.content));
  insert.set("options", definition.options);
  session->execute(insert.str());
  return id;
}

void delete_content_file(Db_session *session, const Id &id) {
  if (session->execute("DELETE FROM " + sql::metadata_table("content_file") +
                       " WHERE id = " + sql::hex(id)) == 0) {
    throw std::runtime_error("The specified REST content file with id " +
                             sql::hex(id) + " was not found.");
  }
}

size_t add_content_directory(Db_session *session, const Id &content_set_id,
                             const std::string &directory, bool requires_auth,
                             std::string_view ignore_list) {
  // Normalised without a trailing separator, so that the request path of a
  // file is the remainder of its path and starts with '/'.
  auto root = fs::absolute(expand_user_path(directory)).lexically_normal();
  if (!root.has_filename()) root = root.parent_path();
  const auto root_text = root.generic_string();

  const auto ignore = ignore_pattern(ignore_list);

  size_t added = 0;
  for (const auto &file : files_below(root)) {
    const auto full_path = file.generic_string();
    if (ignore && std::regex_search(full_path, *ignore)) continue;

    Content_file_definition definition;
    definition.content_set_id = content_set_id;
    definition.request_path = full_path.substr(root_text.size());
    definition.requires_auth = requires_auth;
    definition.content = read_file(file);

    json::Value options = json::Value::object();
    options.set("last_modification", json::Value(last_modification(file)));
    definition.options = options.dump();

    add_content_file(session, definition);
    ++added;
  }

  if (added == 0) {
    throw std::runtime_error("There are no files in '" + root.string() +
                             "' or it's not accessible.");
  }
  return added;
}

// -- SHOW CREATE ----------------------------------------------------------

std::string content_set_create_statement(Db_session *,
                                         const Content_set &content_set,
                                         bool allow_load_scripts) {
  std::string output = "CREATE OR REPLACE REST CONTENT SET " +
                       quote_request_path(content_set.request_path) +
                       "\n    ON SERVICE " + content_set.host_ctx;
  output += enabled_clause(content_set.enabled);
  if (content_set.comments && !content_set.comments->empty()) {
    output += "\n    COMMENT " + sql::quote(*content_set.comments);
  }
  const auto options = format_json_entry("OPTIONS", content_set.options);
  if (!options.empty()) output += "\n" + options;
  output += authentication_clause(content_set.requires_auth);
  if (allow_load_scripts && content_set.content_type == "SCRIPTS") {
    output += load_scripts_clause(content_set);
  }
  return output + ";";
}

std::string content_file_create_statement(Db_session *session,
                                          const Content_file &content_file) {
  std::string content;
  if (content_file.content) {
    content = *content_file.content;
  } else {
    const auto with_content = get_content_file(session, content_file.id, true);
    if (!with_content) {
      throw std::runtime_error("The REST content file " +
                               content_file.request_path + " was not found.");
    }
    content = with_content->content.value_or("");
  }

  std::string output = "CREATE OR REPLACE REST CONTENT FILE " +
                       quote_request_path(content_file.request_path) +
                       "\n    ON SERVICE " +
                       quote_request_path(content_file.host_ctx) +
                       " CONTENT SET " +
                       quote_request_path(content_file.content_set_request_path);
  if (is_text(content)) {
    output += "\n    CONTENT " + quote_content(content);
  } else {
    output += "\n    BINARY CONTENT '" + base64_encode(content) + "'";
  }
  output += enabled_clause(content_file.enabled);
  const auto options = format_json_entry("OPTIONS", content_file.options);
  if (!options.empty()) output += "\n" + options;
  output += authentication_clause(content_file.requires_auth);
  return output + ";";
}

std::vector<std::string> content_set_statements(Db_session *session,
                                                const Content_set &content_set,
                                                bool allow_load_scripts) {
  std::vector<std::string> statements{
      content_set_create_statement(session, content_set, allow_load_scripts)};
  for (const auto &file : get_content_files(session, content_set.id, true)) {
    statements.push_back(content_file_create_statement(session, file));
  }
  return statements;
}

std::vector<std::string> content_set_create_statements(Db_session *session,
                                                       const Id &service_id,
                                                       bool include_dynamic) {
  std::vector<std::string> statements;
  for (const auto &content_set : get_content_sets(session, service_id)) {
    if (content_set.content_type == "SCRIPTS" && !include_dynamic) continue;
    for (auto &statement :
         content_set_statements(session, content_set, include_dynamic)) {
      statements.push_back(std::move(statement));
    }
  }
  return statements;
}

Id clone_content_set(Db_session *session, const Content_set &content_set,
                     const Id &new_service_id) {
  Content_set_definition definition;
  definition.service_id = new_service_id;
  definition.request_path = content_set.request_path;
  definition.requires_auth = content_set.requires_auth;
  definition.enabled = content_set.enabled;
  definition.comments = content_set.comments;
  definition.options = content_set.options;
  definition.content_type = content_set.content_type;

  const Id new_set_id = add_content_set(session, definition).id;

  for (const auto &file : get_content_files(session, content_set.id, true)) {
    Content_file_definition copy;
    copy.content_set_id = new_set_id;
    copy.request_path = file.request_path;
    copy.content = file.content.value_or("");
    copy.requires_auth = file.requires_auth;
    copy.enabled = file.enabled;
    copy.options = file.options;
    add_content_file(session, copy);
  }
  return new_set_id;
}

// -- Helpers --------------------------------------------------------------

bool is_text(std::string_view data) {
  size_t other = 0;
  for (const unsigned char c : data) {
    if ((c >= 32 && c < 127) || c == '\n' || c == '\r' || c == '\t' ||
        c == '\b') {
      continue;
    }
    if (c == 0) return false;
    ++other;
  }
  // 30% or more bytes outside the text range make it binary; so does an
  // empty content, which keeps the Python plugin's behaviour.
  return other * 10 < data.size() * 3;
}

namespace {
constexpr std::string_view k_base64_alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}  // namespace

std::string base64_encode(std::string_view data) {
  std::string result;
  result.reserve((data.size() + 2) / 3 * 4);

  size_t i = 0;
  for (; i + 3 <= data.size(); i += 3) {
    const uint32_t triple = (static_cast<unsigned char>(data[i]) << 16) |
                            (static_cast<unsigned char>(data[i + 1]) << 8) |
                            static_cast<unsigned char>(data[i + 2]);
    result += k_base64_alphabet[(triple >> 18) & 0x3f];
    result += k_base64_alphabet[(triple >> 12) & 0x3f];
    result += k_base64_alphabet[(triple >> 6) & 0x3f];
    result += k_base64_alphabet[triple & 0x3f];
  }

  const size_t rest = data.size() - i;
  if (rest == 1) {
    const uint32_t v = static_cast<unsigned char>(data[i]) << 16;
    result += k_base64_alphabet[(v >> 18) & 0x3f];
    result += k_base64_alphabet[(v >> 12) & 0x3f];
    result += "==";
  } else if (rest == 2) {
    const uint32_t v = (static_cast<unsigned char>(data[i]) << 16) |
                       (static_cast<unsigned char>(data[i + 1]) << 8);
    result += k_base64_alphabet[(v >> 18) & 0x3f];
    result += k_base64_alphabet[(v >> 12) & 0x3f];
    result += k_base64_alphabet[(v >> 6) & 0x3f];
    result += '=';
  }
  return result;
}

std::string base64_decode(std::string_view text) {
  std::string result;
  result.reserve(text.size() / 4 * 3);

  uint32_t accumulator = 0;
  int bits = 0;
  bool padding = false;
  for (const char c : text) {
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
    if (c == '=') {
      padding = true;
      continue;
    }
    const auto pos = k_base64_alphabet.find(c);
    if (pos == std::string_view::npos || padding) {
      throw std::runtime_error("The content is not valid base64.");
    }
    accumulator = (accumulator << 6) | static_cast<uint32_t>(pos);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      result += static_cast<char>((accumulator >> bits) & 0xff);
    }
  }
  return result;
}

}  // namespace metadata
}  // namespace mrs
