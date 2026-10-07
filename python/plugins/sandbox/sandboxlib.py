# Copyright (c) 2026, MariaDB plc.
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335 USA

"""Self-contained implementation of MariaDB server sandbox operations.

This module is the engine behind the ``mariadbSandbox`` shell plugin. It
reproduces the behavior that the (now excluded) AdminAPI ``dba.*Sandbox*``
operations provided through ``provisioning_interface`` and the
``mysql_gadgets`` package, but targets the MariaDB server tool-chain and does
NOT depend on ``mysql_gadgets`` (or any other shell-internal module) at all.

A sandbox is a self-contained MariaDB server deployment living under a single
directory ``<sandboxDir>/<port>``, suitable only for local testing.
"""

import contextlib
import hashlib
import os
import re
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

from mysqlsh import globals, Error, executable

# Default timeout (seconds) to wait for a sandbox to start/stop.
SANDBOX_TIMEOUT = 60

# The address a sandbox listens on unless 'mariadbdOptions' says otherwise.
_LOOPBACK_ADDRESS = "127.0.0.1"

# How often a wait polls, and how long one loopback probe may take. A loopback
# connect is answered by the kernel at once; the short timeout matters on
# Windows, where a refused connect only fails when the timeout runs out.
_POLL_INTERVAL = 0.1
_PROBE_TIMEOUT = 0.25

# Tool names tried in order: prefer the MariaDB-branded names, fall back to the
# legacy mysql* names shared by MariaDB and MySQL packages. The server binary
# lookup deliberately covers both vendors so a MySQL install on the PATH is
# found too; the actual vendor is then detected from its --version banner.
_MYSQLD_NAMES = ["mariadbd", "mysqld"]
_INSTALL_DB_NAMES = ["mariadb-install-db", "mysql_install_db"]
_OPENSSL_NAMES = ["openssl"]

# Server vendors this plugin knows how to deploy.
_VENDOR_MARIADB = "mariadb"
_VENDOR_MYSQL = "mysql"

# Human-readable vendor names used in user-facing messages.
_VENDOR_LABELS = {_VENDOR_MARIADB: "MariaDB", _VENDOR_MYSQL: "MySQL"}

# Certificate/key files generated for a sandbox and the role they play. The
# server references the CA + server pair; the client section references the CA
# + client pair so the bundled clients can authenticate to the server over TLS.
_SSL_FILES = {
    "ca_key": "ca-key.pem",
    "ca_cert": "ca-cert.pem",
    "server_key": "server-key.pem",
    "server_cert": "server-cert.pem",
    "client_key": "client-key.pem",
    "client_cert": "client-cert.pem",
}

# The equivalent files MySQL generates by itself while initializing the data
# directory (auto_generate_certs, on by default), keyed by the same roles. Note
# the CA is named 'ca.pem', not MariaDB's 'ca-cert.pem'. They live in the data
# directory rather than the sandbox root.
_MYSQL_AUTO_SSL_FILES = dict(_SSL_FILES, ca_cert="ca.pem")

# Validity of the self-signed sandbox certificates, in days (10 years).
_SSL_DAYS = "3650"

# Prefix of the per-version boilerplate directory created under the sandbox
# base dir. The expensive data-directory bootstrap is done once per server
# version; every later deployment just copies this initialized data directory.
_BOILERPLATE_PREFIX = "myboilerplate"

# Environment variable naming a directory to keep the boilerplates in instead
# of the sandbox base dir, so deployments to different sandbox dirs can share
# one - e.g. the parallel test workers of scripts/run_unit_tests.py, which
# would otherwise each bootstrap their own. Building is atomic, so concurrent
# deployments sharing the directory are safe.
BOILERPLATE_DIR_ENV = "MARIADB_SANDBOX_BOILERPLATE_DIR"

# Environment variable that, when set to a non-empty value, deploys MariaDB
# sandboxes with 'debug-no-sync': the server skips every mysys my_sync() call.
# On macOS that call is fcntl(F_FULLFSYNC), a full drive cache flush serialized
# across processes, issued several times per DDL statement (.frm, DDL log,
# backup log), so parallel test workers queue behind each other's syncs. InnoDB
# syncs its own files and is unaffected, but the rest of the instance is no
# longer safe against an OS crash or power loss, so this is meant for test runs
# only (such as scripts/run_unit_tests.py). MySQL has no such option, and its
# my_sync() is a plain fsync(), so it is left alone.
NO_SYNC_ENV = "MARIADB_SANDBOX_NO_SYNC"

# InnoDB sizing applied both when bootstrapping the boilerplate and in every
# sandbox's option file. Keeping the system tablespace and redo log small makes
# the per-sandbox copy of the data directory cheap. These MUST stay consistent
# between the boilerplate and the deployed sandboxes, otherwise the copied data
# directory will not match the runtime configuration and the server won't start.
#
# The redo-log option is vendor/version dependent and therefore resolved at
# deployment time from the variables the server actually advertises (see
# _innodb_opts): MySQL 8.0.30+/9.x replaced 'innodb_log_file_size' with
# 'innodb_redo_log_capacity', while MariaDB still uses 'innodb_log_file_size'.
_INNODB_OPTS = {
    "innodb_data_file_path": "ibdata1:10M:autoextend",
    "innodb_buffer_pool_size": "16M",
}

# Small redo-log sizes keyed by the variable name the server supports. 8M is the
# documented minimum for MySQL's innodb_redo_log_capacity.
_INNODB_REDO_OPTS = {
    "innodb_redo_log_capacity": "8M",
    "innodb_log_file_size": "4M",
}

# Unix domain socket paths are capped by the OS (sun_path is 104 bytes on
# macOS, 108 on Linux, including the trailing NUL). Stay safely under that.
_MAX_SOCKET_PATH = 100

# Sub-directories where the MariaDB tools may live. Covers both installed
# layouts (bin/sbin) and in-tree build layouts (sql/scripts/client).
_BIN_SUBDIRS = ["bin", "sbin", "libexec", os.path.join("usr", "sbin"),
                "scripts", "sql", "client", ""]


# --------------------------------------------------------------------------- #
# Generic helpers
# --------------------------------------------------------------------------- #
def _log(level, msg):
    """Send a message to the shell log without aborting on logging errors."""
    try:
        globals.shell.log(level, "mariadbSandbox: " + msg)
    except Exception:
        pass


def _is_executable(path):
    return bool(path) and os.path.isfile(path) and os.access(path, os.X_OK)


def _find_in_dirs(names, base_dir):
    """Look for any of ``names`` inside the known bin sub-dirs of base_dir."""
    for sub in _BIN_SUBDIRS:
        for name in names:
            for candidate in (name, name + ".exe"):
                path = os.path.join(base_dir, sub, candidate)
                if _is_executable(path):
                    return path
    return None


def _find_in_path(names):
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    return None


def _cnf_value(path):
    """'path' with forward slashes, the form option files take on every OS."""
    return path.replace("\\", "/")


def is_listening(host, port, timeout=1.0):
    """Return True if something accepts TCP connections on host:port."""
    try:
        with socket.create_connection((host, int(port)), timeout=timeout):
            return True
    except (OSError, ValueError):
        return False


def _port_open(port):
    """Whether a sandbox accepts TCP connections on 'port' of this machine.

    Probes 127.0.0.1, where a sandbox listens by default, before ::1, rather
    than 'localhost', which may resolve to ::1 first.
    """
    return any(is_listening(host, port, timeout=_PROBE_TIMEOUT)
               for host in (_LOOPBACK_ADDRESS, "::1"))


def _is_socket_listening(path, timeout=1.0):
    """Return True if something accepts connections on the Unix socket 'path'."""
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            sock.settimeout(timeout)
            sock.connect(path)
            return True
    except OSError:
        return False


def _configured_mysqld_options(sandbox_dir):
    """The [mysqld] options in the sandbox's option file.

    Reads the format _write_option_file() writes, plus the 'option = value'
    lines other tools (e.g. the test suite) add to it. Names are normalized to
    use '_', and an option given without a value maps to None. An unreadable
    file yields an empty dict.
    """
    section = None
    options = {}
    try:
        with open(_cnf_path(sandbox_dir)) as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith(("#", ";")):
                    continue
                if line.startswith("[") and line.endswith("]"):
                    section = line[1:-1].strip()
                elif section == "mysqld":
                    key, sep, value = line.partition("=")
                    options[key.strip().replace("-", "_")] = (
                        value.strip() if sep else None)
    except OSError:
        return {}
    return options


def _configured_socket(sandbox_dir):
    """The [mysqld] socket named in the sandbox's option file, or None.

    The value is normally _socket_path(), but 'mariadbdOptions' may replace it.
    """
    return _configured_mysqld_options(sandbox_dir).get("socket") or None


def _is_enabled(options, name):
    """Whether a boolean server option is set, and not to OFF/0/FALSE."""
    if name not in options:
        return False
    value = options[name]
    return value is None or value.lower() not in ("0", "off", "false")


def _networking_disabled(sandbox_dir):
    """Whether the option file makes the server skip its TCP port.

    MySQL 8.0+ turns networking off along with the grant tables, so that no one
    can reach an unprotected server remotely; MariaDB does not.
    """
    options = _configured_mysqld_options(sandbox_dir)
    if _is_enabled(options, "skip_networking"):
        return True
    return (_is_enabled(options, "skip_grant_tables") and
            _read_vendor(sandbox_dir) == _VENDOR_MYSQL)


def _is_ready(port, sandbox_dir):
    """Return True once the instance accepts connections on all its endpoints.

    The server starts listening on its TCP port before it creates its Unix
    socket, so an open port alone does not mean the socket - which root
    sessions use on POSIX, see _open_root_session() - can be connected to yet.
    Windows has no socket file (see _build_option_file).

    A server running without networking (see _networking_disabled()) never
    opens the port, only the socket.
    """
    if os.name == "posix" and _networking_disabled(sandbox_dir):
        socket_path = _configured_socket(sandbox_dir)
        return socket_path is not None and _is_socket_listening(socket_path)
    if not _port_open(port):
        return False
    if os.name != "posix":
        return True
    socket_path = _configured_socket(sandbox_dir)
    return socket_path is None or _is_socket_listening(socket_path)


def _is_listening_anywhere(port, sandbox_dir):
    """Whether the instance still accepts connections on any of its endpoints.

    Checks the socket too: a server running without networking (see
    _networking_disabled()) has no port to close, and the option file may have
    changed since it was started, so it cannot tell which endpoints it opened.
    """
    if _port_open(port):
        return True
    if os.name != "posix":
        return False
    socket_path = _configured_socket(sandbox_dir)
    return socket_path is not None and _is_socket_listening(socket_path)


def _wait_until(predicate, timeout):
    """Poll predicate() every _POLL_INTERVAL until it is True or timeout
    seconds of polling have passed."""
    for _ in range(int(timeout / _POLL_INTERVAL)):
        if predicate():
            return True
        time.sleep(_POLL_INTERVAL)
    return predicate()


def default_sandbox_base_dir():
    """The default base directory for sandboxes.

    Honors the shell ``sandboxDir`` option when it is set, otherwise uses
    ``<MARIADB_SHELL_USER_CONFIG_HOME>/sandboxes`` when that variable is set, and
    ``~/.mariadb-shell/sandboxes``
    (``%userprofile%\\MariaDB\\mariadb-shell\\sandboxes`` on Windows) when not.
    Must stay in sync with the sandboxDir default in
    mysqlshdk/shellcore/shell_options.cc, which is where the option value
    normally comes from.
    """
    try:
        configured = globals.shell.options["sandboxDir"]
        if configured:
            return os.path.expanduser(configured)
    except Exception:
        pass

    config_home = os.environ.get("MARIADB_SHELL_USER_CONFIG_HOME") or \
        os.environ.get("MYSQLSH_USER_CONFIG_HOME")
    if config_home:
        return os.path.join(config_home, "sandboxes")

    if os.name == "nt":
        return os.path.join(
            os.path.expanduser("~"), "MariaDB", "mariadb-shell", "sandboxes"
        )
    return os.path.join(os.path.expanduser("~"), ".mariadb-shell", "sandboxes")


def _sandbox_dir(port, options):
    base = options.get("sandboxDir")
    base = os.path.expanduser(base) if base else default_sandbox_base_dir()
    base = os.path.abspath(base)
    return base, os.path.join(base, str(port))


def _cnf_path(sandbox_dir):
    return os.path.join(sandbox_dir, "my.cnf")


def _error_log_path(sandbox_dir):
    return os.path.join(_datadir(sandbox_dir), "error.log")


def _write_marker(sandbox_dir, name, value):
    """Record 'value' in the one-line file 'name' (best effort)."""
    with contextlib.suppress(OSError):
        with open(os.path.join(sandbox_dir, name), "w") as f:
            f.write(value)


def _read_marker(sandbox_dir, name):
    """The value recorded by _write_marker(), or None when there is none."""
    try:
        with open(os.path.join(sandbox_dir, name)) as f:
            return f.read().strip() or None
    except OSError:
        return None


def _write_vendor(sandbox_dir, vendor):
    """Persist the server vendor so later operations can report it."""
    _write_marker(sandbox_dir, "vendor", vendor)


def _vendor_label(sandbox_dir):
    """Human-readable vendor of the sandbox for user-facing messages.

    See _read_vendor().
    """
    return _VENDOR_LABELS.get(_read_vendor(sandbox_dir),
                              _VENDOR_LABELS[_VENDOR_MARIADB])


def _read_vendor(sandbox_dir):
    """Vendor recorded for the sandbox at deploy time.

    Sandboxes created before MySQL support (and any without a readable marker)
    were always MariaDB, so that is the fallback.
    """
    return _read_marker(sandbox_dir, "vendor") or _VENDOR_MARIADB


def _write_version(sandbox_dir, version):
    """Persist the server version so later operations can report it."""
    _write_marker(sandbox_dir, "version", version)


def _socket_path(sandbox_dir):
    """Path to the instance's Unix socket.

    Normally ``<sandbox_dir>/mysqld.sock``, but when that would exceed the OS
    socket-path limit (deep sandbox directories), fall back to a short,
    deterministic path under the system temp directory so the server can still
    create the socket. The result is stable for a given sandbox directory, so
    deploy/start and stop/connect all agree on it.
    """
    natural = os.path.join(sandbox_dir, "mysqld.sock")
    if len(natural) <= _MAX_SOCKET_PATH:
        return natural
    short_base = "/tmp" if os.path.isdir("/tmp") else tempfile.gettempdir()
    digest = hashlib.sha1(sandbox_dir.encode("utf-8")).hexdigest()[:12]
    return os.path.join(short_base, "mdbsbx-{0}.sock".format(digest))


def _pid_path(sandbox_dir, port):
    return os.path.join(sandbox_dir, "{0}.pid".format(port))


def _datadir(sandbox_dir):
    return os.path.join(sandbox_dir, "sandboxdata")


def _tmpdir(sandbox_dir):
    """The server's private tmpdir, kept inside the sandbox directory.

    Every server deletes all '#sql*' files in its tmpdir at startup (MariaDB's
    and MySQL's mysql_rm_tmp_tables(), also run by --bootstrap and by the data
    directory initialization). Sandboxes sharing the system temp directory
    would therefore wipe the internal temporary tables of any other sandbox
    that happens to be running a query when one of them starts.
    """
    return os.path.join(sandbox_dir, "tmp")


def _start_script_path(sandbox_dir):
    return os.path.join(sandbox_dir,
                        "start.bat" if os.name == "nt" else "start.sh")


def _stop_script_path(sandbox_dir):
    return os.path.join(sandbox_dir,
                        "stop.bat" if os.name == "nt" else "stop.sh")


def _validate_port(port):
    try:
        port = int(port)
    except (TypeError, ValueError):
        raise Error("Invalid value for 'port': a port number is required.")
    if port < 1024 or port > 65535:
        raise Error("Invalid 'port' value {0}: it must be >= 1024 and <= 65535."
                    "".format(port))
    return port


def _existing_sandbox(port, options, need_cnf=False):
    """(port, sandbox_dir) of the sandbox at 'port', raising if there is none.

    'need_cnf' asks for its option file too, not just its directory.
    """
    port = _validate_port(port)
    _, sandbox_dir = _sandbox_dir(port, options)
    exists = os.path.isfile(_cnf_path(sandbox_dir)) if need_cnf \
        else os.path.isdir(sandbox_dir)
    if not exists:
        raise Error("There is no sandbox at '{0}'. Deploy it first."
                    "".format(sandbox_dir))
    return port, sandbox_dir


# --------------------------------------------------------------------------- #
# MariaDB tool resolution
# --------------------------------------------------------------------------- #
def _resolve_mariadbd(mariadbd_path):
    """Locate the MariaDB server binary and infer its basedir.

    ``mariadbd_path`` may be:
      - None: search the PATH for mariadbd/mysqld.
      - a path to the server binary itself.
      - a path to a MariaDB installation root (basedir).

    Returns a ``(mariadbd, basedir)`` tuple.
    """
    found = None
    if mariadbd_path:
        mariadbd_path = os.path.expanduser(mariadbd_path)
        if os.path.isdir(mariadbd_path):
            found = _find_in_dirs(_MYSQLD_NAMES, mariadbd_path)
            if not found:
                raise Error("Could not find a MariaDB server binary (mariadbd "
                            "or mysqld) under '{0}'.".format(mariadbd_path))
        elif _is_executable(mariadbd_path):
            found = mariadbd_path
        else:
            raise Error("Provided 'mariadbdPath' '{0}' is not an executable "
                        "file nor a directory.".format(mariadbd_path))
    else:
        found = _find_in_path(_MYSQLD_NAMES)
        if not found:
            raise Error("Could not find a MariaDB server binary (mariadbd or "
                        "mysqld) on the PATH. Use the 'mariadbdPath' option to "
                        "point to the binary or installation directory.")

    found = os.path.realpath(found)
    # basedir is the parent of the directory holding the binary (e.g. .../bin)
    basedir = os.path.abspath(os.path.join(found, os.pardir, os.pardir))
    return found, basedir


def _server_identity(server):
    """(vendor, version) of a server binary, from one ``--version`` run.

    The vendor is 'mariadb' or 'mysql': MariaDB servers include "MariaDB" in
    the banner, MySQL servers do not, and an unreadable banner counts as
    MariaDB, matching the plugin's historical behavior. The version is the
    token after "Ver" (e.g. '9.7.1', '11.4.2-MariaDB-debug'), vendor/build
    suffix included so the boilerplate key stays specific to the exact build
    (_short_version() gives the numeric form), or 'unknown'.
    """
    text = _server_version(server)
    vendor = _VENDOR_MYSQL if text and "mariadb" not in text.lower() \
        else _VENDOR_MARIADB
    match = re.search(r"Ver\s+([0-9][^\s]*)", text)
    return vendor, (match.group(1) if match else "unknown")


def _server_vendor(server):
    """The vendor of a server binary, see _server_identity()."""
    return _server_identity(server)[0]


def _supported_variables(server):
    """Set of option names (underscored) the server binary advertises.

    Parsed from ``server --no-defaults --verbose --help``. Best effort: an empty
    set is returned when the help output cannot be produced.
    """
    try:
        text = subprocess.run([server, "--no-defaults", "--verbose", "--help"],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              encoding="utf-8", errors="replace",
                              timeout=30).stdout
    except Exception as err:
        _log("warning", "Unable to read server variables: {0}".format(err))
        return set()
    return {m.group(1).replace("-", "_")
            for m in re.finditer(r"^\s*--([A-Za-z0-9-]+)", text, re.MULTILINE)}


def _innodb_opts(server, vendor):
    """InnoDB sizing options for both the boilerplate and the deployed cnf.

    The redo-log option name differs across server vendors/versions, so the
    correct one is chosen from the variables the server actually supports; the
    vendor is used only as a fallback when the probe yields nothing.
    """
    supported = _supported_variables(server)
    fallback = "innodb_redo_log_capacity" if vendor == _VENDOR_MYSQL \
        else "innodb_log_file_size"
    name = next((n for n in _INNODB_REDO_OPTS if n in supported), fallback)
    return dict(_INNODB_OPTS, **{name: _INNODB_REDO_OPTS[name]})


def _resolve_install_db(basedir):
    found = _find_in_dirs(_INSTALL_DB_NAMES, basedir) or \
        _find_in_path(_INSTALL_DB_NAMES)
    if not found:
        raise Error("Could not find the MariaDB data directory initialization "
                    "tool (mariadb-install-db or mysql_install_db). Make sure "
                    "it is on the PATH or in the MariaDB installation pointed "
                    "to by 'mariadbdPath'.")
    return found


def _shell_bundled_openssl():
    """Find the openssl CLI the shell bundles next to its own binary.

    A MariaDB build bundles the openssl CLI so sandbox SSL works out of the box
    without a (usable) system openssl (see src/CMakeLists.txt). This matters
    beyond Windows: macOS ships LibreSSL as its system openssl, whose 'req'
    command is incompatible with our cert-generation config, so the bundled
    OpenSSL must be preferred there too.

    The CLI is bundled next to the shell's bundled OpenSSL libraries, i.e. in the
    embedded interpreter's prefix directory (``INSTALL_LIBDIR`` -> ``bin`` on
    Windows, ``lib/mariadb-shell`` on macOS/Linux). Locate the shell home via
    MARIADB_SHELL_HOME, the interpreter prefix, and (on Windows, where the interpreter
    lives under ``<home>/lib/Python<X.Y>``) the grandparent of that prefix.
    Returns the openssl path or None.
    """
    homes = []
    env_home = os.environ.get("MARIADB_SHELL_HOME") or os.environ.get(
        "MYSQLSH_HOME"
    )
    if env_home:
        homes.append(env_home)
    if sys.prefix:
        # On macOS/Linux sys.prefix is the bundle dir itself (lib/mariadb-shell),
        # which holds the bundled openssl. On Windows it is
        # <mariadb_shell_home>/lib/Python<X.Y>.
        homes.append(sys.prefix)
        if sys.platform == "win32":
            homes.append(os.path.dirname(os.path.dirname(sys.prefix)))
    for home in homes:
        found = _find_in_dirs(_OPENSSL_NAMES, home)
        if found:
            return found
    return None


def _resolve_openssl(basedir, openssl_path):
    """Locate the openssl command used to generate the sandbox certificates.

    ``openssl_path`` may be None (search the MariaDB basedir then the PATH), a
    path to the openssl executable, or a directory to search. Returns the path
    or None when openssl cannot be found.
    """
    if openssl_path:
        openssl_path = os.path.expanduser(openssl_path)
        if os.path.isdir(openssl_path):
            found = _find_in_dirs(_OPENSSL_NAMES, openssl_path)
            if found:
                return found
        elif _is_executable(openssl_path):
            return openssl_path
        raise Error("Provided 'opensslPath' '{0}' does not point to an openssl "
                    "executable or a directory containing one.".format(
                        openssl_path))
    # Search order: the server basedir (a packaged install ships one), then the
    # openssl the shell bundles beside itself (Windows/MariaDB), then the PATH.
    return _find_in_dirs(_OPENSSL_NAMES, basedir) or \
        _shell_bundled_openssl() or \
        _find_in_path(_OPENSSL_NAMES)


def _server_version(mariadbd):
    """Return the version string reported by the server binary (best effort)."""
    try:
        return subprocess.run([mariadbd, "--version"], stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, encoding="utf-8",
                              errors="replace", timeout=30).stdout.strip()
    except Exception as err:
        _log("warning", "Unable to read server version: {0}".format(err))
        return ""


def _short_version(version):
    """Reduce a server version token to its numeric major.minor.patch prefix.

    '13.1.0-MariaDB-debug' -> '13.1.0'; '9.7.1' -> '9.7.1'. Values without a
    leading numeric version (e.g. 'unknown') are returned unchanged.
    """
    if not version:
        return version
    match = re.match(r"\d+(?:\.\d+){0,2}", version)
    return match.group(0) if match else version


def _version_token(vendor, version):
    """A filesystem-safe identifier for the server vendor + version.

    Used to key the per-version boilerplate directory so that mixing server
    versions or vendors (MariaDB vs MySQL) in the same sandbox base dir does
    not reuse an incompatible data directory. Example: ``mysql-9.7.1`` or
    ``mariadb-11.4.2-MariaDB``.
    """
    token = "{0}-{1}".format(vendor, version)
    return re.sub(r"[^A-Za-z0-9._-]", "_", token)


# --------------------------------------------------------------------------- #
# Version-keyed boilerplate (lazy deployment)
# --------------------------------------------------------------------------- #
def _boilerplate_dir(base, version):
    return os.path.join(base, "{0}-{1}".format(_BOILERPLATE_PREFIX, version))


def _boilerplate_base(sandbox_base):
    """Where the boilerplates live: $MARIADB_SANDBOX_BOILERPLATE_DIR when set,
    otherwise the sandbox base dir."""
    override = os.environ.get(BOILERPLATE_DIR_ENV)
    if override:
        return os.path.abspath(os.path.expanduser(override))
    return sandbox_base


def _init_data_dir(install_db, basedir, datadir, mariadbd, vendor, innodb_opts):
    """Bootstrap a data directory in 'datadir' for the given server vendor.

    A throw-away option file is used so the InnoDB system tablespace and redo
    log are created small (see _innodb_opts), which keeps later copies cheap.

    MariaDB is initialized with ``mariadb-install-db``; MySQL 8.0+ no longer
    ships an install-db tool, so the server bootstraps its own data directory
    with ``mysqld --initialize-insecure`` (which creates a passwordless
    ``root@localhost`` we then secure over the local socket).
    """
    cnf = os.path.join(os.path.dirname(datadir), "install.cnf")
    # The throwaway option file is only consumable by tools that read
    # --defaults-file: the POSIX mariadb-install-db shell script and mysqld
    # --initialize-insecure. The native Windows mariadb-install-db.exe does not,
    # so it is written on demand below rather than unconditionally.
    uses_cnf = True
    if vendor == _VENDOR_MYSQL:
        # The data directory already exists and is empty, which is what
        # --initialize-insecure requires.
        args = [mariadbd, "--defaults-file={0}".format(cnf),
                "--initialize-insecure", "--basedir={0}".format(basedir),
                "--datadir={0}".format(_cnf_value(datadir))]
    elif os.name == "nt":
        # Windows ships mariadb-install-db.exe as a native C++ tool (not the
        # POSIX shell script) with a different, smaller option set: it infers
        # basedir from its own location and does not call load_defaults(), so
        # --defaults-file / --basedir / --auth-root-authentication-method are
        # rejected as "unknown variable ...". It only needs the target datadir.
        #
        # Consequence: the InnoDB-sizing option file can't be applied at init
        # time here, so the boilerplate is created with default file sizes. The
        # sandbox's own my.cnf still carries the sizing (see _build_option_file),
        # so the redo log is resized to the small value on the sandbox's first
        # start; only the initial per-version boilerplate build is slightly
        # larger. There is no unix_socket auth on Windows, so root is already
        # created with (empty-password) native auth - the extra POSIX auth flag
        # is neither needed nor accepted.
        uses_cnf = False
        args = [install_db, "--datadir={0}".format(datadir)]
    else:
        args = [install_db, "--defaults-file={0}".format(cnf),
                "--basedir={0}".format(basedir),
                # Create root accounts that authenticate with a password (not the
                # unix socket plugin) so we can connect over the socket and set a
                # password.
                "--auth-root-authentication-method=normal",
                "--skip-test-db"]
    if uses_cnf:
        tmpdir = _tmpdir(os.path.dirname(datadir))
        os.makedirs(tmpdir, exist_ok=True)
        _write_option_file(cnf, {"mysqld": dict({
            "datadir": _cnf_value(datadir),
            "tmpdir": _cnf_value(tmpdir)}, **innodb_opts)})
    _log("debug", "Initializing data dir: {0}".format(" ".join(args)))
    result = subprocess.run(args, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, encoding="utf-8",
                            errors="replace")
    if uses_cnf:
        with contextlib.suppress(OSError):
            os.unlink(cnf)
    if result.returncode != 0:
        raise Error("Failed to initialize the {0} data directory.\n{1}"
                    "".format(_VENDOR_LABELS[vendor], result.stdout.strip()))


def _clean_boilerplate_data(datadir):
    """Drop runtime artifacts so a copied data dir starts fresh.

    'auto.cnf' holds the MySQL server UUID; removing it makes every sandbox
    copied from the boilerplate generate its own UUID on first start (so two
    sandboxes are not born sharing a server_uuid). MariaDB has no such file, so
    the removal is a no-op there.
    """
    for name in ("error.log", "mysqld.sock", "ib_buffer_pool", "auto.cnf"):
        with contextlib.suppress(OSError):
            os.unlink(os.path.join(datadir, name))


def _boilerplate_is_complete(bp_dir, version):
    """True if 'bp_dir' holds a finished boilerplate for this server version.

    The version stamp is written last, so its absence means the build did not
    finish. That case has to be told apart from a finished one by the stamp
    alone: an interrupted 'mariadb-install-db' leaves a data dir which looks
    populated - 'mysql', 'performance_schema' and 'sys' are all there - but
    holds no tables, and a sandbox copied from it dies on start with
    "Can't open and lock privilege tables: Table 'mysql.db' doesn't exist".
    """
    if _read_marker(bp_dir, "version.txt") != version:
        return False

    bp_data = _datadir(bp_dir)
    return os.path.isdir(bp_data) and bool(os.listdir(bp_data))


def _prepare_boilerplate(base, version, install_db, basedir, mariadbd, vendor,
                         innodb_opts):
    """Ensure a boilerplate data dir exists for the server 'version' (see
    _version_token()); return its path.

    The expensive bootstrap runs only the first time for a given server
    version. Subsequent deployments reuse the directory. Building is done in a
    temporary directory and atomically renamed into place, so a half-built
    boilerplate is never reused.
    """
    bp_dir = _boilerplate_dir(base, version)
    bp_data = _datadir(bp_dir)

    # Reuse only a complete boilerplate for this server version; anything else
    # is stale and gets removed rather than trusted.
    if _boilerplate_is_complete(bp_dir, version):
        _log("debug", "Reusing sandbox boilerplate at {0}".format(bp_dir))
        return bp_data
    if os.path.exists(bp_dir):
        shutil.rmtree(bp_dir, ignore_errors=True)

    print("Preparing sandbox boilerplate for {0} (one-time per version)..."
          "".format(version))
    tmp_dir = "{0}.tmp.{1}".format(bp_dir, os.getpid())
    shutil.rmtree(tmp_dir, ignore_errors=True)
    tmp_data = _datadir(tmp_dir)
    os.makedirs(tmp_data)
    try:
        _init_data_dir(install_db, basedir, tmp_data, mariadbd, vendor,
                       innodb_opts)
        _clean_boilerplate_data(tmp_data)
        with open(os.path.join(tmp_dir, "version.txt"), "w") as f:
            f.write(version)  # last: it marks the boilerplate complete
    except Exception:
        shutil.rmtree(tmp_dir, ignore_errors=True)
        raise

    # Publish atomically. If another deployment won the race, reuse theirs -
    # but only a complete one, so a leftover data dir cannot outrank the
    # boilerplate we just built.
    if _boilerplate_is_complete(bp_dir, version):
        shutil.rmtree(tmp_dir, ignore_errors=True)
    else:
        shutil.rmtree(bp_dir, ignore_errors=True)
        try:
            os.rename(tmp_dir, bp_dir)
        except OSError:
            shutil.rmtree(tmp_dir, ignore_errors=True)
            if not _boilerplate_is_complete(bp_dir, version):
                raise
    return bp_data


# --------------------------------------------------------------------------- #
# Option file handling
# --------------------------------------------------------------------------- #
def _parse_option_list(opts):
    """Turn ["key=value", "flag"] into an ordered dict of {key: value|None}."""
    result = {}
    if not opts:
        return result
    if isinstance(opts, str):
        opts = [opts]
    for item in opts:
        if "=" in item:
            key, _, value = item.partition("=")
            result[key.strip()] = value.strip()
        else:
            result[item.strip()] = None
    return result


def _write_option_file(path, sections):
    """Write a my.cnf style file. sections is {section: {key: value|None}}."""
    lines = []
    for section, opts in sections.items():
        lines.append("[{0}]".format(section))
        for key, value in opts.items():
            if value is None:
                lines.append(key)
            else:
                lines.append("{0} = {1}".format(key, value))
        lines.append("")
    with open(path, "w") as f:
        f.write("\n".join(lines))


# --------------------------------------------------------------------------- #
# SSL certificate generation
# --------------------------------------------------------------------------- #
def _ssl_paths(sandbox_dir):
    """Absolute paths of the SSL files for a sandbox, keyed by role."""
    return {role: os.path.join(sandbox_dir, name)
            for role, name in _SSL_FILES.items()}


def _client_is_mariadb():
    """Whether the running shell links MariaDB's Connector/C.

    'shell.version' reports the client library the shell was built against, e.g.
    "Ver 9.7.0 for Linux on aarch64 - for MariaDB 13.1.0-MariaDB (...)" versus
    "... - for MySQL 9.7.0 (...)". This decides which authentication plugin a
    MariaDB sandbox's root account can use: see _root_auth_plugin().
    """
    try:
        return " for MariaDB" in globals.shell.version
    except Exception as err:
        # Be conservative: assume the MariaDB connector (today's behaviour).
        _log("warning", "Unable to determine the shell's client library ({0}); "
             "assuming MariaDB.".format(err))
        return True


def _root_auth_plugin(vendor):
    """Authentication plugin to force on a sandbox's root accounts, or None.

    None means "leave the server default", which is what a matching client always
    wants: MySQL defaults to caching_sha2_password, MariaDB to
    mysql_native_password, and each vendor's own client speaks its default.

    The mismatch is a MySQL-built shell against a MariaDB sandbox. MySQL 9.0
    REMOVED mysql_native_password, client plugin included, so such a shell cannot
    authenticate to a stock MariaDB instance at all - it fails before running any
    SQL with "Authentication plugin 'mysql_native_password' cannot be loaded".
    caching_sha2_password is the one plugin both sides implement (MariaDB ships it
    built-in for MySQL compatibility), so force it there.
    """
    if vendor == _VENDOR_MARIADB and not _client_is_mariadb():
        return "caching_sha2_password"
    return None


def _bootstrap_root_auth_plugin(mariadbd, datadir, innodb_opts, auth_plugin):
    """Switch the root accounts to 'auth_plugin' before the server starts.

    The mismatch _root_auth_plugin() describes bites on the very first
    connection, so this cannot be done over SQL like the password is. 'mariadbd --bootstrap' runs statements
    against the data directory directly, with no client and no listening server.
    The accounts are left password-less (as mariadb-install-db leaves them);
    deploy() then sets the real password through the normal path, keeping this
    plugin.

    The privilege row is updated directly rather than with ALTER USER, because
    --bootstrap implies --skip-grant-tables, under which the server refuses
    account management ("ERROR 1290 ... running with the --skip-grant-tables
    option"). MariaDB keeps the plugin and credential in the JSON 'Priv' column of
    mysql.global_priv; an empty authentication_string means "no password", which
    is the state deploy() expects here.
    """
    statements = (
        "UPDATE mysql.global_priv SET Priv = JSON_SET(Priv, "
        "'$.plugin', '{0}', '$.authentication_string', '') "
        "WHERE User = 'root';\n".format(auth_plugin))

    tmpdir = _tmpdir(os.path.dirname(datadir))
    os.makedirs(tmpdir, exist_ok=True)
    args = [mariadbd, "--no-defaults", "--datadir={0}".format(datadir),
            "--tmpdir={0}".format(tmpdir)]
    args += ["--{0}={1}".format(k.replace("_", "-"), v)
             for k, v in innodb_opts.items()]
    args.append("--bootstrap")

    result = subprocess.run(args, input=statements, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, encoding="utf-8",
                            errors="replace")
    if result.returncode != 0:
        raise Error("Could not switch the sandbox root accounts to '{0}' "
                    "authentication:\n{1}".format(auth_plugin,
                                                    result.stdout.strip()))


def _mysql_auto_ssl_paths(datadir):
    """SSL files MySQL generated for itself while initializing 'datadir'.

    MySQL bootstraps a self-signed CA plus server and client certificates during
    'mysqld --initialize-insecure' (auto_generate_certs), so a MySQL sandbox needs
    no openssl CLI - the certificates arrive with the data directory. Returns the
    same {role: path} mapping _generate_ssl_certs() produces, or None when the set
    is incomplete (then the option file simply omits the ssl_* entries and the
    server falls back to those very defaults).
    """
    paths = {role: os.path.join(datadir, name)
             for role, name in _MYSQL_AUTO_SSL_FILES.items()}
    missing = [p for p in paths.values() if not os.path.isfile(p)]
    if missing:
        _log("warning", "The server did not auto-generate all sandbox SSL files "
             "(missing: {0}); the option file will not reference them."
             "".format(", ".join(os.path.basename(p) for p in missing)))
        return None
    return paths


def _run_openssl(openssl, args):
    """Run one openssl sub-command, raising with its output on failure."""
    result = subprocess.run([openssl] + args, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, encoding="utf-8",
                            errors="replace")
    if result.returncode != 0:
        raise Error("openssl '{0}' command failed:\n{1}".format(
            args[0] if args else "", result.stdout.strip()))


def _generate_ssl_certs(sandbox_dir, openssl):
    """Generate a private CA plus server and client certificates with openssl.

    The certificates are self-signed by a per-sandbox CA and are intended only
    for local sandbox testing. Returns the dict of {role: path} produced by
    _ssl_paths() so the option file can reference them.
    """
    paths = _ssl_paths(sandbox_dir)
    server_req = os.path.join(sandbox_dir, "server-req.pem")
    client_req = os.path.join(sandbox_dir, "client-req.pem")
    # A throw-away extensions file so the server certificate carries the local
    # host names in its SubjectAltName (required by VERIFY_IDENTITY clients).
    ext_path = os.path.join(sandbox_dir, "server-ext.cnf")
    with open(ext_path, "w") as f:
        f.write("subjectAltName = DNS:localhost, IP:127.0.0.1, "
                "IP:0:0:0:0:0:0:0:1\n")

    # A matching extensions file for the client leaf certificate. Its only job is
    # to carry at least one X.509 extension: any extension promotes the cert from
    # v1 to v3, and wolfSSL (bundled by MariaDB's Windows/tarball builds) rejects
    # v1 leaf certificates outright when negotiating TLS v1.2+. Without this the
    # client certificate is v1 and mutual-TLS (REQUIRE X509) against a wolfSSL
    # server fails. The server/CA certs are already v3 via their own extensions.
    client_ext_path = os.path.join(sandbox_dir, "client-ext.cnf")
    with open(client_ext_path, "w") as f:
        f.write("basicConstraints = CA:FALSE\n"
                "keyUsage = digitalSignature, keyEncipherment\n"
                "extendedKeyUsage = clientAuth\n"
                "subjectKeyIdentifier = hash\n")

    # A minimal config passed explicitly to every 'req' invocation. The 'req'
    # command (unlike genrsa/x509) always loads an openssl config, falling back
    # to the build's compiled-in OPENSSLDIR default. Bundled OpenSSL builds (e.g.
    # vcpkg's on Windows) bake in an OPENSSLDIR that does not exist on the target
    # machine, so 'req' aborts with "Can't open .../openssl.cnf for reading".
    # Supplying our own -config makes cert generation self-contained and
    # deterministic on every platform; -subj still provides the actual subject.
    #
    # x509_extensions marks the self-signed CA (the only 'req -x509' call) as a
    # real CA: without basicConstraints CA:TRUE, modern OpenSSL rejects it with
    # "invalid CA certificate" when verifying the leaf certs it signs. The stock
    # openssl.cnf sets this by default, which is what the config-less path was
    # implicitly relying on; it is only consulted for the -x509 (CA) invocation,
    # so it does not affect the server/client CSRs generated with the same file.
    req_cnf = os.path.join(sandbox_dir, "openssl-req.cnf")
    with open(req_cnf, "w") as f:
        f.write("[req]\n"
                "distinguished_name = req_distinguished_name\n"
                "prompt = no\n"
                "x509_extensions = v3_ca\n"
                "[req_distinguished_name]\n"
                "[v3_ca]\n"
                "basicConstraints = critical, CA:TRUE\n"
                "keyUsage = critical, keyCertSign, cRLSign\n"
                "subjectKeyIdentifier = hash\n")

    try:
        # 1) Private certificate authority.
        _run_openssl(openssl, ["genrsa", "-out", paths["ca_key"], "2048"])
        _run_openssl(openssl, [
            "req", "-new", "-x509", "-nodes", "-days", _SSL_DAYS,
            "-config", req_cnf,
            "-key", paths["ca_key"], "-out", paths["ca_cert"],
            "-subj", "/CN=MariaDB Sandbox CA"])

        # 2) Server certificate signed by the CA.
        _run_openssl(openssl, [
            "req", "-newkey", "rsa:2048", "-days", _SSL_DAYS, "-nodes",
            "-config", req_cnf,
            "-keyout", paths["server_key"], "-out", server_req,
            "-subj", "/CN=MariaDB Sandbox Server"])
        _run_openssl(openssl, [
            "x509", "-req", "-in", server_req, "-days", _SSL_DAYS,
            "-CA", paths["ca_cert"], "-CAkey", paths["ca_key"],
            "-set_serial", "01", "-extfile", ext_path,
            "-out", paths["server_cert"]])

        # 3) Client certificate signed by the CA.
        _run_openssl(openssl, [
            "req", "-newkey", "rsa:2048", "-days", _SSL_DAYS, "-nodes",
            "-config", req_cnf,
            "-keyout", paths["client_key"], "-out", client_req,
            "-subj", "/CN=MariaDB Sandbox Client"])
        _run_openssl(openssl, [
            "x509", "-req", "-in", client_req, "-days", _SSL_DAYS,
            "-CA", paths["ca_cert"], "-CAkey", paths["ca_key"],
            "-set_serial", "02", "-extfile", client_ext_path,
            "-out", paths["client_cert"]])
    finally:
        for tmp in (ext_path, client_ext_path, server_req, client_req,
                    req_cnf):
            with contextlib.suppress(OSError):
                os.unlink(tmp)

    # Keep private keys readable only by the owner; the server runs as the same
    # user, so it can still read them.
    _set_modes({path: 0o600 if role.endswith("_key") else 0o644
                for role, path in paths.items()})

    return paths


def _generate_caching_sha2_keypair(datadir, openssl):
    """Provision the RSA keypair MariaDB's caching_sha2_password plugin wants.

    MariaDB ships caching_sha2_password (for MySQL client compatibility) as a
    built-in plugin that initializes at startup and looks for private_key.pem /
    public_key.pem in the data directory. Unlike MySQL's --initialize, it does
    not auto-generate them, so every start logs a spurious
    "[ERROR] caching_sha2_password: failed to read private_key.pem". The sandbox
    creates root with mysql_native_password, so nothing depends on these keys;
    generating them silences the error and lets a user-created
    caching_sha2_password account authenticate over a non-TLS connection (the
    default for MariaDB on Windows). MySQL auto-generates the pair during
    --initialize-insecure, so this is MariaDB-only. Best-effort: the caller
    ignores failures (a missing keypair is only cosmetic).
    """
    private_key = os.path.join(datadir, "private_key.pem")
    public_key = os.path.join(datadir, "public_key.pem")
    _run_openssl(openssl, ["genrsa", "-out", private_key, "2048"])
    _run_openssl(openssl, ["rsa", "-in", private_key, "-pubout",
                           "-out", public_key])
    _set_modes({private_key: 0o600, public_key: 0o644})


def _set_modes(modes):
    """chmod each {path: mode}. POSIX only; best effort."""
    if os.name != "posix":
        return
    for path, mode in modes.items():
        with contextlib.suppress(OSError):
            os.chmod(path, mode)


# --------------------------------------------------------------------------- #
# Management scripts
# --------------------------------------------------------------------------- #
# Each sandbox gets a start/stop script that captures the absolute path to the
# server binary (and the option file) used at deployment time. This mirrors the
# behavior of the old AdminAPI/mysql_gadgets sandboxes: the instance can be
# started and stopped from a terminal even when the server binary is not on
# PATH, and the plugin itself uses these scripts so it does not need to
# re-resolve the binary on every start.
#
# The server requests a restart by exiting with status 16 (e.g. the RESTART
# statement). The start script re-launches it in that case, just like
# mysqld_safe, so RESTART works for sandboxes that are running under it.
_UNIX_START_SCRIPT = """\
#!/bin/bash
# MariaDB sandbox start script, generated by the mariadbSandbox shell plugin.
# The server binary path and the option file are captured here at deployment
# time so the sandbox can be started even when the binary is not on PATH.
MARIADBD={mariadbd}
DEFAULTS_FILE={cnf}
PID_FILE={pidfile}

echo 'Starting MariaDB sandbox...'
# The option file points the server at this private tmpdir; it must exist.
mkdir -p {tmpdir}
export MYSQLD_RESTART_EXIT=16
# Run the server (and its restart loop) detached in the background so the
# terminal returns immediately. Output is redirected and the job is disowned so
# it keeps running after this script (and the shell) exits; the server logs to
# the error log configured in the option file. The script writes the PID file
# itself (so stop works regardless of how the server manages its own pid-file)
# and refreshes it across restarts (the server exits with status 16 to request
# a restart, e.g. the RESTART statement).
(
  while true ; do
    "$MARIADBD" --defaults-file="$DEFAULTS_FILE" "$@" &
    server_pid=$!
    echo "$server_pid" > "$PID_FILE"
    wait "$server_pid"
    [ $? -ne "$MYSQLD_RESTART_EXIT" ] && break
  done
  # Only clean up the PID file while it still refers to *our* server. A
  # stop-then-start cycle races here: this loop is torn down asynchronously after
  # the server exits, so a new start script can already have written its own PID
  # before we get here. Removing that would strand the new instance - stop() and
  # kill() find no PID file and refuse to act ("Could not find the PID file ...
  # although a server is listening on it"). The window is wide enough to hit in
  # practice: the port is free (so the caller proceeds) before the server process
  # is fully reaped.
  if [ "$(cat "$PID_FILE" 2>/dev/null)" = "$server_pid" ] ; then
    rm -f "$PID_FILE"
  fi
) </dev/null >/dev/null 2>&1 &
disown
echo "MariaDB sandbox started in the background."
"""

# Graceful shutdown: a plain SIGTERM already triggers mariadbd's normal
# shutdown sequence (flush, close tables, clean InnoDB shutdown, exit status
# 0, so the start script's restart loop does not relaunch it) - no extra tool
# or credentials needed, unlike Windows below.
_UNIX_STOP_SCRIPT = """\
#!/bin/bash
# MariaDB sandbox stop script, generated by the mariadbSandbox shell plugin.
PID_FILE={pidfile}
echo 'Stopping MariaDB sandbox...'
if [ -f "$PID_FILE" ]; then
  kill "$(cat "$PID_FILE")"
else
  echo "PID file $PID_FILE not found; the sandbox may not be running." >&2
  exit 1
fi
"""

_WIN_START_SCRIPT = """\
@echo off
REM MariaDB sandbox start script, generated by the mariadbSandbox shell plugin.
REM Re-launch ourselves detached (start /B) on the first invocation so the
REM terminal returns immediately; the restart loop then runs in the background.
if "%~1"=="--_bg" goto :loop
echo Starting MariaDB sandbox...
REM The option file points the server at this private tmpdir; it must exist.
if not exist "{tmpdir}" mkdir "{tmpdir}"
start "MariaDB sandbox" /B "%~f0" --_bg
echo MariaDB sandbox started in the background.
EXIT /B 0

:loop
set MYSQLD_RESTART_EXIT=16
:while
"{mariadbd}" --defaults-file="{cnf}"
IF %ERRORLEVEL% EQU %MYSQLD_RESTART_EXIT% (
  goto :while
)
EXIT /B %ERRORLEVEL%
"""

# Windows has no signal that asks an arbitrary process to shut down cleanly (a
# plain kill there is a forceful TerminateProcess, unlike POSIX SIGTERM
# above), and a static script cannot use the shell's own session API the way
# stop_sandbox() does. So the script re-invokes the shell binary that
# generated it - always present, no extra tool to locate - to connect and run
# the SQL SHUTDOWN command. Extra arguments (e.g. --password=...) can be
# appended to the script invocation; with none given, the shell prompts for
# the password itself.
_WIN_STOP_SCRIPT = """\
@echo off
REM MariaDB sandbox stop script, generated by the mariadbSandbox shell plugin.
echo Stopping MariaDB sandbox...
"{shell}" --sql -h 127.0.0.1 -P {port} -u root --execute="SHUTDOWN" %*
"""


def _write_scripts(sandbox_dir, port, mariadbd):
    """Write the per-sandbox start/stop scripts capturing the resolved tools.

    The stop script performs a graceful shutdown: on POSIX by signaling the
    recorded PID (SIGTERM triggers mariadbd's normal shutdown sequence); on
    Windows, which has no such signal, by re-invoking the running shell binary
    to connect and run SQL SHUTDOWN.
    """
    cnf = _cnf_path(sandbox_dir)
    pidfile = _pid_path(sandbox_dir, port)
    tmpdir = _tmpdir(sandbox_dir)
    start_path = _start_script_path(sandbox_dir)
    stop_path = _stop_script_path(sandbox_dir)

    if os.name == "nt":
        start = _WIN_START_SCRIPT.format(mariadbd=os.path.normpath(mariadbd),
                                         cnf=os.path.normpath(cnf),
                                         tmpdir=os.path.normpath(tmpdir))
        stop = _WIN_STOP_SCRIPT.format(shell=os.path.normpath(executable),
                                       port=port)
    else:
        start = _UNIX_START_SCRIPT.format(mariadbd=shlex.quote(mariadbd),
                                          cnf=shlex.quote(cnf),
                                          pidfile=shlex.quote(pidfile),
                                          tmpdir=shlex.quote(tmpdir))
        stop = _UNIX_STOP_SCRIPT.format(pidfile=shlex.quote(pidfile))

    for path, content in ((start_path, start), (stop_path, stop)):
        with open(path, "w") as f:
            f.write(content)
        if os.name == "posix":
            os.chmod(path, 0o700)
    return start_path


def _option_name(key):
    """An option name in the form the server matches it in: '-' and '_' are
    the same, and a 'loose' prefix only changes how unknown options fail."""
    name = key.strip().replace("-", "_")
    return name[len("loose_"):] if name.startswith("loose_") else name


def _build_option_file(port, sandbox_dir, basedir, server_id, overrides,
                       innodb_opts, ssl_files=None, disable_ssl=False,
                       no_sync=False, x_plugin=False):
    """Build the option file for a raw (plain) MariaDB sandbox instance.

    No replication/GTID configuration is written: these are simple standalone
    instances. Callers that want replication can pass the relevant options
    through 'mariadbdOptions'.

    When 'ssl_files' is provided (the dict returned by _generate_ssl_certs),
    TLS is enabled by pointing the server at the CA/server certificate pair and
    the bundled clients at the CA/client pair.
    """
    mysqld = {
        "port": port,
        "basedir": _cnf_value(basedir),
        "datadir": _cnf_value(_datadir(sandbox_dir)),
        "log_error": _cnf_value(_error_log_path(sandbox_dir)),
        # A private tmpdir, see _tmpdir(). The start scripts create it.
        "tmpdir": _cnf_value(_tmpdir(sandbox_dir)),
        "performance_schema": "ON",
        # Loopback only: without it the server listens on every interface, and
        # the instance is meant for this machine. 'mariadbdOptions' can widen it
        # (bind_address=* or an address), as the test harness does.
        "bind_address": _LOOPBACK_ADDRESS,
    }
    # MySQL's X plugin has an address of its own. Not written for MariaDB, which
    # would log an "unknown variable" warning at every start even for 'loose_'.
    if x_plugin:
        mysqld["mysqlx_bind_address"] = _LOOPBACK_ADDRESS
    client = {"port": port, "user": "root", "protocol": "TCP"}
    if os.name == "nt":
        # The batch start script cannot easily capture the child PID, so the
        # server writes the pid-file itself (it runs in the foreground there).
        # There is no Unix socket: --socket would only name a named pipe, which
        # the server does not open without --named-pipe, so it is left out.
        mysqld["pid_file"] = _cnf_value(_pid_path(sandbox_dir, port))
    else:
        # The start script owns the PID file, so the server must NOT manage it
        # too (a pre-existing pid-file with a live PID makes it refuse to
        # start). The socket is the primary local endpoint.
        mysqld["socket"] = client["socket"] = _cnf_value(
            _socket_path(sandbox_dir))
    # InnoDB sizing must match the boilerplate the data dir was copied from.
    mysqld.update(innodb_opts)

    # server_id is only written when explicitly requested; a plain instance
    # does not need one.
    if server_id is not None:
        mysqld["server_id"] = server_id

    # Enable TLS with the generated certificates. These come before the
    # user-supplied overrides so a caller can still tune or replace them.
    if ssl_files:
        for section, peer in ((mysqld, "server"), (client, "client")):
            section["ssl_ca"] = _cnf_value(ssl_files["ca_cert"])
            section["ssl_cert"] = _cnf_value(ssl_files[peer + "_cert"])
            section["ssl_key"] = _cnf_value(ssl_files[peer + "_key"])
    elif disable_ssl:
        # MariaDB 12.x brings up TLS by default and aborts at startup when it
        # cannot load a key ("Failed to setup SSL: Unable to get private key"),
        # even with no ssl_* options configured, so a certless sandbox has to
        # turn it off explicitly. (MySQL auto-generates its own certs.)
        mysqld["skip_ssl"] = None

    # See NO_SYNC_ENV. Before the overrides, which can still turn it back off.
    if no_sync:
        mysqld["debug_no_sync"] = None

    # An override replaces the default it names, however it spells it.
    for key, value in (overrides or {}).items():
        name = _option_name(key)
        if name == "port":
            raise Error("Overriding the 'port' value is not supported. Use the "
                        "'port' argument to choose a different port.")
        for default in [k for k in mysqld if _option_name(k) == name]:
            del mysqld[default]
        mysqld[key] = value

    return {"mysqld": mysqld, "client": client}


# --------------------------------------------------------------------------- #
# Server connection helpers (uses the shell's own session API)
# --------------------------------------------------------------------------- #
def _open_root_session(port, sandbox_dir, password):
    """Open a classic session to the sandbox as root.

    On POSIX the local socket is used (matches root@localhost regardless of how
    the accounts were created); on Windows a TCP connection to 127.0.0.1 is
    used.

    Note: a MySQL 8.0+ server authenticates root with caching_sha2_password, so
    reaching it requires the shell's connector to have access to that client
    authentication plugin (configured at the shell level, e.g. via the
    connector's plugin directory); that is outside this plugin's scope.
    """
    conn = {"scheme": "mysql", "user": "root", "password": password or ""}
    if os.name == "posix":
        conn["socket"] = _socket_path(sandbox_dir)
    else:
        conn["host"] = "127.0.0.1"
        conn["port"] = port
    return globals.shell.open_session(conn)


def _identified_clause(auth_plugin):
    """The 'IDENTIFIED ...' clause for CREATE/ALTER USER, with a '?' placeholder.

    Without a forced plugin the server default applies ('IDENTIFIED BY'). With
    one, MariaDB's syntax is used ('IDENTIFIED VIA <plugin> USING PASSWORD(?)'),
    since the only case that forces a plugin is a MariaDB sandbox reached from a
    MySQL-built shell - see _root_auth_plugin(). Plain 'IDENTIFIED BY' would
    silently reset the account to the server default plugin (native password),
    locking such a shell out again.
    """
    if auth_plugin:
        return "IDENTIFIED VIA {0} USING PASSWORD(?)".format(auth_plugin)
    return "IDENTIFIED BY ?"


def _quote_host(host):
    """'host' as a quoted account host part."""
    return "'{0}'".format(host.replace("\\", "\\\\").replace("'", "''"))


@contextlib.contextmanager
def _no_binlog(session):
    """Keep the statements run inside out of the binary log."""
    session.run_sql("SET sql_log_bin = 0")
    try:
        yield
    finally:
        session.run_sql("SET sql_log_bin = 1")


def _set_root_password(session, password, auth_plugin=None):
    """Set the password for every root account, with binlog disabled.

    Every one, not just the local ones: mariadb-install-db also creates a
    password-less root@<machine hostname> (baked into the boilerplate, so the
    name of the machine it was bootstrapped on), which would otherwise stay open
    to anyone who can reach the port.
    """
    hosts = [row[0] for row in session.run_sql(
        "SELECT Host FROM mysql.user WHERE User = 'root'").fetch_all()]
    with _no_binlog(session):
        for host in hosts:
            session.run_sql(
                "ALTER USER 'root'@{0} {1}"
                "".format(_quote_host(host), _identified_clause(auth_plugin)),
                [password])


def _create_remote_root(session, allow_root_from, password, auth_plugin=None):
    """Create a root account reachable from the given host pattern."""
    # Restrict the pattern to characters valid in a host spec to avoid SQL
    # injection through the account name (the password is bound as a parameter).
    if not re.match(r"^[A-Za-z0-9_.%:\-]+$", allow_root_from):
        raise Error("Invalid 'allowRootFrom' value '{0}'.".format(
            allow_root_from))
    with _no_binlog(session):
        session.run_sql(
            "CREATE USER IF NOT EXISTS 'root'@'{0}' {1}"
            "".format(allow_root_from, _identified_clause(auth_plugin)),
            [password])
        session.run_sql(
            "GRANT ALL ON *.* TO 'root'@'{0}' WITH GRANT OPTION"
            "".format(allow_root_from))


# --------------------------------------------------------------------------- #
# Process control
# --------------------------------------------------------------------------- #
def _spawn_detached(args):
    """Launch a command detached so it outlives the shell."""
    kwargs = {"stdin": subprocess.DEVNULL,
              "stdout": subprocess.DEVNULL,
              "stderr": subprocess.DEVNULL}
    if os.name == "posix":
        kwargs["start_new_session"] = True
    else:
        # CREATE_NO_WINDOW, not DETACHED_PROCESS: the server runs under a hidden
        # console it (and its children) inherit, so no terminal window pops up.
        # DETACHED_PROCESS gives the process no console at all, which makes the
        # console-mode mariadbd.exe allocate its own *visible* one. The server
        # still outlives the shell - it has its own console, independent of the
        # shell's.
        kwargs["creationflags"] = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    subprocess.Popen(args, **kwargs)


def _start_server(sandbox_dir):
    """Start the sandbox by running its generated start script, detached.

    The server runs under the start script's restart loop (mysqld_safe-like).
    Process control goes through the PID file, which the script writes on
    POSIX and the server on Windows.
    """
    script = _start_script_path(sandbox_dir)
    _spawn_detached(["cmd", "/c", script] if os.name == "nt" else [script])


def _start_and_wait(port, sandbox_dir, label, timeout):
    """Start the sandbox and wait until it accepts connections."""
    _start_server(sandbox_dir)
    if not _wait_until(lambda: _is_ready(port, sandbox_dir), timeout):
        raise Error("Timeout waiting for the {0} sandbox on port {1} to "
                    "start. Check the error log at '{2}'.".format(
                        label, port, _error_log_path(sandbox_dir)))


def _read_pid(sandbox_dir, port):
    try:
        with open(_pid_path(sandbox_dir, port)) as f:
            return int(f.readline().strip())
    except (OSError, ValueError):
        return None


def _signal_pid(pid, sig):
    """Send 'sig' to 'pid', ignoring a process that is already gone."""
    if os.name == "posix":
        with contextlib.suppress(ProcessLookupError):
            os.kill(pid, sig)
    else:
        # On Windows only forceful termination is available without extra deps.
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(pid)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


# --------------------------------------------------------------------------- #
# Public operations
# --------------------------------------------------------------------------- #
def create_sandbox(port, options):
    port = _validate_port(port)
    password = options.get("password")
    if password is None:
        raise Error("Missing root password for the deployed instance. Provide "
                    "the 'password' option.")

    timeout = int(options.get("timeout", SANDBOX_TIMEOUT))
    allow_root_from = options.get("allowRootFrom", "%")
    overrides = _parse_option_list(options.get("mariadbdOptions"))
    server_id = options.get("serverId")
    server_id = int(server_id) if server_id is not None else None
    # The TLS default depends on the server vendor, so it is resolved after the
    # binary is identified (see below); None here means "not explicitly set".
    ssl = options.get("ssl")

    base, sandbox_dir = _sandbox_dir(port, options)
    if os.path.isdir(sandbox_dir) and os.listdir(sandbox_dir):
        raise Error("The sandbox directory '{0}' already exists and is not "
                    "empty.".format(sandbox_dir))

    mariadbd, basedir = _resolve_mariadbd(options.get("mariadbdPath"))
    vendor, version = _server_identity(mariadbd)
    label = _VENDOR_LABELS[vendor]

    if ssl is None:
        # TLS is on by default, except for MariaDB on Windows: its bundled
        # wolfSSL cannot complete a handshake with the shell's client, so a TLS
        # sandbox could not even be deployed (MARIADB_PORT.md §11.5). An explicit
        # ssl:true is honored, for a MariaDB built against OpenSSL.
        ssl = not (os.name == "nt" and vendor == _VENDOR_MARIADB)
        if not ssl:
            print("Note: deploying without TLS. MariaDB's bundled wolfSSL on "
                  "Windows cannot negotiate TLS with the shell's client; pass "
                  "the 'ssl' option as true to force certificate generation "
                  "(only useful with a MariaDB built against OpenSSL).")

    innodb_opts = _innodb_opts(mariadbd, vendor)
    # MariaDB is initialized with mariadb-install-db; MySQL 8.0+ dropped that
    # tool and bootstraps its data directory with 'mysqld --initialize-insecure'
    # instead, so no separate installer needs to be resolved for it.
    install_db = _resolve_install_db(basedir) \
        if vendor == _VENDOR_MARIADB else None
    _log("info", "Using {0} server '{1}' (basedir '{2}'), installer '{3}'."
         "".format(vendor, mariadbd, basedir, install_db or "mysqld "
                   "--initialize-insecure"))

    # MariaDB only: MySQL generates its own certificates and RSA keypair while
    # initializing the data directory, so it needs no openssl CLI - and
    # requiring one would break deployment where none is usable (macOS ships
    # LibreSSL, whose 'req' rejects our config). Resolved up-front so that a
    # deployment with SSL fails before doing any work when it is missing; the
    # keypair alone is best effort.
    openssl = None
    if vendor == _VENDOR_MARIADB:
        try:
            openssl = _resolve_openssl(basedir, options.get("opensslPath"))
        except Error:
            if ssl:
                raise
        if ssl and not openssl:
            raise Error("Could not find the 'openssl' tool needed to generate "
                        "the sandbox SSL certificates. Install OpenSSL and make "
                        "sure it is on the PATH, point to it with the "
                        "'opensslPath' option, or disable SSL by setting the "
                        "'ssl' option to false.")

    if _port_open(port):
        raise Error("Port '{0}' is already in use; cannot deploy the sandbox."
                    "".format(port))

    print("Deploying new {0} sandbox instance on port {1}...".format(
        label, port))

    # 1) Ensure the per-version boilerplate exists (bootstrapped only once),
    #    then deploy this instance by copying its initialized data directory.
    boilerplate_data = _prepare_boilerplate(
        _boilerplate_base(base), _version_token(vendor, version), install_db,
        basedir, mariadbd, vendor, innodb_opts)

    datadir = _datadir(sandbox_dir)
    try:
        os.makedirs(sandbox_dir)
        shutil.copytree(boilerplate_data, datadir)
    except OSError as err:
        shutil.rmtree(sandbox_dir, ignore_errors=True)
        raise Error("Unable to deploy the sandbox data directory '{0}': {1}"
                    "".format(datadir, err))

    root_auth_plugin = _root_auth_plugin(vendor)
    ssl_files = None
    try:
        # 2) The SSL certificates (CA + server + client) the instance serves
        #    TLS with. MySQL generated its own set into the data directory;
        #    MariaDB gets one per sandbox, so each has its own CA.
        if ssl:
            if vendor == _VENDOR_MARIADB:
                print("Generating SSL certificates for the sandbox...")
                ssl_files = _generate_ssl_certs(sandbox_dir, openssl)
            else:
                ssl_files = _mysql_auto_ssl_paths(datadir)

        # 2b) MariaDB only, best effort: see _generate_caching_sha2_keypair().
        if openssl:
            try:
                _generate_caching_sha2_keypair(datadir, openssl)
            except Exception as err:
                _log("warning", "Could not provision the caching_sha2_password "
                     "RSA keypair (non-fatal): {0}".format(err))
        elif vendor == _VENDOR_MARIADB:
            _log("debug", "openssl not found; skipping caching_sha2_password "
                 "keypair generation (the server logs a harmless startup "
                 "error).")

        # 2c) Offline, before the first connection: see _root_auth_plugin().
        if root_auth_plugin:
            _log("info", "Switching the sandbox root accounts to '{0}': this "
                 "shell links the MySQL client library, which cannot "
                 "authenticate with MariaDB's default plugin.".format(
                     root_auth_plugin))
            _bootstrap_root_auth_plugin(mariadbd, datadir, innodb_opts,
                                        root_auth_plugin)
    except Exception:
        shutil.rmtree(sandbox_dir, ignore_errors=True)
        raise

    # 3) Write the option file and the start/stop scripts (the scripts capture
    #    the resolved server binary and the option file path).
    _write_option_file(
        _cnf_path(sandbox_dir),
        _build_option_file(port, sandbox_dir, basedir, server_id, overrides,
                           innodb_opts, ssl_files,
                           disable_ssl=(not ssl and vendor == _VENDOR_MARIADB),
                           no_sync=(vendor == _VENDOR_MARIADB
                                    and bool(os.environ.get(NO_SYNC_ENV))),
                           x_plugin=(vendor == _VENDOR_MYSQL)))
    _write_scripts(sandbox_dir, port, mariadbd)
    # Record the vendor and version so later operations can report them.
    _write_vendor(sandbox_dir, vendor)
    _write_version(sandbox_dir, version)

    # 4) Start the server (root still has no password at this point).
    print("Starting {0} sandbox instance...".format(label))
    _start_and_wait(port, sandbox_dir, label, timeout)

    # 5) Set the root password and, optionally, create a remote root account.
    session = _open_root_session(port, sandbox_dir, "")
    try:
        _set_root_password(session, password, root_auth_plugin)
        if allow_root_from:
            _create_remote_root(session, allow_root_from, password,
                                root_auth_plugin)
    finally:
        session.close()

    _log("warning", "Sandbox instances are only suitable for local testing "
         "and are not accessible from external networks.")
    print("\nInstance localhost:{0} successfully deployed and started.".format(
        port))
    if ssl_files:
        print("SSL is enabled; the CA certificate is at '{0}'.".format(
            ssl_files["ca_cert"]))
    print("Use shell.connect('root@localhost:{0}') to connect to it.".format(
        port))


def start_sandbox(port, options):
    port, sandbox_dir = _existing_sandbox(port, options, need_cnf=True)
    timeout = int(options.get("timeout", SANDBOX_TIMEOUT))
    if _port_open(port):
        raise Error("Port '{0}' is already in use; the sandbox may already be "
                    "running.".format(port))

    # The sandbox is normally started through the script written at deploy time,
    # which captures the server binary path, so it works even without the binary
    # on PATH. Regenerate the scripts only if a different binary was requested
    # or the sandbox predates script generation.
    if options.get("mariadbdPath") or \
            not os.path.isfile(_start_script_path(sandbox_dir)):
        mariadbd, _ = _resolve_mariadbd(options.get("mariadbdPath"))
        _write_scripts(sandbox_dir, port, mariadbd)
        # Keep the recorded vendor/version in sync with the (possibly new)
        # binary.
        vendor, version = _server_identity(mariadbd)
        _write_vendor(sandbox_dir, vendor)
        _write_version(sandbox_dir, version)

    label = _vendor_label(sandbox_dir)
    print("Starting {0} sandbox instance on port {1}...".format(label, port))
    _start_and_wait(port, sandbox_dir, label, timeout)
    print("Instance localhost:{0} successfully started.".format(port))


def stop_sandbox(port, options):
    port, sandbox_dir = _existing_sandbox(port, options)
    timeout = int(options.get("timeout", SANDBOX_TIMEOUT))
    label = _vendor_label(sandbox_dir)

    # Close the active shell session if it points at this very sandbox.
    try:
        active = globals.shell.get_session()
        if active is not None:
            uri = globals.shell.parse_uri(active.uri)
            if uri.get("host") in ("localhost", "127.0.0.1") and \
                    int(uri.get("port", 0)) == port:
                print("Closing the active session to the sandbox being stopped.")
                active.close()
    except Exception:
        pass

    if not _port_open(port):
        print("{0} sandbox on port {1} is already stopped.".format(label, port))
        return

    print("Stopping {0} sandbox instance on port {1}...".format(label, port))
    if os.name == "posix":
        pid = _read_pid(sandbox_dir, port)
        if pid is None:
            raise Error("Could not find the PID file for the sandbox on port "
                        "{0}. Use kill() to terminate it.".format(port))
        # SIGTERM triggers a clean shutdown (same as mariadb-admin).
        _signal_pid(pid, signal.SIGTERM)
    else:
        # Windows has no signal asking a process to shut down cleanly (os.kill()
        # there is TerminateProcess), so ask the server over the client protocol.
        # The server closes the connection while shutting down, which is the
        # expected outcome, not a failure.
        session = _open_root_session(port, sandbox_dir,
                                     options.get("password", ""))
        with contextlib.suppress(Exception):
            session.run_sql("SHUTDOWN")
        with contextlib.suppress(Exception):
            session.close()

    if not _wait_until(lambda: not _is_listening_anywhere(port, sandbox_dir),
                       timeout):
        raise Error("Timeout waiting for the {0} sandbox on port {1} to "
                    "stop. You may need to use kill().".format(label, port))

    # The port frees up before the start script's background restart loop is torn
    # down, and that loop removes the PID file on its way out. Wait for it, so a
    # start() right after this call cannot have its fresh PID file deleted by the
    # loop we just ended: the loop only removes the file while it still holds its
    # own PID, but that check is not atomic with the removal. Best effort - if the
    # file lingers (e.g. the server was started outside the script), carry on; a
    # stale PID is handled by the callers.
    _wait_until(lambda: _read_pid(sandbox_dir, port) is None, 5)

    print("Instance localhost:{0} successfully stopped.".format(port))


def kill_sandbox(port, options):
    port, sandbox_dir = _existing_sandbox(port, options)
    label = _vendor_label(sandbox_dir)

    pid = _read_pid(sandbox_dir, port)
    if pid is None:
        if _port_open(port):
            raise Error("Could not find the PID file for the sandbox on port "
                        "{0}, although a server is listening on it.".format(
                            port))
        print("{0} sandbox on port {1} is not running.".format(label, port))
        return

    print("Killing {0} sandbox instance on port {1} (pid {2})...".format(
        label, port, pid))
    _signal_pid(pid, signal.SIGKILL if os.name == "posix" else None)

    _wait_until(lambda: not _port_open(port), 10)
    with contextlib.suppress(OSError):
        os.unlink(_pid_path(sandbox_dir, port))
    print("Instance localhost:{0} successfully killed.".format(port))


def delete_sandbox(port, options):
    port, sandbox_dir = _existing_sandbox(port, options)
    label = _vendor_label(sandbox_dir)

    if _port_open(port):
        raise Error("The {0} sandbox on port {1} is running. Stop it "
                    "before deleting it.".format(label, port))

    print("Deleting {0} sandbox instance on port {1}...".format(label, port))
    last_err = None
    for attempt in range(1, 6):
        try:
            shutil.rmtree(sandbox_dir)
            break
        except OSError as err:
            last_err = err
            time.sleep(attempt)
    else:
        raise Error("Unable to delete the {0} sandbox folder '{1}': {2}"
                    "".format(label, sandbox_dir, last_err))

    # Remove the socket file too if it lives outside the sandbox dir (i.e. the
    # short-path fallback under the temp directory was used). Windows has no
    # socket file (see _build_option_file), so this only applies elsewhere.
    if os.name != "nt":
        sock = _socket_path(sandbox_dir)
        if not sock.startswith(sandbox_dir):
            with contextlib.suppress(OSError):
                os.unlink(sock)

    print("Instance localhost:{0} successfully deleted.".format(port))


def _describe_server(port, options, recorded, from_binary):
    """What sandbox_vendor()/sandbox_version() report.

    With a 'port', the value recorded for that sandbox, recorded(sandbox_dir),
    when there is one. Otherwise - no port, or a sandbox from before the value
    was recorded - from_binary(server) for the server binary a new deployment
    would use, honoring 'mariadbdPath'; None when there is no such binary.
    """
    if port is not None:
        _, sandbox_dir = _existing_sandbox(port, options, need_cnf=True)
        value = recorded(sandbox_dir)
        if value:
            return value
    try:
        mariadbd, _ = _resolve_mariadbd(options.get("mariadbdPath"))
    except Error:
        return None
    return from_binary(mariadbd)


def sandbox_vendor(port=None, options=None):
    """Return the server vendor label ('MariaDB' or 'MySQL'), or None.

    With no 'port', reports the vendor a new deployment would use: the server
    found on the PATH, or the one pointed to by the 'mariadbdPath' option when
    provided. With a 'port', reports the vendor recorded for the existing
    sandbox at that port (falling back to the binary the 'mariadbdPath' option
    or PATH resolves to when the sandbox predates vendor recording).

    Returns None when no server binary can be found and the vendor therefore
    cannot be determined. Still raises when a 'port' is given but no sandbox
    exists there.
    """
    return _describe_server(
        port, options or {},
        lambda d: _VENDOR_LABELS.get(_read_marker(d, "vendor")),
        lambda m: _VENDOR_LABELS[_server_vendor(m)])


def sandbox_version(port=None, options=None):
    """Return the server version as major.minor.patch (e.g. '9.7.1'), or None.

    The vendor (MariaDB vs MySQL) is available through sandbox_vendor(), so any
    vendor/build suffix is dropped here and only the numeric version reported.

    With no 'port', reports the version a new deployment would use: the server
    found on the PATH, or the one pointed to by the 'mariadbdPath' option when
    provided. With a 'port', reports the version recorded for the existing
    sandbox at that port (falling back to the binary the 'mariadbdPath' option
    or PATH resolves to when the sandbox predates version recording).

    Returns None when no server binary can be found and the version therefore
    cannot be determined. Still raises when a 'port' is given but no sandbox
    exists there.
    """
    return _describe_server(
        port, options or {},
        lambda d: _short_version(_read_marker(d, "version")),
        lambda m: _short_version(_server_identity(m)[1]))


def sandbox_path(port, path_id=None, options=None):
    """Return a filesystem path belonging to an existing sandbox instance.

    Args:
        port: The port of the sandbox instance.
        path_id: Which path to return. One of:
            None or "" - the sandbox home directory.
            "config" - the sandbox's my.cnf option file.
            "error" - the sandbox's error log file.
        options: dict, may contain 'sandboxDir'.

    Raises when no sandbox exists at 'port', or when 'path_id' is not one of
    the recognized values.
    """
    _, sandbox_dir = _existing_sandbox(port, options or {})

    path_id = (path_id or "").strip().lower()
    if path_id == "":
        return sandbox_dir
    if path_id == "config":
        return _cnf_path(sandbox_dir)
    if path_id == "error":
        return _error_log_path(sandbox_dir)
    raise Error("Unknown path identifier '{0}'. Use 'config' or 'error', or "
                "omit it for the sandbox home directory.".format(path_id))
