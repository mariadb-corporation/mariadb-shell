#
# Copyright (c) 2026, Oracle and/or its affiliates.
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
import tempfile
import types
import unittest


def _install_mysqlsh_stub():
    mysqlsh = types.ModuleType("mysqlsh")
    mysqlsh.globals = types.SimpleNamespace(shell=None)
    sys.modules.setdefault("mysqlsh", mysqlsh)


_install_mysqlsh_stub()
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))

from mysql_gadgets.common.config_parser import MySQLOptionsParser


class MySQLRawConfigParserCompatTest(unittest.TestCase):
    def _make_parser(self):
        return MySQLOptionsParser.MySQLRawConfigParser(
            allow_no_value=True,
            delimiters=("=",),
            strict=False,
            empty_lines_in_values=False,
            interpolation=None,
            default_section="",
        )

    def _write_sample_cnf(self):
        fd, path = tempfile.mkstemp(suffix=".cnf")
        os.close(fd)

        with open(path, "w", encoding="utf-8") as handle:
            handle.write("[mysqld]\nport=3306\n# comment\n")
        return path

    def test_explicit_comment_prefixes_preserve_comment_behavior(self):
        parser = MySQLOptionsParser.MySQLRawConfigParser(
            allow_no_value=True,
            delimiters=("=",),
            strict=False,
            empty_lines_in_values=False,
            interpolation=None,
            default_section="",
            comment_prefixes=("#", ";"),
            inline_comment_prefixes=(),
        )
        path = self._write_sample_cnf()

        try:
            with open(path, "r", encoding="utf-8") as handle:
                parser.read_file(handle)

            self.assertEqual(["port"], parser.options("mysqld"))
            self.assertFalse(parser.has_option("mysqld", "# comment"))
        finally:
            os.unlink(path)

    def test_default_parser_preserves_comment_behavior(self):
        parser = self._make_parser()

        path = self._write_sample_cnf()

        try:
            with open(path, "r", encoding="utf-8") as handle:
                parser.read_file(handle)

            self.assertEqual(["port"], parser.options("mysqld"))
            self.assertFalse(parser.has_option("mysqld", "# comment"))
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
