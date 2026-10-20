# Copyright (c) 2026, MariaDB plc.
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

# The MariaDB SQL language server, mariadb-lsp, from the src/lsp-server
# submodule (mariadb-corporation/lsp-server), installed next to mariadb-shell.
# mcp_plugin's sql tools look for it there.
#
# It is a CMake project of its own, with its own vcpkg manifest and compiler
# settings, so it is built as a separate build (ExternalProject), not added as
# a subdirectory. It gets this build's compiler, vcpkg toolchain and triplet,
# and MariaDB server tree, and links the OpenSSL and zlib this package already
# bundles: its vcpkg.json must keep this one's baseline and their versions,
# which is checked below.
#
#   -DWITH_LSP=AUTO|ON|OFF       AUTO (default): when the submodule is checked out
#   -DLSP_SERVER_SOURCE_DIR=DIR  another lsp-server checkout (default src/lsp-server)
#   -DLSP_SERVER_BUILD_JOBS=N    parallel jobs of its build (default: half the cores)
#
# Sets BUILD_LSP_SERVER, which packaging.cmake reads.

set(WITH_LSP AUTO CACHE STRING "Build and package mariadb-lsp: AUTO, ON or OFF")
set_property(CACHE WITH_LSP PROPERTY STRINGS AUTO ON OFF)
set(LSP_SERVER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/lsp-server" CACHE PATH
    "The lsp-server source tree to build mariadb-lsp from")
set(_lsp_present FALSE)
if(EXISTS "${LSP_SERVER_SOURCE_DIR}/CMakeLists.txt")
  set(_lsp_present TRUE)
endif()

set(BUILD_LSP_SERVER FALSE)
if(WITH_LSP STREQUAL "AUTO")
  if(_lsp_present)
    set(BUILD_LSP_SERVER TRUE)
  else()
    # A fork's pull request, a source archive, or a clone without
    # --recurse-submodules: the shell builds without it.
    message(STATUS "mariadb-lsp is not built: ${LSP_SERVER_SOURCE_DIR} is empty "
      "(git submodule update --init src/lsp-server)")
  endif()
elseif(WITH_LSP)
  if(NOT _lsp_present)
    message(FATAL_ERROR "WITH_LSP is ON but ${LSP_SERVER_SOURCE_DIR} holds no lsp-server "
      "tree: run 'git submodule update --init src/lsp-server', or pass -DWITH_LSP=OFF")
  endif()
  set(BUILD_LSP_SERVER TRUE)
endif()

if(NOT BUILD_LSP_SERVER)
  return()
endif()

# A submodule left behind by a pull or a branch switch builds an older
# mariadb-lsp than this commit pins, silently. Say so.
if(LSP_SERVER_SOURCE_DIR STREQUAL "${CMAKE_SOURCE_DIR}/src/lsp-server")
  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" ls-tree HEAD src/lsp-server
                    OUTPUT_VARIABLE _lsp_tree OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE _lsp_rc1 ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${LSP_SERVER_SOURCE_DIR}" rev-parse HEAD
                    OUTPUT_VARIABLE _lsp_head OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE _lsp_rc2 ERROR_QUIET)
    if(_lsp_rc1 EQUAL 0 AND _lsp_rc2 EQUAL 0
       AND _lsp_tree MATCHES "^160000 commit ([0-9a-f]+)")
      set(_lsp_pin "${CMAKE_MATCH_1}")
      if(NOT _lsp_head STREQUAL _lsp_pin)
        message(WARNING "src/lsp-server is at ${_lsp_head}, not at ${_lsp_pin}, the "
          "commit this tree pins: run 'git submodule update src/lsp-server' unless "
          "you are working on it.")
      endif()
    endif()
  endif()
endif()

# mariadb-lsp links the OpenSSL and zlib this package bundles: both vcpkg
# manifests must resolve them to the same versions.
if(USING_VCPKG)
  file(READ "${CMAKE_SOURCE_DIR}/vcpkg.json" _shell_manifest)
  file(READ "${LSP_SERVER_SOURCE_DIR}/vcpkg.json" _lsp_manifest)
  string(JSON _shell_baseline GET "${_shell_manifest}" builtin-baseline)
  string(JSON _lsp_baseline GET "${_lsp_manifest}" builtin-baseline)
  if(NOT _shell_baseline STREQUAL _lsp_baseline)
    message(FATAL_ERROR "${LSP_SERVER_SOURCE_DIR}/vcpkg.json has builtin-baseline ${_lsp_baseline}, "
      "this tree's vcpkg.json ${_shell_baseline}: they must be the same.")
  endif()
  foreach(_port openssl zlib)
    foreach(_which shell lsp)
      set(_${_which}_${_port} "")
      string(JSON _n ERROR_VARIABLE _err LENGTH "${_${_which}_manifest}" overrides)
      if(NOT _err AND _n GREATER 0)
        math(EXPR _last "${_n} - 1")
        foreach(_i RANGE ${_last})
          string(JSON _name GET "${_${_which}_manifest}" overrides ${_i} name)
          if(_name STREQUAL _port)
            string(JSON _${_which}_${_port} GET "${_${_which}_manifest}" overrides ${_i} version)
          endif()
        endforeach()
      endif()
    endforeach()
    if(NOT _shell_${_port} STREQUAL _lsp_${_port})
      message(FATAL_ERROR "${_port}: this tree's vcpkg.json overrides it to "
        "'${_shell_${_port}}', ${LSP_SERVER_SOURCE_DIR}/vcpkg.json to '${_lsp_${_port}}': "
        "mariadb-lsp links the ${_port} this package bundles, so they must agree.")
    endif()
  endforeach()
endif()

cmake_host_system_information(RESULT _lsp_cores QUERY NUMBER_OF_LOGICAL_CORES)
math(EXPR _lsp_jobs "(${_lsp_cores} + 1) / 2")
set(LSP_SERVER_BUILD_JOBS ${_lsp_jobs} CACHE STRING
    "Parallel jobs of the mariadb-lsp build, which runs beside this one's")

set(_lsp_args
  -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
  -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
  -DWITH_TESTS=0
  -DMDBLS_CHECK_SERVER_PIN=OFF)
foreach(_var CMAKE_C_COMPILER_LAUNCHER CMAKE_CXX_COMPILER_LAUNCHER
             CMAKE_OSX_DEPLOYMENT_TARGET CMAKE_OSX_ARCHITECTURES BISON_EXECUTABLE)
  if(${_var})
    list(APPEND _lsp_args "-D${_var}=${${_var}}")
  endif()
endforeach()
if(USING_VCPKG)
  list(APPEND _lsp_args
    "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
    "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}")
endif()
if(MARIADB_SOURCE_DIR AND MARIADB_BUILD_DIR)
  list(APPEND _lsp_args
    "-DMARIADB_SOURCE_DIR=${MARIADB_SOURCE_DIR}"
    "-DMARIADB_BUILD_DIR=${MARIADB_BUILD_DIR}")
else()
  # No MariaDB server tree: no live connections, the rest works.
  list(APPEND _lsp_args -DWITH_MARIADB_CLIENT=OFF)
endif()

include(ExternalProject)
set(LSP_SERVER_BINARY_DIR "${CMAKE_BINARY_DIR}/lsp-server")
set(_lsp_binary "${LSP_SERVER_BINARY_DIR}/bin/mariadb-lsp${CMAKE_EXECUTABLE_SUFFIX}")
ExternalProject_Add(lsp_server
  SOURCE_DIR "${LSP_SERVER_SOURCE_DIR}"
  BINARY_DIR "${LSP_SERVER_BINARY_DIR}"
  CMAKE_ARGS ${_lsp_args}
  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target mariadb-lsp
                --parallel ${LSP_SERVER_BUILD_JOBS}
  BUILD_BYPRODUCTS "${_lsp_binary}"
  # The sources are a working tree, not a download: always ask its own build
  # system whether anything changed.
  BUILD_ALWAYS TRUE
  INSTALL_COMMAND ""
  USES_TERMINAL_CONFIGURE TRUE
  USES_TERMINAL_BUILD TRUE)

# Copied next to mariadb-shell with the RPATH of the other bundled binaries,
# which resolves the OpenSSL and zlib of INSTALL_LIBDIR (Windows: the DLLs in
# bin), and installed with the main component.
install_bundled_binaries(
  DESCRIPTION "mariadb-lsp, the SQL language server"
  BINARIES "${_lsp_binary}"
  DESTINATION "${INSTALL_BINDIR}"
  TARGET mariadb-shell)
add_dependencies("copy_mariadb-lsp${CMAKE_EXECUTABLE_SUFFIX}" lsp_server)
