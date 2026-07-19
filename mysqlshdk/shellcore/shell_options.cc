/*
 * Copyright (c) 2014, 2025, Oracle and/or its affiliates.
 * Copyright (c) 2026, MariaDB Corporation.
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

#include "mysqlshdk/include/shellcore/shell_options.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <unordered_set>
#include <vector>

#ifdef MARIADB_BUILD
// MariaDB's my_global.h pulls in my_dbug.h before my_bool is defined; include
// mysql.h first (it provides `typedef char my_bool;`).
#include <mysql.h>
#endif
#include <my_alloc.h>

#ifdef MARIADB_BUILD
#include <my_global.h>
#endif

#include <my_default.h>

#include "modules/mod_utils.h"
#include "mysqlshdk/libs/db/uri_common.h"
#include "mysqlshdk/libs/db/uri_parser.h"
#include "mysqlshdk/libs/utils/log_sql.h"
#include "mysqlshdk/shellcore/credential_manager.h"
#include "shellcore/ishell_core.h"
#include "shellcore/shell_notifications.h"
#include "shellcore/shell_resultset_dumper.h"
#include "utils/utils_file.h"
#include "utils/utils_general.h"
#include "utils/utils_path.h"
#include "utils/utils_string.h"

namespace {
// We do not want to expose password. Override argv's uri with uri string
// without password.
void obfuscate_uri_password(const std::string &nopassword_uri, char *argv) {
  char *value = argv;
  auto value_length = strlen(value);
  assert(nopassword_uri.size() <= value_length);
  // If password is longer than @host:port part then strncpy will leak
  // password ending. We need to clear whole buffer first.
  shcore::clear_buffer(value, value_length);
  strncpy(value, nopassword_uri.c_str(), value_length + 1);
  value[value_length] = '\0';
}
}  // namespace

namespace mysqlsh {

using mysqlshdk::db::Transport_type;
using mysqlshdk::db::uri::Type;

bool Shell_options::Storage::has_connection_data(
    bool require_main_options) const {
  if (require_main_options) {
    return connection_data.has_user() || connection_data.has_host() ||
           connection_data.has_port() || connection_data.has_socket() ||
           connection_data.has_pipe() || connection_data.has_password() ||
           connection_data.has_needs_password(0) ||
           connection_data.has_needs_password(1) ||
           connection_data.has_needs_password(2);
  }
  return connection_data.has_data() || connection_data.has_needs_password(0) ||
         connection_data.has_needs_password(1) ||
         connection_data.has_needs_password(2);
}

mysqlshdk::db::Connection_options Shell_options::Storage::connection_options()
    const {
  auto target_server = connection_data;

  if (!ssh.uri.empty()) {
    auto ssh_dict = shcore::make_dict();
    (*ssh_dict)[mysqlshdk::db::kSsh] = shcore::Value(ssh.uri);

    if (!ssh.identity_file.empty()) {
      (*ssh_dict)[mysqlshdk::db::kSshIdentityFile] =
          shcore::Value(ssh.identity_file);
    }

    if (!ssh.config_file.empty()) {
      (*ssh_dict)[mysqlshdk::db::kSshConfigFile] =
          shcore::Value(ssh.config_file);
    }

    if (!ssh.pwd.empty()) {
      (*ssh_dict)[mysqlshdk::db::kSshPassword] = shcore::Value(ssh.pwd);
    }

    target_server.set_ssh_options(mysqlsh::get_ssh_options(ssh_dict));
  }

  if (has_multi_passwords()) {
    target_server.set_scheme("mysql");
  }

  if (webauthn_client_preserve_privacy.has_value()) {
    target_server.set_unchecked(
        mysqlshdk::db::kWebauthnClientPreservePrivacy,
        (*webauthn_client_preserve_privacy) ? "1" : "0");
  }

  return target_server;
}

namespace {
mysqlsh::SessionType session_type_for_option(const std::string &option) {
  if (shcore::str_beginswith(option, "mysql://")) {
    return mysqlsh::SessionType::Classic;
#ifdef HAVE_X_PROTOCOL
  } else if (shcore::str_beginswith(option, "mysqlx://")) {
    return mysqlsh::SessionType::X;
#endif
  } else if (option == "--mc" || option == "--mysql" || option == "--sqlc") {
    return mysqlsh::SessionType::Classic;
#ifdef HAVE_X_PROTOCOL
  } else if (option == "--mx" || option == "--mysqlx" || option == "--sqlx") {
    return mysqlsh::SessionType::X;
#endif
  } else {
    return mysqlsh::SessionType::Auto;
  }
}
}  // namespace

mysqlsh::SessionType Shell_options::Storage::check_option_session_type_conflict(
    const std::string &option) {
  auto session_type = session_type_for_option(option);

  if (session_type == mysqlsh::SessionType::Auto) return session_type;

  auto format = [](const std::string &opt, bool capitalize) {
    if (opt[0] == '-') return (capitalize ? "Option " : "option ") + opt;
    // URIs
    std::string scheme, rest;
    std::tie(scheme, rest) = shcore::str_partition_after(opt, "://");

    return "URI scheme " + scheme;
  };

  for (const auto &opt : m_session_type_options) {
    auto opt_type = session_type_for_option(opt);

    // any combination is allowed so that options can be placed in my.cnf and
    // overriden from the cmdline, except for --sqlc / --sqlx

    if (session_type != opt_type && (shcore::str_beginswith(opt, "--sql") ||
                                     shcore::str_beginswith(option, "--sql"))) {
      throw shcore::Exception::argument_error(format(option, true) +
                                              " cannot be combined with " +
                                              format(opt, false));
    }
  }

  switch (session_type) {
    case mysqlsh::SessionType::Classic:
      m_session_type_options.push_back(option);
      break;
#ifdef HAVE_X_PROTOCOL
    case mysqlsh::SessionType::X:
      m_session_type_options.push_back(option);
      break;
#endif
    case mysqlsh::SessionType::Auto:
      break;
  }
  return session_type;
}

void Shell_options::Storage::set_uri(const std::string &uri) {
  if (uri.empty()) {
    connection_data = Connection_options();
  } else {
    check_option_session_type_conflict(uri);

    // URLs always override port/socket/schema
    connection_data.clear_schema();
    connection_data.clear_port();
    connection_data.clear_socket();

    connection_data.override_with(Connection_options(uri));
  }
}

using mysqlshdk::db::Ssl_options;
using shcore::opts::assign_value;
using shcore::opts::cmdline;
using shcore::opts::deprecated;
using shcore::opts::Source;
using std::placeholders::_1;
using std::placeholders::_2;

void Shell_options::handle_mycnf_options(int *argc, char ***argv,
                                         MEM_ROOT *argv_alloc) {
  const char *load_default_groups[] = {"mysqlsh", "client", nullptr};

  // Process the default (config-file) options my_load_defaults() has placed at
  // argv[1 .. defaults_end-1], then collapse argv down to
  // [prog, <original command-line args starting at first_arg> ...]:
  //   defaults_end - one past the last config-file option. The slot at
  //                  defaults_end is expendable (a separator on MySQL, the
  //                  first real arg on MariaDB, or the argv terminator) and is
  //                  temporarily nulled so it terminates the defaults sub-list.
  //   first_arg    - index of the first original command-line argument.
  // my.cnf-style option names have '_' normalized to '-' (e.g. --ssl_ca).
  const auto process_defaults_and_collapse = [&](int defaults_end,
                                                 int first_arg) {
    for (int j = 1; j < defaults_end; ++j) {
      for (int i = 0; (*argv)[j][i] != '\0' && (*argv)[j][i] != '='; ++i) {
        if ((*argv)[j][i] == '_') (*argv)[j][i] = '-';
      }
    }

    char *const saved = (*argv)[defaults_end];
    (*argv)[defaults_end] = nullptr;
    try {
      handle_cmdline_options(
          defaults_end, *argv, false,
          std::bind(&Shell_options::custom_cmdline_handler, this, _1));
    } catch (const std::exception &e) {
      throw std::runtime_error(
          std::string("While processing defaults options:\n") + e.what());
    }
    (*argv)[defaults_end] = saved;

    // Move the program name in front of the first real argument and drop the
    // consumed default-option slots, yielding [prog, cmdline args...].
    (*argv)[first_arg - 1] = (*argv)[0];
    *argv = *argv + (first_arg - 1);
    *argc = *argc - (first_arg - 1);
  };

#ifdef MARIADB_BUILD
  // MariaDB's my_load_defaults() takes no MEM_ROOT and has no MySQL-style
  // "----args-separator----" mechanism (no my_getopt_use_args_separator). It
  // prepends the config-file options between argv[0] and the surviving
  // command-line args (see mysys/my_default.c), reusing the original arg
  // pointers verbatim for the args it keeps.
  (void)argv_alloc;

  // Handle --print-defaults ourselves. MariaDB's my_load_defaults() would print
  // the resolved options on its own, but in clear text (exposing passwords) and
  // followed by a bogus "Could not read my.cnf files" error, because it returns
  // 4 for --print-defaults which the generic check below treats as a failure.
  // MySQL's mysys instead masks any --password* value and exit(0)s (see
  // mysys/my_default.cc). To match MySQL we strip --print-defaults from the
  // argv handed to my_load_defaults() (so it does not print the unmasked list),
  // build the option list normally, and print it here with the password masked.
  bool want_print_defaults = false;
  std::vector<char *> pd_args;
  pd_args.reserve(*argc + 1);
  for (int i = 0; i < *argc; ++i) {
    if (i > 0 && !want_print_defaults &&
        std::strcmp((*argv)[i], "--print-defaults") == 0) {
      want_print_defaults = true;  // drop it from the list below
      continue;
    }
    pd_args.push_back((*argv)[i]);
  }

  if (want_print_defaults) {
    pd_args.push_back(nullptr);
    char **pd_argv = pd_args.data();
    int pd_argc = static_cast<int>(pd_args.size()) - 1;
    if (my_load_defaults("my", load_default_groups, &pd_argc, &pd_argv,
                         nullptr)) {
      throw std::runtime_error("Could not read my.cnf files");
    }
    std::printf("%s would have been started with the following arguments:\n",
                pd_argv[0]);
    for (int i = 1; i < pd_argc; ++i) {
      // Mask like MySQL's mysys: any option whose name starts with --password.
      if (std::strncmp(pd_argv[i], "--password", 10) == 0)
        std::printf("%s ", "--password=*****");
      else
        std::printf("%s ", pd_argv[i]);
    }
    std::puts("");
    free_defaults(pd_argv);
    std::exit(0);  // mirrors MySQL's mysys --print-defaults behavior
  }

  // Snapshot the original command-line arg pointers. my_load_defaults() also
  // *consumes* leading defaults-control options (--no-defaults,
  // --defaults-file,
  // --defaults-extra-file, --defaults-group-suffix) off the command line, so
  // the growth of argc is (#config opts inserted - #cmdline opts consumed) and
  // can NOT be used to locate the config/cmdline boundary. Instead we find it
  // by identity: my_load_defaults() reuses the original pointers for the args
  // it keeps (see the memcpy in mysys/my_default.c), while injected config-file
  // options are freshly allocated, so the first surviving original pointer
  // marks the start of the command-line args.
  std::unordered_set<char *> original_cmdline_args;
  original_cmdline_args.reserve(*argc);
  for (int i = 1; i < *argc; ++i) original_cmdline_args.insert((*argv)[i]);

  const int load_rc =
      my_load_defaults("my", load_default_groups, argc, argv, nullptr);

  // my_load_defaults() replaces *argv with a freshly allocated buffer only when
  // it succeeds (returns 0) or when --print-defaults was given (returns 4). On
  // its error returns (1 = an explicit --defaults-file was missing, 2 = fatal)
  // *argv is left pointing at the caller's original argv and must NOT be handed
  // to free_defaults(). Record the base for the two allocating cases FIRST --
  // before the throw below -- so the constructor's free_defaults() always
  // reclaims it. Otherwise the --print-defaults path (rc == 4), which also
  // takes the throw, would leak the buffer: mysys' safemalloc then reports the
  // bytes as lost and aborts at exit (SIGSEGV). MySQL builds don't hit this
  // because the buffer is owned by the caller-supplied MEM_ROOT instead.
  if (load_rc == 0 || load_rc == 4) m_mariadb_defaults_argv = *argv;

  if (load_rc != 0) {
    throw std::runtime_error("Could not read my.cnf files");
  }

  // Locate the first surviving command-line argument by pointer identity. If
  // none survived (everything was a config/defaults option) the boundary is the
  // argv terminator at index *argc.
  int first_arg = *argc;
  for (int i = 1; i < *argc; ++i) {
    if (original_cmdline_args.count((*argv)[i])) {
      first_arg = i;
      break;
    }
  }

  // MariaDB has no separator slot, so the config options end exactly where the
  // first command-line arg begins.
  process_defaults_and_collapse(first_arg, first_arg);
#else
  constexpr const char *defaults_args_separator = "----args-separator----";

  my_getopt_use_args_separator = 1;
  if (my_load_defaults("my", load_default_groups, argc, argv, argv_alloc,
                       nullptr)) {
    throw std::runtime_error("Could not read my.cnf files");
  }

  // find the separator between default options and explicit options
  int sep = 1;
  while ((*argv)[sep] && strcmp((*argv)[sep], defaults_args_separator) != 0)
    ++sep;

  // The separator occupies argv[sep]; the first real command-line arg follows.
  process_defaults_and_collapse(sep, sep + 1);
#endif
}

namespace {
void default_print_err(const std::string &s) { std::cerr << s << std::endl; }

void default_print_out(const std::string &s) { std::cout << s << std::endl; }
}  // namespace

Shell_options::Shell_options(
    int argc, char **argv, const std::string &configuration_file,
    Option_flags_set flags,
    const std::function<void(const std::string &)> &on_error,
    const std::function<void(const std::string &)> &on_warning)
    : Options(configuration_file),
      m_on_error(on_error),
      m_on_warning(on_warning) {
  if (!m_on_error) {
    m_on_error = default_print_err;
  }
  if (!m_on_warning) {
    m_on_warning = default_print_out;
  }

  std::string home = shcore::get_home_dir();
#ifdef WIN32
  home += ("MySQL\\mysql-sandboxes");
#else
  home += ("mysql-sandboxes");
#endif

  const auto create_result_format_handler = [this](const char *value) {
    return [this, value](const std::string &opt_name, const char *) {
      get_option(SHCORE_RESULT_FORMAT)
          .handle_command_line_input(opt_name, value);
    };
  };
  // clang-format off
  add_startup_options(!flags.is_set(Option_flags::CONNECTION_ONLY))
    (&print_cmd_line_helper, false,
        cmdline("-?", "--help"), "Display this help and exit.")
    (cmdline("--"),
        "Triggers API Command Line integration, which allows execution of "
        "methods of the Shell global objects from command line using syntax:\n"
        "  -- <object> <method> [arguments]\n"
        "For more details execute '\\? cmdline' inside of the Shell.")
    (&storage.execute_statement, "", cmdline("-e", "--execute=<cmd>"),
        "Execute command and quit.")
    (&storage.execution_context, "", cmdline("--execution-context=<id>"),
        "Defines a custom Shell Execution context for option reporting.")
#ifdef HAVE_PYTHON
    (cmdline("-c", "--pyc=<cmd>"), "Execute Python command and quit. "
        "Any options specified after this are used as arguments of the "
        "processed command.")
#endif
    (cmdline("-f", "--file=<file>"), "Specify a file to process in batch mode. "
        "Any options specified after this are used as arguments of the "
        "processed file.");

  add_startup_options(true)
    (cmdline("--ssh=<value>"), "Make SSH tunnel connection using"
        "Uniform Resource Identifier. "
        "Format: [user@]host[:port]")
    (&storage.ssh.identity_file, "", cmdline("--ssh-identity-file=<file>"),
        "File from which the private key for public key authentication is "
        "read.",
        [](const std::string &value, Source) {
        if (shcore::str_caseeq(value, "null"))
          return std::string();

        auto path = shcore::path::expand_user(value);
        if (!shcore::is_file(path)) {
          throw std::invalid_argument("ssh-identity-file requires existing "
              "file.");
        }
        shcore::check_file_readable_or_throw(path);
        shcore::check_file_access_rights_to_open(path);
        return value;
      });

  add_named_options(!flags.is_set(Option_flags::CONNECTION_ONLY))
    (&storage.ssh.config_file, "", "ssh.configFile",
      cmdline("--ssh-config-file=<file>"), "Specify a custom path for SSH "
      "configuration.",
      [](const std::string &value, Source) {
         auto path = shcore::path::expand_user(value);
         if (shcore::is_file(path)) {
           shcore::check_file_readable_or_throw(path);
           shcore::check_file_access_rights_to_open(path);
         }
         return value;
      })
    (&storage.ssh.buffer_size, 10240, "ssh.bufferSize",
    "Set buffer size in bytes for data transfer, default is 10240 (10Kb)",
      shcore::opts::Range<int>(0, std::numeric_limits<int>::max()));

#ifdef _WIN32
  add_startup_options()
    (cmdline("--plugin-authentication-kerberos-client-mode=<mode>"),
      "Allows defining the kerberos client mode (SSPI, GSSAPI) when using "
      "kerberos authentication.",
      [this](const std::string&, const char* value){
        storage.connection_data.set_kerberos_auth_mode(value);
      });
#endif

#ifndef MARIADB_BUILD
  add_startup_options()
    (cmdline("--oci-config-file=<file>"),
      "Allows defining the OCI configuration file for OCI authentication.",
      [this](const std::string&, const char* value) {
        auto path = shcore::path::expand_user(value);
        if (!shcore::is_file(path))
          throw shcore::Exception::argument_error("Invalid path or file for OCI authentication configuration file.");
        storage.connection_data.set_oci_config_file(value);
      })
    (cmdline("--authentication-oci-client-config-profile=<name>"),
      "Allows defining the OCI profile used from the configuration for client side OCI authentication.",
      [this](const std::string&, const char* value) {
        storage.connection_data.set_oci_client_config_profile(value);
      })
    (cmdline("--plugin-authentication-webauthn-device=<index>"),
      "Allows selecting the authentication device used for client side webauthn authentication.",
      [this](const std::string&, const char* value) {
        storage.connection_data.set(mysqlshdk::db::kWebauthnClientDeviceIndex, value);
      })
    (cmdline("--authentication-openid-connect-client-id-token-file=<path>"),
      "Allows defining the file path to an OpenId Connect authorization token file when using OpenId Connect authentication",
      [this](const std::string&, const char* value){
        if (storage.connection_data.has_value(mysqlshdk::db::kAuthMethod)) {
          storage.connection_data.remove(mysqlshdk::db::kAuthMethod);
        }
        storage.connection_data.set(mysqlshdk::db::kAuthMethod, mysqlshdk::db::kAuthMethodOpenIdConnect);
        storage.connection_data.set(mysqlshdk::db::kOpenIdConnectAuthenticationClientTokenFile, value);
      });
#endif

  add_startup_options(true)
    (cmdline("--uri=<value>"), "Connect to Uniform Resource Identifier. "
        "Format: [user[:pass]@]host[:port][/db]")
    (cmdline("-h", "--host=<name>"),
        "Connect to host.",
        [this](const std::string&, const char* value) {
          storage.connection_data.set_host(value);
        })
    (cmdline("-P", "--port=<#>"),
        "Port number to use for connection.",
        [this](const std::string& option, const char* value) {
          try {
                storage.connection_data.set_port(shcore::opts::convert<int>(
            value, shcore::opts::Source::Command_line));
          } catch (const std::invalid_argument&) {
            throw std::invalid_argument(shcore::str_format("Invalid value for %s: %s", option.c_str(), value));
          }
        })
    (cmdline("--connect-timeout=<ms>"), "Connection timeout in milliseconds.",
        [this](const std::string&, const char* value) {
          storage.connection_data.set(mysqlshdk::db::kConnectTimeout, value);
        })
#ifndef _WIN32
    (cmdline("-S", "--socket[=<sock>]"), "Socket name to use. "
        "If no value is provided will use default UNIX socket path.",
#else
    (cmdline("-S", "--socket=<sock>"),
        "Pipe name to use (only classic sessions).",
#endif
        [this](const std::string&, const char* value) {
          if (value) {
#ifdef _WIN32
            storage.connection_data.set_pipe(value);
#else
            storage.connection_data.set_socket(value);
#endif
          } else {
            // NOTE: this is not used on Windows, because this option requires a value there
            storage.connection_data.set_transport_type(mysqlshdk::db::k_default_local_transport_type);
          }
        }
      )
    (cmdline("-u", "--user=<name>"),
        "User for the connection to the server.")
    (cmdline("--password=[<pass>]"), "Password to use when connecting to server. "
      "If password is empty, connection will be made without using a password.")
    (cmdline("-p", "--password"), "Request password prompt to set the password")
#ifndef MARIADB_BUILD
    (cmdline("--password1[=<pass>]"),
      "Password for first factor authentication plugin.")
    (cmdline("--password2[=<pass>]"),
      "Password for second factor authentication plugin.")
    (cmdline("--password3[=<pass>]"),
      "Password for third factor authentication plugin.")
#endif
    (cmdline("-C", "--compress[=<value>]"),
        "Use compression in client/server protocol. Valid values: 'REQUIRED', "
        "'PREFFERED', 'DISABLED', 'True', 'False', '1', and '0'. Boolean values"
        " map to REQUIRED and DISABLED respectively. By default compression is "
        "set to DISABLED in classic protocol and PREFERRED in X protocol "
        "connections.",
        [this](const std::string&, const char* value) {
          storage.connection_data.set_compression(
              value == nullptr ? "REQUIRED" : value);
        })
#ifndef MARIADB_BUILD
    (cmdline("--compression-algorithms=<list>"),
        "Use compression algorithm in server/client protocol. Expects comma "
        "separated list of algorithms. Supported algorithms include "
        "'zstd', 'zlib', 'lz4' (X protocol only), and 'uncompressed', "
        "which if appears in a list, causes connection to "
        "succeed even if compression negotiation fails.",
        [this](const std::string&, const char* value) {
          storage.connection_data.set_compression_algorithms(value);
        })
    (cmdline("--compression-level=<int>", "--zstd-compression-level=<int>"),
        "Use this compression level in the client/server protocol. "
        "Supported by X protocol and zstd compression in classic protocol.",
        [this](const std::string& opt, const char* value) {
          try {
            storage.connection_data.set_compression_level(
                shcore::lexical_cast<int64_t>(value));
            if (opt == "--zstd-compression-level"
                && !storage.connection_data.has_compression_algorithms())
              storage.connection_data.set_compression_algorithms("zstd");
          } catch(...) {
            throw std::invalid_argument(
                "The value of 'compression-level' must be an integer.");
          }
        })
#endif
        ;

  add_startup_bool_options(true)
    ("--local-infile",
      "Enable LOAD DATA LOCAL INFILE. (classic protocol only). Boolean option",
      [this](const std::string&, const bool value) {
        if (storage.connection_data.has(mysqlshdk::db::kLocalInfile)) {
          storage.connection_data.remove(mysqlshdk::db::kLocalInfile);
        }

        storage.connection_data.set(mysqlshdk::db::kLocalInfile, value ? "true" : "false");
      });

  add_startup_options(!flags.is_set(Option_flags::CONNECTION_ONLY))
    (cmdline("-D", "--schema=<name>", "--database=<name>"), "Schema to use.",
      [this](const std::string&, const char* value) {
          storage.connection_data.set_schema(value);
        })
#ifndef MARIADB_BUILD
    (&storage.register_factor, "",
        cmdline("--register-factor=<name>"),
        "Specifies authentication factor, for which registration needs to be "
        "done.")
    (cmdline("--plugin-authentication-webauthn-client-preserve-privacy[=<value>]"),
        "Allows selection of discoverable credential to be used for signing "
        "challenge. If not enabled, challenge is signed by all credentials for "
        "given relying party.", [this](const std::string &, const char *value){
          if (value == nullptr) {
            storage.webauthn_client_preserve_privacy=true;
          } else {
            storage.webauthn_client_preserve_privacy = shcore::opts::convert<bool>(value,
              shcore::opts::Source::Command_line);
          }
        })
#endif
        ;

  add_startup_options(true)
#ifdef HAVE_X_PROTOCOL
    (cmdline("--mx", "--mysqlx"),
        "Uses connection data to create an X protocol session.",
        std::bind(
            &Shell_options::override_session_type, this, _1, _2))
#endif
    (cmdline("--mc", "--mysql"),
        "Uses connection data to create a classic session.",
        std::bind(
            &Shell_options::override_session_type, this, _1, _2));

  add_startup_options(!flags.is_set(Option_flags::CONNECTION_ONLY))
#ifdef HAVE_ADMIN_API
    (cmdline("--redirect-primary"), "Ensure that the target server is part of "
        "an InnoDB cluster or ReplicaSet and if it is not a primary, find the "
        "primary and connect to it.",
        assign_value(&storage.redirect_session,
          Shell_options::Storage::Redirect_to::Primary))
    (cmdline("--redirect-secondary"), "Ensure that the target server is part "
        "of an InnoDB Cluster or ReplicaSet and if it is not a secondary, find "
        "a secondary and connect to it.",
        assign_value(&storage.redirect_session,
          Shell_options::Storage::Redirect_to::Secondary))
    (cmdline("--cluster"), "Ensure that the target server is part of an InnoDB "
        "Cluster and if so, set the 'cluster' global variable. Also sets "
        "'clusterset' if the cluster is part of a ClusterSet.",
        [this](const std::string&, const char* value) {
          storage.default_cluster = value ? value : "";
          storage.default_cluster_set = true;
        })
    (cmdline("--replicaset"), "Ensure that the target server is part of an "


        "InnoDB ReplicaSet and if so, set the 'rs' global variable.",
        assign_value(&storage.default_replicaset_set, true))
#endif
    (cmdline("--sql"), "Start in SQL mode, auto-detecting the protocol to use "
        "if it is not specified as part of the connection information.",
        assign_value(
            &storage.initial_mode, shcore::IShell_core::Mode::SQL))
    (cmdline("--sqlc"), "Start in SQL mode using a classic session.",
        std::bind(
            &Shell_options::override_session_type, this, _1, _2))
#ifdef HAVE_X_PROTOCOL
    (cmdline("--sqlx"),
        "Start in SQL mode using an X protocol session.",
        std::bind(
            &Shell_options::override_session_type, this, _1, _2))
#endif
#ifdef HAVE_JS
    (cmdline("--js", "--javascript"), "Start in JavaScript mode.",
        [this](const std::string&, const char*) {
          storage.initial_mode = shcore::IShell_core::Mode::JavaScript;
        }
    )
#endif
#ifdef HAVE_PYTHON
    (cmdline("--py", "--python"), "Start in Python mode.",
        [this](const std::string&, const char*) {
          storage.initial_mode = shcore::IShell_core::Mode::Python;
        }
    )
    (cmdline("--pym <module>"),
       "Run Python library module as a script. Remaining args are forwarded to it.")
#endif
    (&storage.wrap_json, "off", cmdline("--json[=<format>]"),
        "Produce output in JSON format. Allowed values: raw, pretty, and off. "
        "If no format is specified pretty format is produced.",
        [](const std::string &val, Source) {
          if (val == "off") return "off";
          if (val.empty() || val == "pretty") return "json";
          if (val == "raw") return "json/raw";
          throw std::invalid_argument(
              "Value for --json must be either pretty, raw or off.");
        })
    (cmdline("--table"),
        "Produce output in table format (default for interactive mode). This "
        "option can be used to force that format when running in batch mode.",
        create_result_format_handler("table"))
    (cmdline("--tabbed"),
        "Produce output in tab separated format (default for batch mode). This "
        "option can be used to force that format when running in interactive "
        "mode.",
        create_result_format_handler("tabbed"))
    (cmdline("-E", "--vertical"),
        "Print the output of a query (rows) vertically.",
        create_result_format_handler("vertical"));

  add_named_options(!flags.is_set(Option_flags::CONNECTION_ONLY))
    (&storage.result_format, "table", SHCORE_RESULT_FORMAT,
        cmdline("--result-format=<value>"),
        "Determines format of results. Allowed values:"
        " [" RESULTSET_DUMPER_FORMATS "].",
        [](const std::string &val, Source) {
          if (!Resultset_dumper_base::is_valid_format(val))
            throw std::invalid_argument(
                "The acceptable values for the option " SHCORE_RESULT_FORMAT
                " are: " RESULTSET_DUMPER_FORMATS);
          return val;
        })
    (&storage.interactive, false, SHCORE_INTERACTIVE,
        "Enables interactive mode", shcore::opts::Read_only<bool>())
    (&storage.db_name_cache, true, SHCORE_DB_NAME_CACHE,
        "Enable database name caching for autocompletion.")
    (&storage.devapi_schema_object_handles, true,
        SHCORE_DEVAPI_DB_OBJECT_HANDLES,
        "Enable table and collection name handles for the DevAPI db object.")
    (&storage.log_sql_ignore, "*SELECT*:SHOW*",
        SHCORE_LOG_SQL_IGNORE,
        "Colon separated list of SQL statement patterns to filter out, unless logSql is set to 'all' or 'unfiltered'."
        "Default: *SELECT*:SHOW*")
    (&storage.log_sql_ignore_unsafe, "*IDENTIFIED*:*PASSWORD*",
        SHCORE_LOG_SQL_IGNORE_UNSAFE,
        "Colon separated list of SQL statement patterns to filter out, unless logSql is set to 'unfiltered'."
        "Default: *IDENTIFIED*:*PASSWORD*");

  add_startup_options(true)
    (cmdline("--get-server-public-key"), "Request public key from the server "
        "required for RSA key pair-based password exchange. Use when connecting"
        " to MySQL 8.0 servers with classic sessions with SSL mode DISABLED.",
        [this](const std::string&, const char*) {
          storage.connection_data.set(mysqlshdk::db::kGetServerPublicKey, "true");
        })
    (cmdline("--server-public-key-path=<p>"), "The path name to a file "
        "containing a client-side copy of the public key required by the "
        "server for RSA key pair-based password exchange. Use when connecting "
        "to MySQL 8.0 servers with classic sessions with SSL mode DISABLED.",
        [this](const std::string&, const char* value) {
          storage.connection_data.set(mysqlshdk::db::kServerPublicKeyPath, value);
        });

  add_startup_options(!flags.is_set(Option_flags::CONNECTION_ONLY))
    (cmdline("-i", "--interactive[=full]"),
      "To use in batch mode. "
      "It forces emulation of interactive mode processing. Each "
      "line on the batch is processed as if it were in interactive mode.",
      [this](const std::string&, const char* value) {
        if (!value || !value[0]) {
          storage.full_interactive = false;
        } else if (strcmp(value, "full") == 0) {
          storage.full_interactive = true;
        } else {
          throw std::invalid_argument(
                    "Value for --interactive if any, must be full");
        }
        storage.interactive = true;

        // Use the existing --interactive option to indicate the session should be interactive
        storage.connection_data.set_interactive(true);
      });


  // make sure hack for accessing log_level via Value works
  static_assert(
      std::is_integral<
          std::underlying_type<shcore::Logger::LOG_LEVEL>::type>::value,
      "Invalid underlying type of shcore::Logger::LOG_LEVEL enum");
  add_named_options(!flags.is_set(Option_flags::CONNECTION_ONLY))
    (&storage.force, false, SHCORE_BATCH_CONTINUE_ON_ERROR, cmdline("--force"),
        "In SQL batch mode, forces processing to continue if an error "
        "is found.", shcore::opts::Read_only<bool>())
    (&storage.log_file,
        shcore::path::join_path(shcore::get_user_config_path(), "mysqlsh.log"),
        SHCORE_LOG_FILE_NAME, cmdline("--log-file=<path>"),
        "Override location of the Shell log file.",
        shcore::opts::Read_only<std::string>()) // read-only in shell.options
    (reinterpret_cast<int*>(&storage.log_level),
        shcore::Logger::LOG_INFO, "logLevel", cmdline("--log-level=<value>"),
        std::string("Set logging level. ") +
        shcore::Logger::get_level_range_info(),
        [this](const std::string &val, Source) {
          const char* value = val.c_str();

          if (*value == '@') {
            storage.log_to_stderr = true;
            value++;
          } else {
            storage.log_to_stderr = false;
          }

          const auto level = shcore::Logger::parse_log_level(value);

          if (const auto logger = shcore::current_logger(true)) {
            if (storage.log_to_stderr) {
              logger->log_to_stderr();
            } else {
              logger->stop_log_to_stderr();
            }

            logger->set_log_level(level);
          }

          return level;
        })
    (&storage.log_sql, "error", SHCORE_LOG_SQL,
        cmdline("--log-sql=off|error|on|all|unfiltered"),
        "Log SQL statements: "
        "off - none of SQL statements will be logged; "
        "(default) error - SQL statement with error message will be logged "
        "only when error occurs; "
        "on - All SQL statements will be logged except these which match "
        "any of logSql.ignorePattern and logSql.ignorePatternUnsafe glob pattern; "
        "all - All SQL statements will be logged except these which match "
        "any of logSql.ignorePatternUnsafe glob pattern; "
        "unfiltered - All SQL statements will be logged.",
        [](const std::string &val, Source) {
          shcore::Log_sql::parse_log_level(val);
          // Accept |val| if |parse_log_level()| not throw
          return val;
        }
    );

  add_named_bool_options(!flags.is_set(Option_flags::CONNECTION_ONLY))
    (&storage.history_sql_syslog, false, SHCORE_HISTORY_SQL_SYSLOG,
        "--syslog",
        "Log filtered interactive commands to the system log. Filtering of "
        "commands depends on the patterns supplied via histignore option. "
        "Boolean option.");

  add_named_options(!flags.is_set(Option_flags::CONNECTION_ONLY))
    (&storage.verbose_level, 0, SHCORE_VERBOSE,
        cmdline("--verbose[={0|1|2|3|4}]"),
        "Enable diagnostic message output to the console: 0 - display no "
        "messages; 1 - display error, warning and informational messages; 2, 3,"
        " 4 - include higher levels of debug messages. If level is not given, 1"
        " is assumed.", shcore::opts::Range<int>(0, 4))
    (&storage.passwords_from_stdin, false, "passwordsFromStdin",
        cmdline("--passwords-from-stdin"),
        "Read passwords from stdin instead of the console.");

  add_named_bool_options(!flags.is_set(Option_flags::CONNECTION_ONLY))
    (&storage.show_warnings, true, SHCORE_SHOW_WARNINGS,
      "--show-warnings",
      "Automatically display SQL warnings on SQL mode if available. "
      "Boolean option, default value \"true\".");

  add_named_options(!flags.is_set(Option_flags::CONNECTION_ONLY))
    (&storage.show_column_type_info, false, "showColumnTypeInfo",
        cmdline("--column-type-info"),
        "Display column type information in SQL mode. Please be aware that "
        "output may depend on the protocol you are using to connect to the "
        "server, e.g. DbType field is approximated when using X protocol.")
    (&storage.history_max_size, 1000, SHCORE_HISTORY_MAX_SIZE,
        "Max. number of entries to keep in the command line history.",
        shcore::opts::Range<int>(0, std::numeric_limits<int>::max()))
    (&storage.histignore, "*IDENTIFIED*:*PASSWORD*", SHCORE_HISTIGNORE,
        cmdline("--histignore=<filters>"), "Shell's history ignore list.")
    (&storage.history_autosave, true, SHCORE_HISTORY_AUTOSAVE,
        "Automatically persist command line history when exiting the Shell.")
    (&storage.sandbox_directory, home, SHCORE_SANDBOX_DIR,
        "Default sandbox directory")
    (&storage.dba_gtid_wait_timeout, 60, SHCORE_DBA_GTID_WAIT_TIMEOUT,
        "Timeout value in seconds to wait for GTIDs to be synchronized.",
        shcore::opts::Range<int>(0, std::numeric_limits<int>::max()))
    (&storage.dba_restart_wait_timeout, 60, SHCORE_DBA_RESTART_WAIT_TIMEOUT,
        "Timeout in seconds to wait for MySQL server to come back after a "
        "restart during clone recovery.",
        shcore::opts::Range<int>(0, std::numeric_limits<int>::max()))
    (&storage.dba_connectivity_checks, true, SHCORE_DBA_CONNECTIVITY_CHECKS,
        "Checks SSL settings and network connectivity between instances when "
        "creating a cluster, replicaset or clusterset, or adding an instance "
        "to one.")
    (&storage.dba_version_compatibility_checks, true, SHCORE_DBA_VERSION_COMPATIBILITY_CHECKS,
        "Checks version compatibility for asynchronous replication when "
        "managing a ReplicaSet, ClusterSet, or a Cluster with Read-Replicas.")
    (&storage.wizards, true, SHCORE_USE_WIZARDS, "Enables wizard mode.")
    (&storage.initial_mode, shcore::IShell_core::Mode::None,
        "defaultMode", "Specifies the shell mode to use when shell is started "
        "- one of sql, js or py.", std::bind(&shcore::parse_mode, _1),
        [](shcore::IShell_core::Mode mode) {
          return shcore::to_string(mode);
        })
    (&storage.pager, "", SHCORE_PAGER, "PAGER", cmdline("--pager=<value>"),
        "Pager used to display text output of statements executed in SQL mode "
        "as well as some other selected commands. Pager can be manually "
        "enabled in scripting modes. If you don't supply an "
        "option, the default pager is taken from your ENV variable PAGER. "
        "This option only works in interactive mode. This option is disabled "
        "by default.")
    (&storage.default_compress, false, SHCORE_DEFAULT_COMPRESS,
        "Enable compression in client/server protocol by default "
        "in global shell sessions.")
    (&storage.oci_config_file,
        shcore::path::join_path(shcore::path::home(), ".oci", "config"),
        SHCORE_OCI_CONFIG_FILE,
        "Path to Oracle Cloud Infrastructure (OCI) configuration file.")
    (&storage.oci_profile, std::string{"DEFAULT"}, SHCORE_OCI_PROFILE,
        "Oracle Cloud Infrastructure (OCI) configuration file profile name.")
    (&storage.mysql_plugin_dir, shcore::get_default_mysql_plugin_dir(),
        SHCORE_MYSQL_PLUGIN_DIR,
        cmdline("--mysql-plugin-dir[=<path>]"),
        "Directory for client-side authentication plugins.")
    (&storage.connect_timeout, 10.0, SHCORE_CONNECT_TIMEOUT,
        "Default connection timeout used by Shell sessions.",
        shcore::opts::Non_negative<double>())
    (&storage.dba_connect_timeout, 5.0, SHCORE_DBA_CONNECT_TIMEOUT,
        "Default connection timeout used for sessions created in AdminAPI "
        "operations.",
        shcore::opts::Non_negative<double>());

  add_startup_bool_options(!flags.is_set(Option_flags::CONNECTION_ONLY))
    ("--name-cache",
      "Enable database name caching for autocompletion and DevAPI (default). "
      "Boolean option.",
      [this](const std::string &, const bool value) {
        storage.db_name_cache = value;
        storage.db_name_cache_set = true;
        storage.devapi_schema_object_handles = value;
      });

  add_startup_options(!flags.is_set(Option_flags::CONNECTION_ONLY))
    (cmdline("-A", "--no-name-cache"),
      "Disable automatic database name caching for autocompletion and DevAPI. "
      "Use \\rehash to load DB names manually.",
      [this](const std::string &, const char *) {
        storage.db_name_cache = false;
        storage.db_name_cache_set = true;
        storage.devapi_schema_object_handles = false;
      });

  add_startup_bool_options(!flags.is_set(Option_flags::CONNECTION_ONLY))
    (&storage.wizards, true, "--wizard", "Sets wizard mode. "
                                         "Boolean option, default value \"true\".");

  add_startup_options(!flags.is_set(Option_flags::CONNECTION_ONLY))
    (cmdline("--nw", "--no-wizard"), "Disables wizard mode.",
        assign_value(&storage.wizards, false))
    (cmdline("--no-password"),
        "Sets empty password and disables prompt for password.",
        [this](const std::string&, const char*) {
          storage.connection_data.set_needs_password(0, false);
          storage.connection_data.set_password("");
      })
    (cmdline("-V", "--version"),
        "Prints the version of MySQL Shell.", [this](const std::string&, const char*) {
        if (print_cmd_line_version) {
          print_cmd_line_version_extra = true;
        }

        print_cmd_line_version = true;
    });

  add_startup_options(true)
    (cmdline("--ssl-key=<file_name>"),
        "The path to the SSL private key file in PEM format.",
        std::bind(&Ssl_options::set_key, &storage.connection_data.get_ssl_options(), _2))
    (cmdline("--ssl-cert=<file_name>"),
        "The path to the SSL public key certificate file in PEM format.",
        std::bind(&Ssl_options::set_cert, &storage.connection_data.get_ssl_options(), _2))
    (cmdline("--ssl-ca=<file_name>"),
        "The path to the certificate authority file in PEM format.",
        std::bind(&Ssl_options::set_ca, &storage.connection_data.get_ssl_options(), _2))
    (cmdline("--ssl-capath=<dir_name>"),
        "The path to the directory that contains the certificate authority "
        "files in PEM format.",
        std::bind(&Ssl_options::set_capath, &storage.connection_data.get_ssl_options(), _2))
    (cmdline("--ssl-cipher=<cipher_list>"),
        "The list of permissible encryption ciphers for connections that use "
        "TLS protocols up through TLSv1.2.",
        std::bind(&Ssl_options::set_cipher, &storage.connection_data.get_ssl_options(), _2))
    (cmdline("--ssl-crl=<file_name>"),
        "The path to the file containing certificate revocation lists in PEM "
        "format.",
        std::bind(&Ssl_options::set_crl, &storage.connection_data.get_ssl_options(), _2))
    (cmdline("--ssl-crlpath=<dir_name>"),
        "The path to the directory that contains certificate revocation-list "
        "files in PEM format.",
        std::bind(&Ssl_options::set_crlpath, &storage.connection_data.get_ssl_options(), _2))
    (cmdline("--ssl-mode=<mode>"), "SSL mode to use. Allowed values: DISABLED,"
        "PREFERRED, REQUIRED, VERIFY_CA, VERIFY_IDENTITY.",
        std::bind(&Shell_options::set_ssl_mode, this, _1, _2))
    (cmdline("--tls-version=<version>"),
        "TLS version to use. Allowed values: TLSv1.2, TLSv1.3.",
        std::bind(&Ssl_options::set_tls_version, &storage.connection_data.get_ssl_options(), _2))
    (cmdline("--tls-ciphersuites=<name>"), "TLS v1.3 cipher to use.",
        std::bind(&Ssl_options::set_tls_ciphersuites, &storage.connection_data.get_ssl_options(), _2))
    (cmdline("--auth-method=<method>"),
        "Authentication method to use. In case of classic session, this is the "
        "name of the authentication plugin to use, i.e. caching_sha2_password. "
        "In case of X protocol session, it should be one of: AUTO, "
        "FROM_CAPABILITIES, FALLBACK, MYSQL41, PLAIN, SHA256_MEMORY.",
        [this](const std::string&, const char* value) {
          if (storage.connection_data.has_value(mysqlshdk::db::kAuthMethod)) {
            storage.connection_data.remove(mysqlshdk::db::kAuthMethod);
          }
          storage.connection_data.set(mysqlshdk::db::kAuthMethod, value);
        })
#ifdef HAVE_JS
    (cmdline("--js-debug-port=<value>"), "Starts a debug server to debug a "
    "JavaScript file on the indicated port.",
    [this](const std::string&option, const char* value) {
      try {
        shcore::opts::convert<int>(value, shcore::opts::Source::Command_line);
        storage.js_options.debug_port = value;
      } catch (const std::invalid_argument&) {
        throw std::invalid_argument(shcore::str_format("Invalid value for %s: %s",
          option.c_str(), value));
      }
    })
    (cmdline("--js-debug-wait-attached"), "Waits for a debug client to get "
    "attached to a JavaScript debug server.",
    assign_value(&storage.js_options.wait_attached, true))
#endif
    (cmdline("--disable-plugins"), "Disable loading user plugins.",
    assign_value(&storage.disable_user_plugins, true))
    (cmdline("--disable-builtin-plugins"), "Disable loading built-in plugins.",
    assign_value(&storage.disable_builtin_plugins, true));

  add_startup_options(!flags.is_set(Option_flags::CONNECTION_ONLY))
#ifndef NDEBUG
    (cmdline("--trace-proto"), assign_value(&storage.trace_protocol, true))
#endif
        ;

  add_startup_options(!flags.is_set(Option_flags::CONNECTION_ONLY))
    (
      cmdline("--quiet-start[={1|2}]"),
      "Avoids printing information when the shell is started. A value of "
      "1 will prevent printing the shell version information. A value of "
      "2 will prevent printing any information unless it is an error. "
      "If no value is specified uses 1 as default.",
      [this](const std::string&, const char* value) {
        if (!value || !value[0] || !strcmp(value, "1")) {
          storage.quiet_start = Shell_options::Quiet_start::SUPRESS_BANNER;
          storage.full_interactive = false;
        } else if (strcmp(value, "2") == 0) {
          storage.quiet_start = Shell_options::Quiet_start::SUPRESS_INFO;
        } else {
          throw std::invalid_argument("Value for --quiet-start if any, must be any of 1 or 2");
        }
      })

      (cmdline("--debug=<control>"),
      [this](const std::string &, const char* value) {
#ifdef NDEBUG
        // If DBUG is disabled, we just print a warning saying the option won't
        // do anything. This is to keep options compatible between build types
        m_on_warning("WARNING: This build of mysqlsh has the DBUG feature "
          "disabled. --debug option ignored.");
#endif
        storage.dbug_options = value ? value : "";
      })
;  // <-- Note this is on purpose: Mark the termination of the option definition.
  // clang-format on

  if (!flags.is_set(Option_flags::CONNECTION_ONLY))
    shcore::Credential_manager::get().register_options(this);

  try {
    MEM_ROOT argv_alloc{};
    mysqlshdk::db::Connection_options mycnf_connection_data;

    handle_environment_options();
    if (!configuration_file.empty()) {
      handle_persisted_options();
    }

    // process standard my.cnf files and cmdline options first, which will
    // prepend argv with options read from these places and end with a
    // ----args-separator----
    if (flags.is_set(Option_flags::READ_MYCNF))
      handle_mycnf_options(&argc, &argv, &argv_alloc);

    mycnf_connection_data = storage.connection_data;
    // temporarily clear passwords so we can detect if we got them from cmdline
    for (int f = 0; f < mysqlshdk::db::k_max_auth_factors; f++) {
      storage.connection_data.clear_needs_password(f);
      storage.connection_data.clear_mfa_password(f);
    }
    handle_cmdline_options(
        argc, argv, false,
        std::bind(&Shell_options::custom_cmdline_handler, this, _1));

    if (storage.connection_data.has_password() ||
        storage.connection_data.has_mfa_password(1) ||
        storage.connection_data.has_mfa_password(2)) {
      got_cmdline_password = true;
    } else {
      // if we didn't get any password in the cmdline, restore the ones we got
      // from my.cnf (if any)
      for (int f = 0; f < mysqlshdk::db::k_max_auth_factors; f++) {
        if (!storage.connection_data.has_needs_password(f) ||
            !storage.connection_data.needs_password(f)) {
          if (mycnf_connection_data.has_mfa_password(f))
            storage.connection_data.set_mfa_password(
                f, mycnf_connection_data.get_mfa_password(f));
        }
      }
    }

    if (storage.has_connection_data() &&
        !storage.connection_data.has_scheme() &&
        mycnf_connection_data.has_data()) {
      // default to mysql:// if we got connection options from my.cnf but we
      // still don't know what session type to use
      storage.connection_data.set_scheme("mysql");
    }

    check_password_conflicts();
    check_ssh_conflicts();

    if (!flags.is_set(Option_flags::CONNECTION_ONLY)) {
      check_file_execute_conflicts();
      check_result_format();
    }
    check_connection_options();

    if (!flags.is_set(Option_flags::CONNECTION_ONLY))
      shcore::Logger::set_stderr_output_format(storage.wrap_json);
  } catch (const std::exception &e) {
    m_on_error(e.what());
    storage.exit_code = 1;
  }

#ifdef MARIADB_BUILD
  // argv is no longer needed past option parsing; release the buffer allocated
  // by my_load_defaults() (otherwise mysys' debug safemalloc reports it as a
  // leak at exit).
  if (m_mariadb_defaults_argv) {
    free_defaults(m_mariadb_defaults_argv);
    m_mariadb_defaults_argv = nullptr;
  }
#endif
}

static inline std::string value_to_string(const shcore::Value &value) {
  return value.get_type() == shcore::Value_type::String ? value.get_string()
                                                        : value.repr();
}

void Shell_options::set(const std::string &option, const shcore::Value &value) {
  try {
    shcore::Options::set(option, value_to_string(value));
  } catch (const std::exception &e) {
    throw shcore::Exception::argument_error(e.what());
  }
}

void Shell_options::notify(const std::string &option) {
  shcore::Value::Map_type_ref info = shcore::Value::new_map().as_map();
  (*info)["option"] = shcore::Value(option);
  (*info)["value"] = get(option);
  shcore::ShellNotifications::get()->notify(SN_SHELL_OPTION_CHANGED, nullptr,
                                            info);
}

void Shell_options::set_and_notify(const std::string &option,
                                   const std::string &value,
                                   bool save_to_file) {
  try {
    Options::set(option, value);
    notify(option);
    if (save_to_file) save(option);
  } catch (const std::exception &e) {
    throw shcore::Exception::argument_error(e.what());
  }
}

void Shell_options::set_and_notify(const std::string &option,
                                   const shcore::Value &value,
                                   bool save_to_file) {
  set_and_notify(option, value_to_string(value), save_to_file);
}

void Shell_options::unset(const std::string &option, bool save_to_file) {
  if (save_to_file) unsave(option);
  get_option(option).reset_to_default_value();
  notify(option);
}

shcore::Value Shell_options::get(const std::string &option) {
  using shcore::opts::Concrete_option;
  auto it = named_options.find(option);
  if (it == named_options.end())
    throw std::invalid_argument("No option registered under name: " + option);
  Concrete_option<bool> *opt_bool =
      dynamic_cast<Concrete_option<bool> *>(it->second);
  if (opt_bool != nullptr) return shcore::Value(opt_bool->get());

  Concrete_option<int> *opt_int =
      dynamic_cast<Concrete_option<int> *>(it->second);
  if (opt_int != nullptr) return shcore::Value(opt_int->get());

  {
    const auto opt_double = dynamic_cast<Concrete_option<double> *>(it->second);
    if (opt_double != nullptr) return shcore::Value(opt_double->get());
  }

  auto opt_set_string =
      dynamic_cast<Concrete_option<std::vector<std::string>> *>(it->second);

  if (opt_set_string != nullptr) {
    auto value = shcore::Value::new_array();
    auto array = value.as_array();

    for (const auto &val : opt_set_string->get()) {
      array->emplace_back(val);
    }

    return value;
  }

  return shcore::Value(get_value_as_string(option));
}

std::vector<std::string> Shell_options::get_named_options() {
  std::vector<std::string> res;
  for (const auto &op : named_options) res.push_back(op.first);
  return res;
}

static void handle_missing_value(shcore::Options::Iterator *it) {
  if (nullptr == it->value() || '\0' == it->value()[0]) {
    throw std::invalid_argument(std::string(it->iterator()->first()) +
                                ": option " + it->option() +
                                " requires an argument");
  }
}

bool Shell_options::custom_cmdline_handler(Iterator *iterator) {
  const auto option = iterator->option();

  if ("--" == option) {
    if (m_shell_cli_operation)
      throw std::invalid_argument(
          "MySQL Shell can handle only one operation at a time");
    iterator->next_no_value();
    m_shell_cli_operation.reset(new shcore::cli::Shell_cli_operation());
    m_shell_cli_operation->parse(iterator->iterator());
  } else if ("--file" == option || "-f" == option) {
    handle_missing_value(iterator);

    const std::string file = iterator->value();
    iterator->next();

    storage.run_file = file;

    if (shcore::str_endswith(file, ".js")) {
      storage.initial_mode = shcore::IShell_core::Mode::JavaScript;
    } else if (shcore::str_endswith(file, ".py")) {
      storage.initial_mode = shcore::IShell_core::Mode::Python;
    } else if (shcore::str_endswith(file, ".sql")) {
      storage.initial_mode = shcore::IShell_core::Mode::SQL;
    }

    // the rest of the cmdline options, starting from here are all passed
    // through to the script
    storage.script_argv.push_back(file);
    const auto cmdline = iterator->iterator();
    while (cmdline->valid()) storage.script_argv.push_back(cmdline->get());
  } else if ("-c" == option || "--pyc" == option) {
    // Like --py -f , but with an inline command
    // Needed for backwards compatibility with python executable when
    // subprocess spawns it
#ifdef HAVE_PYTHON
    handle_missing_value(iterator);

    storage.initial_mode = shcore::IShell_core::Mode::Python;
    storage.execute_statement = iterator->value();
    iterator->next();

    // the rest of the cmdline options, starting from the next one are all
    // passed through to the script

    storage.script_argv.push_back("-c");
    const auto cmdline = iterator->iterator();
    while (cmdline->valid()) storage.script_argv.push_back(cmdline->get());
#else
    throw std::invalid_argument("Python is not supported.");
#endif
  } else if ("--pym" == option) {
#ifndef HAVE_PYTHON
    throw std::invalid_argument("Python is not supported.");
#endif
    handle_missing_value(iterator);

    const std::string module = iterator->value();
    iterator->next();

    storage.run_module = module;
    storage.initial_mode = shcore::IShell_core::Mode::Python;

    // the rest of the cmdline options, starting from here are all passed
    // through to the script
    storage.script_argv.push_back(module);
    const auto cmdline = iterator->iterator();
    while (cmdline->valid()) storage.script_argv.push_back(cmdline->get());
  } else if ("--uri" == option || '-' != option[0]) {
    handle_missing_value(iterator);

    storage.set_uri(iterator->value());

    {
      char *value = const_cast<char *>(iterator->value());
      const auto nopwd = mysqlshdk::db::uri::hide_password_in_uri(value);
      obfuscate_uri_password(nopwd, value);
    }

    iterator->next();
  } else if ("--user" == option || "-u" == option) {
    handle_missing_value(iterator);

    storage.connection_data.set_user(iterator->value());

    iterator->next();
  } else if ("--ssh" == option) {
    handle_missing_value(iterator);

    storage.ssh.uri = iterator->value();
    storage.ssh.uri_data =
        shcore::get_ssh_connection_options(storage.ssh.uri, false);
    if (storage.ssh.uri_data.has_password()) {
      char *value = const_cast<char *>(iterator->value());
      const auto nopwd = mysqlshdk::db::uri::hide_password_in_uri(value);
      obfuscate_uri_password(nopwd, value);
    }

    iterator->next();
  } else if ("--password" == option || "-p" == option ||
             "--password1" == option || "--password2" == option ||
             "--password3" == option) {
    int mfa_password = 0;
    if (option == "--password2")
      mfa_password = 1;
    else if (option == "--password3")
      mfa_password = 2;

    // Note that in any connection attempt, password prompt will be done if
    // the password is missing.
    // The behavior of the password cmd line argument is as follows:

    // ARGUMENT           EFFECT
    // --password         forces password prompt no matter it was already
    // provided
    // --password value   forces password prompt no matter it was already
    // provided (value is not taken as password)
    // --password=        sets password to empty (password is available but
    // empty so it will not be prompted)
    // -p<value> sets the password to <value>
    // --password=<value> sets the password to <value>

    // --password1 is an alias for -p/--password
    // --password2 and --password3 are MFA passwords and handled like -p

    // if a password option is specified, it's needed
    storage.connection_data.set_needs_password(mfa_password, true);
    if (shcore::Options::Iterator::Type::NO_VALUE == iterator->type()) {
      storage.connection_data.clear_mfa_password(mfa_password);
      iterator->next_no_value();
      storage.prompt_password = true;
    } else if (shcore::Options::Iterator::Type::SEPARATE_VALUE !=
               iterator->type()) {
      // --password=value || -pvalue
      const auto value = iterator->value();
      storage.connection_data.set_mfa_password(mfa_password, value);
      const std::string stars(strlen(value), '*');

      strncpy(const_cast<char *>(value), stars.c_str(), stars.length() + 1);
      iterator->next();
    } else {  // --password value (value is ignored)
      storage.connection_data.clear_mfa_password(mfa_password);
      iterator->next_no_value();
    }
  } else {
    return false;
  }

  return true;
}

void Shell_options::override_session_type(const std::string &option,
                                          const char *value) {
  if (value != nullptr)
    throw std::invalid_argument("Option " + option + " does not support value");

  auto session_type = storage.check_option_session_type_conflict(option);

  if (option.find("--sql") != std::string::npos)
    storage.initial_mode = shcore::IShell_core::Mode::SQL;

  switch (session_type) {
    case mysqlsh::SessionType::Classic:
      storage.connection_data.set_scheme("mysql");
      break;
#ifdef HAVE_X_PROTOCOL
    case mysqlsh::SessionType::X:
      storage.connection_data.set_scheme("mysqlx");
      break;
#endif
    case mysqlsh::SessionType::Auto:
      break;
  }
}

void Shell_options::set_ssl_mode(const std::string &option, const char *value) {
  int mode = mysqlshdk::db::MapSslModeNameToValue::get_value(value);

  if (mode == 0) {
    throw std::invalid_argument(option +
                                " must be any of [DISABLED, PREFERRED, "
                                "REQUIRED, VERIFY_CA, VERIFY_IDENTITY]");
  }

  storage.connection_data.get_ssl_options().set_mode(
      static_cast<mysqlshdk::db::Ssl_mode>(mode));
}

void Shell_options::check_password_conflicts() {
  if (!storage.ssh.pwd.empty() && storage.ssh.uri_data.has_password()) {
    if (storage.ssh.pwd != storage.ssh.uri_data.get_password()) {
      const auto error =
          "Conflicting options: provided SSH password differs from the "
          "password in the SSH-URI.";
      throw std::runtime_error(error);
    }
  }
}

void Shell_options::check_file_execute_conflicts() {
  if (!storage.run_file.empty() && !storage.execute_statement.empty()) {
    throw std::runtime_error(
        "Conflicting options: --execute and --file cannot be used at the same "
        "time.");
  } else if (!storage.run_module.empty() &&
             !storage.execute_statement.empty()) {
    throw std::runtime_error(
        "Conflicting options: --execute and --pym cannot be used at the same "
        "time.");
  }
  // note: --file and --pym are impossible because both consume remaining args
}

void Shell_options::check_ssh_conflicts() {
  if (!storage.ssh.uri.empty() && !storage.connection_data.has_data()) {
    throw std::runtime_error(
        "SSH tunnel can't be started because DB URI is missing");
  }

  if (!storage.ssh.uri.empty() && storage.connection_data.has_socket()) {
    throw std::runtime_error(
        "Socket connections through SSH are not supported");
  }
}

void Shell_options::check_result_format() {
  if (storage.wrap_json != "off" &&
      get_option_source(SHCORE_RESULT_FORMAT) == Source::Command_line &&
      storage.wrap_json != storage.result_format)
    throw std::invalid_argument(shcore::str_format(
        "Conflicting options: " SHCORE_RESULT_FORMAT
        " cannot be set to '%s' when "
        "--json option implying '%s' value is used.",
        storage.result_format.c_str(), storage.wrap_json.c_str()));
}

void Shell_options::check_connection_options() {
  std::vector<std::string> connection_dependent_options;
  if (!storage.register_factor.empty()) {
    connection_dependent_options.push_back("--register-factor");
  }
  if (storage.webauthn_client_preserve_privacy.has_value()) {
    connection_dependent_options.push_back(
        "--plugin-authentication-webauthn-client-preserve-privacy");
  }

  if (!connection_dependent_options.empty() &&
      !storage.connection_data.has_data()) {
    if (connection_dependent_options.size() == 1) {
      m_on_warning(
          shcore::str_format("WARNING: %s was specified without "
                             "connection data, the option will be ignored.",
                             connection_dependent_options.at(0).c_str()));
    } else {
      shcore::str_format(
          "WARNING: The following options were specified without "
          "connection data, they will be ignored: %s.",
          shcore::str_join(connection_dependent_options, ", ").c_str());
    }
  }

  if (!connection_dependent_options.empty() &&
      storage.connection_data.has_scheme() &&
      storage.connection_data.get_scheme() == "mysqlx") {
    if (connection_dependent_options.size() == 1) {
      throw std::runtime_error(
          shcore::str_format("Option %s is only available for MySQL protocol "
                             "connections.",
                             connection_dependent_options.at(0).c_str()));
    } else {
      throw std::runtime_error(shcore::str_format(
          "The following options are only available for MySQL protocol "
          "connections: %s",
          shcore::str_join(connection_dependent_options, ", ").c_str()));
    }
  }

  if (storage.has_multi_passwords()) {
    if (storage.connection_data.has_scheme() &&
        storage.connection_data.get_scheme() == "mysqlx")
      throw shcore::Exception::argument_error(
          "Multi-factor authentication is only compatible with MySQL protocol");
  }
}

}  // namespace mysqlsh
