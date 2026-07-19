/*
  Copyright (c) 2026, MariaDB Corporation.

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
 * MariaDB Windows macro-pollution cleanup.
 *
 * The MariaDB server headers (my_global.h, and my_config.h / config.h) define
 * a number of UNGUARDED POSIX-compatibility function-like macros on Windows,
 * e.g.:
 *     my_global.h : #define sleep(a) Sleep((a)*1000)
 *                   #define strtok_r(A,B,C) strtok((A),(B))
 *     my_config.h : #define setenv(a,b,c) _putenv_s(a,b)
 * These collide with shcore's own identifiers (shcore::sleep, shcore::setenv,
 * ...) both at their declarations and at call/definition sites, breaking the
 * compile (MSVC C2182 / C4003 / C2059).
 *
 * Because the macros are unguarded they cannot be suppressed at the source or
 * via a force-include (which runs before the server headers). Instead, include
 * THIS header AFTER any MariaDB server header in a translation unit that
 * declares, defines or calls the affected shcore functions, to undo them.
 *
 * There is intentionally NO include guard: a TU may include a server header
 * more than once (e.g. utils_general.h undoes the macros, then the .cc pulls
 * my_config.h again), so this cleanup must be re-runnable at each such point.
 *
 * Gated on MARIADB_BUILD: the offending macros come from the MariaDB server
 * headers, so a MySQL-server build must be left untouched.
 */
#if defined(_WIN32) && defined(MARIADB_BUILD)
#undef sleep
#undef setenv
#undef strtok_r
#endif
