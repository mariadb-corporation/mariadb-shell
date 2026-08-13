# Copyright (c) 2010, 2025, Oracle and/or its affiliates.
# Copyright (c) 2026, MariaDB plc.
#
# SPDX-License-Identifier: GPL-2.0-only
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

SET(GOOGLETEST_RELEASE googletest-1.17.0)
IF(MYSQL_SOURCE_DIR)
  SET(GMOCK_SOURCE_DIR "${MYSQL_SOURCE_DIR}/extra/googletest/${GOOGLETEST_RELEASE}/googlemock")
  SET(GTEST_SOURCE_DIR "${MYSQL_SOURCE_DIR}/extra/googletest/${GOOGLETEST_RELEASE}/googletest")
ELSEIF(WITH_GOOGLE_TEST)
  SET(GMOCK_SOURCE_DIR "${WITH_GOOGLE_TEST}/googlemock")
  SET(GTEST_SOURCE_DIR "${WITH_GOOGLE_TEST}/googletest")
ELSE()
  # Neither a bundled source tree nor an explicit path was given: use the
  # Google Test / Google Mock installed on the system.
  FIND_PACKAGE(GTest)
  IF(NOT GTest_FOUND)
    MESSAGE(FATAL_ERROR "Missing Google Test. Install the system Google Test "
      "development packages (e.g. gtest-devel and gmock-devel on Fedora, or "
      "libgtest-dev and libgmock-dev on Debian/Ubuntu), or use "
      "-DWITH_GOOGLE_TEST=<gtest-src-path>")
  ENDIF()

  # FIND_PACKAGE(GTest) sets GTest_FOUND when it locates GTest alone, but the
  # GTest::gmock imported target only exists when Google Mock is also installed
  # (a separate package, e.g. libgmock-dev on Debian/Ubuntu) and CMake is recent
  # enough (>= 3.20) to expose it. This project links GTest::gmock, so require
  # the target explicitly rather than relying on GTest_FOUND alone.
  IF(NOT TARGET GTest::gmock)
    MESSAGE(FATAL_ERROR "Google Test was found but the GTest::gmock target is "
      "missing. Install the Google Mock development package (gmock-devel on "
      "Fedora, libgmock-dev on Debian/Ubuntu) alongside Google Test, ensure "
      "CMake is >= 3.20, or use -DWITH_GOOGLE_TEST=<gtest-src-path>")
  ENDIF()

  SET(GMOCK_FOUND 1)
  SET(GMOCK_FOUND 1 CACHE INTERNAL "" FORCE)

  # The imported GTest::* targets declare INTERFACE_COMPILE_FEATURES "cxx_std_17".
  # This project carries its C++ standard as a raw -std=c++NN flag in
  # CMAKE_CXX_FLAGS rather than via the CXX_STANDARD target property, so CMake
  # cannot see that consumers are already C++23. Left in place, the cxx_std_17
  # requirement makes CMake append -std=gnu++17 *after* our -std=c++23, silently
  # downgrading every unittest translation unit to C++17 (breaking concepts,
  # requires-clauses, std::remove_cvref_t, etc.). Drop the requirement; the
  # project-wide flag already selects a newer, compatible standard.
  FOREACH(_gtest_tgt GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main)
    IF(TARGET ${_gtest_tgt})
      SET_TARGET_PROPERTIES(${_gtest_tgt} PROPERTIES INTERFACE_COMPILE_FEATURES "")
    ENDIF()
  ENDFOREACH()

  # The imported GTest::* targets carry their own SYSTEM include directories,
  # so no extra include paths are needed here.
  SET(GMOCK_INCLUDE_DIRS "" CACHE INTERNAL "")
  SET(GTEST_LIBRARIES GTest::gmock GTest::gtest CACHE INTERNAL "")

  MESSAGE(STATUS "Using system Google Test (GTest::gtest, GTest::gmock)")

  # Skip building Google Test from source (handled below).
  RETURN()
ENDIF()

IF(NOT IS_DIRECTORY "${GTEST_SOURCE_DIR}")
  MESSAGE(FATAL_ERROR "googletest directory not found: ${GTEST_SOURCE_DIR}")
ENDIF()

IF(NOT IS_DIRECTORY "${GMOCK_SOURCE_DIR}")
  MESSAGE(FATAL_ERROR "googlemock directory not found: ${GMOCK_SOURCE_DIR}")
ENDIF()

SET(GMOCK_FOUND 1)
SET(GMOCK_FOUND 1 CACHE INTERNAL "" FORCE)

SET(GMOCK_INCLUDE_DIRS
  ${GMOCK_SOURCE_DIR}
  ${GMOCK_SOURCE_DIR}/include
  ${GTEST_SOURCE_DIR}
  ${GTEST_SOURCE_DIR}/include
  CACHE INTERNAL "")

ADD_LIBRARY(gmock STATIC ${GMOCK_SOURCE_DIR}/src/gmock-all.cc)
ADD_LIBRARY(gtest STATIC ${GTEST_SOURCE_DIR}/src/gtest-all.cc)
SET(GTEST_LIBRARIES gmock gtest)

ADD_LIBRARY(gmock_main STATIC ${GMOCK_SOURCE_DIR}/src/gmock_main.cc)
ADD_LIBRARY(gtest_main STATIC ${GTEST_SOURCE_DIR}/src/gtest_main.cc)

IF(MY_COMPILER_IS_GNU_OR_CLANG)
  SET_TARGET_PROPERTIES(gtest_main gmock_main
    PROPERTIES
    COMPILE_FLAGS "-Wno-undef -Wno-conversion")
ENDIF()

MY_CHECK_CXX_COMPILER_WARNING("-Wmissing-profile" HAS_MISSING_PROFILE)

FOREACH(googletest_library
    gmock
    gtest
    gmock_main
    gtest_main
    )
  TARGET_INCLUDE_DIRECTORIES(${googletest_library} SYSTEM PUBLIC
    ${GMOCK_INCLUDE_DIRS}
    )
  IF(HAS_MISSING_PROFILE)
    TARGET_COMPILE_OPTIONS(${googletest_library} PRIVATE ${HAS_MISSING_PROFILE})
  ENDIF()
ENDFOREACH()
