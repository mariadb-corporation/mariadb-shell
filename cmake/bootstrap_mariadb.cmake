# Copyright (c) 2026, MariaDB Corporation.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,  but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
# the GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
#
##############################################################################
#
# bootstrap_mariadb.cmake
#
# Configure-time fallback that fetches the MariaDB server sources and builds
# only the pieces the shell links against, then sets MARIADB_SOURCE_DIR and
# MARIADB_BUILD_DIR in the parent scope so the main build proceeds unchanged.
#
# The shell consumes, from a MariaDB server tree (see the root CMakeLists.txt):
#   headers : libmariadb/include, include, sql, <build>/include,
#             <build>/libmariadb/include
#   libs    : libmariadbclient, mysys, mysys_ssl, strings, dbug  (static)
#   plugin  : caching_sha2_password (client auth plugin)
#   cache   : <build>/CMakeCache.txt   (read to detect Debug/DBUG)
#   version : <source>/VERSION
#
# This module therefore configures the full server tree (needed for generated
# headers) but builds ONLY that target subset -- storage engines and the
# server binary are never compiled.
#
##############################################################################

# The MariaDB server this port tracks. NOTE: the shell links the server's
# INTERNAL static libraries (mysys/strings/dbug), which are not a stable ABI --
# a mismatched ref will fail to build or link. Pin MARIADB_GIT_TAG to a ref
# known to match this shell revision.
SET(MARIADB_GIT_REPOSITORY "https://github.com/MariaDB/server.git"
  CACHE STRING "Git URL of the MariaDB server to fetch when no source dir is given")
SET(MARIADB_GIT_TAG "main"
  CACHE STRING "Git ref (branch, tag, or commit) of the MariaDB server to fetch")

# Where the server is cloned and built. Kept OUTSIDE the shell build dir by
# default so a clean shell rebuild does not force a full server re-clone/rebuild.
SET(MARIADB_BOOTSTRAP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../mariadb-server"
  CACHE PATH "Directory holding the auto-fetched MariaDB server source + build")

GET_FILENAME_COMPONENT(MARIADB_BOOTSTRAP_DIR "${MARIADB_BOOTSTRAP_DIR}" ABSOLUTE)
SET(_mdb_src "${MARIADB_BOOTSTRAP_DIR}")
SET(_mdb_bld "${MARIADB_BOOTSTRAP_DIR}/bld")

# Build type to configure the server with. Detecting Debug matters: the shell
# reads the server's CMakeCache.txt to decide whether the DBUG runtime is
# present (see root CMakeLists.txt). Mirror the shell's build type when known.
IF(CMAKE_BUILD_TYPE)
  SET(_mdb_build_type "${CMAKE_BUILD_TYPE}")
ELSE()
  SET(_mdb_build_type "RelWithDebInfo")
ENDIF()

# Static-library names differ by platform (libmysys.a vs mysys.lib). Use CMake's
# prefix/suffix so the "already built?" probe matches on every toolchain.
SET(_slp "${CMAKE_STATIC_LIBRARY_PREFIX}")
SET(_sls "${CMAKE_STATIC_LIBRARY_SUFFIX}")
SET(_mdb_client_lib_dir "${_mdb_bld}/libmariadb/libmariadb")
SET(_mdb_mysys_lib "${_mdb_bld}/mysys/${_slp}mysys${_sls}")

##############################################################################
# Fast path: already bootstrapped. If the server cache and the key static libs
# exist, skip the (expensive) clone/configure/build and just export the dirs.
##############################################################################
IF(EXISTS "${_mdb_bld}/CMakeCache.txt"
   AND EXISTS "${_mdb_mysys_lib}"
   AND (EXISTS "${_mdb_client_lib_dir}/${_slp}mariadbclient${_sls}"
        OR EXISTS "${_mdb_client_lib_dir}/mariadbclient${_sls}"))
  MESSAGE(STATUS "MariaDB server already bootstrapped at ${MARIADB_BOOTSTRAP_DIR}; "
    "reusing it. Delete that directory (or set MARIADB_SOURCE_DIR) to change this.")
  SET(MARIADB_SOURCE_DIR "${_mdb_src}" CACHE PATH "Auto-fetched MariaDB server source" FORCE)
  SET(MARIADB_BUILD_DIR  "${_mdb_bld}" CACHE PATH "Auto-built MariaDB server build"  FORCE)
  RETURN()
ENDIF()

MESSAGE(STATUS "==========================================================")
MESSAGE(STATUS "No MYSQL/MARIADB source dir given -- bootstrapping MariaDB.")
MESSAGE(STATUS "  repo    : ${MARIADB_GIT_REPOSITORY}")
MESSAGE(STATUS "  ref     : ${MARIADB_GIT_TAG}")
MESSAGE(STATUS "  location: ${MARIADB_BOOTSTRAP_DIR}")
MESSAGE(STATUS "  (override with -DMARIADB_SOURCE_DIR / -DMARIADB_GIT_TAG /")
MESSAGE(STATUS "   -DMARIADB_BOOTSTRAP_DIR; this runs once and is then cached)")
MESSAGE(STATUS "==========================================================")

FIND_PACKAGE(Git REQUIRED)

FILE(MAKE_DIRECTORY "${MARIADB_BOOTSTRAP_DIR}")

##############################################################################
# 1. Clone (or reuse) the server source and check out the requested ref.
##############################################################################
IF(NOT EXISTS "${_mdb_src}/.git")
  MESSAGE(STATUS "Cloning MariaDB server (this can take a while)...")
  # Speed: --depth 1 grabs only the tip commit (no history), --single-branch
  # (implied by --depth) + --no-tags skips every other branch and MariaDB's many
  # tags, and --progress forces git's progress meter even when stderr is not a
  # TTY (e.g. the IDE build panel), so this reads like a normal `git clone`.
  EXECUTE_PROCESS(
    COMMAND "${GIT_EXECUTABLE}" clone --progress --depth 1 --no-tags
            --branch "${MARIADB_GIT_TAG}"
            "${MARIADB_GIT_REPOSITORY}" "${_mdb_src}"
    RESULT_VARIABLE _rc)
  IF(NOT _rc EQUAL 0)
    # --branch (and shallow) fails on a raw commit SHA; fall back to a full clone
    # + checkout, which needs the history so an arbitrary SHA is reachable.
    # Clear any partial checkout first so the full clone lands in an empty dir.
    MESSAGE(STATUS "Branch/tag clone failed; cloning default and checking out ref...")
    FILE(REMOVE_RECURSE "${_mdb_src}")
    EXECUTE_PROCESS(
      COMMAND "${GIT_EXECUTABLE}" clone --progress "${MARIADB_GIT_REPOSITORY}" "${_mdb_src}"
      RESULT_VARIABLE _rc)
    IF(NOT _rc EQUAL 0)
      MESSAGE(FATAL_ERROR "Failed to clone ${MARIADB_GIT_REPOSITORY}")
    ENDIF()
    EXECUTE_PROCESS(
      COMMAND "${GIT_EXECUTABLE}" -C "${_mdb_src}" checkout "${MARIADB_GIT_TAG}"
      RESULT_VARIABLE _rc)
    IF(NOT _rc EQUAL 0)
      MESSAGE(FATAL_ERROR "Failed to check out ref '${MARIADB_GIT_TAG}'")
    ENDIF()
  ENDIF()
ELSE()
  MESSAGE(STATUS "Reusing existing MariaDB checkout at ${_mdb_src}")
ENDIF()

# The Connector/C (libmariadb) lives in a git submodule; the shell links it.
# Initialise only that submodule -- the storage-engine submodules are large and
# unused here.
MESSAGE(STATUS "Initialising the libmariadb submodule...")
# --depth 1 fetches only the submodule tip (no history); --progress keeps the
# meter visible in non-TTY build panels, matching the clone above.
EXECUTE_PROCESS(
  COMMAND "${GIT_EXECUTABLE}" -C "${_mdb_src}" submodule update --init --progress
          --depth 1 libmariadb
  RESULT_VARIABLE _rc)
IF(NOT _rc EQUAL 0)
  MESSAGE(FATAL_ERROR "Failed to init the libmariadb submodule")
ENDIF()

##############################################################################
# 2. Configure the server build tree.
##############################################################################
FILE(MAKE_DIRECTORY "${_mdb_bld}")

SET(_mdb_configure_args
  "-DCMAKE_BUILD_TYPE=${_mdb_build_type}"
  # Build the Connector/C against OpenSSL. On macOS/Linux it otherwise defaults
  # to GnuTLS, which clashes with the shell's OpenSSL at link time.
  "-DCONC_WITH_SSL=OPENSSL"
  # Trim configure/build cost: we never build the server binary or engines.
  "-DPLUGIN_TOKUDB=NO"
  "-DPLUGIN_ROCKSDB=NO"
  "-DPLUGIN_MROONGA=NO"
  "-DPLUGIN_SPIDER=NO"
  "-DPLUGIN_COLUMNSTORE=NO"
  "-DWITHOUT_SERVER=1")

# Propagate the OpenSSL location the caller chose for the shell, so the server
# and the shell agree on OpenSSL (see MARIADB_PORT.md).
IF(OPENSSL_ROOT_DIR)
  LIST(APPEND _mdb_configure_args "-DOPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}")
ENDIF()

# When the shell is configured through vcpkg (Windows, or macOS for a
# redistributable build), the server tree must use the same toolchain file and
# target triplet or its dependencies (OpenSSL, etc.) will not resolve and the
# ABI can mismatch the shell. Propagate what the caller chose. This mirrors the
# USING_VCPKG detection in the top-level CMakeLists.txt, replicated here because
# this file is included before that variable is defined.
IF(VCPKG_TARGET_TRIPLET AND CMAKE_TOOLCHAIN_FILE MATCHES "[Vv]cpkg")
  LIST(APPEND _mdb_configure_args "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
  LIST(APPEND _mdb_configure_args "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}")
ENDIF()

# Match the shell's generator so nested "cmake --build" uses the same toolchain.
IF(CMAKE_GENERATOR)
  LIST(APPEND _mdb_configure_args "-G" "${CMAKE_GENERATOR}")
ENDIF()

# Invoke via WORKING_DIRECTORY + trailing source path rather than -S/-B so this
# works on the older CMake versions the project still supports (-S/-B need 3.13).
MESSAGE(STATUS "Configuring MariaDB server (${_mdb_build_type})...")
EXECUTE_PROCESS(
  COMMAND "${CMAKE_COMMAND}" ${_mdb_configure_args} "${_mdb_src}"
  WORKING_DIRECTORY "${_mdb_bld}"
  RESULT_VARIABLE _rc)
IF(NOT _rc EQUAL 0)
  MESSAGE(FATAL_ERROR "MariaDB server configure failed (see output above)")
ENDIF()

##############################################################################
# 3. Build only the targets the shell links against.
##############################################################################
# Core static libs + the connector -- these MUST succeed. Build one target per
# invocation: passing several targets to a single "--build --target" needs
# CMake >= 3.15, and the project still supports older versions.
# Parallel build: "cmake --build --parallel" is CMake >= 3.12. On older CMake we
# omit it (Ninja still parallelises by default; Make/MSBuild fall back to serial).
SET(_mdb_build_parallel "")
IF(NOT CMAKE_VERSION VERSION_LESS "3.12")
  SET(_mdb_build_parallel "--parallel")
ENDIF()

SET(_mdb_core_targets mariadbclient mysys mysys_ssl caching_sha2_password GenError)
FOREACH(_tgt ${_mdb_core_targets})
  MESSAGE(STATUS "Building MariaDB core lib: ${_tgt}")
  EXECUTE_PROCESS(
    COMMAND "${CMAKE_COMMAND}" --build "${_mdb_bld}"
            --config "${_mdb_build_type}"
            --target "${_tgt}" ${_mdb_build_parallel}
    RESULT_VARIABLE _rc)
  IF(NOT _rc EQUAL 0)
    MESSAGE(FATAL_ERROR "Failed to build MariaDB dependency '${_tgt}'")
  ENDIF()
ENDFOREACH()

# Client auth plugin -- best-effort: some connector layouts fold it into the
# connector build, so a missing target here is a warning, not a hard failure.
# MESSAGE(STATUS "Building MariaDB client auth plugin: caching_sha2_password")
# EXECUTE_PROCESS(
#   COMMAND "${CMAKE_COMMAND}" --build "${_mdb_bld}"
#           --config "${_mdb_build_type}"
#           --target caching_sha2_password ${_mdb_build_parallel}
#   RESULT_VARIABLE _rc)
# IF(NOT _rc EQUAL 0)
#   MESSAGE(WARNING "Could not build 'caching_sha2_password' as a standalone "
#     "target; if authentication plugins are missing later, build it manually "
#     "in ${_mdb_bld}.")
# ENDIF()

##############################################################################
# 4. Export the resolved dirs for the rest of the build.
##############################################################################
SET(MARIADB_SOURCE_DIR "${_mdb_src}" CACHE PATH "Auto-fetched MariaDB server source" FORCE)
SET(MARIADB_BUILD_DIR  "${_mdb_bld}" CACHE PATH "Auto-built MariaDB server build"  FORCE)

MESSAGE(STATUS "MariaDB bootstrap complete:")
MESSAGE(STATUS "  MARIADB_SOURCE_DIR = ${MARIADB_SOURCE_DIR}")
MESSAGE(STATUS "  MARIADB_BUILD_DIR  = ${MARIADB_BUILD_DIR}")
