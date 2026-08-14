#
# Copyright (c) 2026, Oracle and/or its affiliates.
# Copyright (c) 2026, MariaDB plc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# This program is designed to work with certain software (including
# but not limited to OpenSSL) that is licensed under separate terms,
# as designated in a particular file or component or in included license
# documentation.  The authors of MySQL hereby grant you an additional
# permission to link the program and your derivative works with the
# separately licensed software that they have either included with
# the program or referenced in the documentation.
#
# This program is distributed in the hope that it will be useful,  but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
# the GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

import os
import sys
import types
import unittest


def _install_mysqlsh_stub():
    mysqlsh = types.ModuleType("mysqlsh")
    mysqlsh.globals = types.SimpleNamespace(shell=None)
    sys.modules.setdefault("mysqlsh", mysqlsh)


_install_mysqlsh_stub()
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))

from mysql_gadgets import (
    MAX_MYSQL_VERSION,
    MYSQL_VERSION_RANGE_DESCRIPTION,
    MYSQL_VERSION_RANGES,
    _next_calendar_mysql_version_series,
    is_supported_mysql_version,
)


class MySQLVersionSupportTest(unittest.TestCase):
    def test_supported_ranges_skip_unreleased_version_gap(self):
        self.assertTrue(is_supported_mysql_version((5, 7, 17)))
        self.assertTrue(is_supported_mysql_version((9, 7, 1)))
        self.assertFalse(is_supported_mysql_version((10,)))
        self.assertFalse(is_supported_mysql_version((10, 0, 0)))
        self.assertFalse(is_supported_mysql_version((26, 6, 99)))
        self.assertTrue(is_supported_mysql_version((26, 7)))
        self.assertTrue(is_supported_mysql_version((26, 7, 0)))
        self.assertTrue(is_supported_mysql_version((26, 10, 0)))
        self.assertFalse(is_supported_mysql_version((26, 11, 0)))
        self.assertFalse(is_supported_mysql_version((27, 0, 0)))
        self.assertTrue(is_supported_mysql_version((27, 1, 0)))
        self.assertTrue(is_supported_mysql_version((28, 4, 99)))
        self.assertFalse(is_supported_mysql_version((28, 7, 0)))

    def test_max_supported_series_is_28_04(self):
        self.assertEqual((28, 4), MAX_MYSQL_VERSION)
        self.assertIn("< '10.0.0'", MYSQL_VERSION_RANGE_DESCRIPTION)
        self.assertIn("<= '28.4'", MYSQL_VERSION_RANGE_DESCRIPTION)

    def test_calendar_range_upper_bound_is_derived(self):
        self.assertEqual((28, 7, 0), MYSQL_VERSION_RANGES[1][1])
        self.assertEqual((29, 1, 0),
                         _next_calendar_mysql_version_series((28, 10)))


if __name__ == "__main__":
    unittest.main()
