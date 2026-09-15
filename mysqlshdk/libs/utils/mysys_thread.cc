/*
 * Copyright (c) 2026, MariaDB plc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is designed to work with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms,
 * as designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have either included with
 * the program or referenced in the documentation.
 *
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "mysqlshdk/libs/utils/mysys_thread.h"

#ifdef MARIADB_BUILD
// my_thread_init()/my_thread_end() live in my_pthread.h, which needs the
// my_global.h typedefs (and mysql.h's my_bool) ahead of it.
#include <mysql.h>

#include <my_global.h>

#include <my_pthread.h>
#endif

namespace mysqlshdk {
namespace utils {

#ifdef MARIADB_BUILD

Mysys_thread_scope::Mysys_thread_scope() { my_thread_init(); }

Mysys_thread_scope::~Mysys_thread_scope() { my_thread_end(); }

#else  // !MARIADB_BUILD

// The MySQL build does not link the server mysys the same way and has never
// needed per-thread initialization here; keep this a no-op so the MySQL build
// is bit-for-bit unaffected.
Mysys_thread_scope::Mysys_thread_scope() = default;

Mysys_thread_scope::~Mysys_thread_scope() = default;

#endif  // !MARIADB_BUILD

}  // namespace utils
}  // namespace mysqlshdk
