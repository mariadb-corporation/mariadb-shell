/*
 * Copyright (c) 2017, 2024, Oracle and/or its affiliates.
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

#ifndef MYSQLSHDK_LIBS_UTILS_DEBUG_H_
#define MYSQLSHDK_LIBS_UTILS_DEBUG_H_

#ifdef MARIADB_BUILD
// MariaDB's my_dbug.h needs my_bool (mysql.h) and ATTRIBUTE_COLD (my_global.h)
// included first. my_config.h (pulled by my_global.h) redefines autoconf
// size/HAVE_* macros that Python's pyconfig.h also defines; the values are
// identical, so silence the redefinition diagnostic for builds combining both
// (HAVE_PYTHON). "-Wmacro-redefined" is a Clang diagnostic name; GCC does not
// recognize it (and would error under -Werror=pragmas), while GCC stays silent
// for identical redefinitions anyway, so the suppression is Clang-only.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmacro-redefined"
#endif
#include <mysql.h>
#include <my_global.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif
#include <my_dbug.h>

// debug.h must include the server my_global.h/my_config.h above (my_dbug.h
// depends on them), but must not leak their unguarded Windows POSIX-compat
// macros (sleep/setenv/strtok_r) to the many TUs that include debug.h and call
// shcore::sleep / shcore::setenv. Undo them here at the gateway. Inert off
// Windows/MySQL. (Direct includers of my_config.h still need their own cleanup.)
#include "mysqlshdk/libs/utils/mariadb_win_undef.h"
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>

namespace shcore {
namespace debug {

#ifndef NDEBUG

/**
 * Debug support for tracking object allocations and deallocations.
 *
 * To use, call:
 *  DEBUG_OBJ_ENABLE(class) on the file with class implementation
 *  DEBUG_OBJ_ALLOC(class) on all contructors and
 *  DEBUG_OBJ_DEALLOC(class) on destructor
 */

#define DEBUG_OBJ_FOR_CLASS_(klass) , shcore::debug::Debug_object_for<klass>

#define DEBUG_OBJ_FOR_CLASS(klass) shcore::debug::Debug_object_for<klass>

#define DEBUG_OBJ_ENABLE(name) \
  static shcore::debug::Debug_object_info *g_debug_obj_##name = nullptr

#define DEBUG_OBJ_ALLOC_N(name, n)                             \
  do {                                                         \
    if (!g_debug_obj_##name) {                                 \
      g_debug_obj_##name =                                     \
          shcore::debug::debug_object_enable(STRINGIFY(name)); \
    }                                                          \
    g_debug_obj_##name->on_alloc(this, n);                     \
  } while (0)

#define DEBUG_OBJ_ALLOC(name)                                  \
  do {                                                         \
    if (!g_debug_obj_##name) {                                 \
      g_debug_obj_##name =                                     \
          shcore::debug::debug_object_enable(STRINGIFY(name)); \
    }                                                          \
    g_debug_obj_##name->on_alloc(this);                        \
  } while (0)

#define DEBUG_OBJ_ALLOC2(name, get_debug)                      \
  do {                                                         \
    if (!g_debug_obj_##name) {                                 \
      g_debug_obj_##name =                                     \
          shcore::debug::debug_object_enable(STRINGIFY(name)); \
    }                                                          \
    g_debug_obj_##name->on_alloc(this, get_debug);             \
  } while (0)

#define DEBUG_OBJ_DEALLOC(name) g_debug_obj_##name->on_dealloc(this)

#define DEBUG_OBJ_MALLOC(name, ptr)                            \
  do {                                                         \
    if (!g_debug_obj_##name) {                                 \
      g_debug_obj_##name =                                     \
          shcore::debug::debug_object_enable(STRINGIFY(name)); \
    }                                                          \
    g_debug_obj_##name->on_alloc(ptr);                         \
  } while (0)

#define DEBUG_OBJ_MALLOC_N(name, ptr, tag)                     \
  do {                                                         \
    if (!g_debug_obj_##name) {                                 \
      g_debug_obj_##name =                                     \
          shcore::debug::debug_object_enable(STRINGIFY(name)); \
    }                                                          \
    g_debug_obj_##name->on_alloc(ptr, tag);                    \
  } while (0)

#define DEBUG_OBJ_FREE(name, ptr) g_debug_obj_##name->on_dealloc(ptr)

class Debug_object_info {
 public:
  std::string name;
  uint32_t allocs = 0;
  uint32_t m_deallocs = 0;
  std::mutex m_mtx;
  bool track_instances = false;
  bool fatal_leaks = false;
  std::set<void *> instances;
  std::map<void *, std::string> instance_tags;
  std::function<std::string(void *)> get_debug;
  void dump();

 public:
  explicit Debug_object_info(const std::string &n);
  void on_alloc(void *p, const std::string &tag = "");
  void on_alloc(void *p, std::function<std::string(void *)> get_debug,
                const std::string &tag = "");
  void on_dealloc(void *p);
};

Debug_object_info *debug_object_enable(const char *name);
Debug_object_info *debug_object_enable_fatal(const char *name);

template <typename C>
class Debug_object_for {
 public:
  Debug_object_for() { debug_object_enable(typeid(C).name())->on_alloc(this); }

  virtual ~Debug_object_for() {
    debug_object_enable(typeid(C).name())->on_dealloc(this);
  }
};

bool debug_object_dump_report(bool verbose);

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#if __has_builtin(__builtin_trap)
#define DEBUG_TRAP __builtin_trap()
#else
#define DEBUG_TRAP abort()
#endif

#else  // NDEBUG

#define DEBUG_OBJ_FOR_CLASS_(klass)

#define DEBUG_OBJ_FOR_CLASS(klass)

#define DEBUG_OBJ_ENABLE(name) struct dummy_##name

#define DEBUG_OBJ_ALLOC(name) \
  do {                        \
  } while (0)

#define DEBUG_OBJ_ALLOC2(name, get_debug) \
  do {                                    \
  } while (0)

#define DEBUG_OBJ_ALLOC_N(name, n) \
  do {                             \
  } while (0)

#define DEBUG_OBJ_DEALLOC(name) \
  do {                          \
  } while (0)

#define DEBUG_OBJ_MALLOC(name, ptr) \
  do {                              \
  } while (0)

#define DEBUG_OBJ_MALLOC_N(name, ptr, n) \
  do {                                   \
  } while (0)

#define DEBUG_OBJ_FREE(name, ptr) \
  do {                            \
  } while (0)

#define DEBUG_TRAP \
  do {             \
  } while (0)

#endif  // NDEBUG

}  // namespace debug
}  // namespace shcore

// Right now, this is in mysql-trunk only, remove once DBUG_TRACE hits
// our target branch
// DBUG_OFF is checked in addition to NDEBUG: the DBUG runtime (_db_enter_,
// _db_stack_frame_, ...) exists only when the linked MariaDB was built with
// DBUG, which the shell signals by *not* defining DBUG_OFF. A Debug shell
// (NDEBUG unset) linked against a non-Debug MariaDB has DBUG_OFF defined, so
// fall back to the no-op DBUG_TRACE below instead of referencing missing _db_*.
#if !defined(DBUG_PRETTY_FUNCTION) && !defined(NDEBUG) && !defined(DBUG_OFF)

#if defined(__GNUC__)
// GCC, Clang, and compatible compilers.
#define DBUG_PRETTY_FUNCTION __PRETTY_FUNCTION__
#elif defined(__FUNCSIG__)
// For MSVC; skips the __cdecl. (__PRETTY_FUNCTION__ in GCC is not a
// preprocessor constant, but __FUNCSIG__ in MSVC is.)
#define DBUG_PRETTY_FUNCTION strchr(__FUNCSIG__, ' ') + 1
#else
// Standard C++; does not include the class name.
#define DBUG_PRETTY_FUNCTION __func__
#endif

/**
  A RAII helper to do DBUG_ENTER / DBUG_RETURN for you automatically. Use
  like this:

   int foo() {
     DBUG_TRACE;
     return 42;
   }
 */
class AutoDebugTrace {
 public:
  AutoDebugTrace(const char *function, const char *filename, int line) {
    // Remove the return type, if it's there.
    const char *begin = strchr(function, ' ');
    if (begin != nullptr) {
      function = begin + 1;
    }

    // Cut it off at the first parenthesis; the argument list is
    // often too long to be interesting.
    const char *end = strchr(function, '(');

    if (end == nullptr) {
      _db_enter_(function, filename, line, &m_stack_frame);
    } else {
      _db_enter_(function, /* end - function, */ filename, line,
                 &m_stack_frame);
    }
  }

  ~AutoDebugTrace() { _db_return_(&m_stack_frame); }

 private:
  _db_stack_frame_ m_stack_frame;
};

#undef DBUG_TRACE
#define DBUG_TRACE \
  AutoDebugTrace _db_trace(DBUG_PRETTY_FUNCTION, __FILE__, __LINE__)

#else

#undef DBUG_TRACE

#define DBUG_TRACE \
  do {             \
  } while (0)

#endif

// See the note above: only override DBUG_LOG with the _db_*()-based
// implementation when the DBUG runtime is actually present (DBUG not turned
// off). Otherwise leave MariaDB's no-op DBUG_LOG from my_dbug.h in place.
#if !defined(NDEBUG) && !defined(DBUG_OFF)

// workaround for BUG30071277

#undef DBUG_LOG

#ifndef MARIADB_BUILD
#define DBUG_LOG(keyword, v)                 \
  do {                                       \
    _db_pargs_(__LINE__, keyword);           \
    if (_db_enabled_()) {                    \
      std::ostringstream sout;               \
      sout << v;                             \
      _db_doprnt_("%s", sout.str().c_str()); \
    }                                        \
  } while (0)
#else
#define DBUG_LOG(keyword, v)                 \
  do {                                       \
    _db_pargs_(__LINE__, keyword);           \
    if (DBUG_IF(keyword)) {                  \
      std::ostringstream sout;               \
      sout << v;                             \
      _db_doprnt_("%s", sout.str().c_str()); \
    }                                        \
  } while (0)
#endif  // MARIADB_BUILD

#endif  // !NDEBUG

#ifdef MARIADB_BUILD
// MariaDB's my_dbug.h provides DBUG_IF but not MySQL's DBUG_EVALUATE_IF.
// In NDEBUG builds DBUG_IF is 0, so this yields the false value, matching
// MySQL's non-debug behavior.
#ifndef DBUG_EVALUATE_IF
#define DBUG_EVALUATE_IF(keyword, a, b) (DBUG_IF(keyword) ? (a) : (b))
#endif
#endif  // MARIADB_BUILD

#endif  // MYSQLSHDK_LIBS_UTILS_DEBUG_H_
