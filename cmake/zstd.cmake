# Copyright (c) 2020, 2024, Oracle and/or its affiliates.
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
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

# cmake -DWITH_ZSTD=bundled|system
# system is the default

include(mysql_server_dependency)

macro (FIND_SYSTEM_ZSTD)
  find_path(PATH_TO_ZSTD NAMES zstd.h)
  find_library(ZSTD_SYSTEM_LIBRARY NAMES zstd)
  if(PATH_TO_ZSTD AND ZSTD_SYSTEM_LIBRARY)
    set(SYSTEM_ZSTD_FOUND 1)
    include_directories(SYSTEM ${PATH_TO_ZSTD})
    set(ZSTD_LIBRARY ${ZSTD_SYSTEM_LIBRARY})
    message(STATUS "PATH_TO_ZSTD ${PATH_TO_ZSTD}")
    message(STATUS "ZSTD_LIBRARY ${ZSTD_LIBRARY}")
  endif()
endmacro()

macro(MYSQL_USE_BUNDLED_ZSTD)
  if(MYSQL_SOURCE_DIR AND MYSQL_BUILD_DIR)
    set(WITH_ZSTD "bundled" CACHE STRING "By default use bundled zstd library")
    set(BUILD_BUNDLED_ZSTD 1)

    mysql_resolve_versioned_dependency("zstd"
        "${MYSQL_SOURCE_DIR}/extra/zstd" "zstd-" ZSTD_SOURCE_DIR ZSTD_VERSION)
    set(ZSTD_INCLUDE_FILE "${ZSTD_SOURCE_DIR}/lib/zstd.h")

    if(NOT EXISTS "${ZSTD_INCLUDE_FILE}")
      message(FATAL_ERROR "Could not find \"${ZSTD_INCLUDE_FILE}\"")
    endif()

    get_filename_component(ZSTD_INCLUDE_DIR ${ZSTD_INCLUDE_FILE} DIRECTORY)
    include_directories(BEFORE SYSTEM ${ZSTD_INCLUDE_DIR})

    if(WIN32)
      find_file(ZSTD_LIBRARY NAMES zstd.lib PATHS "${MYSQL_BUILD_DIR}/archive_output_directory/"
        PATH_SUFFIXES ${CMAKE_BUILD_TYPE} RelWithDebInfo Release Debug)
    else()
      find_file(ZSTD_LIBRARY NAMES libzstd.a PATHS "${MYSQL_BUILD_DIR}/archive_output_directory/")
    endif()
  endif()
endmacro()

macro(MYSQL_USE_ZSTD_SOURCE_DIR)
  if(NOT EXISTS "${WITH_ZSTD}/lib/zstd.h")
    message(FATAL_ERROR "Could not find \"zstd.h\" in \"${WITH_ZSTD}/lib\". "
      "WITH_ZSTD must be bundled, system or a path to the zstd sources.")
  endif()

  if(NOT EXISTS "${WITH_ZSTD}/build/cmake/CMakeLists.txt")
    message(FATAL_ERROR "Could not find \"${WITH_ZSTD}/build/cmake/CMakeLists.txt\". "
      "WITH_ZSTD must be bundled, system or a path to the zstd sources.")
  endif()

  include_directories(BEFORE SYSTEM "${WITH_ZSTD}/lib")
  add_subdirectory("${WITH_ZSTD}/build/cmake" zstd)
  set(ZSTD_LIBRARY libzstd_static)

  message(STATUS "PATH_TO_ZSTD ${WITH_ZSTD}/lib")
  message(STATUS "ZSTD_LIBRARY ${ZSTD_LIBRARY}")
endmacro()

macro(USE_BUNDLED_ZSTD)
  file(GLOB_RECURSE ZSTD_INCLUDE_FILE ${WITH_ZSTD}/include/zstd.h)

  if(NOT ZSTD_INCLUDE_FILE)
    message(FATAL_ERROR "Could not find \"zstd.h\" in \"${WITH_ZSTD}/include\". WITH_ZSTD must point to the zstd install directory.")
  endif()

  get_filename_component(ZSTD_INCLUDE_DIR ${ZSTD_INCLUDE_FILE} DIRECTORY)
  include_directories(BEFORE SYSTEM ${ZSTD_INCLUDE_DIR})

  if(WIN32)
    find_file(ZSTD_LIBRARY NAMES zstd.lib PATHS "${WITH_ZSTD}/lib/"
      PATH_SUFFIXES ${CMAKE_BUILD_TYPE} RelWithDebInfo Release Debug)
  else()
    find_file(ZSTD_LIBRARY NAMES libzstd.a PATHS "${WITH_ZSTD}/lib/" "${WITH_ZSTD}/lib64/")
  endif()
endmacro()

if(NOT WITH_ZSTD)
  set(WITH_ZSTD "system" CACHE STRING "By default use system zstd library")
endif()

macro(MYSQL_CHECK_ZSTD)
  if(WITH_ZSTD STREQUAL "bundled")
    MYSQL_USE_BUNDLED_ZSTD()
  elseif(WITH_ZSTD STREQUAL "system")
    FIND_SYSTEM_ZSTD()
    if(NOT SYSTEM_ZSTD_FOUND)
      message(FATAL_ERROR "Cannot find system zstd libraries.")
    endif()
  else()
    IF (MARIADB_BUILD)
      USE_BUNDLED_ZSTD()
    ELSE()
      MYSQL_USE_ZSTD_SOURCE_DIR()
    ENDIF()
  endif()
endmacro()
