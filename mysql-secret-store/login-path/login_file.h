/*
 * Copyright (c) 2026, MariaDB Corporation.
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

#ifndef MYSQL_SECRET_STORE_LOGIN_PATH_LOGIN_FILE_H_
#define MYSQL_SECRET_STORE_LOGIN_PATH_LOGIN_FILE_H_

#include <string>

#include "mysql-secret-store/login-path/entry.h"

namespace mysql {
namespace secret_store {
namespace login_path {

/**
 * Native reader/writer for the MySQL `.mylogin.cnf` login file.
 *
 * MariaDB neither ships `mysql_config_editor` nor implements `.mylogin.cnf` in
 * its mysys/Connector-C, so the `login-path` helper cannot delegate to an
 * external binary (write) nor to `my_load_defaults()` (read) the way the MySQL
 * build does. This class implements both halves in-tree, on top of OpenSSL,
 * which the shell already links.
 *
 * The public interface mirrors Config_editor_invoker exactly (plus
 * get_secret(), which the MySQL build has no need for), so every call site in
 * Login_path_helper stays unchanged.
 *
 * The on-disk format is kept bit-compatible with MySQL's:
 *
 *     offset 0    4 bytes    zeroes, reserved for future use
 *     offset 4   20 bytes    the login key
 *     offset 24  ...         repeating: [4-byte LE length][AES-128-ECB cipher]
 *
 * one record per line of the plain-text option file, the trailing newline
 * included. The key lives in the file next to the data it encrypts, so this is
 * obfuscation, not encryption - the file's own permissions are what protect it.
 */
class Login_file {
 public:
  /**
   * Checks that the login file can be used: its directory exists (or can be
   * created) and OpenSSL provides the cipher we need.
   *
   * Succeeds when the login file itself does not exist yet - it is created on
   * demand by store().
   */
  void validate();

  /**
   * Serializes the whole store to the option-file text, in the same shape as
   * `mysql_config_editor print --all`: password values are masked.
   */
  std::string list();

  /**
   * Writes the entry, replacing any existing login path of the same name.
   */
  void store(const Entry &entry, const std::string &password);

  /**
   * Removes the whole login path.
   */
  void erase(const Entry &entry);

  /**
   * Removes only the `port` option of the login path.
   */
  void erase_port(const Entry &entry);

  /**
   * Removes only the `socket` option of the login path.
   */
  void erase_socket(const Entry &entry);

  /**
   * Version of this implementation.
   */
  std::string version();

  /**
   * Reads back the password stored under the given login path.
   *
   * @throws Helper_exception if the login path has no password.
   */
  std::string get_secret(const Entry &entry);

 private:
  const std::string &path();

  std::string m_path;
};

}  // namespace login_path
}  // namespace secret_store
}  // namespace mysql

#endif  // MYSQL_SECRET_STORE_LOGIN_PATH_LOGIN_FILE_H_
