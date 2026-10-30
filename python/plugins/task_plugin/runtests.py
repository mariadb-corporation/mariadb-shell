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

"""Run the task plugin tests with pytest.

The tests exercise mysqlsh.task_manager, the library behind the task global
object, with a plain Python interpreter: it imports nothing of a running
shell.

    python3 runtests.py              # every test
    python3 runtests.py -v -k cancel # verbose, the tests matching "cancel"

Any extra arguments are forwarded to pytest.
"""

import os
import sys

_PLUGIN_ROOT = os.path.dirname(os.path.abspath(__file__))


def main(argv):
    try:
        import pytest
    except ImportError:
        sys.stderr.write(
            "pytest is required to run these tests. Install it with:\n"
            "    python3 -m pip install pytest\n")
        return 2

    os.chdir(_PLUGIN_ROOT)
    forwarded = argv[1:]
    has_path = any(not a.startswith("-") for a in forwarded)
    args = forwarded if has_path else ["test"] + forwarded
    return pytest.main(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
