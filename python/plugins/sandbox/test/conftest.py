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

"""Pytest configuration and shared fixtures for the sandbox plugin.

This module is imported by pytest before collecting any tests. It:
  * puts the plugin directory on sys.path so ``import sandboxlib`` works, and
  * installs a stub ``mysqlsh`` module into sys.modules.

The stub lets ``sandboxlib`` (which does ``from mysqlsh import globals, Error``)
be imported and exercised with a plain Python interpreter, without a built
MySQL/MariaDB Shell.
"""

import os
import sys
import types

import pytest

# Make the plugin's own modules importable (sandboxlib lives one level up).
_PLUGIN_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _PLUGIN_ROOT not in sys.path:
    sys.path.insert(0, _PLUGIN_ROOT)


class FakeShell:
    """Minimal stand-in for the shell global object used by sandboxlib.

    Only the surface touched by the pure-logic code paths is implemented;
    operations that need a real server pass their own fakes into the helpers.
    """

    def __init__(self):
        self.options = {}
        self.log_records = []

    def log(self, level, msg):
        self.log_records.append((level, msg))


class RecordingSession:
    """Fake classic session that records the SQL it is asked to run."""

    def __init__(self):
        self.calls = []  # list of (sql, args)
        self.closed = False

    def run_sql(self, sql, args=None):
        self.calls.append((sql, args))
        return None

    def close(self):
        self.closed = True


def _install_fake_mysqlsh():
    """Install (once) and return the stub ``mysqlsh`` module."""
    existing = sys.modules.get("mysqlsh")
    if existing is not None and getattr(existing, "_sandbox_test_fake", False):
        return existing

    mod = types.ModuleType("mysqlsh")
    mod._sandbox_test_fake = True

    class Error(Exception):
        pass

    mod.Error = Error
    mod.globals = types.SimpleNamespace(shell=FakeShell())
    sys.modules["mysqlsh"] = mod
    return mod


# Install the stub at import time, before any test module imports sandboxlib.
_install_fake_mysqlsh()


def pytest_configure(config):
    config.addinivalue_line(
        "markers", "unit: fast, isolated tests that stub the shell runtime")
    config.addinivalue_line(
        "markers", "integration: end-to-end tests that deploy a real MariaDB "
        "sandbox using the built shell (opt-in, see test_integration_*.py)")


def pytest_collection_modifyitems(config, items):
    """Auto-apply the unit/integration marker based on the file name, so the
    naming convention (test_unit_*.py / test_integration_*.py) drives marker
    selection (e.g. -m integration / -m 'not integration')."""
    for item in items:
        name = os.path.basename(str(item.fspath))
        if name.startswith("test_integration_"):
            item.add_marker("integration")
        elif name.startswith("test_unit_"):
            item.add_marker("unit")


@pytest.fixture
def shell():
    """Provide a fresh stub shell installed on ``mysqlsh.globals.shell``."""
    import mysqlsh
    new = FakeShell()
    mysqlsh.globals.shell = new
    yield new


@pytest.fixture
def session():
    """Provide a fresh recording session."""
    return RecordingSession()


@pytest.fixture
def sandboxlib():
    """Import and return the module under test."""
    import sandboxlib
    return sandboxlib
