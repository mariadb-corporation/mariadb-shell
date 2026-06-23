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

"""Tests for runtime helpers: networking, waiting, pid files, cleanup and the
session-based password/account operations."""

import os
import socket

import pytest


# --------------------------------------------------------------------------- #
# is_listening
# --------------------------------------------------------------------------- #
def test_is_listening_true_for_open_socket(sandboxlib):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    port = srv.getsockname()[1]
    try:
        assert sandboxlib.is_listening("127.0.0.1", port) is True
    finally:
        srv.close()
    # Once closed, nothing should be accepting connections there.
    assert sandboxlib.is_listening("127.0.0.1", port) is False


# --------------------------------------------------------------------------- #
# _wait_until (time.sleep patched out so the test is instant)
# --------------------------------------------------------------------------- #
def test_wait_until_returns_true_immediately(sandboxlib, monkeypatch):
    monkeypatch.setattr(sandboxlib.time, "sleep", lambda *_: None)
    assert sandboxlib._wait_until(lambda: True, timeout=5) is True


def test_wait_until_becomes_true(sandboxlib, monkeypatch):
    monkeypatch.setattr(sandboxlib.time, "sleep", lambda *_: None)
    state = {"n": 0}

    def predicate():
        state["n"] += 1
        return state["n"] >= 3

    assert sandboxlib._wait_until(predicate, timeout=10) is True


def test_wait_until_times_out(sandboxlib, monkeypatch):
    monkeypatch.setattr(sandboxlib.time, "sleep", lambda *_: None)
    assert sandboxlib._wait_until(lambda: False, timeout=3) is False


# --------------------------------------------------------------------------- #
# _read_pid
# --------------------------------------------------------------------------- #
def test_read_pid_reads_value(sandboxlib, tmp_path):
    sb = str(tmp_path)
    with open(sandboxlib._pid_path(sb, 3310), "w") as f:
        f.write("12345\n")
    assert sandboxlib._read_pid(sb, 3310) == 12345


def test_read_pid_missing_returns_none(sandboxlib, tmp_path):
    assert sandboxlib._read_pid(str(tmp_path), 3310) is None


def test_read_pid_garbage_returns_none(sandboxlib, tmp_path):
    sb = str(tmp_path)
    with open(sandboxlib._pid_path(sb, 3310), "w") as f:
        f.write("not-a-pid\n")
    assert sandboxlib._read_pid(sb, 3310) is None


# --------------------------------------------------------------------------- #
# _clean_boilerplate_data
# --------------------------------------------------------------------------- #
def test_clean_boilerplate_data_removes_runtime_files(sandboxlib, tmp_path):
    datadir = str(tmp_path)
    for name in ("error.log", "mysqld.sock", "ib_buffer_pool"):
        open(os.path.join(datadir, name), "w").close()
    keep = os.path.join(datadir, "ibdata1")
    open(keep, "w").close()

    sandboxlib._clean_boilerplate_data(datadir)

    for name in ("error.log", "mysqld.sock", "ib_buffer_pool"):
        assert not os.path.exists(os.path.join(datadir, name))
    assert os.path.exists(keep)  # data files are preserved


def test_clean_boilerplate_data_tolerates_missing(sandboxlib, tmp_path):
    # Must not raise when the runtime files are absent.
    sandboxlib._clean_boilerplate_data(str(tmp_path))


# --------------------------------------------------------------------------- #
# _set_root_password
# --------------------------------------------------------------------------- #
def test_set_root_password_sql_sequence(sandboxlib, session):
    sandboxlib._set_root_password(session, "secret")

    sqls = [sql for sql, _ in session.calls]
    assert sqls[0] == "SET sql_log_bin = 0"
    assert sqls[-1] == "SET sql_log_bin = 1"

    altered_hosts = []
    for sql, args in session.calls:
        if sql.startswith("ALTER USER"):
            assert "IF EXISTS" in sql
            assert args == ["secret"]
            altered_hosts.append(sql)
    # localhost, 127.0.0.1 and ::1 are all updated.
    assert len(altered_hosts) == 3
    assert any("'root'@'localhost'" in s for s in altered_hosts)
    assert any("'root'@'127.0.0.1'" in s for s in altered_hosts)
    assert any("'root'@'::1'" in s for s in altered_hosts)


# --------------------------------------------------------------------------- #
# _create_remote_root
# --------------------------------------------------------------------------- #
def test_create_remote_root_creates_and_grants(sandboxlib, session):
    sandboxlib._create_remote_root(session, "%", "secret")

    sqls = [sql for sql, _ in session.calls]
    assert sqls[0] == "SET sql_log_bin = 0"
    assert sqls[-1] == "SET sql_log_bin = 1"
    assert any(s.startswith("CREATE USER IF NOT EXISTS 'root'@'%'")
               for s in sqls)
    assert any(s.startswith("GRANT ALL ON *.* TO 'root'@'%'") for s in sqls)
    # The password is always passed as a bound parameter, never inlined.
    for sql, args in session.calls:
        if sql.startswith("CREATE USER"):
            assert args == ["secret"]


@pytest.mark.parametrize("bad", ["ho st", "a'b", "x;y", "a)b", "1=1 OR x"])
def test_create_remote_root_rejects_unsafe_host(sandboxlib, session, bad):
    with pytest.raises(sandboxlib.Error):
        sandboxlib._create_remote_root(session, bad, "secret")
    # Nothing should have been executed against the server.
    assert session.calls == []
