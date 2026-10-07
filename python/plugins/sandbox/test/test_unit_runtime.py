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
# _is_socket_listening / _configured_socket / _is_ready
# --------------------------------------------------------------------------- #
@pytest.fixture
def short_dir():
    """A directory short enough to bind a Unix socket in (104 bytes on macOS,
    which pytest's tmp_path can exceed)."""
    import shutil
    import tempfile
    path = tempfile.mkdtemp(prefix="sbx", dir="/tmp" if os.path.isdir("/tmp")
                            else None)
    yield path
    shutil.rmtree(path, ignore_errors=True)


def _unix_listener(path):
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(path)
    srv.listen(1)
    return srv


def _tcp_listener():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    return srv, srv.getsockname()[1]


@pytest.mark.skipif(os.name != "posix", reason="Unix sockets are POSIX only")
def test_is_socket_listening(sandboxlib, short_dir):
    path = os.path.join(short_dir, "s.sock")
    # no file yet - the window between the server's TCP and socket listeners
    assert sandboxlib._is_socket_listening(path) is False
    srv = _unix_listener(path)
    try:
        assert sandboxlib._is_socket_listening(path) is True
    finally:
        srv.close()
    # the file is left behind, but nothing accepts on it any more
    assert sandboxlib._is_socket_listening(path) is False


def test_configured_socket(sandboxlib, short_dir):
    sandboxlib._write_option_file(
        sandboxlib._cnf_path(short_dir),
        {"mysqld": {"port": 1, "skip_ssl": None, "socket": "/x/m.sock"},
         "client": {"socket": "/y/c.sock"}})
    assert sandboxlib._configured_socket(short_dir) == "/x/m.sock"

    sandboxlib._write_option_file(sandboxlib._cnf_path(short_dir),
                                  {"mysqld": {"port": 1}})
    assert sandboxlib._configured_socket(short_dir) is None
    assert sandboxlib._configured_socket(
        os.path.join(short_dir, "missing")) is None


@pytest.mark.skipif(os.name != "posix", reason="Unix sockets are POSIX only")
def test_is_ready_waits_for_the_socket(sandboxlib, short_dir, monkeypatch):
    sock_path = os.path.join(short_dir, "m.sock")
    sandboxlib._write_option_file(sandboxlib._cnf_path(short_dir),
                                  {"mysqld": {"socket": sock_path}})
    srv, port = _tcp_listener()
    # is_listening() probes "localhost", which may resolve to ::1 first
    monkeypatch.setattr(sandboxlib, "is_listening",
                        lambda host, p, timeout=1.0: p == port)
    try:
        # TCP port open, socket not created yet: not ready
        assert sandboxlib._is_ready(port, short_dir) is False
        usrv = _unix_listener(sock_path)
        try:
            assert sandboxlib._is_ready(port, short_dir) is True
        finally:
            usrv.close()
    finally:
        srv.close()


def test_is_ready_without_a_configured_socket(sandboxlib, short_dir,
                                              monkeypatch):
    sandboxlib._write_option_file(sandboxlib._cnf_path(short_dir),
                                  {"mysqld": {"port": 1}})
    monkeypatch.setattr(sandboxlib, "is_listening", lambda *a, **k: True)
    assert sandboxlib._is_ready(1, short_dir) is True
    monkeypatch.setattr(sandboxlib, "is_listening", lambda *a, **k: False)
    assert sandboxlib._is_ready(1, short_dir) is False


def _append_mysqld_lines(sandboxlib, sandbox_dir, *lines):
    # the way other tools (e.g. the test suite's change_sandbox_conf()) edit
    # the option file after deploy
    with open(sandboxlib._cnf_path(sandbox_dir), "a") as f:
        f.write("\n[mysqld]\n" + "\n".join(lines) + "\n")


@pytest.mark.parametrize("lines, vendor, expected", [
    ([], "mysql", False),
    (["skip_networking"], "mariadb", True),
    (["skip-networking = ON"], "mysql", True),
    (["skip-networking = OFF"], "mysql", False),
    (["skip-grant-tables = ON"], "mysql", True),
    (["skip-grant-tables"], "mysql", True),
    (["skip-grant-tables = OFF"], "mysql", False),
    # MariaDB keeps networking on without the grant tables
    (["skip-grant-tables = ON"], "mariadb", False),
    # no vendor marker: the MariaDB fallback
    (["skip-grant-tables = ON"], None, False),
])
def test_networking_disabled(sandboxlib, short_dir, lines, vendor, expected):
    sandboxlib._write_option_file(sandboxlib._cnf_path(short_dir),
                                  {"mysqld": {"port": 1}})
    _append_mysqld_lines(sandboxlib, short_dir, *lines)
    if vendor:
        sandboxlib._write_vendor(short_dir, vendor)
    assert sandboxlib._networking_disabled(short_dir) is expected


@pytest.mark.skipif(os.name != "posix", reason="Unix sockets are POSIX only")
def test_is_ready_without_networking(sandboxlib, short_dir, monkeypatch):
    # MySQL started with --skip-grant-tables never opens its TCP port
    sock_path = os.path.join(short_dir, "m.sock")
    sandboxlib._write_option_file(sandboxlib._cnf_path(short_dir),
                                  {"mysqld": {"socket": sock_path}})
    _append_mysqld_lines(sandboxlib, short_dir, "skip-grant-tables = ON")
    sandboxlib._write_vendor(short_dir, "mysql")
    monkeypatch.setattr(sandboxlib, "is_listening", lambda *a, **k: False)

    assert sandboxlib._is_ready(1, short_dir) is False
    usrv = _unix_listener(sock_path)
    try:
        assert sandboxlib._is_ready(1, short_dir) is True
    finally:
        usrv.close()


@pytest.mark.skipif(os.name != "posix", reason="Unix sockets are POSIX only")
def test_is_listening_anywhere(sandboxlib, short_dir, monkeypatch):
    sock_path = os.path.join(short_dir, "m.sock")
    sandboxlib._write_option_file(sandboxlib._cnf_path(short_dir),
                                  {"mysqld": {"socket": sock_path}})

    monkeypatch.setattr(sandboxlib, "is_listening", lambda *a, **k: True)
    assert sandboxlib._is_listening_anywhere(1, short_dir) is True

    # port closed (or never opened), socket still accepting: not stopped yet
    monkeypatch.setattr(sandboxlib, "is_listening", lambda *a, **k: False)
    usrv = _unix_listener(sock_path)
    try:
        assert sandboxlib._is_listening_anywhere(1, short_dir) is True
    finally:
        usrv.close()
    assert sandboxlib._is_listening_anywhere(1, short_dir) is False


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
# _boilerplate_is_complete / _prepare_boilerplate
# --------------------------------------------------------------------------- #
def _make_boilerplate(sandboxlib, base, version, stamp=None, tables=True):
    """Create a boilerplate dir for 'version'; return its path.

    'stamp' is the version written to version.txt (None writes no stamp, which
    is what an interrupted build leaves behind), 'tables' whether the data dir
    holds anything at all.
    """
    bp_dir = sandboxlib._boilerplate_dir(base, version)
    data = sandboxlib._datadir(bp_dir)
    os.makedirs(data)
    if tables:
        os.makedirs(os.path.join(data, "mysql"))
        open(os.path.join(data, "ibdata1"), "w").close()
    if stamp is not None:
        with open(os.path.join(bp_dir, "version.txt"), "w") as f:
            f.write(stamp)
    return bp_dir


def test_boilerplate_is_complete(sandboxlib, tmp_path):
    base = str(tmp_path)
    _make_boilerplate(sandboxlib, base, "mariadb-12.3.2", stamp="mariadb-12.3.2")
    assert sandboxlib._boilerplate_is_complete(
        sandboxlib._boilerplate_dir(base, "mariadb-12.3.2"),
        "mariadb-12.3.2") is True


def test_boilerplate_without_version_stamp_is_incomplete(sandboxlib, tmp_path):
    # An interrupted 'mariadb-install-db' leaves the data dir looking populated
    # but holding no tables; the missing stamp is the only way to tell.
    base = str(tmp_path)
    _make_boilerplate(sandboxlib, base, "mariadb-12.3.2", stamp=None)
    assert not sandboxlib._boilerplate_is_complete(
        sandboxlib._boilerplate_dir(base, "mariadb-12.3.2"), "mariadb-12.3.2")


def test_boilerplate_of_another_version_is_incomplete(sandboxlib, tmp_path):
    base = str(tmp_path)
    _make_boilerplate(sandboxlib, base, "mariadb-12.3.2", stamp="mariadb-11.4.2")
    assert not sandboxlib._boilerplate_is_complete(
        sandboxlib._boilerplate_dir(base, "mariadb-12.3.2"), "mariadb-12.3.2")


def test_boilerplate_with_empty_data_dir_is_incomplete(sandboxlib, tmp_path):
    base = str(tmp_path)
    _make_boilerplate(sandboxlib, base, "mariadb-12.3.2",
                      stamp="mariadb-12.3.2", tables=False)
    assert not sandboxlib._boilerplate_is_complete(
        sandboxlib._boilerplate_dir(base, "mariadb-12.3.2"), "mariadb-12.3.2")


def test_prepare_boilerplate_rebuilds_over_unstamped_dir(sandboxlib, tmp_path,
                                                         monkeypatch):
    """A stale, unstamped boilerplate must not survive a rebuild.

    It used to: the stale dir was left in place, and the freshly built one was
    then discarded as if a concurrent deployment had won a race - so every
    sandbox was copied from a data dir with no tables in it.
    """
    base = str(tmp_path)
    version = "mariadb-12.3.2"
    _make_boilerplate(sandboxlib, base, version, stamp=None)

    def fake_init(install_db, basedir, datadir, mariadbd, vendor, innodb_opts):
        open(os.path.join(datadir, "built-here"), "w").close()

    monkeypatch.setattr(sandboxlib, "_init_data_dir", fake_init)

    bp_data = sandboxlib._prepare_boilerplate(base, version, "install-db",
                                              "basedir", "mariadbd", "mariadb",
                                              {})

    assert os.path.exists(os.path.join(bp_data, "built-here"))
    assert sandboxlib._boilerplate_is_complete(
        sandboxlib._boilerplate_dir(base, version), version)


def test_prepare_boilerplate_reuses_complete_dir(sandboxlib, tmp_path,
                                                 monkeypatch):
    base = str(tmp_path)
    version = "mariadb-12.3.2"
    _make_boilerplate(sandboxlib, base, version, stamp=version)

    def fail_init(*_args, **_kwargs):
        raise AssertionError("the complete boilerplate should be reused")

    monkeypatch.setattr(sandboxlib, "_init_data_dir", fail_init)

    bp_data = sandboxlib._prepare_boilerplate(base, version, "install-db",
                                              "basedir", "mariadbd", "mariadb",
                                              {})
    assert bp_data == sandboxlib._datadir(
        sandboxlib._boilerplate_dir(base, version))


# --------------------------------------------------------------------------- #
# _set_root_password
# --------------------------------------------------------------------------- #
def test_set_root_password_sql_sequence(sandboxlib, session):
    # What mariadb-install-db leaves: the local accounts plus one for the name
    # of the machine the boilerplate was bootstrapped on.
    session.rows["SELECT Host FROM mysql.user WHERE User = 'root'"] = [
        ("localhost",), ("127.0.0.1",), ("::1",), ("build-host",)]
    sandboxlib._set_root_password(session, "secret")

    sqls = [sql for sql, _ in session.calls]
    assert sqls[0] == "SELECT Host FROM mysql.user WHERE User = 'root'"
    assert sqls[1] == "SET sql_log_bin = 0"
    assert sqls[-1] == "SET sql_log_bin = 1"

    altered_hosts = []
    for sql, args in session.calls:
        if sql.startswith("ALTER USER"):
            assert args == ["secret"]
            altered_hosts.append(sql)
    # Every root account gets the password, the hostname one included.
    assert len(altered_hosts) == 4
    for host in ("localhost", "127.0.0.1", "::1", "build-host"):
        assert any("'root'@'{0}'".format(host) in s for s in altered_hosts)


def test_set_root_password_quotes_host(sandboxlib, session):
    session.rows["SELECT Host FROM mysql.user WHERE User = 'root'"] = [
        ("o'host",)]
    sandboxlib._set_root_password(session, "secret")
    assert any(sql.startswith("ALTER USER 'root'@'o''host' ")
               for sql, _ in session.calls)


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
