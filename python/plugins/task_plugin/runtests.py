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

    # a test to run is a file or folder, with a "::name" after it or not. It
    # is named from where the runner was called, and made absolute before
    # the change of directory below, or pytest would look for it in the
    # plugin folder. An option's value is not one, even where a file or
    # folder happens to be named like it, as "test" is: what follows an
    # option is taken for its value, unless it is unmistakably a test file
    def test_target(arg, previous):
        path, sep, name = arg.partition("::")
        if arg.startswith("-") or not os.path.exists(path):
            return None
        if previous.startswith("-") and not (sep or path.endswith(".py")):
            return None
        return os.path.abspath(path) + sep + name

    args = argv[1:]
    targets = [test_target(arg, previous) for previous, arg in zip([""] + args, args)]
    forwarded = [target or arg for target, arg in zip(targets, args)]
    os.chdir(_PLUGIN_ROOT)
    args = forwarded if any(targets) else ["test"] + forwarded
    return pytest.main(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
