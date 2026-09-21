/*
 * Copyright (c) 2017, 2026, Oracle and/or its affiliates.
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

#ifndef MYSQLSHDK_LIBS_DB_CONNECTION_OPTIONS_H_
#define MYSQLSHDK_LIBS_DB_CONNECTION_OPTIONS_H_

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "mysqlshdk/include/mysqlshdk_export.h"
#include "mysqlshdk/libs/db/ssl_options.h"
#include "mysqlshdk/libs/db/utils_connection.h"
#include "mysqlshdk/libs/ssh/ssh_connection_options.h"
#include "mysqlshdk/libs/utils/connection.h"
#include "mysqlshdk/libs/utils/nullable_options.h"

namespace mysqlsh {
enum class SessionType {
  Auto,
#ifdef HAVE_X_PROTOCOL
  X,
#endif
  Classic
};
}  // namespace mysqlsh

namespace mysqlshdk {
namespace db {

enum class Transport_type { Tcp, Socket, Pipe };

inline constexpr Transport_type k_default_local_transport_type =
#ifdef _WIN32
    mysqlshdk::db::Transport_type::Pipe
#else
    mysqlshdk::db::Transport_type::Socket
#endif
    ;

std::string to_string(Transport_type type);

inline constexpr int k_default_mysql_port = 3306;
inline constexpr int k_default_mysql_x_port = 33060;

inline constexpr int k_max_auth_factors = 3;
class SHCORE_PUBLIC Connection_options : public IConnection {
 public:
  using Nullable_options = mysqlshdk::utils::Nullable_options;
  using Comparison_mode = mysqlshdk::utils::nullable_options::Comparison_mode;

  Connection_options();
  explicit Connection_options(Comparison_mode mode);
  explicit Connection_options(
      const std::string &uri,
      Comparison_mode mode = Comparison_mode::CASE_INSENSITIVE);

  Connection_options(const Connection_options &other) = default;
  Connection_options(Connection_options &&other) = default;

  Connection_options &operator=(const Connection_options &other) = default;
  Connection_options &operator=(Connection_options &&other) = default;

  ~Connection_options() = default;

  void override_with(const Connection_options &options);

  void set_login_options_from(const Connection_options &options);
  void set_ssl_options(const Ssl_options &options);
  void set_ssh_options(ssh::Ssh_connection_options &&options);

  /**
   * Takes the `+ssh` of `mariadb+ssh://`, and refuses any other extension.
   *
   * Turning tunnelling on is its only effect: it is what allows the `ssh-*`
   * options into the URI, and what makes set_default_data work out the SSH
   * endpoint and the forwarding target.
   */
  void set_scheme_extension(const std::string &extension) override;

  /**
   * Accepts `mariadb+ssh` as well as a bare scheme name.
   *
   * A dictionary is the other way a connection is described, and it has to be
   * able to say everything a URI can - otherwise a tunnelled connection turned
   * into a map and back would come out direct. The extension therefore rides
   * on the scheme there, exactly as it does in a URI; get_scheme() still
   * answers with the bare name, so every comparison against it is unaffected.
   */
  void set_scheme(const std::string &scheme) override;

  /**
   * Works the SSH endpoint out from the `+ssh` extension and the authority.
   *
   * Public because it has to run once everything else is known, and the two
   * callers that build a Connection_options piecemeal - the URI constructor
   * and mysqlsh::get_connection_options - are the ones that know when that
   * is. Idempotent.
   */
  void apply_ssh_scheme_extension();

  /**
   * `ssh` for a connection that was written as `mariadb+ssh://`, else empty.
   *
   * Only for connections that used the extension, NOT for every connection
   * that happens to tunnel. A tunnel configured the older way - the `ssh:
   * "user@jump"` dictionary key, or --ssh on the command line - keeps the
   * spelling it has always had, so that nothing which prints a connection
   * back to the user suddenly grows a query string full of values it
   * defaulted rather than values anybody wrote.
   *
   * That older form still loses its tunnel when written out as a URI. It is a
   * real gap and predates this; fixing it means deciding what a defaulted
   * ssh-user should serialize as, which is a question of its own.
   */
  std::string get_scheme_extension() const override {
    return m_scheme_extension;
  }

  /** Whether this connection is to be made through an SSH tunnel. */
  bool has_scheme_extension_ssh() const {
    return m_scheme_extension == kSchemeExtensionSsh;
  }

  /**
   * Whether the SSH host was NAMED, rather than defaulted to the authority.
   *
   * The two are different connections - named, the tunnel forwards to the
   * authority host as the SSH server resolves it; defaulted, it forwards to
   * loopback there - so anything that serializes a connection has to keep
   * them apart. See query_attributes and mysqlsh::get_connection_map.
   */
  bool ssh_host_was_given() const { return m_ssh_host_given; }
  const std::string &get_schema() const { return get_value(kSchema); }
  const std::string &get_socket() const { return get_value(kSocket); }
  const std::string &get_pipe() const { return get_value(kPipe); }
  int get_port() const override;
  int get_target_port() const;

  const std::string &get_password() const override;
  // password1/factor 0 is an alias for regular password
  const std::string &get_mfa_password(int factor) const;

  void set_transport_type(Transport_type type);
  Transport_type get_transport_type() const;
  const std::string &get_compression() const {
    return m_extra_options.get_value(kCompression);
  }
  const std::string &get_compression_algorithms() const {
    return m_extra_options.get_value(kCompressionAlgorithms);
  }
  int64_t get_compression_level() const;

  const Ssl_options &get_ssl_options() const { return m_ssl_options; }
  Ssl_options &get_ssl_options() { return m_ssl_options; }

  const ssh::Ssh_connection_options &get_ssh_options() const {
    return m_ssh_options;
  }

  ssh::Ssh_connection_options &get_ssh_options() { return m_ssh_options; }

  const std::vector<std::string> &get_warnings() const { return m_warnings; }
  bool has_password() const override { return has_value(kPassword); }
  bool has_mfa_password(int factor) const;
  bool has_needs_password(int factor) const;

  bool needs_password(int factor) const;

  /**
   * This function returns a boolean indicating if the password should be
   * prompted.
   *
   * By default the function will return true if the password is missing for
   * factor 0. If mandatory is true or the factor is not 0, the function will
   * only return true if the password is missing and one of the password options
   * was used:
   *
   * -p
   * -password
   * -password1
   * -password2
   * -password3
   */
  bool should_prompt_password(int factor) const;

  bool has_data() const override;
  bool has_schema() const { return has_value(kSchema); }
  bool has_socket() const { return m_options.has_value(kSocket); }
  bool has_pipe() const { return m_options.has_value(kPipe); }
  bool has_transport_type() const { return m_transport_type.has_value(); }
  bool has_compression() const { return m_extra_options.has(kCompression); }
  bool has_compression_algorithms() const {
    return m_extra_options.has(kCompressionAlgorithms);
  }
  bool has_compression_level() const { return m_compress_level.has_value(); }

  mysqlshdk::db::uri::Type get_type() const override {
    return mysqlshdk::db::uri::Type::DevApi;
  }
  bool has(const std::string &name) const override;
  bool has_value(const std::string &name) const override;
  const std::string &get(const std::string &name) const override;
  int get_numeric(const std::string &name) const override;
  std::vector<std::pair<std::string, std::optional<std::string>>>
  query_attributes() const override;

  bool has_ssh_options() const;

  void set_host(const std::string &host) override;
  void set_port(int port) override;
  void set_schema(const std::string &schema) { _set_fixed(kSchema, schema); }
  void set_socket(const std::string &socket);
  void set_pipe(const std::string &pipe);
  void set_compression(const std::string &compress);
  void set_compression_algorithms(const std::string &compression_algorithms);
  void set_compression_level(int64_t compression_level) {
    m_compress_level = compression_level;
  }
  void set_password(const std::string &password) override;
  void set_mfa_password(int factor, const std::string &password);
  void set_needs_password(int factor, bool flag);

  void set(const std::string &attribute, const std::string &value) override;
  void set(const std::string &attribute, int value) override;
  void set(const std::string &attribute,
           const std::vector<std::string> &values) override;

  void set_unchecked(const std::string &name, const char *value = nullptr);

  void clear_host() override;
  void clear_port() override;
  void clear_password() override;
  void clear_mfa_password(int factor);
  void clear_needs_password(int factor);
  void clear_schema() { clear_value(kSchema); }
  void clear_socket();
  void clear_pipe();
  void clear_compression_level() { m_compress_level.reset(); }
  void clear_transport_type();

  void clear_warnings() { m_warnings.clear(); }

  void remove(const std::string &name);

  bool operator==(const Connection_options &other) const;
  bool operator!=(const Connection_options &other) const;

  std::string uri_endpoint() const {
    return as_uri(uri::formats::only_transport());
  }

  const Nullable_options &get_extra_options() const { return m_extra_options; }
  bool is_connection_attributes_enabled() const {
    return m_enable_connection_attributes;
  }
  const Nullable_options &get_connection_attributes() const {
    return m_connection_attributes;
  }

  void set_connection_attributes(const std::vector<std::string> &attributes);
  void set_connection_attribute(const std::string &attribute,
                                const std::string &value);

  mysqlsh::SessionType get_session_type() const;

  /**
   * Sets the default connection data, if they're missing:
   * - uses system user if no user is specified,
   * - uses localhost if host is not specified and transport type is not
   *   specified or it's TCP.
   */

  void set_default_data() override;

  int compare(const std::string &lhs, const std::string &rhs) const {
    return m_options.compare(lhs, rhs);
  }

  static void throw_invalid_timeout(const char *name, const std::string &value);

  bool is_auth_method(const std::string &method_id) const;

  bool has_oci_config_file() const;
  void set_oci_config_file(const std::string &path);
  std::string get_oci_config_file() const;

  bool has_oci_client_config_profile() const;
  void set_oci_client_config_profile(const std::string &profile);
  std::string get_oci_client_config_profile() const;

#ifdef _WIN32
  bool has_kerberos_auth_mode() const;
  void set_kerberos_auth_mode(const std::string &mode);
  std::string get_kerberos_auth_mode() const;
#endif

  bool has_connect_timeout() const;
  void set_connect_timeout(int value);
  int get_connect_timeout() const;
  void clear_connect_timeout();

  bool has_net_read_timeout() const;
  void set_net_read_timeout(int value);
  int get_net_read_timeout() const;
  void clear_net_read_timeout();

  bool has_net_write_timeout() const;
  void set_net_write_timeout(int value);
  int get_net_write_timeout() const;
  void clear_net_write_timeout();

  bool is_enabled(const std::string &option) const;
  void set_interactive(bool value);
  bool is_interactive() const;

  inline void set_reject_local_infile_requests(bool value) {
    m_reject_local_infile_requests = value;
  }

  inline bool reject_local_infile_requests() const {
    return m_reject_local_infile_requests;
  }

  bool has_webauth_device_index() const;
  void set_webauth_device_index(uint8_t index);
  uint8_t get_webauth_device_index() const;

 private:
  void _set_fixed(const std::string &key, const std::string &val);
  bool is_extra_option(const std::string &option);
  bool is_bool_value(const std::string &value);
  bool uses_local_transport() const;

  void check_compression_conflicts();

  std::optional<Transport_type> m_default_transport_type;
  std::optional<Transport_type> m_transport_type;
  std::optional<int64_t> m_compress_level;
  std::optional<int> m_connect_timeout;
  std::optional<int> m_net_read_timeout;
  std::optional<int> m_net_write_timeout;
  std::optional<uint8_t> m_webauth_device_index;

  Ssl_options m_ssl_options;
  void set_ssh_uri_option(const std::string &name, const std::string &value);

  ssh::Ssh_connection_options m_ssh_options;
  // The scheme's `+extension`, empty for none. Kept beside the scheme rather
  // than inside it so that every `get_scheme() == "mariadb"` comparison in the
  // code goes on meaning what it did.
  std::string m_scheme_extension;
  // Whether `ssh-host` was named, as opposed to defaulted from the authority.
  bool m_ssh_host_given = false;
  Nullable_options m_extra_options;
  bool m_enable_connection_attributes;
  Nullable_options m_connection_attributes;
  // mfa_password1 is the regular password
  std::optional<std::string> m_mfa_password_2;
  std::optional<std::string> m_mfa_password_3;
  bool m_interactive = false;
  bool m_reject_local_infile_requests = false;

  // whether a (mfa) password is needed even if not given
  // regular password is assumed to be required for compatibility options
  std::array<std::optional<bool>, k_max_auth_factors> m_needs_password;

  std::vector<std::string> m_warnings;
};

int64_t default_connect_timeout();
std::string default_mysql_plugins_dir();

}  // namespace db
}  // namespace mysqlshdk

#endif  // MYSQLSHDK_LIBS_DB_CONNECTION_OPTIONS_H_
