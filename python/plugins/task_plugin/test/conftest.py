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

"""Makes mysqlsh.task_manager import from the tree the tests sit in.

From the source tree it is in python/packages; from a build or an
installation, in python-packages next to the plugins folder. Run in the
shell, mysqlsh is already imported from the shell's own packages, so the
tree's mysqlsh folder is put first on its search path, for the tests to
exercise the code next to them.
"""

import os
import sys

_PLUGIN_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

for _candidate in (
    os.path.join(_PLUGIN_ROOT, "..", "..", "packages"),
    os.path.join(_PLUGIN_ROOT, "..", "..", "python-packages"),
):
    _candidate = os.path.abspath(_candidate)
    if os.path.isfile(os.path.join(_candidate, "mysqlsh", "task_manager.py")):
        sys.path.insert(0, _candidate)
        import mysqlsh

        mysqlsh.__path__.insert(0, os.path.join(_candidate, "mysqlsh"))
        break
