# Copyright (c) 2026, MariaDB Corporation.
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

"""Tests for MariaDB tool resolution and version detection.

These build small executable shell scripts and so are POSIX-only.
"""

import os
import stat

import pytest

pytestmark = pytest.mark.skipif(os.name != "posix",
                                reason="requires POSIX executable scripts")


def _make_exe(path, body="#!/bin/sh\nexit 0\n"):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(body)
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP |
             stat.S_IXOTH)
    return path


# --------------------------------------------------------------------------- #
# _resolve_mariadbd
# --------------------------------------------------------------------------- #
def test_resolve_mariadbd_from_binary_file(sandboxlib, tmp_path):
    binp = _make_exe(str(tmp_path / "bin" / "mariadbd"))
    found, basedir = sandboxlib._resolve_mariadbd(binp)
    assert os.path.realpath(found) == os.path.realpath(binp)
    assert basedir == os.path.realpath(str(tmp_path))


def test_resolve_mariadbd_from_install_dir(sandboxlib, tmp_path):
    _make_exe(str(tmp_path / "bin" / "mariadbd"))
    found, basedir = sandboxlib._resolve_mariadbd(str(tmp_path))
    assert found.endswith(os.path.join("bin", "mariadbd"))
    assert basedir == os.path.realpath(str(tmp_path))


def test_resolve_mariadbd_legacy_mysqld_name(sandboxlib, tmp_path):
    _make_exe(str(tmp_path / "bin" / "mysqld"))
    found, _ = sandboxlib._resolve_mariadbd(str(tmp_path))
    assert found.endswith(os.path.join("bin", "mysqld"))


def test_resolve_mariadbd_bad_path(sandboxlib, tmp_path):
    with pytest.raises(sandboxlib.Error):
        sandboxlib._resolve_mariadbd(str(tmp_path / "does-not-exist"))


def test_resolve_mariadbd_empty_install_dir(sandboxlib, tmp_path):
    with pytest.raises(sandboxlib.Error):
        sandboxlib._resolve_mariadbd(str(tmp_path))


# --------------------------------------------------------------------------- #
# _resolve_install_db
# --------------------------------------------------------------------------- #
def test_resolve_install_db_in_scripts(sandboxlib, tmp_path):
    tool = _make_exe(str(tmp_path / "scripts" / "mariadb-install-db"))
    found = sandboxlib._resolve_install_db(str(tmp_path))
    assert os.path.realpath(found) == os.path.realpath(tool)


def test_resolve_install_db_not_found(sandboxlib, tmp_path, monkeypatch):
    # Empty basedir and nothing on PATH -> error.
    monkeypatch.setattr(sandboxlib, "_find_in_path", lambda names: None)
    with pytest.raises(sandboxlib.Error):
        sandboxlib._resolve_install_db(str(tmp_path))


# --------------------------------------------------------------------------- #
# _resolve_openssl
# --------------------------------------------------------------------------- #
def test_resolve_openssl_from_binary_file(sandboxlib, tmp_path):
    tool = _make_exe(str(tmp_path / "bin" / "openssl"))
    assert sandboxlib._resolve_openssl("/unused", tool) == tool


def test_resolve_openssl_from_directory(sandboxlib, tmp_path):
    tool = _make_exe(str(tmp_path / "bin" / "openssl"))
    found = sandboxlib._resolve_openssl("/unused", str(tmp_path))
    assert os.path.realpath(found) == os.path.realpath(tool)


def test_resolve_openssl_in_basedir(sandboxlib, tmp_path):
    tool = _make_exe(str(tmp_path / "bin" / "openssl"))
    found = sandboxlib._resolve_openssl(str(tmp_path), None)
    assert os.path.realpath(found) == os.path.realpath(tool)


def test_resolve_openssl_bad_explicit_path(sandboxlib, tmp_path):
    with pytest.raises(sandboxlib.Error, match="opensslPath"):
        sandboxlib._resolve_openssl("/unused", str(tmp_path / "nope"))


def test_resolve_openssl_not_found(sandboxlib, tmp_path, monkeypatch):
    monkeypatch.setattr(sandboxlib, "_find_in_path", lambda names: None)
    assert sandboxlib._resolve_openssl(str(tmp_path), None) is None


# --------------------------------------------------------------------------- #
# _generate_ssl_certs
# --------------------------------------------------------------------------- #
@pytest.mark.skipif(__import__("shutil").which("openssl") is None,
                    reason="requires the openssl command-line tool")
def test_generate_ssl_certs_produces_verifiable_certs(sandboxlib, tmp_path):
    import shutil
    import subprocess

    openssl = shutil.which("openssl")
    paths = sandboxlib._generate_ssl_certs(str(tmp_path), openssl)

    # All six files exist and the temporary CSR/ext files were cleaned up.
    for role in ("ca_cert", "ca_key", "server_cert", "server_key",
                 "client_cert", "client_key"):
        assert os.path.isfile(paths[role])
    for tmp in ("server-req.pem", "client-req.pem", "server-ext.cnf"):
        assert not os.path.exists(str(tmp_path / tmp))

    # Server and client certificates verify against the generated CA.
    for cert in (paths["server_cert"], paths["client_cert"]):
        result = subprocess.run(
            [openssl, "verify", "-CAfile", paths["ca_cert"], cert],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert result.returncode == 0, result.stdout.decode("utf-8", "replace")

    # Private keys are not world/group readable.
    for role in ("ca_key", "server_key", "client_key"):
        assert (os.stat(paths[role]).st_mode & 0o077) == 0


# --------------------------------------------------------------------------- #
# _version_token
# --------------------------------------------------------------------------- #
def test_version_token_parses_version(sandboxlib, tmp_path):
    body = ("#!/bin/sh\n"
            "echo 'mariadbd Ver 11.4.2-MariaDB-log for osx10.21 on arm64'\n")
    binp = _make_exe(str(tmp_path / "mariadbd"), body)
    # The token is prefixed with the server vendor so different vendors never
    # share a boilerplate directory.
    assert sandboxlib._version_token(binp, "mariadb") == \
        "mariadb-11.4.2-MariaDB-log"


def test_version_token_prefixes_vendor(sandboxlib, tmp_path):
    body = ("#!/bin/sh\n"
            "echo 'mysqld  Ver 9.7.1 for macos15 on arm64 (MySQL "
            "Community Server - GPL)'\n")
    binp = _make_exe(str(tmp_path / "mysqld"), body)
    assert sandboxlib._version_token(binp, "mysql") == "mysql-9.7.1"


def test_version_token_unknown_when_no_match(sandboxlib, tmp_path):
    binp = _make_exe(str(tmp_path / "mariadbd"), "#!/bin/sh\necho 'nope'\n")
    assert sandboxlib._version_token(binp, "mariadb") == "mariadb-unknown"


def test_version_token_sanitizes_unsafe_chars(sandboxlib, tmp_path):
    body = "#!/bin/sh\necho 'mariadbd Ver 11.4/2 weird'\n"
    binp = _make_exe(str(tmp_path / "mariadbd"), body)
    token = sandboxlib._version_token(binp, "mariadb")
    assert "/" not in token
    assert token == "mariadb-11.4_2"


# --------------------------------------------------------------------------- #
# _server_vendor / _supported_variables / _innodb_opts
# --------------------------------------------------------------------------- #
def test_server_vendor_detects_mariadb(sandboxlib, tmp_path):
    body = ("#!/bin/sh\n"
            "echo 'mariadbd  Ver 13.1.0-MariaDB-debug for osx10.21 on arm64'\n")
    binp = _make_exe(str(tmp_path / "mariadbd"), body)
    assert sandboxlib._server_vendor(binp) == "mariadb"


def test_server_vendor_detects_mysql(sandboxlib, tmp_path):
    body = ("#!/bin/sh\n"
            "echo 'mysqld  Ver 9.7.1 for macos15 on arm64 (MySQL "
            "Community Server - GPL)'\n")
    binp = _make_exe(str(tmp_path / "mysqld"), body)
    assert sandboxlib._server_vendor(binp) == "mysql"


def test_server_vendor_defaults_to_mariadb_when_unknown(sandboxlib, tmp_path):
    # Empty/unreadable version banner falls back to the historical behavior.
    binp = _make_exe(str(tmp_path / "mysqld"), "#!/bin/sh\nexit 1\n")
    assert sandboxlib._server_vendor(binp) == "mariadb"


def test_supported_variables_parses_help(sandboxlib, tmp_path):
    body = ("#!/bin/sh\n"
            "echo '  --innodb-redo-log-capacity=#'\n"
            "echo '  --performance-schema'\n")
    binp = _make_exe(str(tmp_path / "mysqld"), body)
    variables = sandboxlib._supported_variables(binp)
    assert "innodb_redo_log_capacity" in variables
    assert "performance_schema" in variables


def test_innodb_opts_uses_redo_capacity_for_mysql(sandboxlib, tmp_path):
    body = "#!/bin/sh\necho '  --innodb-redo-log-capacity=#'\n"
    binp = _make_exe(str(tmp_path / "mysqld"), body)
    opts = sandboxlib._innodb_opts(binp, "mysql")
    assert "innodb_redo_log_capacity" in opts
    assert "innodb_log_file_size" not in opts
    assert opts["innodb_data_file_path"]


def test_innodb_opts_uses_log_file_size_for_mariadb(sandboxlib, tmp_path):
    body = "#!/bin/sh\necho '  --innodb-log-file-size=#'\n"
    binp = _make_exe(str(tmp_path / "mariadbd"), body)
    opts = sandboxlib._innodb_opts(binp, "mariadb")
    assert "innodb_log_file_size" in opts
    assert "innodb_redo_log_capacity" not in opts


def test_innodb_opts_falls_back_to_family_default(sandboxlib, tmp_path):
    # No redo variable advertised: the family decides the fallback.
    binp = _make_exe(str(tmp_path / "mysqld"), "#!/bin/sh\necho 'nothing'\n")
    assert "innodb_redo_log_capacity" in sandboxlib._innodb_opts(binp, "mysql")
    assert "innodb_log_file_size" in sandboxlib._innodb_opts(binp, "mariadb")


# --------------------------------------------------------------------------- #
# sandbox_vendor
# --------------------------------------------------------------------------- #
def _make_mysql_install(tmp_path):
    """A fake server installation whose mysqld reports a MySQL banner."""
    _make_exe(str(tmp_path / "bin" / "mysqld"),
              "#!/bin/sh\necho 'mysqld  Ver 9.7.1 for macos15 on arm64 "
              "(MySQL Community Server - GPL)'\n")
    return str(tmp_path)


def test_sandbox_vendor_no_port_detects_from_binary(sandboxlib, tmp_path):
    install = _make_mysql_install(tmp_path)
    assert sandboxlib.sandbox_vendor(
        options={"mariadbdPath": install}) == "MySQL"


def test_sandbox_vendor_by_port_reads_marker(sandboxlib, tmp_path):
    base = tmp_path / "sandboxes"
    sbx = base / "3311"
    sbx.mkdir(parents=True)
    (sbx / "my.cnf").write_text("[mysqld]\n")
    sandboxlib._write_vendor(str(sbx), "mysql")
    assert sandboxlib.sandbox_vendor(
        3311, {"sandboxDir": str(base)}) == "MySQL"


def test_sandbox_vendor_by_port_without_marker_uses_binary(sandboxlib,
                                                           tmp_path):
    # An existing sandbox that predates vendor recording: fall back to the
    # server binary (here provided through mariadbdPath) instead of guessing.
    install = _make_mysql_install(tmp_path)
    base = tmp_path / "sandboxes"
    sbx = base / "3311"
    sbx.mkdir(parents=True)
    (sbx / "my.cnf").write_text("[mysqld]\n")  # no 'vendor' marker
    assert sandboxlib.sandbox_vendor(
        3311, {"sandboxDir": str(base), "mariadbdPath": install}) == "MySQL"


def test_sandbox_vendor_by_port_missing_sandbox_raises(sandboxlib, tmp_path):
    with pytest.raises(sandboxlib.Error, match="no sandbox"):
        sandboxlib.sandbox_vendor(3311, {"sandboxDir": str(tmp_path)})


def test_sandbox_vendor_no_binary_returns_none(sandboxlib, tmp_path,
                                               monkeypatch):
    # No server on the PATH and no mariadbdPath: the vendor cannot be
    # determined, so None is returned instead of raising.
    monkeypatch.setattr(sandboxlib, "_find_in_path", lambda names: None)
    assert sandboxlib.sandbox_vendor() is None


# --------------------------------------------------------------------------- #
# sandbox_version
# --------------------------------------------------------------------------- #
def test_sandbox_version_no_port_detects_from_binary(sandboxlib, tmp_path):
    install = _make_mysql_install(tmp_path)
    assert sandboxlib.sandbox_version(
        options={"mariadbdPath": install}) == "9.7.1"


def test_sandbox_version_by_port_reads_marker(sandboxlib, tmp_path):
    base = tmp_path / "sandboxes"
    sbx = base / "3311"
    sbx.mkdir(parents=True)
    (sbx / "my.cnf").write_text("[mysqld]\n")
    sandboxlib._write_version(str(sbx), "9.7.1")
    assert sandboxlib.sandbox_version(
        3311, {"sandboxDir": str(base)}) == "9.7.1"


def test_sandbox_version_strips_vendor_suffix(sandboxlib, tmp_path):
    # A MariaDB build token is reduced to major.minor.patch on output.
    base = tmp_path / "sandboxes"
    sbx = base / "3311"
    sbx.mkdir(parents=True)
    (sbx / "my.cnf").write_text("[mysqld]\n")
    sandboxlib._write_version(str(sbx), "13.1.0-MariaDB-debug")
    assert sandboxlib.sandbox_version(
        3311, {"sandboxDir": str(base)}) == "13.1.0"


def test_short_version(sandboxlib):
    assert sandboxlib._short_version("13.1.0-MariaDB-debug") == "13.1.0"
    assert sandboxlib._short_version("9.7.1") == "9.7.1"
    assert sandboxlib._short_version("11.4-log") == "11.4"
    assert sandboxlib._short_version("unknown") == "unknown"


def test_sandbox_version_by_port_without_marker_uses_binary(sandboxlib,
                                                            tmp_path):
    install = _make_mysql_install(tmp_path)
    base = tmp_path / "sandboxes"
    sbx = base / "3311"
    sbx.mkdir(parents=True)
    (sbx / "my.cnf").write_text("[mysqld]\n")  # no 'version' marker
    assert sandboxlib.sandbox_version(
        3311, {"sandboxDir": str(base), "mariadbdPath": install}) == "9.7.1"


def test_sandbox_version_by_port_missing_sandbox_raises(sandboxlib, tmp_path):
    with pytest.raises(sandboxlib.Error, match="no sandbox"):
        sandboxlib.sandbox_version(3311, {"sandboxDir": str(tmp_path)})


def test_sandbox_version_no_binary_returns_none(sandboxlib, tmp_path,
                                                monkeypatch):
    monkeypatch.setattr(sandboxlib, "_find_in_path", lambda names: None)
    assert sandboxlib.sandbox_version() is None
