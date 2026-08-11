# Copyright (c) 2016, 2026, Oracle and/or its affiliates.
# Copyright (c) 2026, MariaDB Corporation.
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

# This is a small CMake project to copy this directory to the top of
# the source, and at the same time do some modifications to these
# files

cmake_minimum_required(VERSION 3.8.0)
project(mysh_deb_init NONE)

# ----------------------------------------------------------------------
# Set some variables to replace
# Use the version variables from "version.cmake"
# ----------------------------------------------------------------------

set(ROOT_PROJECT_DIR "${CMAKE_SOURCE_DIR}/../..")
include(../../version.cmake)

#
# Timestamp for use in debian/changelog
#
# Note: Debian tools use the timestamp of the last entry in the changelog
# as the value of SOURCE_DATE_EPOCH env. variable which fixes build time
# at that point. This will, for example, fix the build date reported in
# in INFO_BIN file. Here we determine the value of the timestamp of the
# last changelog entry generated from changelog.in below.
#

IF (NOT DEFINED DEB_CHANGELOG_TIMESTAMP)
  execute_process(
    COMMAND date --rfc-2822
    OUTPUT_VARIABLE DEB_CHANGELOG_TIMESTAMP
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  SET (DEB_CHANGELOG_TIMESTAMP ${DEB_CHANGELOG_TIMESTAMP} CACHE STRING "")
ENDIF()

# A string(TIMESTAMP YEAR "%Y") would do but as using "date" above, use the same
IF (NOT DEFINED DEB_COPYRIGHT_YEAR)
  SET (DEB_COPYRIGHT_YEAR "2026" CACHE STRING "")
ENDIF()

set(PRODUCT   "MariaDB Shell ${MYSH_BASE_VERSION}")

if(NOT DEBIAN_REVISION)
  set(DEBIAN_REVISION 1)
endif()

# Distinguish between community and non-community builds, with the
# default being a community build. This does not impact the feature
# set that will be compiled in; it's merely provided as a hint to
# custom packaging steps.
option(COMMUNITY_BUILD "Set to true if this is a community build" ON)

if(NOT COMMUNITY_BUILD)
  set(PRODUCT_SUFFIX "-commercial")
  set(CONFLICTING_PRODUCT_SUFFIX "")
  set(VERSION "${MYSH_VERSION}+commercial-${DEBIAN_REVISION}")
else()
  set(PRODUCT_SUFFIX "")
  set(CONFLICTING_PRODUCT_SUFFIX "-commercial")
  set(VERSION "${MYSH_VERSION}-${DEBIAN_REVISION}")
endif()

# ----------------------------------------------------------------------
# Find out if Debian/Ubuntu, the codename, distribution and version
# ----------------------------------------------------------------------

execute_process(
  COMMAND lsb_release --short --id
  OUTPUT_VARIABLE lsb_id
  RESULT_VARIABLE lsb_result
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT lsb_result EQUAL 0)
  message(FATAL_ERROR "Can't run lsb_release")
endif()

string(TOLOWER "${lsb_id}" lsb_id)

if(NOT lsb_id STREQUAL "debian" AND NOT lsb_id STREQUAL "ubuntu")
  message(FATAL_ERROR "We can only handle Debian or Ubuntu Deb packaging")
endif()

execute_process(
  COMMAND lsb_release --short --release
  OUTPUT_VARIABLE lsb_release
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
  COMMAND lsb_release --short --codename
  OUTPUT_VARIABLE CODENAME
  OUTPUT_STRIP_TRAILING_WHITESPACE
)


if(lsb_id STREQUAL "debian")
  # For Debian we want just the major release number
  string(REGEX REPLACE "\\..*" "" lsb_release "${lsb_release}")
endif()

set(ID_RELEASE "${lsb_id}${lsb_release}")

message(STATUS "OS distribution : ${ID_RELEASE}")
message(STATUS "OS code name    : ${CODENAME}")

# ----------------------------------------------------------------------
# Create the argument list to the "cmake" call and possibly extra deps
# if not passed as argument
# ----------------------------------------------------------------------

set(EXTRA_CMAKE_OPTS)
set(DEB_BUILD_DEPS)
set(DEB_DEPS)
if(BUNDLED_SSH_DIR)
  list(APPEND EXTRA_CMAKE_OPTS "-DBUNDLED_SSH_DIR=${BUNDLED_SSH_DIR}")
else()
  list(APPEND DEB_BUILD_DEPS "libssh-dev (>=0.9.2)")
endif()

if(WITH_SSL)
  list(APPEND EXTRA_CMAKE_OPTS "-DWITH_SSL=${WITH_SSL}")
else()
  list(APPEND DEB_BUILD_DEPS "openssl")
endif()

if(WITH_CURL)
  list(APPEND EXTRA_CMAKE_OPTS "-DWITH_CURL=${WITH_CURL}")
else()
  list(APPEND DEB_BUILD_DEPS "libcurl4-openssl-dev | libcurl-dev")
endif()

if(MYSQLCLIENT_STATIC_LINKING OR ENV{MYSQLCLIENT_STATIC_LINKING})
  list(APPEND EXTRA_CMAKE_OPTS "-DMYSQLCLIENT_STATIC_LINKING=ON")
endif()

if(NOT MYSQL_DIR AND NOT MYSQL_INCLUDE_DIR AND NOT MYSQL_LIB_DIR AND
   "$ENV{MYSQL_DIR}" AND NOT "$ENV{MYSQL_INCLUDE_DIR}" AND NOT "$ENV{MYSQL_LIB_DIR}")
  list(APPEND DEB_BUILD_DEPS "libmysqlclient-dev")
endif()

IF (NOT MARIADB_SOURCE_DIR)
  if(WITH_PROTOBUF)
    list(APPEND EXTRA_CMAKE_OPTS "-DWITH_PROTOBUF=${WITH_PROTOBUF}")
  else()
    if (NOT MYSQL_SOURCE_DIR)
      list(APPEND DEB_BUILD_DEPS "libprotobuf-dev")
      list(APPEND DEB_BUILD_DEPS "protobuf-compiler")
    endif()
  endif()
ENDIF()

if(JIT_EXECUTOR_LIB)
  list(APPEND EXTRA_CMAKE_OPTS "-DJIT_EXECUTOR_LIB=${JIT_EXECUTOR_LIB}")
endif()

if(HAVE_PYTHON)
  list(APPEND EXTRA_CMAKE_OPTS "-DHAVE_PYTHON=1")
  if (BUNDLED_PYTHON_DIR)
    list(APPEND EXTRA_CMAKE_OPTS "-DBUNDLED_PYTHON_DIR=${BUNDLED_PYTHON_DIR}")
  else()
    list(APPEND DEB_BUILD_DEPS "python3-dev (>= 3.8)")
  endif()
endif()

if(MYSQL_SOURCE_DIR)
  list(APPEND EXTRA_CMAKE_OPTS "-DMYSQL_SOURCE_DIR=${MYSQL_SOURCE_DIR} -DMYSQL_BUILD_DIR=${MYSQL_SOURCE_DIR}/bld")
endif()

if(MARIADB_SOURCE_DIR)
  list(APPEND EXTRA_CMAKE_OPTS "-DMARIADB_SOURCE_DIR=${MARIADB_SOURCE_DIR} -DMARIADB_BUILD_DIR=${MARIADB_SOURCE_DIR}/bld")
endif()

if(WITH_GMOCK OR WITH_TESTS)
  list(APPEND EXTRA_CMAKE_OPTS "-DWITH_TESTS=ON")
endif()

if(PYTHON_DEPS)
  list(APPEND EXTRA_CMAKE_OPTS "-DPYTHON_DEPS=${PYTHON_DEPS}")
endif()

if (BUNDLED_MYSQL_CONFIG_EDITOR)
  list(APPEND EXTRA_CMAKE_OPTS "-DBUNDLED_MYSQL_CONFIG_EDITOR=${BUNDLED_MYSQL_CONFIG_EDITOR}")
endif()

if (BUNDLED_MYSQLBINLOG)
  list(APPEND EXTRA_CMAKE_OPTS "-DBUNDLED_MYSQLBINLOG=${BUNDLED_MYSQLBINLOG}")
endif()

if(BUNDLED_ANTLR_DIR)
  list(APPEND EXTRA_CMAKE_OPTS "-DBUNDLED_ANTLR_DIR=${BUNDLED_ANTLR_DIR}")
else()
  list(APPEND DEB_BUILD_DEPS "libantlr4-runtime-dev (>=4.10)")
  list(APPEND DEB_BUILD_DEPS "libantlr4-runtime-dev (<<4.11)")
endif()

string(REPLACE ";" " "  EXTRA_CMAKE_OPTS "${EXTRA_CMAKE_OPTS}")
string(REPLACE ";" ", " DEB_BUILD_DEPS   "${DEB_BUILD_DEPS}")
string(REPLACE ";" ", " DEB_DEPS         "${DEB_DEPS}")

# ----------------------------------------------------------------------
# Copy this directory and process the .in files
# ----------------------------------------------------------------------

set(DEST_DIR ${CMAKE_SOURCE_DIR}/../../debian)

file(
  COPY ${CMAKE_SOURCE_DIR}/
  DESTINATION ${DEST_DIR}
  PATTERN "*.in"   EXCLUDE
  PATTERN "CMake*" EXCLUDE
)

set(in_files
  changelog
  control
  copyright
  rules
  postrm
)

foreach(_in_file ${in_files})
  string(REPLACE "PRODUCTSUFFIX" "${PRODUCT_SUFFIX}" _out_file "${_in_file}")
  configure_file(
    ${CMAKE_SOURCE_DIR}/${_in_file}.in
    ${DEST_DIR}/${_out_file}
    @ONLY
  )
endforeach()

# We need the .install file to be dynamic depending on BUNDLED_PYTHON_DIR
# option, and configure_file() does not have control statements, therefore we
# are creating this file in following way.
set(INSTALL_FILE "${DEST_DIR}/mariadb-shell${PRODUCT_SUFFIX}.install")

file(WRITE "${INSTALL_FILE}"
"usr/bin/mariadb-shell
usr/bin/msh
usr/bin/mariadb-secret-store-secret-service
usr/bin/mariadb-secret-store-login-path
usr/share/mariadb-shell/LICENSE    usr/share/doc/mariadb-shell
usr/share/mariadb-shell/README.md     usr/share/doc/mariadb-shell
usr/share/mariadb-shell/Docs/INFO_SRC     usr/share/mariadb-shell/Docs
usr/share/mariadb-shell/Docs/INFO_BIN     usr/share/mariadb-shell/Docs
usr/share/mariadb-shell/prompt/*   usr/share/mariadb-shell/prompt
usr/lib/mariadb-shell/plugins
usr/lib/mariadb-shell/python-packages
usr/share/man/man1/mariadb-shell.1
usr/share/man/man1/msh.1
")

# The following are MySQL-server specific and are not built for MariaDB. This
# project only sees the cache variables passed in (it is configured standalone,
# see project() above), not the top-level HAVE_ADMIN_API / HAVE_UPGRADE_CHECKER,
# so key off MARIADB_SOURCE_DIR the same way the top-level CMakeLists.txt derives
# those flags.
if (NOT MARIADB_SOURCE_DIR)
file(APPEND "${INSTALL_FILE}"
"# AdminAPI metadata scripts
usr/share/mariadb-shell/adminapi-metadata/* usr/share/mariadb-shell/adminapi-metadata
# Upgrade Checker resources
usr/share/mariadb-shell/upgrade_checker.msg
usr/share/mariadb-shell/sysvars.json
")
endif()

# Bundled mysqlbinlog, used by util.dumpBinlogs/loadBinlogs.
if (BUNDLED_MYSQLBINLOG)
file(APPEND "${INSTALL_FILE}"
"usr/libexec/mariadb-shell/mysqlbinlog
")
endif()

if (BUNDLED_PYTHON_DIR OR PYTHON_DEPS)
file(APPEND "${INSTALL_FILE}"
"usr/lib/mariadb-shell/lib/*   usr/lib/mariadb-shell/lib
")
endif()

if (BUNDLED_PYTHON_DIR)
file(APPEND "${INSTALL_FILE}"
"# For python bundle
usr/lib/mariadb-shell/include
usr/lib/mariadb-shell/libpython*.so*
usr/lib/mariadb-shell/bin/python3.*
")
endif()

if (BUNDLED_SSH_DIR)
file(APPEND "${INSTALL_FILE}"
"# For libssh bundle
usr/lib/mariadb-shell/libssh*.so*
")
endif()

if (MYSQL_SOURCE_DIR)
file(APPEND "${INSTALL_FILE}"
"# For client authentication plugins
usr/lib/mariadb/plugins/*.so
usr/lib/mariadb-shell/libfido2.so*
# For protobuf library
usr/lib/mariadb-shell/libprotobuf*.so*
usr/lib/mariadb-shell/libabsl_*.so
")
endif()

if (MARIADB_SOURCE_DIR)
file(APPEND "${INSTALL_FILE}"
"# For client authentication plugins
usr/lib/mariadb/plugins/*.so
")
endif()

if (BUNDLED_MYSQL_CONFIG_EDITOR OR MYSQL_DIR)
file(APPEND "${INSTALL_FILE}"
"
usr/libexec/mariadb-shell/mysql_config_editor
")
endif()

if (BUNDLED_ANTLR_DIR)
file(APPEND "${INSTALL_FILE}"
"# For antlr4 bundle
usr/lib/mariadb-shell/libantlr4-runtime*.so*
")
endif()

if (WITH_YACC_PARSER)
file(APPEND "${INSTALL_FILE}"
"
usr/lib/mariadb-shell/yparser
")
endif()

if(JIT_EXECUTOR_LIB)
file(APPEND "${INSTALL_FILE}"
"
usr/lib/mariadb-shell/libjitexecutor.so
")
endif()
