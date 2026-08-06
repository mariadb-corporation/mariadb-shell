/*
 * Copyright (c) 2026, MariaDB Corporation.
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

#ifndef MYSQLSHDK_LIBS_UTILS_MYSYS_THREAD_H_
#define MYSQLSHDK_LIBS_UTILS_MYSYS_THREAD_H_

namespace mysqlshdk {
namespace utils {

/**
 * Gives the calling thread valid mysys thread-local state for its lifetime.
 *
 * The shell links the server's mysys separately from the client library, and
 * mysys keeps per-thread state (notably the error slot behind `my_errno`).
 * `my_init()` sets this up for the main thread only (see MARIADB_PORT.md
 * section 6); a thread that calls into mysys without `my_thread_init()` gets a
 * null thread-var and segfaults on the first error path. That is reachable from
 * ordinary work - e.g. the charset table initialization in the dumper, which
 * stats a charset index file that need not exist and then writes `my_errno`.
 *
 * Declared free of mysys headers on purpose: this is pulled in by
 * scoped_contexts.h, which is included very widely, and my_global.h /
 * my_config.h clash with Python's pyconfig.h and with the Windows POSIX-compat
 * macros.
 *
 * A no-op when the shell is not built against MariaDB's mysys.
 */
class Mysys_thread_scope final {
 public:
  Mysys_thread_scope();

  Mysys_thread_scope(const Mysys_thread_scope &) = delete;
  Mysys_thread_scope(Mysys_thread_scope &&) = delete;

  Mysys_thread_scope &operator=(const Mysys_thread_scope &) = delete;
  Mysys_thread_scope &operator=(Mysys_thread_scope &&) = delete;

  ~Mysys_thread_scope();
};

}  // namespace utils
}  // namespace mysqlshdk

#endif  // MYSQLSHDK_LIBS_UTILS_MYSYS_THREAD_H_
