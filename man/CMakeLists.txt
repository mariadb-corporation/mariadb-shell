# Copyright (c) 2006, 2024, Oracle and/or its affiliates.
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
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

# Copy man pages
SET(MAN1_SHELL
  mariadb-shell.1
)

INSTALL(FILES ${MAN1_SHELL} DESTINATION ${INSTALL_MANDIR}/man1 COMPONENT ManPages)

# One-line .so stubs for the binary aliases installed by src/CMakeLists.txt, so
# `man msh` resolves to the real page. Plain files rather than symlinks: they
# survive CPack's DESTDIR staging and the .deb/.rpm payloads without being
# dereferenced into duplicate copies.
FOREACH(_alias msh)
  SET(_stub "${CMAKE_CURRENT_BINARY_DIR}/${_alias}.1")
  FILE(WRITE "${_stub}" ".so man1/mariadb-shell.1\n")
  INSTALL(FILES "${_stub}" DESTINATION ${INSTALL_MANDIR}/man1 COMPONENT ManPages)
ENDFOREACH()
