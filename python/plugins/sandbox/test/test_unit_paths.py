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

"""Tests for path/port helpers and default sandbox directory resolution."""

import os

import pytest


# --------------------------------------------------------------------------- #
# _validate_port
# --------------------------------------------------------------------------- #
def test_validate_port_accepts_valid(sandboxlib):
    assert sandboxlib._validate_port(3310) == 3310
    assert sandboxlib._validate_port("3310") == 3310


@pytest.mark.parametrize("bad", [1023, 0, -1, 65536, 70000])
def test_validate_port_rejects_out_of_range(sandboxlib, bad):
    with pytest.raises(sandboxlib.Error):
        sandboxlib._validate_port(bad)


@pytest.mark.parametrize("bad", ["abc", None, "12x"])
def test_validate_port_rejects_non_numeric(sandboxlib, bad):
    with pytest.raises(sandboxlib.Error):
        sandboxlib._validate_port(bad)


# --------------------------------------------------------------------------- #
# path helpers
# --------------------------------------------------------------------------- #
def test_path_helpers(sandboxlib):
    sb = os.path.join("base", "3310")
    assert sandboxlib._cnf_path(sb) == os.path.join(sb, "my.cnf")
    assert sandboxlib._socket_path(sb) == os.path.join(sb, "mysqld.sock")
    assert sandboxlib._pid_path(sb, 3310) == os.path.join(sb, "3310.pid")
    assert sandboxlib._datadir(sb) == os.path.join(sb, "sandboxdata")


def test_boilerplate_dir(sandboxlib):
    got = sandboxlib._boilerplate_dir("/base", "11.4.2-MariaDB")
    assert got == os.path.join("/base", "myboilerplate-11.4.2-MariaDB")


# --------------------------------------------------------------------------- #
# _socket_path (with short-path fallback for deep sandbox dirs)
# --------------------------------------------------------------------------- #
def test_socket_path_uses_natural_path_when_short(sandboxlib):
    sb = "/tmp/sbx/3310"
    assert sandboxlib._socket_path(sb) == os.path.join(sb, "mysqld.sock")


def test_socket_path_falls_back_when_too_long(sandboxlib):
    deep = "/tmp/" + "x" * 200 + "/3310"
    sock = sandboxlib._socket_path(deep)
    # Must not be the (too long) natural path, must stay within the OS limit,
    # and must end in .sock.
    assert sock != os.path.join(deep, "mysqld.sock")
    assert len(sock) <= sandboxlib._MAX_SOCKET_PATH
    assert sock.endswith(".sock")


def test_socket_path_fallback_is_deterministic(sandboxlib):
    deep = "/tmp/" + "y" * 200 + "/3310"
    assert sandboxlib._socket_path(deep) == sandboxlib._socket_path(deep)
    other = "/tmp/" + "z" * 200 + "/3310"
    assert sandboxlib._socket_path(deep) != sandboxlib._socket_path(other)


# --------------------------------------------------------------------------- #
# _sandbox_dir / default_sandbox_base_dir
# --------------------------------------------------------------------------- #
def test_sandbox_dir_with_explicit_option(sandboxlib, tmp_path):
    base, sandbox_dir = sandboxlib._sandbox_dir(3310, {"sandboxDir": str(tmp_path)})
    assert base == os.path.abspath(str(tmp_path))
    assert sandbox_dir == os.path.join(base, "3310")


def test_sandbox_dir_expands_user(sandboxlib):
    base, sandbox_dir = sandboxlib._sandbox_dir(3310, {"sandboxDir": "~/sbx"})
    assert base == os.path.join(os.path.expanduser("~"), "sbx")
    assert sandbox_dir.endswith(os.path.join("sbx", "3310"))


def test_default_base_dir_uses_shell_option(sandboxlib, shell, tmp_path):
    shell.options["sandboxDir"] = str(tmp_path / "configured")
    base, _ = sandboxlib._sandbox_dir(3310, {})
    assert base == os.path.abspath(str(tmp_path / "configured"))


def test_default_base_dir_fallback(sandboxlib, shell):
    # No sandboxDir option set -> default under the home directory.
    assert shell.options == {}
    result = sandboxlib.default_sandbox_base_dir()
    assert result.startswith(os.path.expanduser("~"))
    assert result.endswith(os.path.join("mariadb-shell", "sandboxes"))
