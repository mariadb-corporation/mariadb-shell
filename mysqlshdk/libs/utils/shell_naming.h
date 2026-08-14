/*
 * Copyright (c) 2026, MariaDB plc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
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

#ifndef MYSQLSHDK_LIBS_UTILS_SHELL_NAMING_H_
#define MYSQLSHDK_LIBS_UTILS_SHELL_NAMING_H_

/*
 * Single source of truth for every user-visible name derived from the product
 * name. The shell was renamed from "mysqlsh" to "mariadb-shell"; keeping these
 * in one place means the on-disk layout (install dirs, config dir, log file,
 * startup script) and the CMake install destinations cannot drift apart.
 *
 * The CMake side of the same names lives in the top-level CMakeLists.txt
 * (INSTALL_LIBDIR / INSTALL_SHAREDIR / INSTALL_LIBEXECDIR / INSTALL_INCLUDEDIR)
 * and in cmake/packaging.cmake. Change both together.
 */

namespace shcore {

/** Name of the shell executable, without any platform suffix. */
constexpr const char k_shell_binary_name[] = "mariadb-shell";

/**
 * Leaf name of the per-user configuration directory, as created directly under
 * $HOME on Unix (i.e. "~/.mariadb-shell").
 */
constexpr const char k_shell_user_config_dir_unix[] = ".mariadb-shell";

/**
 * Windows configuration directory, relative to %AppData% (per-user) and
 * %ProgramData% (global): "<vendor>\<dir>".
 */
constexpr const char k_shell_config_dir_win_vendor[] = "MariaDB";
constexpr const char k_shell_config_dir_win[] = "mariadb-shell";

/** Unix global (system-wide) configuration directory. */
constexpr const char k_shell_global_config_dir_unix[] =
    "/etc/mysql/mariadb-shell";

/** Leaf name of the shell's own log file, inside the user config directory. */
constexpr const char k_shell_log_file_name[] = "mariadb-shell.log";

/**
 * Leaf name of the directory holding deployed sandbox instances, inside the
 * per-user shell directory: "~/.mariadb-shell/sandboxes" on Unix,
 * "%userprofile%\MariaDB\mariadb-shell\sandboxes" on Windows. The sandbox
 * plugin derives the same default, see
 * python/plugins/sandbox/sandboxlib.py:default_sandbox_base_dir().
 */
constexpr const char k_shell_sandbox_dir_name[] = "sandboxes";

/**
 * Base name of the per-language startup script, loaded from the user config
 * directory and from the share directory. The language extension (".js" /
 * ".py"), or nothing at all, is appended by the caller.
 */
constexpr const char k_shell_startup_script_name[] = "mariadb-shellrc";

/**
 * Name used to identify the shell to the outside world: the my.cnf/option-file
 * group it reads, the syslog ident, and the client program_name reported to the
 * server through connection attributes.
 */
constexpr const char k_shell_option_group[] = "mariadb-shell";

/**
 * Leaf name of the install subdirectory under share/, lib/, libexec/ and
 * include/. Must match INSTALL_SHAREDIR & friends in the top-level
 * CMakeLists.txt.
 */
constexpr const char k_shell_install_dir_name[] = "mariadb-shell";

/**
 * Prefix of the shell's environment variables. Historically MYSQLSH_*; the old
 * prefix is still honoured as a fallback, see shcore::getenv_shell().
 */
constexpr const char k_shell_env_prefix[] = "MARIADB_SHELL_";
constexpr const char k_shell_legacy_env_prefix[] = "MYSQLSH_";

/**
 * Prefix of the secret store helper executables installed beside the shell
 * binary. get_available_helpers() discovers helpers by scanning the binary
 * folder for this prefix and takes whatever follows it as the helper name, so
 * the prefix is what keeps the shell from picking up MySQL Shell's identically
 * shaped mysql-secret-store-* helpers out of a shared bindir.
 *
 * The CMake side that names the executables lives in
 * mysql-secret-store/cmake/mysql_secret_store.cmake. Change both together.
 */
constexpr const char k_secret_store_helper_prefix[] = "mariadb-secret-store-";

}  // namespace shcore

#endif  // MYSQLSHDK_LIBS_UTILS_SHELL_NAMING_H_
