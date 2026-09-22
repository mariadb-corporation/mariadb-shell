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

"""End-to-end exercise of the sandbox plugin against packaged binaries.

Driven by scripts/verify_sandbox.sh, which puts a packaged shell and a packaged
sandbox server on the PATH and then runs this inside the shell:

    mariadb-shell --quiet-start=2 --py -f scripts/verify_sandbox.py

Nothing here reaches into the repository: the plugin under test is the one
bundled in the package, and the server is whichever mariadbd the PATH resolves
to. That is the point -- this runs on machines that never built either.

The full cycle is deploy -> verify -> stop -> start -> verify -> kill -> delete,
where "verify" means connect to the instance, list its schemas, assert the
'mysql' schema is among them, and disconnect.

Environment:
  SBX_DIR       sandbox directory (required; keep it short, a Unix socket lives
                under it and must fit in sun_path)
  SBX_PORT      port to deploy on (default: a free port picked from the OS)
  SBX_PASSWORD  root password for the deployed instance

Success is reported by printing SANDBOX_VERIFICATION_PASSED as the last line.
The wrapper checks for that line as well as the exit status, so a shell that
swallows a script error cannot turn a failure into a pass.
"""

import os
import socket
import sys
import time
import traceback

from mysqlsh import globals

shell = globals.shell
sandbox = globals.sandbox

PASS_SENTINEL = "SANDBOX_VERIFICATION_PASSED"
FAIL_SENTINEL = "SANDBOX_VERIFICATION_FAILED"

# Seconds to wait for a port to start or stop accepting connections. The plugin
# already waits for its own operations to complete; this only covers the gap
# between an operation returning and the OS releasing the listening socket.
SETTLE_TIMEOUT = 60

# What the failure handler needs to find the instance's error log.
state = {"port": None, "options": None}


def log(message):
    print("[verify] {0}".format(message))


def step(title):
    print("")
    print("==> {0}".format(title))


def fail(message):
    raise AssertionError(message)


def pick_port():
    """A port to deploy on: SBX_PORT, or one the OS says is free."""
    configured = os.environ.get("SBX_PORT", "").strip()
    if configured:
        return int(configured)
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]
    finally:
        sock.close()


def listening(port):
    try:
        conn = socket.create_connection(("127.0.0.1", port), 1)
        conn.close()
        return True
    except OSError:
        return False


def wait_until(predicate, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if predicate():
            return True
        time.sleep(1)
    return predicate()


def verify_instance(port, password, label):
    """Connect, list the schemas, assert 'mysql' is there, disconnect."""
    step("Verify sandbox on port {0} ({1})".format(port, label))

    # A connection dictionary rather than a URI: the password is passed as data,
    # so nothing here depends on URI-escaping it correctly.
    session = shell.open_session({
        "scheme": "mysql",
        "host": "127.0.0.1",
        "port": port,
        "user": "root",
        "password": password,
    })
    log("connected to 127.0.0.1:{0}".format(port))

    try:
        schemas = [row[0] for row in session.run_sql("SHOW SCHEMAS").fetch_all()]
        log("schemas: {0}".format(", ".join(schemas) or "(none)"))
        if "mysql" not in schemas:
            fail("the 'mysql' schema is missing from the instance on port "
                 "{0}; got: {1}".format(port, schemas))
        log("the 'mysql' schema is present")
    finally:
        session.close()

    # close() is what the check is about, so confirm it took effect rather than
    # trusting that it returned. Older sessions may not expose is_open().
    is_open = getattr(session, "is_open", None)
    if is_open is not None and is_open():
        fail("the session is still open after close()")
    log("disconnected")


def report_environment():
    """Log which server the PATH resolved to. Informational only."""
    for name in ("vendor", "version"):
        try:
            log("server {0}: {1}".format(name, getattr(sandbox, name)()))
        except Exception as err:
            log("could not read the server {0}: {1}".format(name, err))


def main():
    sandbox_dir = os.environ.get("SBX_DIR", "").strip()
    if not sandbox_dir:
        fail("SBX_DIR is not set; scripts/verify_sandbox.sh sets it")
    password = os.environ.get("SBX_PASSWORD", "").strip() or "S4ndb0x-Verify"
    port = pick_port()

    options = {"sandboxDir": sandbox_dir}
    state["port"] = port
    state["options"] = options

    step("Environment")
    log("sandbox directory: {0}".format(sandbox_dir))
    log("port: {0}".format(port))
    report_environment()

    # --- deploy ------------------------------------------------------------
    # SSL is left at its default (on everywhere except MariaDB on Windows, where
    # the plugin turns it off because those builds bundle wolfSSL): a package
    # whose bundled openssl CLI went missing must fail here, not be skipped.
    step("Deploy sandbox on port {0}".format(port))
    deploy_options = dict(options)
    deploy_options["password"] = password
    sandbox.deploy(port, deploy_options)

    instance_dir = sandbox.get_path(port, "", options)
    log("instance directory: {0}".format(instance_dir))
    if not os.path.isdir(instance_dir):
        fail("deploy reported success but '{0}' does not exist"
             "".format(instance_dir))
    if not listening(port):
        fail("nothing is listening on port {0} after deploy".format(port))
    log("deployed and listening")

    verify_instance(port, password, "after deploy")

    # --- stop --------------------------------------------------------------
    step("Stop sandbox on port {0}".format(port))
    # password: only Windows needs it (the shutdown is authenticated there
    # rather than signalled), and it is ignored elsewhere.
    sandbox.stop(port, dict(options, password=password))
    if not wait_until(lambda: not listening(port), SETTLE_TIMEOUT):
        fail("port {0} is still accepting connections after stop".format(port))
    if not os.path.isdir(instance_dir):
        fail("stop removed the sandbox directory '{0}'".format(instance_dir))
    log("stopped, directory kept")

    # --- start -------------------------------------------------------------
    step("Start sandbox on port {0}".format(port))
    sandbox.start(port, options)
    if not wait_until(lambda: listening(port), SETTLE_TIMEOUT):
        fail("port {0} is not accepting connections after start".format(port))
    log("started")

    verify_instance(port, password, "after restart")

    # --- kill --------------------------------------------------------------
    step("Kill sandbox on port {0}".format(port))
    sandbox.kill(port, options)
    if not wait_until(lambda: not listening(port), SETTLE_TIMEOUT):
        fail("port {0} is still accepting connections after kill".format(port))
    # kill terminates the process and leaves the deployment on disk -- delete is
    # what removes it. Asserting that here keeps the two apart, so a kill that
    # started wiping the directory would be caught rather than pass as "deleted".
    if not os.path.isdir(instance_dir):
        fail("kill removed the sandbox directory '{0}'; only delete should"
             "".format(instance_dir))
    log("killed, directory kept")

    # --- delete ------------------------------------------------------------
    step("Delete sandbox on port {0}".format(port))
    sandbox.delete(port, options)
    if os.path.exists(instance_dir):
        fail("the sandbox directory '{0}' still exists after delete"
             "".format(instance_dir))
    log("deleted, directory gone")

    step("All sandbox operations completed")
    print(PASS_SENTINEL)


def dump_error_log():
    """Print the instance's error log, if the deployment got far enough."""
    port, options = state["port"], state["options"]
    if port is None or options is None:
        return
    try:
        path = sandbox.get_path(port, "error", options)
    except Exception:
        return
    if not path or not os.path.isfile(path):
        return
    print("")
    print("--- server error log ({0}) ---".format(path))
    try:
        with open(path, "r", errors="replace") as log_file:
            sys.stdout.write(log_file.read())
    except OSError as err:
        print("(could not be read: {0})".format(err))
    print("--- end of server error log ---")


try:
    main()
except Exception:
    print("")
    traceback.print_exc()
    dump_error_log()
    print("")
    print(FAIL_SENTINEL)
    sys.exit(1)
