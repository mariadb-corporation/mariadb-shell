/*
  Copyright (c) 2026, MariaDB plc.

  SPDX-License-Identifier: GPL-2.0-only

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335 USA
*/

/*
 * MariaDB Windows build compatibility shim.
 *
 * Force-included (cl.exe /FI) into every translation unit of the MariaDB port
 * on Windows. It reconciles an incompatibility between two MariaDB headers the
 * shell pulls into the same TU:
 *
 *   - Connector/C  libmariadb/include/ma_global.h defines `my_socket` as a
 *     preprocessor MACRO on Windows: `#define my_socket unsigned long long`
 *     (guarded by `my_socket_defined`).
 *   - The server    include/my_global.h defines it as a TYPE:
 *     `typedef SOCKET my_socket;` (and does NOT participate in the
 *     `my_socket_defined` guard).
 *
 * The shell links the Connector/C client API and also uses the server's
 * mysys/strings/dbug libraries, so both headers land in many TUs. Whenever the
 * connector header is seen first, its macro rewrites the server line into
 * `typedef SOCKET unsigned long long;`, which fails to compile (MSVC C2628).
 *
 * On Linux/macOS both headers use `typedef int my_socket;` (identical
 * typedefs, legal), so the clash is Windows-only.
 *
 * Fix: define `my_socket` ourselves as the real socket type up front and set
 * the connector's guard so ma_global.h skips its macro. The server's later
 * `typedef SOCKET my_socket;` then resolves to the same underlying type
 * (SOCKET == UINT_PTR), so the repeated typedef is legal.
 */
#ifndef MARIADB_WIN_COMPAT_H
#define MARIADB_WIN_COMPAT_H

/*
 * Gated on MARIADB_BUILD (defined on the command line, so it is visible even
 * though this file is force-included first): the clash is between the MariaDB
 * Connector/C and server headers, so a MySQL-server build must be untouched.
 * The CMake wiring only force-includes this for MariaDB builds anyway; the
 * guard makes the intent explicit and keeps the file inert otherwise.
 */
#if defined(_WIN32) && defined(MARIADB_BUILD)
#include <basetsd.h> /* UINT_PTR (the underlying type of winsock's SOCKET) */
#ifndef my_socket_defined
#define my_socket_defined
typedef UINT_PTR my_socket;
#endif
#endif /* _WIN32 && MARIADB_BUILD */

#endif /* MARIADB_WIN_COMPAT_H */
