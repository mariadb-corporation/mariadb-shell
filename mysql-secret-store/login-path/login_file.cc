/*
 * Copyright (c) 2026, MariaDB plc.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335 USA
 */

#include "mysql-secret-store/login-path/login_file.h"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef _WIN32
#include <sys/file.h>
#include <unistd.h>
#endif  // !_WIN32

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "mysql-secret-store/include/helper.h"
#include "mysqlshdk/libs/utils/utils_file.h"
#include "mysqlshdk/libs/utils/utils_general.h"
#include "mysqlshdk/libs/utils/utils_path.h"
#include "mysqlshdk/libs/utils/utils_string.h"

namespace mysql {
namespace secret_store {
namespace login_path {

using mysql::secret_store::common::Helper_exception;

namespace {

/*
  Format constants, mirroring mysys/my_default_priv.h and
  client/mysql_config_editor.cc of MySQL.
*/
constexpr size_t k_login_key_len = 20;        // LOGIN_KEY_LEN
constexpr size_t k_max_cipher_store_len = 4;  // MAX_CIPHER_STORE_LEN
constexpr size_t k_unused_len = 4;            // "reserved for future use"
constexpr size_t k_header_len = k_unused_len + k_login_key_len;  // 24
constexpr size_t k_line_max = 4096;                              // MY_LINE_MAX
constexpr size_t k_aes_block_len = 16;

/*
  mysys reads a login-file line into a 4k buffer and refuses anything longer,
  relying on mysql_config_editor never writing such a line. The largest cipher
  record which still fits alongside its 4-byte length prefix is 4080 bytes, so
  the longest plain-text line - its newline included - is 4079 bytes.
*/
constexpr size_t k_max_plain_line = 4079;

using Login_key = std::array<unsigned char, k_login_key_len>;

/**
 * Reduces the 20-byte login key to the 16 bytes AES-128 needs.
 *
 * mysql_config_editor hands a LOGIN_KEY_LEN (20) byte key to a 128-bit cipher
 * and mysys folds it down inside my_aes_create_key() (mysys/my_aes.cc): the
 * result is zeroed, then every key byte is XOR-ed into it, cycling over the
 * 16 output bytes. Calling OpenSSL EVP directly means reproducing that here -
 * getting it wrong yields a file that looks valid but no MySQL tool can read.
 */
void fold_login_key(const Login_key &key,
                    unsigned char (&rkey)[k_aes_block_len]) {
  memset(rkey, 0, sizeof(rkey));

  size_t j = 0;

  for (size_t i = 0; i < key.size(); ++i) {
    rkey[j] ^= key[i];

    if (++j == sizeof(rkey)) {
      j = 0;
    }
  }
}

struct Evp_ctx_deleter {
  void operator()(EVP_CIPHER_CTX *ctx) const { EVP_CIPHER_CTX_free(ctx); }
};

using Evp_ctx = std::unique_ptr<EVP_CIPHER_CTX, Evp_ctx_deleter>;

Evp_ctx new_evp_ctx() {
  Evp_ctx ctx{EVP_CIPHER_CTX_new()};

  if (!ctx) {
    throw Helper_exception{"Failed to initialize the cipher context"};
  }

  return ctx;
}

/**
 * AES-128-ECB with PKCS padding, as used by my_aes_encrypt(..., my_aes_128_ecb,
 * nullptr) - no IV, padding enabled.
 */
std::string aes_encrypt(const Login_key &key, const std::string &plain) {
  unsigned char rkey[k_aes_block_len];
  fold_login_key(key, rkey);

  const auto ctx = new_evp_ctx();
  std::string cipher;
  cipher.resize(plain.length() + k_aes_block_len);

  int update_len = 0;
  int final_len = 0;
  const auto out = reinterpret_cast<unsigned char *>(cipher.data());

  if (1 != EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_ecb(), nullptr, rkey,
                              nullptr) ||
      1 != EVP_EncryptUpdate(
               ctx.get(), out, &update_len,
               reinterpret_cast<const unsigned char *>(plain.data()),
               static_cast<int>(plain.length())) ||
      1 != EVP_EncryptFinal_ex(ctx.get(), out + update_len, &final_len)) {
    ERR_clear_error();
    throw Helper_exception{"Failed to encrypt the login file"};
  }

  cipher.resize(static_cast<size_t>(update_len) +
                static_cast<size_t>(final_len));

  return cipher;
}

std::string aes_decrypt(const Login_key &key, const char *cipher,
                        size_t cipher_len) {
  unsigned char rkey[k_aes_block_len];
  fold_login_key(key, rkey);

  const auto ctx = new_evp_ctx();
  std::string plain;
  plain.resize(cipher_len + k_aes_block_len);

  int update_len = 0;
  int final_len = 0;
  const auto out = reinterpret_cast<unsigned char *>(plain.data());

  if (1 != EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_ecb(), nullptr, rkey,
                              nullptr) ||
      1 != EVP_DecryptUpdate(ctx.get(), out, &update_len,
                             reinterpret_cast<const unsigned char *>(cipher),
                             static_cast<int>(cipher_len)) ||
      1 != EVP_DecryptFinal_ex(ctx.get(), out + update_len, &final_len)) {
    ERR_clear_error();
    throw Helper_exception{
        "Failed to decrypt the login file, it may be corrupted"};
  }

  plain.resize(static_cast<size_t>(update_len) +
               static_cast<size_t>(final_len));

  return plain;
}

/**
 * Generates a login key the same shape as generate_login_key() does: 20 bytes
 * in the 0-31 range. The reduction is deliberate on MySQL's side ("a sequence
 * of random non-printable ASCII") and has to be reproduced, because it is the
 * folded result of these bytes that both implementations must agree on.
 */
Login_key generate_login_key() {
  Login_key key{};

  if (1 != RAND_bytes(key.data(), static_cast<int>(key.size()))) {
    ERR_clear_error();
    throw Helper_exception{"Failed to generate the login key"};
  }

  for (auto &b : key) {
    b %= 32;
  }

  return key;
}

int32_t read_int4_le(const char *buffer) {
  const auto b = reinterpret_cast<const unsigned char *>(buffer);
  return static_cast<int32_t>(static_cast<uint32_t>(b[0]) |
                              (static_cast<uint32_t>(b[1]) << 8) |
                              (static_cast<uint32_t>(b[2]) << 16) |
                              (static_cast<uint32_t>(b[3]) << 24));
}

void append_int4_le(std::string *out, uint32_t value) {
  out->push_back(static_cast<char>(value & 0xFF));
  out->push_back(static_cast<char>((value >> 8) & 0xFF));
  out->push_back(static_cast<char>((value >> 16) & 0xFF));
  out->push_back(static_cast<char>((value >> 24) & 0xFF));
}

/**
 * The decrypted login file: its key (so a rewrite keeps it) and the option-file
 * text.
 */
struct Contents {
  Login_key key{};
  bool has_key = false;
  std::string plain;
};

/**
 * Keeps the key an existing file already carries, so a rewrite does not change
 * it; generates one when there is nothing to keep. mysql_config_editor does the
 * same - only its `reset` command re-keys.
 */
void ensure_key(Contents *contents) {
  if (!contents->has_key) {
    contents->key = generate_login_key();
    contents->has_key = true;
  }
}

/**
 * An advisory exclusive lock on the login file, held across the read-modify-
 * write of an update. mysql_config_editor does not lock, we do.
 *
 * Updates replace the file through rename(), so a lock taken on the file we
 * opened can end up being a lock on an unlinked inode. After acquiring it we
 * re-check that the descriptor still refers to the path and start over if it
 * does not.
 */
class Login_file_lock final {
 public:
  explicit Login_file_lock(const std::string &path) {
#ifndef _WIN32
    // bounded, so a pathologically busy store cannot spin here forever - the
    // lock is advisory, losing it only costs us the guarantee
    for (int attempt = 0; attempt < 10; ++attempt) {
      m_fd =
          ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, S_IRUSR | S_IWUSR);

      if (m_fd < 0) {
        // the file cannot be opened for writing - let the operation itself
        // report the real problem
        return;
      }

      if (0 != ::flock(m_fd, LOCK_EX)) {
        release();
        return;
      }

      struct stat by_fd = {};
      struct stat by_path = {};

      if (0 != ::fstat(m_fd, &by_fd) || 0 != ::stat(path.c_str(), &by_path)) {
        return;
      }

      if (by_fd.st_dev == by_path.st_dev && by_fd.st_ino == by_path.st_ino) {
        return;
      }

      // somebody renamed a new file over ours while we were waiting
      release();
    }
#else   // _WIN32
    (void)path;
#endif  // _WIN32
  }

  Login_file_lock(const Login_file_lock &) = delete;
  Login_file_lock &operator=(const Login_file_lock &) = delete;

  ~Login_file_lock() { release(); }

 private:
  void release() {
#ifndef _WIN32
    if (m_fd >= 0) {
      ::flock(m_fd, LOCK_UN);
      ::close(m_fd);
      m_fd = -1;
    }
#endif  // !_WIN32
  }

  int m_fd = -1;
};

/**
 * Resolves the login file path exactly like my_default_get_login_file().
 */
std::string login_file_path() {
  if (const auto test_file = ::getenv("MYSQL_TEST_LOGIN_FILE")) {
    return test_file;
  }

#ifdef _WIN32
  if (const auto app_data = ::getenv("APPDATA")) {
    return std::string{app_data} + "\\MySQL\\.mylogin.cnf";
  }
#else   // !_WIN32
  if (const auto home = ::getenv("HOME")) {
    return std::string{home} + "/.mylogin.cnf";
  }
#endif  // !_WIN32

  throw Helper_exception{
      "Unable to determine the location of the login file, neither "
      "MYSQL_TEST_LOGIN_FILE nor HOME is set"};
}

std::string read_whole_file(const std::string &path) {
  std::ifstream f{path, std::ios::binary};

  if (!f.is_open()) {
    throw Helper_exception{"Failed to open the login file \"" + path + "\""};
  }

  std::string data{std::istreambuf_iterator<char>{f},
                   std::istreambuf_iterator<char>{}};

  if (f.bad()) {
    throw Helper_exception{"Failed to read the login file \"" + path + "\""};
  }

  return data;
}

/**
 * Reads and decrypts the login file. A file which does not exist (or which is
 * still empty, i.e. has no header yet) reads back as an empty store.
 */
Contents read_contents(const std::string &path) {
  Contents contents;

  if (!shcore::path_exists(path)) {
    return contents;
  }

  /*
    A group/other-readable login file is a problem, but MySQL only warns about
    it (check_file_permissions() in mysys/my_default.cc) and refusing here would
    lock out anybody with an existing 0644 file. The helper protocol has no
    channel for a warning either - its stderr is folded into the stdout the
    shell parses - so this proceeds silently. store() writes 0600.
  */

  const auto data = read_whole_file(path);

  if (data.empty()) {
    return contents;
  }

  if (data.length() < k_header_len) {
    throw Helper_exception{"The login file \"" + path +
                           "\" is truncated, it has no valid header"};
  }

  memcpy(contents.key.data(), data.data() + k_unused_len, k_login_key_len);
  contents.has_key = true;

  size_t pos = k_header_len;

  while (pos + k_max_cipher_store_len <= data.length()) {
    const auto cipher_len = read_int4_le(data.data() + pos);
    pos += k_max_cipher_store_len;

    if (cipher_len <= 0 || static_cast<size_t>(cipher_len) > k_line_max ||
        static_cast<size_t>(cipher_len) > data.length() - pos) {
      throw Helper_exception{"The login file \"" + path +
                             "\" is corrupted, invalid record length"};
    }

    contents.plain += aes_decrypt(contents.key, data.data() + pos,
                                  static_cast<size_t>(cipher_len));
    pos += static_cast<size_t>(cipher_len);
  }

  return contents;
}

/**
 * Encrypts the option-file text into the on-disk representation: the 4 unused
 * bytes, the login key, then one length-prefixed record per line.
 */
std::string encrypt_contents(const Contents &contents) {
  std::string data(k_unused_len, '\0');
  data.append(reinterpret_cast<const char *>(contents.key.data()),
              contents.key.size());

  size_t pos = 0;

  while (pos < contents.plain.length()) {
    auto end = contents.plain.find('\n', pos);

    if (std::string::npos == end) {
      // mysql_config_editor drops a trailing line which has no newline; we
      // never write one, but an externally written file could have it
      break;
    }

    // the record covers the line and its newline
    const auto line = contents.plain.substr(pos, end - pos + 1);
    pos = end + 1;

    if (line.length() > k_max_plain_line) {
      throw Helper_exception{
          "The login-path helper cannot store an option line longer than " +
          std::to_string(k_max_plain_line - 1) +
          " characters. Please keep in mind that all '\\' characters are "
          "prepended with '\\', decreasing available space."};
    }

    const auto cipher = aes_encrypt(contents.key, line);
    append_int4_le(&data, static_cast<uint32_t>(cipher.length()));
    data += cipher;
  }

  return data;
}

/**
 * Writes the login file. The new contents go to a temporary file in the same
 * directory which is then renamed over the original, so an interrupted update
 * cannot destroy the store.
 */
void write_contents(const std::string &path, const Contents &contents) {
  const auto data = encrypt_contents(contents);

#ifndef _WIN32
  auto temp_path = path + ".shell-tmp-XXXXXX";
  const auto fd = ::mkstemp(temp_path.data());

  if (fd < 0) {
    throw Helper_exception{"Failed to create a temporary file next to \"" +
                           path + "\""};
  }

  shcore::Scoped_callback cleanup{[&temp_path]() {
    if (!temp_path.empty()) {
      ::remove(temp_path.c_str());
    }
  }};

  // mkstemp() already creates the file 0600; preserve the mode of an existing
  // login file instead, so an update does not silently change it
  mode_t mode = S_IRUSR | S_IWUSR;
  struct stat original = {};

  if (0 == ::stat(path.c_str(), &original)) {
    mode = original.st_mode & 07777;
  }

  if (0 != ::fchmod(fd, mode)) {
    ::close(fd);
    throw Helper_exception{"Failed to set the permissions of \"" + temp_path +
                           "\""};
  }

  size_t written = 0;

  while (written < data.length()) {
    const auto rc = ::write(fd, data.data() + written, data.length() - written);

    if (rc <= 0) {
      ::close(fd);
      throw Helper_exception{"Failed to write \"" + temp_path + "\""};
    }

    written += static_cast<size_t>(rc);
  }

  if (0 != ::fsync(fd) || 0 != ::close(fd)) {
    throw Helper_exception{"Failed to write \"" + temp_path + "\""};
  }
#else   // _WIN32
  auto temp_path = path + ".shell-tmp";

  {
    std::ofstream f{temp_path, std::ios::binary | std::ios::trunc};

    if (!f.is_open()) {
      throw Helper_exception{"Failed to create a temporary file next to \"" +
                             path + "\""};
    }

    f.write(data.data(), data.length());
    f.close();

    if (f.fail()) {
      ::remove(temp_path.c_str());
      throw Helper_exception{"Failed to write \"" + temp_path + "\""};
    }
  }

  shcore::Scoped_callback cleanup{[&temp_path]() {
    if (!temp_path.empty()) {
      ::remove(temp_path.c_str());
    }
  }};

  // rename() does not replace an existing file on Windows
  ::remove(path.c_str());
#endif  // _WIN32

  if (0 != ::rename(temp_path.c_str(), path.c_str())) {
    throw Helper_exception{"Failed to replace the login file \"" + path + "\""};
  }

  temp_path.clear();
}

////////////////////////////////////////////////////////////////////////////////
// option-file text handling
////////////////////////////////////////////////////////////////////////////////

struct Option {
  std::string line;   // verbatim, as it appears in the file
  std::string name;   // parsed name, empty if the line has none
  std::string value;  // raw value, quotes and escapes intact
  bool has_value = false;
};

struct Section {
  bool named = false;  // false only for lines preceding the first [group]
  std::string name;
  std::vector<Option> options;
};

bool is_space(char c) {
  return ' ' == c || '\t' == c || '\r' == c || '\n' == c || '\v' == c ||
         '\f' == c;
}

std::string strip(const std::string &s) {
  size_t begin = 0;
  size_t end = s.length();

  while (begin < end && is_space(s[begin])) ++begin;
  while (end > begin && is_space(s[end - 1])) --end;

  return s.substr(begin, end - begin);
}

Option parse_option(std::string line) {
  Option option;
  const auto pos = line.find('=');

  if (std::string::npos == pos) {
    option.name = strip(line);
  } else {
    option.name = strip(line.substr(0, pos));
    option.value = strip(line.substr(pos + 1));
    option.has_value = true;
  }

  option.line = std::move(line);

  return option;
}

std::vector<Section> parse_sections(const std::string &plain) {
  std::vector<Section> sections;
  size_t pos = 0;

  while (pos < plain.length()) {
    auto end = plain.find('\n', pos);

    if (std::string::npos == end) {
      end = plain.length();
    }

    auto line = plain.substr(pos, end - pos);
    pos = end + 1;

    const auto stripped = strip(line);

    if (!stripped.empty() && '[' == stripped.front() &&
        ']' == stripped.back()) {
      sections.emplace_back();
      sections.back().named = true;
      sections.back().name = stripped.substr(1, stripped.length() - 2);
      continue;
    }

    if (sections.empty()) {
      sections.emplace_back();
    }

    sections.back().options.emplace_back(parse_option(std::move(line)));
  }

  return sections;
}

std::string serialize_sections(const std::vector<Section> &sections) {
  std::string plain;

  for (const auto &section : sections) {
    if (section.named) {
      plain += '[';
      plain += section.name;
      plain += "]\n";
    }

    for (const auto &option : section.options) {
      plain += option.line;
      plain += '\n';
    }
  }

  return plain;
}

/**
 * Index of the first login path with the given name. mysql_config_editor also
 * acts on the first match only.
 */
size_t find_section(const std::vector<Section> &sections,
                    const std::string &name) {
  for (size_t i = 0; i < sections.size(); ++i) {
    if (sections[i].named && sections[i].name == name) {
      return i;
    }
  }

  return sections.size();
}

/**
 * Quotes a value the way dynstr_append_quoted() does for mysql_config_editor:
 * wrap in double quotes, escaping any double quote inside. Backslashes are
 * deliberately not escaped here - the caller has already done that, exactly as
 * in the MySQL build.
 */
std::string quote_value(const std::string &value) {
  return '"' + shcore::str_replace(value, "\"", "\\\"") + '"';
}

/**
 * Reverses the option-value processing of search_default_file_with_ext():
 * matching outer quotes are dropped, then escape sequences are expanded.
 */
std::string unescape_value(const std::string &raw) {
  auto value = raw;

  if (value.length() > 1 && ('"' == value.front() || '\'' == value.front()) &&
      value.back() == value.front()) {
    value = value.substr(1, value.length() - 2);
  }

  std::string result;
  result.reserve(value.length());

  for (size_t i = 0; i < value.length(); ++i) {
    // a trailing backslash is kept verbatim, as mysys does
    if ('\\' != value[i] || i + 1 == value.length()) {
      result += value[i];
      continue;
    }

    switch (value[++i]) {
      case 'n':
        result += '\n';
        break;
      case 't':
        result += '\t';
        break;
      case 'r':
        result += '\r';
        break;
      case 'b':
        result += '\b';
        break;
      case 's':
        result += ' ';
        break;
      case '"':
        result += '"';
        break;
      case '\'':
        result += '\'';
        break;
      case '\\':
        result += '\\';
        break;
      default:
        result += '\\';
        result += value[i];
        break;
    }
  }

  return result;
}

constexpr auto k_option_password = "password";

}  // namespace

const std::string &Login_file::path() {
  if (m_path.empty()) {
    m_path = login_file_path();
  }

  return m_path;
}

void Login_file::validate() {
  const auto &file = path();
  const auto dir = shcore::path::dirname(file);

  if (!dir.empty() && !shcore::is_folder(dir)) {
    try {
      shcore::create_directory(dir, true, 0700);
    } catch (const std::exception &ex) {
      throw Helper_exception{"Failed to create the directory \"" + dir +
                             "\" which holds the login file: " + ex.what()};
    }
  }

  // the file itself does not have to exist yet, store() creates it - but if it
  // does, it has to be readable
  if (shcore::path_exists(file)) {
    std::ifstream f{file, std::ios::binary};

    if (!f.is_open()) {
      throw Helper_exception{"The login file \"" + file +
                             "\" exists but cannot be read"};
    }
  }

  // AES-128-ECB has to be available in whatever OpenSSL we ended up with
  const Login_key probe_key{};
  const auto cipher = aes_encrypt(probe_key, "probe");

  if (aes_decrypt(probe_key, cipher.data(), cipher.length()) != "probe") {
    throw Helper_exception{
        "OpenSSL does not provide a usable AES-128-ECB implementation"};
  }
}

std::string Login_file::list() {
  const auto contents = read_contents(path());
  auto sections = parse_sections(contents.plain);

  /*
    mysql_config_editor print --all masks the password before printing it
    (mask_password_and_print()). Login_path_helper only cares that the option
    is there, so keep the same shape.
  */
  for (auto &section : sections) {
    for (auto &option : section.options) {
      if (k_option_password == option.name) {
        option.line = std::string{k_option_password} + " = *****";
      }
    }
  }

  /*
    Config_editor_invoker::invoke() strips the output of
    `mysql_config_editor print --all` before handing it to parse_ini(), which
    relies on that: an empty trailing line has no " = " and would be indexed
    out of range. Strip here for the same reason.
  */
  return shcore::str_strip(serialize_sections(sections));
}

void Login_file::store(const Entry &entry, const std::string &password) {
  const auto &file = path();
  const Login_file_lock lock{file};

  auto contents = read_contents(file);
  ensure_key(&contents);

  auto sections = parse_sections(contents.plain);
  const auto existing = find_section(sections, entry.name);

  if (existing != sections.size()) {
    sections.erase(sections.begin() + existing);
  }

  sections.emplace_back();
  auto &section = sections.back();
  section.named = true;
  section.name = entry.name;

  // same options, same order and same quoting as set_command()
  const auto add = [&section](const char *name, const std::string &value) {
    section.options.emplace_back(
        parse_option(std::string{name} + " = " + quote_value(value)));
  };

  if (!entry.user.empty()) {
    add("user", entry.user);
  }

  add(k_option_password, password);

  if (!entry.host.empty()) {
    add("host", entry.host);
  }

  if (!entry.socket.empty()) {
    add("socket", entry.socket);
  }

  if (!entry.port.empty()) {
    // mysql_config_editor writes the port unquoted
    section.options.emplace_back(parse_option("port = " + entry.port));
  }

  contents.plain = serialize_sections(sections);

  write_contents(file, contents);
}

void Login_file::erase(const Entry &entry) {
  const auto &file = path();
  const Login_file_lock lock{file};

  auto contents = read_contents(file);
  ensure_key(&contents);

  auto sections = parse_sections(contents.plain);
  const auto existing = find_section(sections, entry.name);

  if (existing == sections.size()) {
    // mysql_config_editor remove is a no-op for an unknown login path
    return;
  }

  sections.erase(sections.begin() + existing);
  contents.plain = serialize_sections(sections);

  write_contents(file, contents);
}

void Login_file::erase_port(const Entry &entry) {
  const auto &file = path();
  const Login_file_lock lock{file};

  auto contents = read_contents(file);
  ensure_key(&contents);

  auto sections = parse_sections(contents.plain);
  const auto existing = find_section(sections, entry.name);

  if (existing == sections.size()) {
    return;
  }

  auto &options = sections[existing].options;

  for (auto it = options.begin(); it != options.end(); ++it) {
    if ("port" == it->name) {
      options.erase(it);
      break;
    }
  }

  contents.plain = serialize_sections(sections);

  write_contents(file, contents);
}

void Login_file::erase_socket(const Entry &entry) {
  const auto &file = path();
  const Login_file_lock lock{file};

  auto contents = read_contents(file);
  ensure_key(&contents);

  auto sections = parse_sections(contents.plain);
  const auto existing = find_section(sections, entry.name);

  if (existing == sections.size()) {
    return;
  }

  auto &options = sections[existing].options;

  for (auto it = options.begin(); it != options.end(); ++it) {
    if ("socket" == it->name) {
      options.erase(it);
      break;
    }
  }

  contents.plain = serialize_sections(sections);

  write_contents(file, contents);
}

std::string Login_file::version() { return shcore::get_long_version(); }

std::string Login_file::get_secret(const Entry &entry) {
  const auto contents = read_contents(path());
  const auto sections = parse_sections(contents.plain);
  const auto existing = find_section(sections, entry.name);

  if (existing != sections.size()) {
    for (const auto &option : sections[existing].options) {
      if (k_option_password == option.name && option.has_value) {
        return unescape_value(option.value);
      }
    }
  }

  throw Helper_exception{"Failed to read the secret"};
}

}  // namespace login_path
}  // namespace secret_store
}  // namespace mysql
