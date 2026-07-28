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

"""End-to-end integration tests for the sandbox plugin.

Unlike the test_unit_*.py files, these load the plugin in the real (built)
MySQL/MariaDB Shell and deploy actual MariaDB sandbox instances, so they need:

  * a built ``mysqlsh`` binary -- set MARIADB_SANDBOX_TEST_MYSQLSH, or the
    repository's build/bin/mysqlsh is used automatically when present;
  * an unpacked MariaDB server -- set MARIADB_SANDBOX_TEST_BASEDIR to its root
    (e.g. /path/to/mariadb-13.1.0-osx10.21-arm64).

When those are not available the whole module is skipped. Run only these with:

    python3 runtests.py -m integration
"""

import glob
import os
import shutil
import socket
import subprocess
import tempfile

import pytest

_PLUGIN_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_PLUGIN_ROOT)))
_DEFAULT_MYSQLSH = os.path.join(_REPO_ROOT, "build", "bin", "mysqlsh")

MYSQLSH = os.environ.get("MARIADB_SANDBOX_TEST_MYSQLSH") or (
    _DEFAULT_MYSQLSH if os.path.isfile(_DEFAULT_MYSQLSH) else None)
BASEDIR = os.environ.get("MARIADB_SANDBOX_TEST_BASEDIR")

pytestmark = pytest.mark.skipif(
    not (MYSQLSH and os.path.isfile(MYSQLSH) and BASEDIR and
         os.path.isdir(BASEDIR)),
    reason="set MARIADB_SANDBOX_TEST_MYSQLSH (or build the shell) and "
           "MARIADB_SANDBOX_TEST_BASEDIR to run the integration tests")


# --------------------------------------------------------------------------- #
# Helpers
# --------------------------------------------------------------------------- #
def _free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def _listening(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=1):
            return True
    except OSError:
        return False


class _Driver:
    """Runs plugin operations through the real shell with an isolated config."""

    def __init__(self, config_home, sandbox_dir):
        self.config_home = config_home
        self.sandbox_dir = sandbox_dir
        self._deployed = set()

    def run(self, code, timeout=300):
        env = dict(os.environ)
        env["MYSQLSH_USER_CONFIG_HOME"] = self.config_home
        env["MYSQLSH_TERM_COLOR_MODE"] = "nocolor"
        proc = subprocess.run(
            [MYSQLSH, "--quiet-start=2", "--py", "-e", code],
            capture_output=True, text=True, env=env, timeout=timeout)
        return proc.stdout + proc.stderr

    def _opts(self, extra=None):
        opts = {"sandboxDir": self.sandbox_dir}
        if extra:
            opts.update(extra)
        return opts

    def deploy(self, port, **extra):
        self._deployed.add(port)
        opts = self._opts({"password": "rootpass", "mariadbdPath": BASEDIR})
        opts.update(extra)
        return self.run("sandbox.deploy({0}, {1!r})".format(port, opts))

    def op(self, name, port, extra=None):
        return self.run("sandbox.{0}({1}, {2!r})".format(
            name, port, self._opts(extra)))

    def query(self, port, sql="select 1"):
        return self.run(
            "s = shell.open_session('root:rootpass@localhost:{0}'); "
            "print('QUERY_OK', s.run_sql({1!r}).fetch_one()[0]); "
            "s.close()".format(port, sql))

    def cleanup(self):
        for port in self._deployed:
            try:
                self.op("kill", port)
                self.op("delete", port)
            except Exception:
                pass


def _short_tmpdir(prefix):
    # Keep the path short: a sandbox's Unix socket lives under it and must fit
    # in the OS sun_path limit (~104 chars on macOS), which pytest's deep
    # tmp_path easily exceeds.
    base = "/tmp" if os.path.isdir("/tmp") else tempfile.gettempdir()
    return tempfile.mkdtemp(prefix=prefix, dir=base)


@pytest.fixture(scope="session")
def plugin_config_home():
    """An isolated MYSQLSH_USER_CONFIG_HOME with the plugin installed."""
    home = _short_tmpdir("mdsbx-cfg-")
    dst = os.path.join(home, "plugins", "sandbox")
    os.makedirs(dst)
    for name in ("init.py", "sandboxlib.py"):
        shutil.copy(os.path.join(_PLUGIN_ROOT, name), os.path.join(dst, name))
    yield home
    shutil.rmtree(home, ignore_errors=True)


@pytest.fixture(scope="session")
def sandbox_base():
    """A short-path sandbox base dir shared by the suite.

    Sharing it means the per-version boilerplate is bootstrapped only once for
    the whole integration run.
    """
    base = _short_tmpdir("mdsbx-")
    yield base
    shutil.rmtree(base, ignore_errors=True)


@pytest.fixture
def driver(plugin_config_home, sandbox_base):
    drv = _Driver(plugin_config_home, sandbox_base)
    try:
        yield drv
    finally:
        drv.cleanup()


# --------------------------------------------------------------------------- #
# Tests
# --------------------------------------------------------------------------- #
def test_deploy_connect_lifecycle(driver):
    port = _free_port()

    out = driver.deploy(port)
    assert "successfully deployed" in out, out
    # The per-version boilerplate must have been created.
    assert glob.glob(os.path.join(driver.sandbox_dir, "myboilerplate-*"))

    # The instance is reachable and the root password was set.
    assert "QUERY_OK 1" in driver.query(port)

    # Graceful stop.
    out = driver.op("stop", port)
    assert "successfully stopped" in out, out
    assert not _listening(port)

    # Start again.
    out = driver.op("start", port, {"mariadbdPath": BASEDIR})
    assert "successfully started" in out, out
    assert _listening(port)

    # Forceful kill.
    out = driver.op("kill", port)
    assert "successfully killed" in out, out

    # Delete removes the instance directory.
    out = driver.op("delete", port)
    assert "successfully deleted" in out, out
    assert not os.path.isdir(os.path.join(driver.sandbox_dir, str(port)))


def test_raw_config_has_no_replication(driver):
    port = _free_port()
    assert "successfully deployed" in driver.deploy(port)

    cnf = os.path.join(driver.sandbox_dir, str(port), "my.cnf")
    with open(cnf) as f:
        contents = f.read()
    for forbidden in ("log_bin", "binlog_format", "gtid", "log_slave_updates",
                      "report_host", "report_port"):
        assert forbidden not in contents, \
            "raw sandbox my.cnf must not contain " + forbidden


def test_ssl_enabled_by_default(driver):
    port = _free_port()
    assert "successfully deployed" in driver.deploy(port)

    sbx = os.path.join(driver.sandbox_dir, str(port))
    # The certificate authority plus server and client certificates exist. Where
    # they live and how the CA is named depends on who produced them: MariaDB gets
    # them generated with openssl into the sandbox directory, while MySQL
    # auto-generates its own set into the data directory as it is initialized.
    with open(os.path.join(sbx, "vendor")) as f:
        vendor = f.read().strip()

    if vendor == "mysql":
        cert_dir = os.path.join(sbx, "sandboxdata")
        ca_name = "ca.pem"
    else:
        cert_dir = sbx
        ca_name = "ca-cert.pem"

    for name in (ca_name, "ca-key.pem", "server-cert.pem",
                 "server-key.pem", "client-cert.pem", "client-key.pem"):
        assert os.path.isfile(os.path.join(cert_dir, name)), name

    # The server option file points the instance at the server certificates.
    with open(os.path.join(sbx, "my.cnf")) as f:
        contents = f.read()
    assert "ssl_ca = " in contents
    assert "ssl_cert = " in contents
    assert "ssl_key = " in contents

    # The connection to the running server is actually encrypted. Checked via the
    # session's Ssl_cipher status variable (non-empty only for a TLS connection)
    # rather than the 'have_ssl' variable, which MySQL removed - and which only
    # said TLS was available, not that it was in use.
    out = driver.run(
        "s = shell.open_session('root:rootpass@localhost:{0}'); "
        "cipher = s.run_sql(\"SHOW STATUS LIKE 'Ssl_cipher'\").fetch_one()[1]; "
        "print('ENCRYPTED', bool(cipher)); s.close()".format(port))
    assert "ENCRYPTED True" in out, out


def test_ssl_can_be_disabled(driver):
    port = _free_port()
    assert "successfully deployed" in driver.deploy(port, ssl=False)

    sbx = os.path.join(driver.sandbox_dir, str(port))
    assert not os.path.isfile(os.path.join(sbx, "ca-cert.pem"))
    with open(os.path.join(sbx, "my.cnf")) as f:
        contents = f.read()
    assert "ssl_ca" not in contents


def test_boilerplate_reused_across_deployments(driver):
    p1, p2 = _free_port(), _free_port()

    assert "successfully deployed" in driver.deploy(p1)
    out2 = driver.deploy(p2)
    assert "successfully deployed" in out2

    # Exactly one boilerplate for the single server version under test.
    boilerplates = glob.glob(
        os.path.join(driver.sandbox_dir, "myboilerplate-*"))
    assert len(boilerplates) == 1, boilerplates

    # The second deployment must reuse it, not rebuild it.
    assert "Preparing MariaDB sandbox boilerplate" not in out2, out2

    # Both instances are independent and reachable.
    assert "QUERY_OK 1" in driver.query(p1)
    assert "QUERY_OK 1" in driver.query(p2)
