/*
 * Copyright (c) 2018, 2025, Oracle and/or its affiliates.
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

#ifndef MYSQLSHDK_SHELLCORE_CREDENTIAL_MANAGER_H_
#define MYSQLSHDK_SHELLCORE_CREDENTIAL_MANAGER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "mysql-secret-store/include/mysql-secret-store/secret_type.h"
#include "mysqlshdk/include/shellcore/shell_notifications.h"
#include "mysqlshdk/libs/db/connection_options.h"
#include "mysqlshdk/libs/utils/connection.h"
#include "mysqlshdk/libs/utils/options.h"

namespace mysql {
namespace secret_store {
namespace api {

class Helper_interface;

}  // namespace api
}  // namespace secret_store
}  // namespace mysql

namespace shcore {

class Credential_manager : public NotificationObserver {
 public:
  enum class Save_passwords { ALWAYS, PROMPT, NEVER };

  Credential_manager(const Credential_manager &) = delete;
  Credential_manager(Credential_manager &&) = delete;
  Credential_manager &operator=(const Credential_manager &) = delete;
  Credential_manager &operator=(Credential_manager &&) = delete;

  static Credential_manager &get();

  void register_options(Options *options);

  void initialize();

  void handle_notification(const std::string &name,
                           const Object_bridge_ref &sender,
                           Value::Map_type_ref data) override;

  void set_helper(const std::string &helper);

  bool get_password(mysqlshdk::IConnection *options) const;

  bool save_password(const mysqlshdk::IConnection &options);

  bool remove_password(const mysqlshdk::IConnection &options);

  bool get_credential(const std::string &url, std::string *credential) const;

  bool should_save_password(const std::string &url);

  // shell API

  std::vector<std::string> list_credential_helpers() const;

  inline void store_credential(const std::string &url,
                               const std::string &credential) {
    store_secret(Secret_type::PASSWORD, url, credential);
  }

  inline void delete_credential(const std::string &url) {
    delete_secret(Secret_type::PASSWORD, url);
  }

  inline void delete_all_credentials() {
    delete_all_secrets(Secret_type::PASSWORD);
  }

  inline std::vector<std::string> list_credentials() const {
    return list_secrets(Secret_type::PASSWORD);
  }

  // the group of a secret is a lower-case UUID, or empty for the default group

  void store_secret(const std::string &id, const std::string &secret,
                    const std::string &group = {});

  std::string read_secret(const std::string &id, const std::string &group = {});

  inline void delete_secret(const std::string &id,
                            const std::string &group = {}) {
    delete_secret(Secret_type::GENERIC, id, group);
  }

  inline void delete_all_secrets(const std::string &group = {}) {
    delete_all_secrets(Secret_type::GENERIC, group);
  }

  inline std::vector<std::string> list_secrets(
      const std::string &group = {}) const {
    return list_secrets(Secret_type::GENERIC, group);
  }

  /**
   * Lists the secrets of every group, as (key, group) pairs; the default group
   * is reported by its name.
   */
  std::vector<std::pair<std::string, std::string>> list_secrets_of_all_groups()
      const;

 private:
  using Secret_type = ::mysql::secret_store::api::Secret_type;

  Credential_manager();
  ~Credential_manager() = default;

  void check_helper(const std::string &context) const;

  void add_ignore_filter(const std::string &filter);

  bool is_ignored_url(const std::string &url) const;

  void store_secret(Secret_type type, const std::string &id,
                    const std::string &secret, const std::string &group = {});

  std::string read_secret(Secret_type type, const std::string &id,
                          const std::string &group = {});

  void delete_secret(Secret_type type, const std::string &id,
                     const std::string &group = {});

  void delete_all_secrets(Secret_type type, const std::string &group = {});

  std::vector<std::string> list_secrets(Secret_type type,
                                        const std::string &group = {}) const;

  std::unique_ptr<::mysql::secret_store::api::Helper_interface> m_helper;
  std::string m_helper_string;
  Save_passwords m_save_passwords = Save_passwords::PROMPT;
  std::vector<std::string> m_ignore_filters;
  bool m_is_initialized = false;
};

}  // namespace shcore

#endif  // MYSQLSHDK_SHELLCORE_CREDENTIAL_MANAGER_H_
