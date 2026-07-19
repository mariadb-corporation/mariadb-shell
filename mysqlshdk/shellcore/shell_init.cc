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
 * Copyright (c) 2018, 2026 Oracle and/or its affiliates.
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

#include "shellcore/shell_init.h"

#include <curl/curl.h>
#include <mysql.h>
#include <openssl/opensslv.h>
#include <stdlib.h>
#include <stdexcept>

#include "mysqlshdk/libs/utils/utils_file.h"
#include "mysqlshdk/libs/utils/utils_general.h"
#include "mysqlshdk/libs/utils/utils_path.h"

#ifdef MARIADB_BUILD
#include <cstdlib>
// my_init() initializes the server mysys library (libmysys) that the shell
// links for my_load_defaults()/my_stat()/charset functions. Its error state
// lives in thread-local storage that must be set up before first use;
// libmariadb's mysql_library_init() does not initialize the server mysys lib.
extern "C" char my_init(void);
extern "C" void my_end(int infoflag);

namespace {
bool g_mysys_ended = false;
// Tears down mysys exactly once. Registered via atexit() right after my_init()
// so that on every exit path (including early exits such as --version that
// bypass global_end()) it runs before mysys' own safemalloc atexit handler
// (sf_terminate), which would otherwise crash resolving the leak backtrace.
void mariadb_mysys_end() {
  if (!g_mysys_ended) {
    g_mysys_ended = true;
    my_end(0);
  }
}
}  // namespace
#endif

namespace mysqlsh {

namespace {

void init_openssl_modules() {
#ifdef BUNDLE_OPENSSL_MODULES
  constexpr auto k_env_var = "OPENSSL_MODULES";

  // don't override path set by the user
  if (!::getenv(k_env_var)) {
    shcore::setenv(
        k_env_var,
        shcore::path::join_path(shcore::get_library_folder(), "ossl-modules"));
  }
#endif
}

}  // namespace

void thread_init() { mysql_thread_init(); }

void thread_end() { mysql_thread_end(); }

void global_init() {
#ifdef MARIADB_BUILD
  // Must run before any libmysys call (e.g. my_load_defaults while parsing
  // command-line/my.cnf options).
  my_init();
  std::atexit(mariadb_mysys_end);
#endif
  mysql_library_init(0, nullptr, nullptr);

  thread_init();

  srand(time(0));
  curl_global_init(CURL_GLOBAL_ALL);
  init_openssl_modules();

#if OPENSSL_VERSION_NUMBER < 0x30000000L /* 3.0.x */
  // disable Python's cryptography warning regarding OpenSSL < 3.0
  shcore::setenv(
      "PYTHONWARNINGS",
      "ignore::UserWarning:cryptography.hazmat.backends.openssl.backend");
#endif
}

void global_end() {
  thread_end();
  mysql_library_end();
#ifdef MARIADB_BUILD
  // Pair the my_init() done in global_init(); releases libmysys global state.
  // (Idempotent; also registered via atexit for early-exit paths.)
  mariadb_mysys_end();
#endif
}

Mysql_thread::Mysql_thread() {
  if (mysql_thread_init()) {
    throw std::runtime_error(
        "Cannot allocate specific memory for the MySQL thread.");
  }
}

Mysql_thread::~Mysql_thread() { mysql_thread_end(); }

}  // namespace mysqlsh
