# Copyright (c) 2026, Oracle and/or its affiliates.
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

OPTION(WITH_PARFAIT "Build MySQL Shell for Parfait analysis without test code" OFF)

IF(WITH_PARFAIT)
  # Parfait 2026.04 uses a clang-15 based translator that cannot parse the
  # C++23 standard-library headers from the current Apple SDK.
  SET(PARFAIT_CXX_STD "20" CACHE STRING "C++ standard used for Parfait builds")

  IF(WIN32)
    SET(PARFAIT_C_WRAPPER_NAMES parfait-cl)
    SET(PARFAIT_CXX_WRAPPER_NAMES parfait-cl)
  ELSEIF(APPLE)
    SET(PARFAIT_C_WRAPPER_NAMES parfait-clang parfait-gcc)
    SET(PARFAIT_CXX_WRAPPER_NAMES parfait-clang++ parfait-g++)
  ELSE()
    SET(PARFAIT_C_WRAPPER_NAMES parfait-gcc parfait-clang)
    SET(PARFAIT_CXX_WRAPPER_NAMES parfait-g++ parfait-clang++)
  ENDIF()

  SET(PARFAIT_DEFAULT_PATH /usr/local/parfait/bin)

  FIND_PROGRAM(PARFAIT_C_COMPILER NAMES ${PARFAIT_C_WRAPPER_NAMES}
    PATHS ${PARFAIT_DEFAULT_PATH})
  FIND_PROGRAM(PARFAIT_CXX_COMPILER NAMES ${PARFAIT_CXX_WRAPPER_NAMES}
    PATHS ${PARFAIT_DEFAULT_PATH})
  FIND_PROGRAM(PARFAIT_AR NAMES parfait-ar PATHS ${PARFAIT_DEFAULT_PATH})
  FIND_PROGRAM(PARFAIT_LD NAMES parfait-ld PATHS ${PARFAIT_DEFAULT_PATH})

  IF(NOT PARFAIT_C_COMPILER)
    MESSAGE(FATAL_ERROR "WITH_PARFAIT requires a Parfait C compiler wrapper (${PARFAIT_C_WRAPPER_NAMES}) in PATH.")
  ENDIF()
  IF(NOT PARFAIT_CXX_COMPILER)
    MESSAGE(FATAL_ERROR "WITH_PARFAIT requires a Parfait C++ compiler wrapper (${PARFAIT_CXX_WRAPPER_NAMES}) in PATH.")
  ENDIF()
  IF(NOT PARFAIT_AR)
    MESSAGE(FATAL_ERROR "WITH_PARFAIT requires parfait-ar in PATH.")
  ENDIF()
  IF(NOT PARFAIT_LD)
    MESSAGE(FATAL_ERROR "WITH_PARFAIT requires parfait-ld in PATH.")
  ENDIF()

  SET(CMAKE_C_COMPILER "${PARFAIT_C_COMPILER}" CACHE FILEPATH "C compiler" FORCE)
  SET(CMAKE_CXX_COMPILER "${PARFAIT_CXX_COMPILER}" CACHE FILEPATH "C++ compiler" FORCE)
  SET(CMAKE_AR "${PARFAIT_AR}" CACHE FILEPATH "Archive tool" FORCE)
  SET(CMAKE_LINKER "${PARFAIT_LD}" CACHE FILEPATH "Linker" FORCE)

  IF(APPLE AND NOT CMAKE_OSX_SYSROOT)
    # Prefer the newest locally installed macOS 15 SDK, whose libc++ headers
    # are still accepted by Parfait's translator. Respect an explicit sysroot.
    SET(PARFAIT_MACOS_SDK_DIRS
      "$ENV{DEVELOPER_DIR}/Platforms/MacOSX.platform/Developer/SDKs"
      "/Library/Developer/CommandLineTools/SDKs"
      "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs")
    SET(PARFAIT_MACOS_SDK_NAMES MacOSX15.4.sdk MacOSX15.sdk)

    FOREACH(sdk_dir ${PARFAIT_MACOS_SDK_DIRS})
      FOREACH(sdk_name ${PARFAIT_MACOS_SDK_NAMES})
        IF(NOT PARFAIT_MACOS_SYSROOT_DEFAULT AND
            EXISTS "${sdk_dir}/${sdk_name}/usr/include/c++/v1")
          SET(PARFAIT_MACOS_SYSROOT_DEFAULT "${sdk_dir}/${sdk_name}")
        ENDIF()
      ENDFOREACH()
    ENDFOREACH()

    IF(PARFAIT_MACOS_SYSROOT_DEFAULT)
      SET(PARFAIT_MACOS_SYSROOT "${PARFAIT_MACOS_SYSROOT_DEFAULT}" CACHE PATH
        "macOS SDK used for Parfait builds")
      SET(CMAKE_OSX_SYSROOT "${PARFAIT_MACOS_SYSROOT}" CACHE PATH
        "macOS SDK used for Parfait builds" FORCE)
    ENDIF()
  ENDIF()

  MESSAGE(STATUS "Building with Parfait support.")
  MESSAGE(STATUS "  C compiler: ${CMAKE_C_COMPILER}")
  MESSAGE(STATUS "  C++ compiler: ${CMAKE_CXX_COMPILER}")
  MESSAGE(STATUS "  Archive tool: ${CMAKE_AR}")
  MESSAGE(STATUS "  Linker: ${CMAKE_LINKER}")
  MESSAGE(STATUS "  C++ standard: ${PARFAIT_CXX_STD}")
  IF(APPLE AND CMAKE_OSX_SYSROOT)
    MESSAGE(STATUS "  macOS SDK: ${CMAKE_OSX_SYSROOT}")
  ENDIF()
  MESSAGE(STATUS "Disabling test code for Parfait build.")
  SET(WITH_TESTS OFF CACHE BOOL "Enable unit-tests" FORCE)
  SET(WITH_TESTS_BENCHMARK OFF CACHE BOOL "Enable benchmark tests" FORCE)
ENDIF()
