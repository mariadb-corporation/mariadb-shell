# Copyright (c) 2020, 2025, Oracle and/or its affiliates.
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

import sys
from typing import Union


class Error(Exception):
    code: Union[int, None] = None
    msg: Union[str, None] = None

    def __init__(self, code: int, msg: Union[str, None] = None):
        if msg is None:
            self.msg = str(code)
            self.code = None
        else:
            self.code = code
            self.msg = msg
        self.args = (code, msg)

    def __str__(self):
        if not self.code:
            msg = "Shell Error: %s" % (self.msg, )
        else:
            msg = "Shell Error (%s): %s" % (self.code, self.msg)
        return msg


class DBError(Error):
    def __init__(self, code: int, msg: str, sqlstate=None):
        super().__init__(code, msg)
        self.sqlstate = sqlstate

    def __str__(self):
        return "MySQL Error (%s): %s" % (self.code, self.msg)


class ShellGlobals(object):
    def __setattr__(self, name, value):
        self.__dict__[name] = value

    def __delattr__(self, name):
        del self.__dict__[name]

    def __getattr__(self, name):
        # backwards compatibility. The `mysql`/`mysqlx` modules are injected
        # into this package's namespace by the shell at startup; `mysqlx` is
        # absent in builds without X protocol support (e.g. MariaDB), so look
        # them up defensively instead of referencing a possibly-undefined name.
        if name in ("mysql", "mysqlx"):
            try:
                return sys.modules[__name__].__dict__[name]
            except KeyError:
                raise AttributeError(name)
        try:
            return self.__dict__[name]
        except KeyError:
            raise AttributeError(name)


globals = ShellGlobals()

# Register the globals holder as a real submodule so shell globals set on it by
# the shell (see Python_context::set_global) can be imported, e.g.
#   from mysqlsh.globals import sandbox
sys.modules[__name__ + ".globals"] = globals

del ShellGlobals
