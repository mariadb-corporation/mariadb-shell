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

"""Run the sandbox plugin tests with pytest.

Tests are split by file name:
  * test_unit_*.py        -- fast, isolated; stub the ``mysqlsh`` runtime
                             (see test/conftest.py) and need only pytest.
  * test_integration_*.py -- deploy real MariaDB sandboxes through the built
                             shell; skipped unless MARIADB_SANDBOX_TEST_MYSQLSH
                             (or build/bin/mariadb-shell) and MARIADB_SANDBOX_TEST_BASEDIR
                             are available.

conftest.py auto-applies a matching 'unit' / 'integration' marker based on the
file name, so you can select by type:

    python3 runtests.py                  # everything (integration auto-skips
                                         # unless configured)
    python3 runtests.py -m unit          # only unit tests
    python3 runtests.py -m integration   # only integration tests
    python3 runtests.py -v               # verbose
    python3 runtests.py -k options       # tests matching "options"

Any extra arguments are forwarded to pytest.
"""

import os
import sys

_PLUGIN_ROOT = os.path.dirname(os.path.abspath(__file__))
_TEST_DIR = os.path.join(_PLUGIN_ROOT, "test")


def main(argv):
    try:
        import pytest
    except ImportError:
        sys.stderr.write(
            "pytest is required to run these tests. Install it with:\n"
            "    python3 -m pip install pytest\n")
        return 2

    # Run from the plugin root and default discovery to the test directory, so
    # the runner works regardless of the caller's current directory. Extra
    # arguments (flags like -v / -k EXPR, or explicit test paths) are forwarded.
    os.chdir(_PLUGIN_ROOT)
    forwarded = argv[1:]
    has_path = any(not a.startswith("-") for a in forwarded)
    args = forwarded if has_path else ["test"] + forwarded
    return pytest.main(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
