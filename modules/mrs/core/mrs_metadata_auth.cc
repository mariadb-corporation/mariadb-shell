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

#include "modules/mrs/core/mrs_metadata_auth.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <random>
#include <stdexcept>

namespace mrs {
namespace metadata {

using sql::Value;

namespace {

// -- Password hashing -----------------------------------------------------
//
// The router verifies MRS passwords with a SCRAM-like scheme: the stored
// key is SHA256(HMAC(PBKDF2(password, salt), "Client Key")). The core may
// not depend on a crypto library, so SHA-256 is implemented here.

constexpr uint32_t k_sha256_round_constants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotate_right(uint32_t value, int bits) {
  return (value >> bits) | (value << (32 - bits));
}

void sha256_transform(uint32_t state[8], const unsigned char block[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
           (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
           static_cast<uint32_t>(block[i * 4 + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = rotate_right(w[i - 15], 7) ^ rotate_right(w[i - 15], 18) ^
                        (w[i - 15] >> 3);
    const uint32_t s1 = rotate_right(w[i - 2], 17) ^ rotate_right(w[i - 2], 19) ^
                        (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
  for (int i = 0; i < 64; ++i) {
    const uint32_t s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
    const uint32_t choice = (e & f) ^ (~e & g);
    const uint32_t t1 = h + s1 + choice + k_sha256_round_constants[i] + w[i];
    const uint32_t s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t t2 = s0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

// The 32 raw bytes of the SHA-256 digest.
std::string sha256(std::string_view input) {
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

  std::string message(input);
  const uint64_t bit_length = static_cast<uint64_t>(input.size()) * 8;
  message += '\x80';
  while (message.size() % 64 != 56) message += '\0';
  for (int i = 7; i >= 0; --i) {
    message += static_cast<char>((bit_length >> (i * 8)) & 0xff);
  }
  for (size_t offset = 0; offset < message.size(); offset += 64) {
    sha256_transform(
        state, reinterpret_cast<const unsigned char *>(message.data()) + offset);
  }

  std::string digest;
  digest.reserve(32);
  for (const uint32_t word : state) {
    digest += static_cast<char>((word >> 24) & 0xff);
    digest += static_cast<char>((word >> 16) & 0xff);
    digest += static_cast<char>((word >> 8) & 0xff);
    digest += static_cast<char>(word & 0xff);
  }
  return digest;
}

std::string hmac_sha256(std::string_view key, std::string_view data) {
  std::string block_key(key);
  if (block_key.size() > 64) block_key = sha256(block_key);
  block_key.resize(64, '\0');

  std::string inner(64, '\0');
  std::string outer(64, '\0');
  for (size_t i = 0; i < 64; ++i) {
    inner[i] = static_cast<char>(block_key[i] ^ 0x36);
    outer[i] = static_cast<char>(block_key[i] ^ 0x5c);
  }
  return sha256(outer + sha256(inner + std::string(data)));
}

// PBKDF2-HMAC-SHA256 with a derived key of one digest length.
std::string pbkdf2_sha256(std::string_view password, std::string_view salt,
                          int iterations) {
  std::string block = hmac_sha256(password, std::string(salt) + std::string("\0\0\0\1", 4));
  std::string result = block;
  for (int i = 1; i < iterations; ++i) {
    block = hmac_sha256(password, block);
    for (size_t j = 0; j < result.size(); ++j) {
      result[j] = static_cast<char>(result[j] ^ block[j]);
    }
  }
  return result;
}

std::string base64_encode(std::string_view data) {
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  size_t i = 0;
  while (i + 2 < data.size()) {
    const uint32_t triple = (static_cast<unsigned char>(data[i]) << 16) |
                            (static_cast<unsigned char>(data[i + 1]) << 8) |
                            static_cast<unsigned char>(data[i + 2]);
    result += alphabet[(triple >> 18) & 0x3f];
    result += alphabet[(triple >> 12) & 0x3f];
    result += alphabet[(triple >> 6) & 0x3f];
    result += alphabet[triple & 0x3f];
    i += 3;
  }
  const size_t remaining = data.size() - i;
  if (remaining == 1) {
    const uint32_t value = static_cast<unsigned char>(data[i]) << 16;
    result += alphabet[(value >> 18) & 0x3f];
    result += alphabet[(value >> 12) & 0x3f];
    result += "==";
  } else if (remaining == 2) {
    const uint32_t value = (static_cast<unsigned char>(data[i]) << 16) |
                           (static_cast<unsigned char>(data[i + 1]) << 8);
    result += alphabet[(value >> 18) & 0x3f];
    result += alphabet[(value >> 12) & 0x3f];
    result += alphabet[(value >> 6) & 0x3f];
    result += '=';
  }
  return result;
}

std::string random_bytes(size_t count) {
  std::random_device device;
  std::uniform_int_distribution<int> byte(0, 255);
  std::string result;
  result.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    result += static_cast<char>(byte(device));
  }
  return result;
}

// The strength rules of the Python plugin: one of each character class.
bool password_strength_valid(std::string_view password) {
  constexpr std::string_view special = "!#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
  bool upper = false, lower = false, digit = false, punctuation = false;
  for (const char c : password) {
    const auto uc = static_cast<unsigned char>(c);
    upper = upper || std::isupper(uc);
    lower = lower || std::islower(uc);
    digit = digit || std::isdigit(uc);
    punctuation = punctuation || special.find(c) != std::string_view::npos;
  }
  return upper && lower && digit && punctuation;
}

// Throws unless the password may be stored for an MRS user.
void check_password(const std::string &password) {
  if (password.size() < 8) {
    throw std::runtime_error(
        "The minimum authentication string length is 8 characters.");
  }
  if (!password_strength_valid(password)) {
    throw std::runtime_error(
        "The authentication string needs to contain at least one uppercase, "
        "lowercase, a special and a numeric character.");
  }
}

// -- Small helpers --------------------------------------------------------

std::optional<std::string> optional_text(const Db_value &value) {
  return value.as_optional_string();
}

std::optional<Id> optional_id(const Db_value &value) {
  if (value.is_null()) return std::nullopt;
  return value.as_string();
}

// "text" as the Python plugin's quote_str writes it.
std::string double_quote(std::string_view text) {
  std::string result = "\"";
  for (const char c : text) {
    if (c == '\\' || c == '"' || c == '\'') result += '\\';
    result += c;
  }
  return result + "\"";
}

// Whether a JSON text holds anything worth printing in SHOW CREATE output:
// null and empty objects or arrays are left out.
bool json_has_content(const std::optional<std::string> &text) {
  if (!text || text->empty()) return false;
  const auto doc = json::try_parse(*text);
  if (!doc) return true;
  if (doc->is_null()) return false;
  if (doc->is_object()) return !doc->as_object().empty();
  if (doc->is_array()) return !doc->as_array().empty();
  return true;
}

std::string join(const std::vector<std::string> &items,
                 const std::string &separator) {
  std::string result;
  for (const auto &item : items) {
    if (!result.empty()) result += separator;
    result += item;
  }
  return result;
}

// The members of a SET column value.
std::vector<std::string> split_set(const std::string &text) {
  std::vector<std::string> items;
  size_t start = 0;
  while (start <= text.size()) {
    auto end = text.find(',', start);
    if (end == std::string::npos) end = text.size();
    if (end > start) items.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return items;
}

bool contains(const std::vector<std::string> &items, const std::string &item) {
  return std::find(items.begin(), items.end(), item) != items.end();
}

// The operations in the order of the SET column, so the stored value and
// the SHOW output agree.
std::vector<std::string> canonical_operations(
    const std::vector<std::string> &operations) {
  std::vector<std::string> result;
  for (const char *name : {"CREATE", "READ", "UPDATE", "DELETE"}) {
    if (contains(operations, name)) result.emplace_back(name);
  }
  return result;
}

std::string role_service_clause(const Role &role) {
  if (!role.specific_to_service) return "ON ANY SERVICE";
  return "ON SERVICE " + quote_request_path(*role.specific_to_service);
}

// -- Queries --------------------------------------------------------------

const char *k_role_select = R"(
SELECT r.id, r.derived_from_role_id, pr.caption AS derived_from_role_caption,
    r.specific_to_service_id,
    CONCAT(h.name, s.url_context_root) AS specific_to_service_request_path,
    s.url_context_root AS specific_to_service,
    r.caption, r.description, r.options
FROM `mysql_rest_service_metadata`.`mrs_role` r
    LEFT JOIN `mysql_rest_service_metadata`.`mrs_role` pr
        ON r.derived_from_role_id = pr.id
    LEFT JOIN `mysql_rest_service_metadata`.`service` s
        ON r.specific_to_service_id = s.id
    LEFT JOIN `mysql_rest_service_metadata`.`url_host` h
        ON s.url_host_id = h.id
)";

Role role_from_row(const Db_row &row) {
  Role r;
  r.id = row["id"].as_string();
  r.derived_from_role_id = optional_id(row["derived_from_role_id"]);
  r.derived_from_role_caption = optional_text(row["derived_from_role_caption"]);
  r.specific_to_service_id = optional_id(row["specific_to_service_id"]);
  r.specific_to_service = optional_text(row["specific_to_service"]);
  r.specific_to_service_request_path =
      row["specific_to_service_request_path"].as_string();
  r.caption = row["caption"].as_string();
  r.description = optional_text(row["description"]);
  r.options = optional_text(row["options"]);
  return r;
}

std::vector<Role> query_roles(Db_session *session, const std::string &where) {
  std::string query = k_role_select;
  if (!where.empty()) query += " WHERE " + where;
  query += " ORDER BY r.caption, specific_to_service_request_path";

  std::vector<Role> roles;
  for (const auto &row : session->query(query).rows) {
    roles.push_back(role_from_row(row));
  }
  return roles;
}

const char *k_privilege_select = R"(
SELECT p.id, p.role_id, r.caption AS role_caption, p.crud_operations,
    p.service_path, p.schema_path, p.object_path
FROM `mysql_rest_service_metadata`.`mrs_privilege` p
    JOIN `mysql_rest_service_metadata`.`mrs_role` r ON p.role_id = r.id
)";

Privilege privilege_from_row(const Db_row &row) {
  Privilege p;
  p.id = row["id"].as_string();
  p.role_id = row["role_id"].as_string();
  p.role_caption = row["role_caption"].as_string();
  p.crud_operations = split_set(row["crud_operations"].as_string());
  p.service_path = row["service_path"].as_string();
  p.schema_path = row["schema_path"].as_string();
  p.object_path = row["object_path"].as_string();
  return p;
}

const char *k_auth_app_select = R"(
SELECT a.id, a.auth_vendor_id, a.name, a.description, a.url,
    a.url_direct_auth, a.access_token, a.app_id, a.enabled,
    a.limit_to_registered_users, a.default_role_id, a.options,
    v.name AS auth_vendor
FROM `mysql_rest_service_metadata`.`auth_app` a
    LEFT JOIN `mysql_rest_service_metadata`.`auth_vendor` v
        ON v.id = a.auth_vendor_id
)";

Auth_app auth_app_from_row(const Db_row &row) {
  Auth_app a;
  a.id = row["id"].as_string();
  a.auth_vendor_id = row["auth_vendor_id"].as_string();
  a.auth_vendor = row["auth_vendor"].as_string();
  a.name = row["name"].as_string();
  a.description = optional_text(row["description"]);
  a.url = optional_text(row["url"]);
  a.url_direct_auth = optional_text(row["url_direct_auth"]);
  a.access_token = optional_text(row["access_token"]);
  a.app_id = optional_text(row["app_id"]);
  // The column is nullable; an unset flag counts as enabled
  a.enabled = row["enabled"].is_null() || row["enabled"].as_bool();
  a.limit_to_registered_users = row["limit_to_registered_users"].as_bool();
  a.default_role_id = optional_id(row["default_role_id"]);
  a.options = optional_text(row["options"]);
  return a;
}

std::vector<Auth_app> query_auth_apps(Db_session *session,
                                      const std::string &where) {
  std::string query = k_auth_app_select;
  if (!where.empty()) query += " WHERE " + where;
  query += " ORDER BY a.name";

  std::vector<Auth_app> auth_apps;
  for (const auto &row : session->query(query).rows) {
    auth_apps.push_back(auth_app_from_row(row));
  }
  return auth_apps;
}

const char *k_user_select = R"(
SELECT u.id, u.auth_app_id, u.name, u.email, u.vendor_user_id,
    u.login_permitted, u.mapped_user_id, u.app_options, u.options,
    (u.auth_string IS NOT NULL) AS has_auth_string,
    a.name AS auth_app_name
FROM `mysql_rest_service_metadata`.`mrs_user` u
    JOIN `mysql_rest_service_metadata`.`auth_app` a ON a.id = u.auth_app_id
)";

User user_from_row(const Db_row &row) {
  User u;
  u.id = row["id"].as_string();
  u.auth_app_id = row["auth_app_id"].as_string();
  u.auth_app_name = row["auth_app_name"].as_string();
  u.name = row["name"].as_string();
  u.email = optional_text(row["email"]);
  u.vendor_user_id = optional_text(row["vendor_user_id"]);
  u.mapped_user_id = optional_text(row["mapped_user_id"]);
  u.login_permitted = row["login_permitted"].as_bool();
  u.app_options = optional_text(row["app_options"]);
  u.options = optional_text(row["options"]);
  u.has_auth_string = row["has_auth_string"].as_bool();
  return u;
}

std::vector<User> query_users(Db_session *session, const std::string &where,
                              const std::string &order_by = "u.name") {
  std::string query = k_user_select;
  if (!where.empty()) query += " WHERE " + where;
  query += " ORDER BY " + order_by;

  std::vector<User> users;
  for (const auto &row : session->query(query).rows) {
    users.push_back(user_from_row(row));
  }
  return users;
}

// Whether users of an auth app have their password stored by MRS.
bool stores_passwords(Db_session *session, const Id &auth_app_id) {
  const auto auth_app = get_auth_app(session, auth_app_id);
  if (!auth_app) throw std::runtime_error("The auth_app was not found");
  return auth_app->auth_vendor_id == mrs_vendor_id();
}

// The OPTIONS document of a user as SHOW CREATE prints it: the stored
// options with the columns that came out of them put back.
std::string user_options_document(const User &user) {
  json::Value options = json::Value::object();
  if (user.options) {
    if (const auto doc = json::try_parse(*user.options); doc && doc->is_object()) {
      options = *doc;
    }
  }
  if (user.email && !user.email->empty()) options.set("email", *user.email);
  if (user.vendor_user_id && !user.vendor_user_id->empty()) {
    options.set("vendor_user_id", *user.vendor_user_id);
  }
  if (user.mapped_user_id && !user.mapped_user_id->empty()) {
    options.set("mapped_user_id", *user.mapped_user_id);
  }
  if (options.as_object().empty()) return {};
  return options.dump();
}

}  // namespace

// -- Auth vendors ---------------------------------------------------------

namespace {

std::string auth_vendor_select() {
  return "SELECT id, name, enabled, comments FROM " +
         sql::metadata_table("auth_vendor");
}

Auth_vendor auth_vendor_from_row(const Db_row &row) {
  Auth_vendor vendor;
  vendor.id = row["id"].as_string();
  vendor.name = row["name"].as_string();
  vendor.enabled = row["enabled"].as_bool();
  vendor.comments = optional_text(row["comments"]);
  return vendor;
}

}  // namespace

std::optional<Auth_vendor> find_auth_vendor(Db_session *session,
                                            std::string_view name) {
  const auto result = session->query(auth_vendor_select() +
                                     " WHERE UPPER(name) = UPPER(" +
                                     sql::quote(name) + ")");
  if (result.empty()) return std::nullopt;
  return auth_vendor_from_row(result.first());
}

std::vector<Auth_vendor> get_auth_vendors(Db_session *session) {
  std::vector<Auth_vendor> vendors;
  for (const auto &row :
       session->query(auth_vendor_select() + " ORDER BY name").rows) {
    vendors.push_back(auth_vendor_from_row(row));
  }
  return vendors;
}

// -- Roles ----------------------------------------------------------------

std::optional<Role> get_role(Db_session *session, const Id &id) {
  auto roles = query_roles(session, "r.id = " + sql::id(id));
  if (roles.empty()) return std::nullopt;
  return std::move(roles.front());
}

std::optional<Role> find_role(Db_session *session, std::string_view caption,
                              const std::optional<Id> &service_id) {
  std::string where = "r.caption = " + sql::quote(caption);
  if (service_id) {
    where += " AND r.specific_to_service_id = " + sql::id(*service_id);
  } else {
    where += " AND r.specific_to_service_id IS NULL";
  }
  auto roles = query_roles(session, where);
  if (roles.empty()) return std::nullopt;
  return std::move(roles.front());
}

std::vector<Role> get_roles(Db_session *session,
                            const std::optional<Id> &service_id,
                            bool include_global) {
  std::string where;
  if (service_id) {
    where = "r.specific_to_service_id = " + sql::id(*service_id);
    if (include_global) where += " OR r.specific_to_service_id IS NULL";
  } else if (!include_global) {
    where = "r.specific_to_service_id IS NOT NULL";
  }
  return query_roles(session, where);
}

std::vector<Granted_role> get_granted_roles(
    Db_session *session, const std::optional<Id> &service_id,
    const std::optional<std::string> &user_name,
    const std::optional<std::string> &auth_app_name, bool include_users) {
  std::string query = R"(
SELECT r.id, r.derived_from_role_id, pr.caption AS derived_from_role_caption,
    r.specific_to_service_id,
    CONCAT(h.name, s.url_context_root) AS specific_to_service_request_path,
    s.url_context_root AS specific_to_service,
    r.caption, r.description, r.options)";
  if (include_users) {
    query +=
        ",\n    GROUP_CONCAT(CONCAT(u.name, '@', a.name) ORDER BY u.name, a.name "
        "SEPARATOR ', ') AS users";
  }
  query += R"(
FROM `mysql_rest_service_metadata`.`mrs_role` r
    JOIN `mysql_rest_service_metadata`.`mrs_user_has_role` ur
        ON ur.role_id = r.id
    LEFT JOIN `mysql_rest_service_metadata`.`mrs_user` u ON u.id = ur.user_id
    LEFT JOIN `mysql_rest_service_metadata`.`auth_app` a ON a.id = u.auth_app_id
    LEFT JOIN `mysql_rest_service_metadata`.`service` s
        ON r.specific_to_service_id = s.id
    LEFT JOIN `mysql_rest_service_metadata`.`url_host` h ON s.url_host_id = h.id
    LEFT JOIN `mysql_rest_service_metadata`.`mrs_role` pr
        ON r.derived_from_role_id = pr.id
)";

  std::vector<std::string> conditions;
  if (service_id) {
    conditions.push_back("(r.specific_to_service_id IS NULL OR "
                         "r.specific_to_service_id = " +
                         sql::id(*service_id) + ")");
  }
  if (user_name) conditions.push_back("u.name = " + sql::quote(*user_name));
  if (auth_app_name) conditions.push_back("a.name = " + sql::quote(*auth_app_name));
  if (!conditions.empty()) query += " WHERE " + join(conditions, " AND ");

  // Every selected column is listed: ONLY_FULL_GROUP_BY does not derive the
  // columns that depend on r.id.
  if (include_users) {
    query +=
        " GROUP BY r.id, r.derived_from_role_id, pr.caption,"
        " r.specific_to_service_id, h.name, s.url_context_root,"
        " r.caption, r.description, r.options";
  }
  query += " ORDER BY r.caption, specific_to_service_request_path";

  std::vector<Granted_role> roles;
  for (const auto &row : session->query(query).rows) {
    Granted_role granted;
    granted.role = role_from_row(row);
    if (include_users) granted.users = optional_text(row["users"]);
    roles.push_back(std::move(granted));
  }
  return roles;
}

Id add_role(Db_session *session, const Role_definition &definition) {
  const std::string duplicate_message =
      "DUPLICATION ERROR: The REST role `" + definition.caption +
      "` has already been defined for the given service. Use the SHOW REST "
      "ROLES; command to display all existing roles.";

  // The unique index does not catch global roles (NULL service), so they
  // are checked here
  if (!definition.specific_to_service_id &&
      find_role(session, definition.caption, std::nullopt)) {
    throw std::runtime_error(duplicate_message);
  }

  const Id id = new_id(session);
  sql::Insert insert("mrs_role");
  insert.set("id", Value::id(id));
  insert.set("derived_from_role_id", Value::id(definition.derived_from_role_id));
  insert.set("specific_to_service_id",
             Value::id(definition.specific_to_service_id));
  insert.set("caption", definition.caption);
  insert.set("description", definition.description);
  insert.set("options", definition.options);
  try {
    session->execute(insert.str());
  } catch (const Db_error &e) {
    if (e.code() == 1062) throw std::runtime_error(duplicate_message);
    throw;
  }
  return id;
}

void delete_role(Db_session *session, const Id &id) {
  try {
    session->execute("DELETE FROM " + sql::metadata_table("mrs_role") +
                     " WHERE id = " + sql::id(id));
  } catch (const Db_error &e) {
    if (e.code() == 1451) {
      throw std::runtime_error(
          "REFERENCE ERROR: This role is referenced by other roles. Please "
          "drop those roles first. Use the SHOW REST ROLES; command to "
          "display all existing roles.");
    }
    throw;
  }
}

std::string role_create_statement(Db_session *session, const Role &role) {
  std::string output = "CREATE REST ROLE " + sql::quote_identifier(role.caption);
  if (role.derived_from_role_caption) {
    output += " EXTENDS " + sql::quote_identifier(*role.derived_from_role_caption);
  }

  if (role.specific_to_service_id) {
    const auto service = get_service(session, *role.specific_to_service_id);
    output += " ON SERVICE " + (service ? service->full_service_path
                                        : role.specific_to_service_request_path);
  } else {
    output += " ON ANY SERVICE";
  }

  if (role.description) {
    output += "\n    COMMENT " + sql::quote(*role.description);
  }
  if (json_has_content(role.options)) {
    output += "\n" + format_json_entry("OPTIONS", role.options);
  }
  return output + ";";
}

std::vector<std::string> role_create_statements(Db_session *session,
                                                const Id &service_id) {
  std::vector<std::string> statements;
  for (const auto &role : get_roles(session, service_id, false)) {
    statements.push_back(role_create_statement(session, role));
  }
  return statements;
}

// -- Privileges -----------------------------------------------------------

std::vector<Privilege> get_role_privileges(Db_session *session,
                                           const Id &role_id) {
  const std::string query =
      std::string(k_privilege_select) + " WHERE p.role_id = " + sql::id(role_id) +
      " ORDER BY p.service_path, p.schema_path, p.object_path";

  std::vector<Privilege> privileges;
  for (const auto &row : session->query(query).rows) {
    privileges.push_back(privilege_from_row(row));
  }
  return privileges;
}

Id add_role_privilege(Db_session *session, const Id &role_id,
                      const std::vector<std::string> &operations,
                      const std::string &service_path,
                      const std::string &schema_path,
                      const std::string &object_path) {
  // Grants on the same paths extend the existing privilege
  for (const auto &privilege : get_role_privileges(session, role_id)) {
    if (privilege.service_path != service_path ||
        privilege.schema_path != schema_path ||
        privilege.object_path != object_path) {
      continue;
    }
    auto merged = privilege.crud_operations;
    for (const auto &operation : operations) {
      if (!contains(merged, operation)) merged.push_back(operation);
    }
    session->execute(sql::Update("mrs_privilege")
                         .set("crud_operations",
                              join(canonical_operations(merged), ","))
                         .where("id = " + sql::id(privilege.id))
                         .str());
    return privilege.id;
  }

  const Id id = new_id(session);
  session->execute(sql::Insert("mrs_privilege")
                       .set("id", Value::id(id))
                       .set("role_id", Value::id(role_id))
                       .set("crud_operations",
                            join(canonical_operations(operations), ","))
                       .set("service_path", service_path)
                       .set("schema_path", schema_path)
                       .set("object_path", object_path)
                       .str());
  return id;
}

bool delete_role_privilege(Db_session *session, const Id &role_id,
                           const std::vector<std::string> &operations,
                           const std::string &service_path,
                           const std::string &schema_path,
                           const std::string &object_path) {
  bool found = false;
  for (const auto &privilege : get_role_privileges(session, role_id)) {
    if (privilege.service_path != service_path ||
        privilege.schema_path != schema_path ||
        privilege.object_path != object_path) {
      continue;
    }
    found = true;

    std::vector<std::string> remaining;
    for (const auto &operation : privilege.crud_operations) {
      if (!contains(operations, operation)) remaining.push_back(operation);
    }
    if (remaining.size() == privilege.crud_operations.size()) continue;

    if (remaining.empty()) {
      session->execute("DELETE FROM " + sql::metadata_table("mrs_privilege") +
                       " WHERE id = " + sql::id(privilege.id));
    } else {
      session->execute(sql::Update("mrs_privilege")
                           .set("crud_operations", join(remaining, ","))
                           .where("id = " + sql::id(privilege.id))
                           .str());
    }
  }
  return found;
}

std::string privilege_grant_statement(const Privilege &privilege,
                                      const Role &role) {
  // A service path may carry a host name in front of the request path
  std::string host;
  std::string request_path;
  const auto &service = privilege.service_path;
  if (service.empty() || service[0] == '/' || service == "*") {
    request_path = service;
  } else {
    const auto slash = service.find('/');
    host = service.substr(0, slash);
    if (slash != std::string::npos) request_path = service.substr(slash);
  }

  std::string target = "SERVICE ";
  if (!host.empty()) target += double_quote(host) + " ";
  target += quote_request_path(request_path);
  target += " SCHEMA " + quote_request_path(privilege.schema_path);
  target += " OBJECT " + quote_request_path(privilege.object_path);

  return "GRANT REST " + join(privilege.crud_operations, ",") + " ON " + target +
         " TO " + sql::quote_identifier(privilege.role_caption) + " " +
         role_service_clause(role);
}

// -- Auth apps ------------------------------------------------------------

std::optional<Auth_app> get_auth_app(Db_session *session, const Id &id) {
  auto auth_apps = query_auth_apps(session, "a.id = " + sql::id(id));
  if (auth_apps.empty()) return std::nullopt;
  return std::move(auth_apps.front());
}

std::optional<Auth_app> find_auth_app(Db_session *session, std::string_view name) {
  auto auth_apps = query_auth_apps(
      session, "UPPER(a.name) = UPPER(" + sql::quote(name) + ")");
  if (auth_apps.empty()) return std::nullopt;
  return std::move(auth_apps.front());
}

std::vector<Auth_app> get_auth_apps(Db_session *session,
                                    const std::optional<Id> &service_id) {
  if (!service_id) return query_auth_apps(session, {});
  return query_auth_apps(session, "a.id IN (SELECT auth_app_id FROM " +
                                      sql::metadata_table("service_has_auth_app") +
                                      " WHERE service_id = " +
                                      sql::id(*service_id) + ")");
}

Id add_auth_app(Db_session *session, const Auth_app_definition &definition) {
  const Id id = new_id(session);
  session->execute(
      sql::Insert("auth_app")
          .set("id", Value::id(id))
          .set("auth_vendor_id", Value::id(definition.auth_vendor_id))
          .set("name", definition.name)
          .set("description", definition.description)
          .set("url", definition.url)
          .set("url_direct_auth", definition.url_direct_auth)
          .set("access_token", definition.access_token)
          .set("app_id", definition.app_id)
          .set("enabled", definition.enabled)
          .set("limit_to_registered_users", definition.limit_to_registered_users)
          .set("default_role_id", Value::id(definition.default_role_id))
          .set("options", definition.options)
          .str());
  return id;
}

void update_auth_app(Db_session *session, const Id &id,
                     const Auth_app_changes &changes) {
  sql::Update update("auth_app");
  if (changes.name) update.set("name", *changes.name);
  if (changes.description) update.set("description", *changes.description);
  if (changes.enabled) update.set("enabled", *changes.enabled);
  if (changes.limit_to_registered_users) {
    update.set("limit_to_registered_users", *changes.limit_to_registered_users);
  }
  if (changes.default_role_id) {
    update.set("default_role_id", Value::id(*changes.default_role_id));
  }
  if (changes.url) update.set("url", *changes.url);
  if (changes.access_token) update.set("access_token", *changes.access_token);
  if (changes.app_id) update.set("app_id", *changes.app_id);

  if (update.empty()) return;
  update.where("id = " + sql::id(id));
  session->execute(update.str());
}

void delete_auth_app(Db_session *session, const Id &id) {
  session->execute("DELETE FROM " + sql::metadata_table("service_has_auth_app") +
                   " WHERE auth_app_id = " + sql::id(id));
  // The users of the app are deleted by the auth_app_BEFORE_DELETE trigger
  session->execute("DELETE FROM " + sql::metadata_table("auth_app") +
                   " WHERE id = " + sql::id(id));
}

void link_auth_app(Db_session *session, const Id &auth_app_id,
                   const Id &service_id) {
  try {
    session->execute(sql::Insert("service_has_auth_app")
                         .set("service_id", Value::id(service_id))
                         .set("auth_app_id", Value::id(auth_app_id))
                         .str());
  } catch (const Db_error &e) {
    if (e.code() == 1062) {
      throw std::runtime_error(
          "The REST auth app has already been added to the REST service.");
    }
    throw;
  }
}

void unlink_auth_app(Db_session *session, const Id &auth_app_id,
                     const Id &service_id) {
  const auto affected = session->execute(
      "DELETE FROM " + sql::metadata_table("service_has_auth_app") +
      " WHERE service_id = " + sql::id(service_id) +
      " AND auth_app_id = " + sql::id(auth_app_id));
  if (affected == 0) {
    throw std::runtime_error(
        "The REST auth app cannot be removed as it is not assigned to the "
        "REST service.");
  }
}

std::string auth_app_create_statement(Db_session *session,
                                      const Auth_app &auth_app,
                                      bool include_users) {
  std::string upper_vendor = auth_app.auth_vendor;
  std::transform(upper_vendor.begin(), upper_vendor.end(), upper_vendor.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  std::string vendor;
  if (upper_vendor == "MRS") {
    vendor = "MRS";
  } else if (upper_vendor == "MYSQL INTERNAL") {
    vendor = "MYSQL";
  } else {
    vendor = sql::quote_identifier(auth_app.auth_vendor);
  }

  std::string output = "CREATE OR REPLACE REST AUTH APP " +
                       sql::quote_identifier(auth_app.name) + "\n    VENDOR " +
                       vendor;
  if (!auth_app.enabled) output += "\n    DISABLED";
  if (auth_app.description && !auth_app.description->empty()) {
    output += "\n    COMMENT " + sql::quote(*auth_app.description);
  }
  if (!auth_app.limit_to_registered_users) {
    output += "\n    ALLOW NEW USERS TO REGISTER";
  }
  if (auth_app.default_role_id) {
    if (const auto role = get_role(session, *auth_app.default_role_id)) {
      output += "\n    DEFAULT ROLE " + sql::quote_identifier(role->caption);
    }
  }
  output += ";";

  if (include_users) {
    for (const auto &user : get_users(session, auth_app.id)) {
      output += "\n\n" + user_create_statement(session, user, true);
    }
  }
  return output;
}

// -- Users ----------------------------------------------------------------

std::optional<User> find_user(Db_session *session, const Id &auth_app_id,
                              std::string_view name) {
  auto users = query_users(session, "u.auth_app_id = " + sql::id(auth_app_id) +
                                        " AND u.name = " + sql::quote(name));
  if (users.empty()) return std::nullopt;
  return std::move(users.front());
}

std::vector<User> get_users(Db_session *session, const Id &auth_app_id) {
  return query_users(session, "u.auth_app_id = " + sql::id(auth_app_id));
}

std::vector<User> get_users(Db_session *session,
                            const std::optional<Id> &service_id,
                            const std::optional<Id> &auth_app_id) {
  std::vector<std::string> conditions;
  if (service_id) {
    conditions.push_back("u.auth_app_id IN (SELECT auth_app_id FROM " +
                         sql::metadata_table("service_has_auth_app") +
                         " WHERE service_id = " + sql::id(*service_id) + ")");
  }
  if (auth_app_id) {
    conditions.push_back("u.auth_app_id = " + sql::id(*auth_app_id));
  }

  std::string where;
  for (const auto &condition : conditions) {
    if (!where.empty()) where += " AND ";
    where += condition;
  }
  return query_users(session, where, "a.name, u.name");
}

std::string hash_password(std::string_view password) {
  constexpr int k_iterations = 5000;
  const std::string salt = random_bytes(20);
  const std::string salted = pbkdf2_sha256(password, salt, k_iterations);
  const std::string client_key = hmac_sha256(salted, "Client Key");
  const std::string stored_key = sha256(client_key);

  // "$A$005$<salt>$<key>": the scheme letter and the iterations in
  // thousands, as the router expects them
  return "$A$005$" + base64_encode(salt) + "$" + base64_encode(stored_key);
}

Id add_user(Db_session *session, const User_definition &definition) {
  std::optional<std::string> auth_string;
  if (stores_passwords(session, definition.auth_app_id)) {
    if (!definition.password || definition.password->empty()) {
      throw std::runtime_error(
          "The authentication string is required for this app.");
    }
    check_password(*definition.password);
    auth_string = hash_password(*definition.password);
  } else if (definition.password) {
    throw std::runtime_error(
        "Password changing not supported for this authentication method");
  }

  const Id id = new_id(session);
  const auto affected = session->execute(
      sql::Insert("mrs_user")
          .set("id", Value::id(id))
          .set("auth_app_id", Value::id(definition.auth_app_id))
          .set("name", definition.name)
          .set("email", definition.email)
          .set("vendor_user_id", definition.vendor_user_id)
          .set("login_permitted", definition.login_permitted)
          .set("mapped_user_id", definition.mapped_user_id)
          .set("options", definition.options)
          .set("app_options", definition.app_options)
          .set("auth_string", auth_string)
          .str());
  if (affected == 0) throw std::runtime_error("Failed to insert the new user.");
  return id;
}

void update_user(Db_session *session, const User &user,
                 const User_changes &changes) {
  sql::Update update("mrs_user");
  if (changes.password && !changes.password->empty()) {
    if (!stores_passwords(session, user.auth_app_id)) {
      throw std::runtime_error(
          "Password change not supported for the authentication method");
    }
    check_password(*changes.password);
    update.set("auth_string", hash_password(*changes.password));
  }
  if (changes.login_permitted) update.set("login_permitted", *changes.login_permitted);
  if (changes.email) update.set("email", *changes.email);
  if (changes.vendor_user_id) update.set("vendor_user_id", *changes.vendor_user_id);
  if (changes.mapped_user_id) update.set("mapped_user_id", *changes.mapped_user_id);
  if (changes.app_options) update.set("app_options", *changes.app_options);
  if (changes.options) {
    if (changes.merge_options) {
      update.set_raw("options = JSON_MERGE_PATCH(COALESCE(options, '{}'), " +
                     sql::quote(*changes.options) + ")");
    } else {
      update.set("options", *changes.options);
    }
  }

  if (update.empty()) return;
  update.where("id = " + sql::id(user.id));
  session->execute(update.str());
}

void delete_user(Db_session *session, const Id &id) {
  session->execute("DELETE FROM " + sql::metadata_table("mrs_user") +
                   " WHERE id = " + sql::id(id));
}

std::vector<User_role> get_user_roles(Db_session *session, const Id &user_id) {
  const std::string query = R"(
SELECT ur.comments AS grant_comments, ur.options AS grant_options,
    r.id, r.derived_from_role_id, pr.caption AS derived_from_role_caption,
    r.specific_to_service_id,
    CONCAT(h.name, s.url_context_root) AS specific_to_service_request_path,
    s.url_context_root AS specific_to_service,
    r.caption, r.description, r.options
FROM `mysql_rest_service_metadata`.`mrs_user_has_role` ur
    JOIN `mysql_rest_service_metadata`.`mrs_role` r ON ur.role_id = r.id
    LEFT JOIN `mysql_rest_service_metadata`.`mrs_role` pr
        ON r.derived_from_role_id = pr.id
    LEFT JOIN `mysql_rest_service_metadata`.`service` s
        ON s.id = r.specific_to_service_id
    LEFT JOIN `mysql_rest_service_metadata`.`url_host` h ON s.url_host_id = h.id
WHERE ur.user_id = )" + sql::id(user_id) +
                            " ORDER BY r.caption, specific_to_service_request_path";

  std::vector<User_role> roles;
  for (const auto &row : session->query(query).rows) {
    User_role user_role;
    user_role.role = role_from_row(row);
    user_role.comments = optional_text(row["grant_comments"]);
    user_role.options = optional_text(row["grant_options"]);
    roles.push_back(std::move(user_role));
  }
  return roles;
}

void add_user_role(Db_session *session, const Id &user_id, const Id &role_id,
                   const std::optional<std::string> &comments) {
  session->execute(sql::Insert("mrs_user_has_role")
                       .set("user_id", Value::id(user_id))
                       .set("role_id", Value::id(role_id))
                       .set("comments", comments)
                       .str());
}

void delete_user_role(Db_session *session, const Id &user_id,
                      const Id &role_id) {
  session->execute("DELETE FROM " + sql::metadata_table("mrs_user_has_role") +
                   " WHERE user_id = " + sql::id(user_id) +
                   " AND role_id = " + sql::id(role_id));
}

std::string user_role_grant_statement(const User &user,
                                      const User_role &user_role) {
  std::string output = "GRANT REST ROLE " +
                       sql::quote_identifier(user_role.role.caption) + " " +
                       role_service_clause(user_role.role) + " TO " +
                       sql::quote_identifier(user.name) + "@" +
                       sql::quote_identifier(user.auth_app_name);
  if (user_role.comments) {
    output += "\n    COMMENT " + sql::quote(*user_role.comments);
  }
  const auto options = format_json_entry("OPTIONS", user_role.options);
  if (!options.empty()) output += "\n" + options;
  return output + ";";
}

std::string user_create_statement(Db_session *session, const User &user,
                                  bool include_grants) {
  std::string output = "CREATE OR REPLACE REST USER " +
                       sql::quote_identifier(user.name) + "@" +
                       sql::quote_identifier(user.auth_app_name);
  if (!user.login_permitted) output += "\n    ACCOUNT LOCK";
  if (user.has_auth_string) output += "\n    IDENTIFIED BY '[Stored Password]'";

  const auto options = user_options_document(user);
  if (!options.empty()) output += "\n" + format_json_entry("OPTIONS", options);
  const auto app_options = format_json_entry("APP OPTIONS", user.app_options);
  if (!app_options.empty()) output += "\n" + app_options;
  output += ";";

  if (include_grants) {
    for (const auto &user_role : get_user_roles(session, user.id)) {
      output += "\n\n" + user_role_grant_statement(user, user_role);
    }
  }
  return output;
}

}  // namespace metadata
}  // namespace mrs
