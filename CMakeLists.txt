# Copyright (c) 2014, 2026, Oracle and/or its affiliates.
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
MESSAGE(STATUS "Running cmake version ${CMAKE_VERSION}")

# When the caller asks for vcpkg-managed dependencies via -DWITH_VCPKG_TRIPLET,
# fetch and bootstrap vcpkg and derive CMAKE_TOOLCHAIN_FILE / VCPKG_TARGET_TRIPLET
# (and VCPKG_ROOT). This must run before the first project() call (CMake reads
# CMAKE_TOOLCHAIN_FILE there) and before bootstrap_mariadb.cmake, which
# propagates the toolchain + triplet to the server tree. See
# cmake/bootstrap_vcpkg.cmake.
INCLUDE(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bootstrap_vcpkg.cmake)

# When the caller passes -DWITH_PYTHON_SOURCE=<cpython-src>, configure, build and
# install that Python into a cached "Python-<version>" prefix and derive
# BUNDLED_PYTHON_DIR from it (consumed further down and in src/CMakeLists.txt).
# This is included AFTER bootstrap_vcpkg.cmake on purpose: the vcpkg dependency
# closure must already be installed so Python links _ssl/zlib/... against it (via
# CPPFLAGS/LDFLAGS and --with-openssl). See cmake/bootstrap_python.cmake.
INCLUDE(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bootstrap_python.cmake)

# The shell links directly against a MariaDB (or MySQL) server source + build
# tree. When neither is provided, and this is a real shell build (not a source
# package), fetch the MariaDB server sources and build just the pieces the shell
# consumes, then set MARIADB_SOURCE_DIR / MARIADB_BUILD_DIR so the logic below
# proceeds unchanged. See cmake/bootstrap_mariadb.cmake.
IF(NOT MYSQL_SOURCE_DIR AND NOT MARIADB_SOURCE_DIR AND NOT BUILD_SOURCE_PACKAGE)
  INCLUDE(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bootstrap_mariadb.cmake)
ENDIF()

IF(MYSQL_SOURCE_DIR)
  FILE(TO_CMAKE_PATH "${MYSQL_SOURCE_DIR}" MYSQL_SOURCE_DIR)
ENDIF()

IF(MYSQL_BUILD_DIR)
  FILE(TO_CMAKE_PATH "${MYSQL_BUILD_DIR}" MYSQL_BUILD_DIR)
ENDIF()

IF(MARIADB_SOURCE_DIR)
  FILE(TO_CMAKE_PATH "${MARIADB_SOURCE_DIR}" MARIADB_SOURCE_DIR)

  SET(MARIADB_BUILD 1)
  ADD_DEFINITIONS(-DMARIADB_BUILD)

  # The MariaDB server's include/mariadb_capi_rename.h rewrites the client C API
  # symbols (mysql_get_ssl_cipher -> server_mysql_get_ssl_cipher, mysql_init,
  # mysql_real_connect, ...) with a `server_` prefix for the server's OWN
  # internal use (see the header's comment). The shell is a standalone client
  # that links the MariaDB Connector/C (libmariadbclient), which exports the
  # UNPREFIXED symbols -- but server headers on our include path transitively
  # pull that rename in (include/mysql.h includes it), so any TU that includes a
  # server header and then calls a renamed C API function links against the
  # missing server_* symbol (e.g. undefined reference to
  # `server_mysql_get_ssl_cipher` from Session_impl::get_ssl_cipher, which is the
  # only renamed function called inline in session.h). Pre-defining the header's
  # include guard makes it a no-op for every shell TU, so all C API calls resolve
  # to the connector's unprefixed symbols. Harmless where the rename never
  # activated (macOS/Windows); the shell always links the connector.
  ADD_DEFINITIONS(-DMARIADB_CAPI_RENAME_INCLUDED)

  IF(WITH_RAPIDJSON)
    FILE(TO_CMAKE_PATH "${WITH_RAPIDJSON}" WITH_RAPIDJSON)
  ENDIF()
  # RapidJSON headers are resolved after project() (see RAPIDJSON_INCLUDE_DIR
  # below); the system search needs project()'s search paths initialized.
ENDIF()

IF(MARIADB_BUILD_DIR)
  FILE(TO_CMAKE_PATH "${MARIADB_BUILD_DIR}" MARIADB_BUILD_DIR)
ENDIF()

# The Upgrade Checker is MySQL-server specific and is incompatible with MariaDB
# servers, so it is only built (and its code only compiled) for non-MariaDB
# builds. Code gates on the HAVE_UPGRADE_CHECKER definition.
#
# The AdminAPI (InnoDB Cluster/ReplicaSet/ClusterSet) and the X protocol /
# X DevAPI are likewise MySQL-server specific and unavailable for MariaDB
# builds. Code gates on the HAVE_ADMIN_API and HAVE_X_PROTOCOL definitions
# instead of testing MARIADB_BUILD directly.
IF(NOT MARIADB_BUILD)
  SET(HAVE_UPGRADE_CHECKER 1)
  ADD_DEFINITIONS(-DHAVE_UPGRADE_CHECKER)

  SET(HAVE_ADMIN_API 1)
  ADD_DEFINITIONS(-DHAVE_ADMIN_API)

  SET(HAVE_X_PROTOCOL 1)
  ADD_DEFINITIONS(-DHAVE_X_PROTOCOL)

  SET(HAVE_DUMP_AND_LOAD 1)
  ADD_DEFINITIONS(-DHAVE_DUMP_AND_LOAD)

  SET(HAVE_LOGIN_PATH 1)
  ADD_DEFINITIONS(-DHAVE_LOGIN_PATH)
ENDIF()


IF(WIN32)
  # CMake 3.8.0 is needed for Visual Studio 2017 and x64 toolset.
  CMAKE_MINIMUM_REQUIRED(VERSION 3.8.0)
  EXECUTE_PROCESS(COMMAND wmic os get version
    OUTPUT_VARIABLE NT_RELEASE_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE WMIC_RESULT
    )
  IF(WMIC_RESULT EQUAL 0)
    STRING(REPLACE "\r" "" NT_RELEASE_VERSION "${NT_RELEASE_VERSION}")
    STRING(REPLACE "\n" "" NT_RELEASE_VERSION "${NT_RELEASE_VERSION}")
    STRING(REGEX MATCH "Version[ ]+([0-9\.]+)" V_NUM "${NT_RELEASE_VERSION}")
    MESSAGE(STATUS "NT_RELEASE_VERSION is ${NT_RELEASE_VERSION}")
    IF(CMAKE_MATCH_1)
      IF(CMAKE_MATCH_1 VERSION_LESS "10")
        MESSAGE(FATAL_ERROR
          "Need at least Windows Server 2016, or Windows 10, to build")
      ENDIF()
    ENDIF()
  ENDIF()
  IF(CMAKE_GENERATOR MATCHES "Visual Studio" AND CMAKE_GENERATOR MATCHES "2019")
    IF(CMAKE_VERSION MATCHES "MSVC")
      # It is the bundled version, ignore version check,
      # (although this seems to be buggy too).
    ELSE()
      # Bug in msbuild, install the latest in the 3.15 series as a workaround.
      # https://gitlab.kitware.com/cmake/cmake/issues/19303
      # custom commands are re-built every time
      CMAKE_MINIMUM_REQUIRED(VERSION 3.15.3)
    ENDIF()
  ENDIF()
ELSEIF(APPLE)
  # Version 3.12.4 is needed because the new build system of Xcode is not
  # supported by cmake. 3.12.4 will force using the legacy build system.
  # Version 3.9.2 is needed because INCLUDE_DIRECTORIES(SYSTEM ...) wasn't
  # handled properly by the cmake Xcode generator.
  IF(CMAKE_GENERATOR STREQUAL "Xcode")
    CMAKE_MINIMUM_REQUIRED(VERSION 3.12.4)
  ELSE()
    CMAKE_MINIMUM_REQUIRED(VERSION 3.9.2)
  ENDIF()
  # If this is macOS 11, we need cmake 3.18
  # System libraries like
  #    /usr/lib/libresolv.dylib
  # are no longer present in the file system.
  # cmake >= 3.18 will look for .tbd files in the SDK instead
  # So we end up linking with:
  #    /Applications/Xcode.app/.../usr/lib/libresolv.tbd
  # We must postpone the version test until we have called 'uname -r' below.
ELSEIF(UNIX)
  # This is currently minimum version on all supported platforms.
  IF(CMAKE_VERSION VERSION_LESS 3.5.1)
    # Default cmake is 2.8.12.2 on RedHat
    IF(EXISTS "/etc/redhat-release")
      MESSAGE(WARNING "Please use cmake3 rather than cmake on this platform")
      FIND_PROGRAM(MY_CMAKE3 cmake3 /bin /usr/bin /usr/local/bin)
      IF(MY_CMAKE3)
        MESSAGE(STATUS "Found ${MY_CMAKE3}")
      ELSE()
        MESSAGE(STATUS "Please install cmake3 (yum install cmake3)")
      ENDIF()
    ELSE()
      # On SunOS /opt/csw/bin/cmake is (most likely) too old.
      FIND_PROGRAM(MY_UNAME uname /bin /usr/bin /usr/local/bin /sbin)
      IF(MY_UNAME)
        EXECUTE_PROCESS(
          COMMAND uname -s
          OUTPUT_VARIABLE MY_HOST_SYSTEM_NAME
          OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        IF(MY_HOST_SYSTEM_NAME MATCHES "SunOS")
          FIND_PROGRAM(MY_CMAKE cmake /usr/bin
            NO_CMAKE_ENVIRONMENT_PATH
            NO_SYSTEM_ENVIRONMENT_PATH
            )
          IF(MY_CMAKE)
            MESSAGE(STATUS "Found ${MY_CMAKE}")
            EXECUTE_PROCESS(
              COMMAND ${MY_CMAKE} --version
              OUTPUT_VARIABLE _my_cmake_version_out
              OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            MESSAGE(STATUS "${_my_cmake_version_out}")
          ELSE()
            MESSAGE(STATUS "Please install /usr/bin/cmake ")
          ENDIF()
        ENDIF()
      ENDIF()
    ENDIF()
  ENDIF()
ENDIF()

# CMake 3.5 is needed for TARGET_SOURCES(... $<TARGET_OBJECTS:${LIB}_objlib>)
# CMake 3.7 is needeed for VERSION_GREATER_EQUAL
# For the set of currently supported platforms, we can bump up to:
CMAKE_MINIMUM_REQUIRED(VERSION 3.14.6)
INCLUDE(CMakePushCheckState)

# On Linux el7/el8/el9 the default gcc is too old,
# see if devtoolset/gcc-toolset is installed.
# Same with SUSE linux 15, look for gcc 12 there.
# We need to look for gcc before calling PROJECT below.
OPTION(FORCE_UNSUPPORTED_COMPILER "Disable compiler version checks" OFF)
MARK_AS_ADVANCED(FORCE_UNSUPPORTED_COMPILER)

OPTION(MAINTAINER_MODE "Enable maintainer-specific build checks" OFF)

# Use 'uname -r' and 'rpm -qf /' to figure out host system.
# For Docker images we cannot trust uname, so use rpm instead.
IF(UNIX)
  FIND_PROGRAM(MY_UNAME uname /bin /usr/bin /usr/local/bin /sbin)
  IF(MY_UNAME)
    EXECUTE_PROCESS(COMMAND ${MY_UNAME} -s
      OUTPUT_VARIABLE MY_HOST_SYSTEM_NAME
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE MY_UNAME_RESULT
      )
    EXECUTE_PROCESS(COMMAND ${MY_UNAME} -m
      OUTPUT_VARIABLE MY_HOST_MACHINE_NAME
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE MY_UNAME_MACHINE_RESULT
      )
    EXECUTE_PROCESS(
      COMMAND ${MY_UNAME} -r
      OUTPUT_VARIABLE MY_HOST_SYSTEM_VERSION
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE MY_UNAME_VERSION_RESULT
    )
  ENDIF()
  FIND_PROGRAM(MY_DPKG_BUILDFLAGS dpkg-buildflags /bin /usr/bin)
  FIND_PROGRAM(MY_RPM rpm /bin /usr/bin)
  IF(MY_RPM)
    EXECUTE_PROCESS(COMMAND ${MY_RPM} -qf /
      OUTPUT_VARIABLE MY_HOST_FILESYSTEM_NAME
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE MY_RPM_RESULT
      )
  ENDIF()
ENDIF()

# STRING(APPEND ...) from cmake VERSION 3.4
MACRO(STRING_APPEND STRING_VAR INPUT)
  SET(${STRING_VAR} "${${STRING_VAR}}${INPUT}")
ENDMACRO()

MACRO(STRING_PREPEND STRING_VAR INPUT)
  SET(${STRING_VAR} "${INPUT}${${STRING_VAR}}")
ENDMACRO()

IF(MY_HOST_SYSTEM_NAME MATCHES "SunOS")
  SET(SOLARIS 1)
ENDIF()

IF(MY_HOST_SYSTEM_NAME MATCHES "Linux")
  # Trust 'rpm -qf /' rather than 'uname -s'
  STRING(REGEX MATCH "\\.el([6789])\\." MATCH_FSYS "${MY_HOST_FILESYSTEM_NAME}")
  IF(NOT CMAKE_MATCH_1)
    STRING(REGEX MATCH "\\.el(10)\\." MATCH_FSYS "${MY_HOST_FILESYSTEM_NAME}")
  ENDIF()

  # Set LINUX_RHEL6, LINUX_RHEL7, LINUX_RHEL8 or LINUX_RHEL9 or LINUX_RHEL10
  IF(CMAKE_MATCH_1)
    SET(LINUX_RHEL 1)
    SET(LINUX_RHEL${CMAKE_MATCH_1} 1)
  ENDIF()
ENDIF()

IF(NOT LINUX_RHEL AND MY_HOST_SYSTEM_NAME MATCHES "Linux")
  IF(EXISTS "/etc/os-release")
    FILE(READ "/etc/os-release" MY_OS_RELEASE)
    IF(MY_OS_RELEASE MATCHES "Debian")
      SET(LINUX_DEBIAN 1)
    ELSEIF(MY_OS_RELEASE MATCHES "Ubuntu")
      SET(LINUX_UBUNTU 1)
      IF(MY_OS_RELEASE MATCHES "16.04")
        SET(LINUX_UBUNTU_16_04 1)
      ELSEIF(MY_OS_RELEASE MATCHES "18.04")
        SET(LINUX_UBUNTU_18_04 1)
      ENDIF()
      # /etc/os-release contains a line like
      # VERSION_ID="20.04"
      # Match the numeric value, including the dot, ignore the rest:
      STRING(REGEX MATCH
        "VERSION_ID=\"([0-9\\.]+).*" UNUSED ${MY_OS_RELEASE})
      IF(CMAKE_MATCH_1)
        SET(LINUX_UBUNTU_VERSION_ID ${CMAKE_MATCH_1})
      ENDIF()
    ENDIF()
  ENDIF()
ENDIF()

IF(EXISTS "/etc/SuSE-release")
  SET(LINUX_SUSE 1)
ENDIF()

IF(LINUX_SUSE)
  FILE(READ "/etc/SuSE-release" MY_OS_RELEASE)
  IF(MY_OS_RELEASE MATCHES "SUSE Linux Enterprise Server 12")
    SET(LINUX_SUSE_12 1)
  ELSEIF(MY_OS_RELEASE MATCHES "SUSE Linux Enterprise Server 15" OR
      MY_OS_RELEASE MATCHES "openSUSE .* 15")
    SET(LINUX_SUSE_15 1)
  ELSE()
    MESSAGE(WARNING "Unknown SUSE version.")
  ENDIF()
ENDIF()

IF(CMAKE_HOST_UNIX AND NOT FORCE_UNSUPPORTED_COMPILER
    AND NOT CMAKE_C_COMPILER AND NOT CMAKE_CXX_COMPILER)
  # Cannot INCLUDE(CMakeDetermineSystem) prior to PROJECT initialization below.
  SET (ENV_CC "$ENV{CC}")
  SET (ENV_CXX "$ENV{CXX}")
  IF (ENV_CC STREQUAL "" AND ENV_CXX STREQUAL "")
    IF(LINUX_RHEL)
      MESSAGE(STATUS "This is ${MATCH_FSYS} as found from 'rpm -qf /'")
    ENDIF()
    IF(LINUX_RHEL AND NOT LINUX_RHEL10)
      MESSAGE(STATUS "Looking for a devtoolset compiler")
      IF(LINUX_RHEL7)
        SET(ALTERNATIVE_PATHS "/opt/rh/devtoolset-11")
      ELSEIF(LINUX_RHEL8)
        SET(ALTERNATIVE_PATHS "/opt/rh/gcc-toolset-14")
      ELSEIF(LINUX_RHEL9)
        SET(ALTERNATIVE_PATHS "/opt/rh/gcc-toolset-14")
      ELSEIF(LINUX_RHEL10)
        # Use default /usr/bin/gcc which is 14.2.1
      ENDIF()

      FOREACH(OPT_PATH ${ALTERNATIVE_PATHS})
        FIND_PROGRAM(ALTERNATIVE_GCC gcc
          NO_DEFAULT_PATH
          PATHS "${OPT_PATH}/root/usr/bin")
        FIND_PROGRAM(ALTERNATIVE_GPP g++
          NO_DEFAULT_PATH
          PATHS "${OPT_PATH}/root/usr/bin")
        FIND_PROGRAM(ALTERNATIVE_LD ld
          NO_DEFAULT_PATH
          PATHS "${OPT_PATH}/root/usr/bin")
        FIND_PROGRAM(ALTERNATIVE_AR gcc-ar
          NO_DEFAULT_PATH
          PATHS "${OPT_PATH}/root/usr/bin")
        FIND_PROGRAM(ALTERNATIVE_AR ar
          NO_DEFAULT_PATH
          PATHS "${OPT_PATH}/root/usr/bin")
        FIND_PROGRAM(ALTERNATIVE_RANLIB gcc-ranlib
          NO_DEFAULT_PATH
          PATHS "${OPT_PATH}/root/usr/bin")
        FIND_PROGRAM(ALTERNATIVE_RANLIB ranlib
          NO_DEFAULT_PATH
          PATHS "${OPT_PATH}/root/usr/bin")
        FIND_PROGRAM(ALTERNATIVE_ENABLE enable
          NO_DEFAULT_PATH
          PATHS "${OPT_PATH}")
      ENDFOREACH()
      # A missing ALTERNATIVE_LD may generate bad executables.
      IF(ALTERNATIVE_GCC AND ALTERNATIVE_GPP AND ALTERNATIVE_LD)
        # Set correct search path for executables, libraries, and data files.
        GET_FILENAME_COMPONENT(GCC_B_PREFIX ${ALTERNATIVE_GCC} DIRECTORY)
        STRING_PREPEND(CMAKE_C_FLAGS "-B${GCC_B_PREFIX} ")
        GET_FILENAME_COMPONENT(GPP_B_PREFIX ${ALTERNATIVE_GPP} DIRECTORY)
        STRING_PREPEND(CMAKE_CXX_FLAGS "-B${GPP_B_PREFIX} ")

        SET(CMAKE_C_COMPILER ${ALTERNATIVE_GCC})
        SET(CMAKE_CXX_COMPILER ${ALTERNATIVE_GPP})
        SET(CMAKE_LINKER ${ALTERNATIVE_LD})
        SET(CMAKE_LINKER ${ALTERNATIVE_LD} CACHE PATH "Alternative ld")
        IF(ALTERNATIVE_AR)
          SET(CMAKE_AR ${ALTERNATIVE_AR})
          SET(CMAKE_AR ${ALTERNATIVE_AR} CACHE PATH "Alternative ar")
        ENDIF()
        IF(ALTERNATIVE_RANLIB)
          SET(CMAKE_RANLIB ${ALTERNATIVE_RANLIB})
          SET(CMAKE_RANLIB ${ALTERNATIVE_RANLIB} CACHE PATH "Alternative ranlib")
        ENDIF()
        MESSAGE(STATUS "Using ${ALTERNATIVE_GCC}")
        MESSAGE(STATUS "Using ${ALTERNATIVE_GPP}")
      ELSE()
        IF(LINUX_RHEL7)
          SET(DEV_PACKAGES
            "devtoolset-11-gcc devtoolset-11-gcc-c++ devtoolset-11-binutils")
        ELSEIF(LINUX_RHEL8)
          SET(DEV_PACKAGES
            "gcc-toolset-14-gcc gcc-toolset-14-gcc-c++ gcc-toolset-14-binutils")
          STRING_APPEND(DEV_PACKAGES " gcc-toolset-14-annobin-annocheck")
          STRING_APPEND(DEV_PACKAGES " gcc-toolset-14-annobin-plugin-gcc")
        ELSEIF(LINUX_RHEL9)
          SET(DEV_PACKAGES
            "gcc-toolset-14-gcc gcc-toolset-14-gcc-c++ gcc-toolset-14-binutils")
          STRING_APPEND(DEV_PACKAGES " gcc-toolset-14-annobin-annocheck")
          STRING_APPEND(DEV_PACKAGES " gcc-toolset-14-annobin-plugin-gcc")
        ENDIF()
        MESSAGE(WARNING
          "Could not find devtoolset compiler/linker in ${ALTERNATIVE_PATHS}")
        MESSAGE(WARNING "You need to install the required packages:\n"
          " yum install ${DEV_PACKAGES}\n")
        MESSAGE(FATAL_ERROR
          "Or you can set CMAKE_C_COMPILER and CMAKE_CXX_COMPILER explicitly.")
      ENDIF()

    ELSEIF(LINUX_SUSE_15)
      MESSAGE(STATUS "We need to look for a newer GCC on SUSE Linux.")
      IF(LINUX_SUSE_15)
        FIND_PROGRAM(ALTERNATIVE_GCC gcc-13
          NO_DEFAULT_PATH
          PATHS "/usr/bin")
        FIND_PROGRAM(ALTERNATIVE_GPP g++-13
          NO_DEFAULT_PATH
          PATHS "/usr/bin")
        FIND_PROGRAM(GCC_AR_EXECUTABLE gcc-ar-13
          NO_DEFAULT_PATH
          PATHS "/usr/bin")
        FIND_PROGRAM(GCC_RANLIB_EXECUTABLE gcc-ranlib-13
          NO_DEFAULT_PATH
          PATHS "/usr/bin")
      ENDIF()
      IF(GCC_AR_EXECUTABLE)
        SET(CMAKE_AR ${GCC_AR_EXECUTABLE})
        SET(CMAKE_AR ${GCC_AR_EXECUTABLE} CACHE PATH "Alternative ar")
      ENDIF()
      IF(GCC_RANLIB_EXECUTABLE)
        SET(CMAKE_RANLIB ${GCC_RANLIB_EXECUTABLE})
        SET(CMAKE_RANLIB ${GCC_RANLIB_EXECUTABLE} CACHE PATH "Alternative ranlib")
      ENDIF()
      IF (ALTERNATIVE_GCC AND ALTERNATIVE_GPP)
        SET(CMAKE_C_COMPILER ${ALTERNATIVE_GCC})
        SET(CMAKE_CXX_COMPILER ${ALTERNATIVE_GPP})
        MESSAGE(STATUS "Using ${ALTERNATIVE_GCC}")
        MESSAGE(STATUS "Using ${ALTERNATIVE_GPP}")
      ELSE()
        MESSAGE(WARNING "Could not find newer gcc.")
        MESSAGE(FATAL_ERROR "Please do zypper install gcc13 gcc13-c++\n"
          "or set CMAKE_C_COMPILER and CMAKE_CXX_COMPILER explicitly.")
      ENDIF()
    ELSEIF(SOLARIS)
      MESSAGE(STATUS "Looking for GCC 11 on Solaris.")
      FIND_PROGRAM(ALTERNATIVE_GCC gcc
        NO_DEFAULT_PATH
        PATHS "/usr/gcc/11/bin")
      FIND_PROGRAM(ALTERNATIVE_GPP g++
        NO_DEFAULT_PATH
        PATHS "/usr/gcc/11/bin")
      IF (ALTERNATIVE_GCC AND ALTERNATIVE_GPP)
        SET(CMAKE_C_COMPILER ${ALTERNATIVE_GCC})
        SET(CMAKE_CXX_COMPILER ${ALTERNATIVE_GPP})
        MESSAGE(STATUS "Using ${ALTERNATIVE_GCC}")
        MESSAGE(STATUS "Using ${ALTERNATIVE_GPP}")
      ELSE()
        MESSAGE(WARNING "Could not find /usr/gcc/11/bin/gcc")
      ENDIF()
    ENDIF()
  ENDIF()
ENDIF()

project(mariadb-shell)

# Resolve RapidJSON headers for MariaDB builds. Done after project() so that,
# when WITH_RAPIDJSON is not provided, FIND_PATH can search the system include
# paths (CMAKE_SYSTEM_PREFIX_PATH), which are only initialized by project().
IF(MARIADB_BUILD)
  IF(WITH_RAPIDJSON)
    # Explicit path to a RapidJSON source tree (headers under <path>/include).
    SET(RAPIDJSON_INCLUDE_DIR "${WITH_RAPIDJSON}/include")
    IF(NOT EXISTS "${RAPIDJSON_INCLUDE_DIR}/rapidjson/rapidjson.h")
      MESSAGE(FATAL_ERROR "Could not find \"rapidjson/rapidjson.h\" under "
        "\"${RAPIDJSON_INCLUDE_DIR}\". WITH_RAPIDJSON must point to the "
        "RapidJSON source directory.")
    ENDIF()
  ELSE()
    # WITH_RAPIDJSON not provided: look for RapidJSON headers on the system.
    FIND_PATH(RAPIDJSON_INCLUDE_DIR NAMES rapidjson/rapidjson.h)
    IF(NOT RAPIDJSON_INCLUDE_DIR)
      MESSAGE(FATAL_ERROR "RapidJSON is missing. Install the system RapidJSON "
        "development headers, or use -DWITH_RAPIDJSON=<rapidjson-src-path>")
    ENDIF()
  ENDIF()

  MESSAGE(STATUS "RAPIDJSON_INCLUDE_DIR ${RAPIDJSON_INCLUDE_DIR}")

  # Treat RapidJSON as a system include so its headers (e.g. the memcpy on a
  # non-trivially-copyable GenericValue in document.h) do not trip -Werror
  # diagnostics such as GCC's -Wclass-memaccess. Matches how the other bundled
  # third-party headers (lz4, zlib) are included.
  INCLUDE_DIRECTORIES(SYSTEM ${RAPIDJSON_INCLUDE_DIR})
ENDIF()

IF(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  SET(MY_COMPILER_IS_CLANG 1)
ELSEIF(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  SET(MY_COMPILER_IS_GNU 1)
  IF(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9 AND
      NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 8)
    SET(MY_COMPILER_IS_GNU_8 1)
  ENDIF()
ELSEIF(CMAKE_CXX_COMPILER_ID MATCHES "SunPro")
  SET(MY_COMPILER_IS_SUNPRO 1)
ENDIF()

IF(MY_COMPILER_IS_CLANG OR MY_COMPILER_IS_GNU)
  SET(MY_COMPILER_IS_GNU_OR_CLANG 1)
ENDIF()

# Include the platform-specific file. To allow exceptions, this code
# looks for files in order of how specific they are. If there is, for
# example, a generic Linux.cmake and a version-specific
# Linux-2.6.28-11-generic, it will pick Linux-2.6.28-11-generic and
# include it. It is then up to the file writer to include the generic
# version if necessary.
FOREACH(_base
    ${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_VERSION}-${CMAKE_SYSTEM_PROCESSOR}
    ${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_VERSION}
    ${CMAKE_SYSTEM_NAME})
  SET(_file ${CMAKE_SOURCE_DIR}/cmake/os/${_base}.cmake)
  IF(EXISTS ${_file})
    INCLUDE(${_file})
    BREAK()
  ENDIF()
ENDFOREACH()

IF(CMAKE_VERSION VERSION_GREATER "3.12.0")
  CMAKE_POLICY(SET CMP0075 NEW)
ENDIF()

set(CMAKE_MODULE_PATH
    ${CMAKE_SOURCE_DIR}/cmake
    ${CMAKE_MODULE_PATH}
)

IF(LINUX_RHEL10 AND MY_HOST_MACHINE_NAME MATCHES "aarch64")
  SET(CMAKE_POSITION_INDEPENDENT_CODE ON)
  MESSAGE(STATUS "Building with PIC")
ENDIF()

include(TestBigEndian)
TEST_BIG_ENDIAN(IS_BIG_ENDIAN)
add_definitions(-DIS_BIG_ENDIAN=${IS_BIG_ENDIAN})

### Configuring package year
IF(NOT PACKAGE_YEAR)
  SET(PACKAGE_YEAR "2026")
ENDIF()
add_definitions(-DPACKAGE_YEAR="${PACKAGE_YEAR}")

# Build the test suite by default. WITH_TESTS gates test compilation and
# ENABLE_TESTING below; when the caller did not set it either way, turn it on.
# Pass -DWITH_TESTS=0 to build without tests.
IF(NOT DEFINED WITH_TESTS)
  SET(WITH_TESTS 1)
ENDIF()

IF(WITH_TESTS)
  ENABLE_TESTING()
ENDIF()

###
### Initial configuration
###
INCLUDE(version.cmake)

###
### Detect Stuff
###
INCLUDE(link_options)
INCLUDE(cpu_info)
INCLUDE(install_macros)
INCLUDE(exeutils)
INCLUDE(libutils)
INCLUDE(compiler)
INCLUDE(static_analysis)
include(compile_flags)
INCLUDE(msvc)

# Set correct search path for executables, libraries, and data files.
IF(LINUX_RHEL)
  IF(ALTERNATIVE_GCC)
    GET_FILENAME_COMPONENT(GCC_B_PREFIX ${ALTERNATIVE_GCC} DIRECTORY)
    STRING_PREPEND(CMAKE_C_FLAGS "-B${GCC_B_PREFIX} ")
  ENDIF()
  IF(ALTERNATIVE_GPP)
    GET_FILENAME_COMPONENT(GPP_B_PREFIX ${ALTERNATIVE_GPP} DIRECTORY)
    STRING_PREPEND(CMAKE_CXX_FLAGS "-B${GPP_B_PREFIX} ")
  ENDIF()
ENDIF()

IF(CMAKE_C_FLAGS MATCHES " -flto" OR CMAKE_CXX_FLAGS MATCHES " -flto")
  SET(CMAKE_COMPILER_FLAG_WITH_LTO 1)
ENDIF()

include(CheckCSourceCompiles)
include(CheckCXXSourceCompiles)

FUNCTION(MY_CHECK_CXX_COMPILER_WARNING WARNING_OPTION RETURN_VALUE)
  # Strip off possible leading -Wno- or -W
  STRING(REGEX REPLACE "^-Wno-" "" WARNING_OPTION ${WARNING_OPTION})
  STRING(REGEX REPLACE "^-W" "" WARNING_OPTION ${WARNING_OPTION})
  STRING(REGEX MATCH "^error=" ERROR_PREFIX ${WARNING_OPTION})
  # Use HAVE_CXX_W_warning_option as cache variable.
  STRING(REPLACE "-" "_" VAR_NAME ${WARNING_OPTION})
  STRING(REPLACE "=" "_" VAR_NAME ${VAR_NAME})
  STRING(REPLACE "c++" "cpp" VAR_NAME ${VAR_NAME})
  SET(VAR_NAME "HAVE_CXX_W_${VAR_NAME}")
  IF(MY_COMPILER_IS_GNU_OR_CLANG)
    IF(SHOW_SUPPRESSED_COMPILER_WARNINGS AND NOT ERROR_PREFIX)
      MY_CHECK_CXX_COMPILER_FLAG("-Werror=${WARNING_OPTION}" ${VAR_NAME})
      IF(${VAR_NAME})
        SET(WARNING_OPTION "error=${WARNING_OPTION}")
      ENDIF()
    ENDIF()
    IF(NOT ${VAR_NAME})
      MY_CHECK_CXX_COMPILER_FLAG("-W${WARNING_OPTION}" ${VAR_NAME})
    ENDIF()
  ELSE()
    SET(${VAR_NAME} 0 CACHE INTERNAL "-W${WARNING_OPTION} not supported")
  ENDIF()
  IF(${VAR_NAME})
    SET(${RETURN_VALUE} "-Wno-${WARNING_OPTION}" PARENT_SCOPE)
  ELSE()
    SET(${RETURN_VALUE} 0 PARENT_SCOPE)
  ENDIF()
ENDFUNCTION()

FUNCTION(GET_MAJOR_MINOR input_ver output_var)
  STRING(REGEX REPLACE "^([0-9]+)\\.([0-9]+).*" "\\1.\\2" input_ver "${input_ver}")
  SET(${output_var} ${input_ver} PARENT_SCOPE)
ENDFUNCTION()

# Use lld for Clang if available and not explicitly disabled.
# Also works for gcc on Debian/Ubuntu. Do 'apt install lld'.
# LTO build fails with lld, so turn it off by default.
IF(LINUX AND NOT WITH_LTO AND NOT CMAKE_COMPILER_FLAG_WITH_LTO)
  OPTION(USE_LD_LLD "Use llvm lld linker" ON)
ELSE()
  OPTION(USE_LD_LLD "Use llvm lld linker" OFF)
ENDIF()

IF(USE_LD_LLD)
  CMAKE_PUSH_CHECK_STATE(RESET)

  # RPM builds on Fedora use -specs files.
  # This may cause the test for -fuse-ld=lld to succeed, when it should fail.
  SET(SAVE_CMAKE_C_FLAGS ${CMAKE_C_FLAGS})
  SET(SAVE_CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
  IF(CMAKE_C_FLAGS)
    STRING(REGEX REPLACE "-specs=/usr/lib/rpm/redhat/.*"  ""
      CMAKE_C_FLAGS ${CMAKE_C_FLAGS})
  ENDIF()
  IF(CMAKE_CXX_FLAGS)
    STRING(REGEX REPLACE "-specs=/usr/lib/rpm/redhat/.*"  ""
      CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
  ENDIF()

  SET(CMAKE_REQUIRED_LIBRARIES "-fuse-ld=lld")
  CHECK_C_SOURCE_COMPILES("int main() {}" C_LD_LLD_RESULT)
  CHECK_CXX_SOURCE_COMPILES("int main() {}" CXX_LD_LLD_RESULT)
  IF(C_LD_LLD_RESULT AND CXX_LD_LLD_RESULT)
    FOREACH(flag
        CMAKE_C_LINK_FLAGS
        CMAKE_CXX_LINK_FLAGS
        CMAKE_EXE_LINKER_FLAGS
        CMAKE_MODULE_LINKER_FLAGS
        CMAKE_SHARED_LINKER_FLAGS
        )
      STRING_APPEND(${flag} " -fuse-ld=lld")
    ENDFOREACH()
  ENDIF()
  SET(CMAKE_C_FLAGS ${SAVE_CMAKE_C_FLAGS})
  SET(CMAKE_CXX_FLAGS ${SAVE_CMAKE_CXX_FLAGS})
  CMAKE_POP_CHECK_STATE()
ENDIF()

# Use gold on x86 if available and not explicitly disabled.
# LTO build fails with gold, so turn it off by default.
IF(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64"
    AND NOT WIN32
    AND NOT WITH_LTO
    AND NOT CMAKE_COMPILER_FLAG_WITH_LTO)
  OPTION(USE_LD_GOLD "Use GNU gold linker" ON)
ELSE()
  OPTION(USE_LD_GOLD "Use GNU gold linker" OFF)
ENDIF()

IF (USE_LD_GOLD AND NOT (C_LD_LLD_RESULT AND CXX_LD_LLD_RESULT))
  CMAKE_PUSH_CHECK_STATE(RESET)
  SET(CMAKE_REQUIRED_LIBRARIES "-fuse-ld=gold")
  CHECK_C_SOURCE_COMPILES("int main() {}" C_LD_GOLD_RESULT)
  CHECK_CXX_SOURCE_COMPILES("int main() {}" CXX_LD_GOLD_RESULT)
  IF (C_LD_GOLD_RESULT AND CXX_LD_GOLD_RESULT AND NOT SOLARIS)
    STRING_APPEND(CMAKE_C_LINK_FLAGS " -fuse-ld=gold")
    STRING_APPEND(CMAKE_CXX_LINK_FLAGS " -fuse-ld=gold")

    # Seemingly --gc-sections causes problems with randomization, so only
    # turn it on if we are not making a randomized build.
    # The --gc-sections seems to be causing OOM errors when building 32-bit
    # binaries for Ubuntu 18.04, do not use it there.
    IF (NOT LINK_RANDOMIZE AND NOT LINUX_UBUNTU_18_04)
      STRING_APPEND(CMAKE_C_LINK_FLAGS " -Wl,--gc-sections")
      STRING_APPEND(CMAKE_CXX_LINK_FLAGS " -Wl,--gc-sections")
    ENDIF()
  ENDIF()
  CMAKE_POP_CHECK_STATE()
ENDIF()

# Linker/binutils bug in Fedora 28, undefined reference to symbol '__bss_start'
# Similar bug on 32bit Ubuntu.
# Fedora 34: undefined reference to symbol 'crc32_z@@ZLIB_1.2.9'
IF(LINUX_FEDORA_28 OR LINUX_FEDORA_34 OR LINUX_UBUNTU_16_04)
  IF(NOT CMAKE_CXX_LINK_FLAGS MATCHES "-fuse-ld=lld" AND
      NOT CMAKE_CXX_LINK_FLAGS MATCHES "-fuse-ld=gold")
    STRING_APPEND(CMAKE_CXX_LINK_FLAGS " -Wl,--copy-dt-needed-entries")
  ENDIF()
ENDIF()

IF(LINUX)
  FIND_PROGRAM(PATCHELF_EXECUTABLE patchelf)

  IF(PATCHELF_EXECUTABLE)
    EXECUTE_PROCESS(
      COMMAND ${PATCHELF_EXECUTABLE} --version
      OUTPUT_VARIABLE PATCHELF_VERSION
      OUTPUT_STRIP_TRAILING_WHITESPACE
      )
    STRING(REPLACE "patchelf" "" PATCHELF_VERSION "${PATCHELF_VERSION}")
    MESSAGE(STATUS "Using patchelf: ${PATCHELF_EXECUTABLE}, version: ${PATCHELF_VERSION}")
    IF(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" AND PATCHELF_VERSION VERSION_LESS "0.14.5")
      SET(PATCHELF_PAGE_SIZE_ARGS --page-size 65536)
    ENDIF()
  ENDIF()
ENDIF()

include(fuzzer)

INCLUDE(CheckIncludeFiles)
INCLUDE(CheckFunctionExists)

# --- vcpkg dependency bundling -------------------------------------------
# When configured with the vcpkg toolchain
# (-DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
# -DVCPKG_TARGET_TRIPLET=<triplet>), the third-party dependencies (OpenSSL,
# libssh, antlr4, curl, zlib, zstd, ...) are resolved from vcpkg's per-triplet
# tree instead of the system. On Windows and macOS there is no system package
# manager to satisfy those at runtime, so - exactly like the explicit
# BUNDLED_*_DIR options - a vcpkg build must ship the full dependency closure
# inside the package. USING_VCPKG drives that automatic bundling (consumed in
# src/CMakeLists.txt).
IF(NOT DEFINED USING_VCPKG)
  IF(VCPKG_TARGET_TRIPLET AND CMAKE_TOOLCHAIN_FILE MATCHES "[Vv]cpkg")
    SET(USING_VCPKG TRUE)
  ELSE()
    SET(USING_VCPKG FALSE)
  ENDIF()
ENDIF()
IF(USING_VCPKG)
  MESSAGE(STATUS "vcpkg toolchain detected (triplet '${VCPKG_TARGET_TRIPLET}'); "
                 "runtime dependencies will be bundled into the package")
ENDIF()

include(curl)
MYSQL_CHECK_CURL()

include(zlib)
MYSQL_CHECK_ZLIB()

# Build with Python scripting support by default. HAVE_PYTHON gates the whole
# Python-detection block below; when the caller did not set it either way, turn
# it on. Pass -DHAVE_PYTHON=0 to build without Python.
IF(NOT DEFINED HAVE_PYTHON)
  SET(HAVE_PYTHON 1)
ENDIF()

IF (PYTHON_DEPS AND NOT HAVE_PYTHON)
  MESSAGE(FATAL_ERROR "PYTHON_DEPS should not be used when python is not available.")
ENDIF()

IF(BUNDLED_OPENSSL_DIR)
  IF(CMAKE_SYSTEM_NAME MATCHES "Linux")
    # this will force the MYSQL_CHECK_SSL() macro to use shared libraries
    SET(LINUX_STANDALONE 1)
  ENDIF()
  SET(WITH_SSL "${BUNDLED_OPENSSL_DIR}")
ELSE()
  SET(WITH_SSL "system")
ENDIF()

INCLUDE(ssl)
MYSQL_CHECK_SSL()

SET(OPENSSL_VERSION_ID "${OPENSSL_VERSION_MAJOR}${OPENSSL_VERSION_MINOR}${OPENSSL_FIX_VERSION}")
ADD_DEFINITIONS(-DOPENSSL_VERSION_ID=${OPENSSL_VERSION_ID})

INCLUDE(bison)

if(NOT BUILD_SOURCE_PACKAGE)
  if(BUNDLED_ANTLR_DIR)
    set(ANTLR4_INCLUDE_DIRS "${BUNDLED_ANTLR_DIR}/include")
    set(ANTLR4_LIBRARIES "${BUNDLED_ANTLR_DIR}/lib")
  else()
    find_package(antlr4-runtime REQUIRED)
    get_filename_component(ANTLR4_INCLUDE_DIRS ${ANTLR4_INCLUDE_DIR} DIRECTORY)
    set(ANTLR4_LIBRARIES "${ANTLR4_LIB_DIR}")
    unset(ANTLR4_FOUND)
    unset(ANTLR4_INCLUDE_DIR)
    unset(ANTLR4_LIB_DIR)
  endif()

  INCLUDE(FindANTLR4)

  if(NOT ANTLR4_INCLUDE_DIRS OR NOT ANTLR4_LIBRARIES)
    message(FATAL_ERROR "Could not find antlr4 runtime!")
  endif()

  if(NOT ANTLR4_VERSION_SHORT VERSION_GREATER_EQUAL "4.10")
    # we're storing files generated by antlr4 4.10.1, so we require at least
    # that version
    message(FATAL_ERROR "Required version of antlr4 runtime is at least 4.10")
  endif()
endif()

if(HAVE_PYTHON)
  SET(MINIMUM_PYTHON_VERSION "3.8")
  IF(NOT USE_PYTHON_VERSION)
    SET(USE_PYTHON_VERSION "${MINIMUM_PYTHON_VERSION}")
  ENDIF()

  IF(USE_PYTHON_VERSION VERSION_LESS MINIMUM_PYTHON_VERSION)
    MESSAGE(FATAL_ERROR "Minimum supported Python version is ${MINIMUM_PYTHON_VERSION}")
  ENDIF()

  FUNCTION(GET_PYTHON_VERSION python_dir output_var)
    FILE(GLOB_RECURSE patch_level "${python_dir}/patchlevel.h")

    IF(patch_level AND EXISTS "${patch_level}")
      FILE(STRINGS "${patch_level}" python_version_str
           REGEX "^#define[ \t]+PY_VERSION[ \t]+\"[^\"]+\"")
      STRING(REGEX REPLACE "^#define[ \t]+PY_VERSION[ \t]+\"([^\"]+)\".*" "\\1"
                           python_version_str "${python_version_str}")
      SET(${output_var} "${python_version_str}" PARENT_SCOPE)
    ELSE()
      MESSAGE(FATAL_ERROR "Couldn't find patchlevel.h in ${python_dir}")
    ENDIF()
  ENDFUNCTION()

  # On windows stores the path to the python libraries since they will be bundled
  IF(WIN32)
    IF(MARIADB_BUILD)
      # The deprecated FindPythonLibs locates Python via the registry key
      # SOFTWARE\Python\PythonCore\<ver>, but python.org tags that key with the
      # architecture per PEP 514 (e.g. <ver>-arm64), which FindPythonLibs does
      # not search - so it cannot auto-detect an ARM64 Python. FindPython3
      # understands the arch tags. Map its results onto the legacy PYTHON_*
      # variables the rest of this file and the bundling logic expect.
      #
      # Prefer a python.org install over vcpkg's Python. vcpkg's Python has a
      # split layout (headers under include/pythonX.Y, runtime under
      # tools/python3, no self-contained root); the bundling logic below now
      # handles that layout, but a python.org install remains the more predictable
      # bundling source. The vcpkg toolchain injects its tree via BOTH
      # CMAKE_PREFIX_PATH and CMAKE_FIND_ROOT_PATH, so the soft FindPython3 knobs
      # (FIND_REGISTRY / FIND_STRATEGY) cannot reliably steer away from it.
      # Exclude vcpkg's tree for just this find so the python.org install (located
      # via the Windows registry, PEP 514) wins when both are present. To force
      # vcpkg's Python instead, pass -DPython3_ROOT_DIR=<prefix>/tools/python3,
      # which takes precedence and disables the exclusion.
      #
      # NOTE: FindPython3 caches Python3_EXECUTABLE; clear the CMake cache when
      # changing which Python to use or a stale value is reused.
      SET(Python3_FIND_REGISTRY FIRST)
      IF(NOT Python3_ROOT_DIR)
        SET(_saved_prefix_path "${CMAKE_PREFIX_PATH}")
        SET(_saved_find_root_path "${CMAKE_FIND_ROOT_PATH}")
        SET(CMAKE_PREFIX_PATH "")
        SET(CMAKE_FIND_ROOT_PATH "")
      ENDIF()
      FIND_PACKAGE(Python3 "${USE_PYTHON_VERSION}" REQUIRED
        COMPONENTS Interpreter Development.Embed)
      IF(NOT Python3_ROOT_DIR)
        SET(CMAKE_PREFIX_PATH "${_saved_prefix_path}")
        SET(CMAKE_FIND_ROOT_PATH "${_saved_find_root_path}")
      ENDIF()
      SET(PYTHON_INCLUDE_DIRS "${Python3_INCLUDE_DIRS}")
      SET(PYTHON_LIBRARIES "${Python3_LIBRARIES}")
      SET(PYTHONLIBS_VERSION_STRING "${Python3_VERSION}")
      SET(PYTHON_EXECUTABLE "${Python3_EXECUTABLE}")
      # The block that emits the -DHAVE_PYTHON compile definition keys off
      # PYTHONLIBS_FOUND (a FindPythonLibs variable); FindPython3 sets
      # Python3_FOUND instead, so mirror it or the C++ sees no scripting mode.
      SET(PYTHONLIBS_FOUND "${Python3_FOUND}")
    ELSE()
      FIND_PACKAGE(PythonLibs "${USE_PYTHON_VERSION}" REQUIRED)
    ENDIF()
    # PYTHON_PATH and PYTHON_FOLDER are used to properly copy the python libraries where needed
    # By default they take the value of PB2WORKDIR and PYTHON_SOURCENAME env vars
    IF(NOT PYTHON_PATH AND NOT PYTHON_FOLDER)
      SET(PYTHON_PATH "$ENV{PB2WORKDIR}")
      SET(PYTHON_FOLDER "$ENV{PYTHON_SOURCENAME}")
    ENDIF()

    IF(PYTHON_PATH AND PYTHON_FOLDER)
      # Explicit PB2 layout (Oracle's official build): <PATH>/<FOLDER> is the root.
      SET(PYTHON_ROOT "${PYTHON_PATH}/${PYTHON_FOLDER}")
    ELSEIF(PYTHON_EXECUTABLE)
      # Derive the runtime root from the interpreter that was found, NOT from the
      # headers. python.exe always sits at the root of a Windows Python tree with
      # Lib/ and DLLs/ as siblings, which holds for BOTH a python.org install
      # (root == <prefix>) and a vcpkg install (root == <prefix>/tools/python3).
      #
      # vcpkg splits the tree: headers live under <prefix>/include/pythonX.Y while
      # the runtime lives under <prefix>/tools/python3. Walking up from
      # PYTHON_INCLUDE_DIRS therefore lands on <prefix>/include and tries to
      # bundle empty/nonexistent Lib and DLLs directories. Anchoring on the
      # executable handles both layouts without having to special-case vcpkg.
      GET_FILENAME_COMPONENT(PYTHON_ROOT "${PYTHON_EXECUTABLE}" DIRECTORY)
    ELSE()
      # Legacy fallback (FindPythonLibs, python.org layout): the runtime root is
      # the parent of the include directory.
      GET_FILENAME_COMPONENT(PYTHON_PATH ${PYTHON_INCLUDE_DIRS} DIRECTORY)
      GET_FILENAME_COMPONENT(PYTHON_FOLDER ${PYTHON_PATH} NAME)
      GET_FILENAME_COMPONENT(PYTHON_PATH ${PYTHON_PATH} DIRECTORY)
      SET(PYTHON_ROOT "${PYTHON_PATH}/${PYTHON_FOLDER}")
    ENDIF()

    IF(PYTHON_ROOT)
      FILE(TO_CMAKE_PATH "${PYTHON_ROOT}" PYTHON_ROOT)
      SET(PYTHON_LIBRARY_PATH "${PYTHON_ROOT}/Lib")
      SET(PYTHON_DLLS_PATH "${PYTHON_ROOT}/DLLs")
      # Only synthesize the interpreter path for the layouts where we derived the
      # root ourselves. In the FindPython3 case PYTHON_EXECUTABLE already points at
      # the real interpreter (and equals ${PYTHON_ROOT}/python.exe anyway).
      IF(NOT PYTHON_EXECUTABLE)
        SET(PYTHON_EXECUTABLE "${PYTHON_ROOT}/python.exe")
      ENDIF()
    ENDIF()
  ELSEIF(BUNDLED_PYTHON_DIR)
    # If we're using a Python build for bundling, ensure we build against it too
    GET_PYTHON_VERSION("${BUNDLED_PYTHON_DIR}" PYTHONLIBS_VERSION_STRING)
    GET_MAJOR_MINOR("${PYTHONLIBS_VERSION_STRING}" PYTHONLIBS_MAJOR_MINOR)
    FIND_PROGRAM(PYTHON_EXECUTABLE
      NAMES
        python${PYTHONLIBS_MAJOR_MINOR}mu
        python${PYTHONLIBS_MAJOR_MINOR}m
        python${PYTHONLIBS_MAJOR_MINOR}u
        python${PYTHONLIBS_MAJOR_MINOR}
      HINTS
        "${BUNDLED_PYTHON_DIR}/bin"
      NO_DEFAULT_PATH
    )
    FIND_PATH(PYTHON_INCLUDE_DIRS
      NAMES
        Python.h
      HINTS
        "${BUNDLED_PYTHON_DIR}/include"
      PATH_SUFFIXES
        python${PYTHONLIBS_MAJOR_MINOR}mu
        python${PYTHONLIBS_MAJOR_MINOR}m
        python${PYTHONLIBS_MAJOR_MINOR}u
        python${PYTHONLIBS_MAJOR_MINOR}
      NO_DEFAULT_PATH
    )
    FIND_LIBRARY(PYTHON_LIBRARIES
      NAMES
        python${PYTHONLIBS_MAJOR_MINOR}mu
        python${PYTHONLIBS_MAJOR_MINOR}m
        python${PYTHONLIBS_MAJOR_MINOR}u
        python${PYTHONLIBS_MAJOR_MINOR}
      HINTS
        "${BUNDLED_PYTHON_DIR}/lib"
      NO_DEFAULT_PATH
    )
    IF(NOT PYTHON_LIBRARIES MATCHES ".*\\.a$")
      SET(BUNDLED_SHARED_PYTHON "YES")
    ENDIF()
  ELSE()
    # CMP0148: FindPythonInterp/FindPythonLibs removed in CMake >= 3.27.
    # Keep OLD behavior for now.
    # TODO: migrate to FindPython3.
    IF(POLICY CMP0148)
      CMAKE_POLICY(SET CMP0148 OLD)
    ENDIF()
    FIND_PACKAGE(PythonLibs "${USE_PYTHON_VERSION}" REQUIRED)
    IF(NOT PYTHON_EXECUTABLE)
      # Can't use FIND_PACKAGE(Python ...) as it finds the latest Python interp and not the version we want
      GET_MAJOR_MINOR("${PYTHONLIBS_VERSION_STRING}" PYTHONLIBS_MAJOR_MINOR)
      FIND_PROGRAM(PYTHON_EXECUTABLE
        NAMES
          python${PYTHONLIBS_MAJOR_MINOR}mu
          python${PYTHONLIBS_MAJOR_MINOR}m
          python${PYTHONLIBS_MAJOR_MINOR}u
          python${PYTHONLIBS_MAJOR_MINOR}
      )
    ENDIF()
  ENDIF()
endif()

IF (JIT_EXECUTOR_LIB)
  FIND_LIBRARY(JIT_EXECUTOR_LIBRARY
    NAMES
      jitexecutor
    HINTS
      "${JIT_EXECUTOR_LIB}"
    NO_DEFAULT_PATH)

  INCLUDE_DIRECTORIES(SYSTEM "${JIT_EXECUTOR_LIB}")
  MESSAGE(STATUS "Jit Executor Library: ${JIT_EXECUTOR_LIBRARY}")  
  SET(HAVE_JS 1)
  SET(HAVE_JIT_EXECUTOR 1)
  ADD_DEFINITIONS(-DHAVE_JS)
  ADD_DEFINITIONS(-DHAVE_JIT_EXECUTOR)
ENDIF()



IF(WIN32)
  # Speed up build process excluding unused header files
  # Also definitely resolves the trouble of using both
  # windows.h and winsock2.h
  ADD_DEFINITIONS(-DWIN32_LEAN_AND_MEAN)
  ADD_DEFINITIONS(-DNOMINMAX)
  ADD_DEFINITIONS(-DNOGDI)
  ADD_DEFINITIONS(-DNOMB)
  ADD_DEFINITIONS(-DNOMEMMGR)
  ADD_DEFINITIONS(-DNOMETAFILE)
  ADD_DEFINITIONS(-DNOSCROLL)
  ADD_DEFINITIONS(-DNOSERVICE)
  ADD_DEFINITIONS(-DNOSOUND)
  ADD_DEFINITIONS(-DNOTEXTMETRIC)
  ADD_DEFINITIONS(-DNOWH)
  ADD_DEFINITIONS(-DNOWINOFFSETS)
  ADD_DEFINITIONS(-DNOCOMM)
  ADD_DEFINITIONS(-DNOKANJI)
  ADD_DEFINITIONS(-DNOHELP)
  ADD_DEFINITIONS(-DNOPROFILER)
  ADD_DEFINITIONS(-DNODEFERWINDOWPOS)
  ADD_DEFINITIONS(-DNOMCX)

  # Silence warnings about functions that are perfectly fine for posix
  ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS)

  # Sets the windows versions that should be supported
  ADD_DEFINITIONS(-DNTDDI_VERSION=0x06000100 -D_WIN32_WINNT=0x0600)

  IF (MSVC)
      # Enable parallel builds
      ADD_COMPILE_OPTIONS(/MP)
      # Make sure source files and execution context are set to UTF8
      ADD_COMPILE_OPTIONS(/utf-8)
      # Make sure we keep with the language standards conformance
      ADD_COMPILE_OPTIONS(/permissive-)
  ENDIF()

ELSE()
  # Non windows builds should build dev packages by default
  SET(WITH_DEV "1")
ENDIF()

IF(PYTHONLIBS_FOUND OR BUNDLED_PYTHON_DIR)
  set(HAVE_PYTHON "YES")         # Variable for CMake processing
  IF(BUNDLED_PYTHON_DIR)
    add_definitions(-DHAVE_PYTHON=2) # 2 means bundled
  ELSE()
    add_definitions(-DHAVE_PYTHON=1) # 1 means normal (or windows)
  ENDIF()
  message(STATUS "Python ${PYTHONLIBS_VERSION_STRING}")
  message(STATUS "PYTHON_EXECUTABLE: ${PYTHON_EXECUTABLE}")
  message(STATUS "PYTHON_INCLUDE_DIRS: ${PYTHON_INCLUDE_DIRS}")
  message(STATUS "PYTHON_LIBRARIES: ${PYTHON_LIBRARIES}")

  IF(NOT PYTHONLIBS_VERSION_STRING)
    MESSAGE(FATAL_ERROR "Couldn't detect Python's version")
  ENDIF()

  # supported Python versions: (v >= 3.8)
  IF(PYTHONLIBS_VERSION_STRING VERSION_LESS "3.8")
    MESSAGE(FATAL_ERROR "Python support requires Python 3.8 or greater")
  ENDIF()

  GET_MAJOR_MINOR("${PYTHONLIBS_VERSION_STRING}" PYTHONLIBS_MAJOR_MINOR)
  message(STATUS "Python major.minor: ${PYTHONLIBS_MAJOR_MINOR}")

  # TODO: Temporary flag indicating python is static as well
  # Proper logic should be added to correctly configure this
  IF(WITH_STATIC_MSVCRT)
    SET(WITH_STATIC_PYTHON_LIB 1)
  ENDIF()

ELSE()
  message(WARNING "Python is unavailable: building without Python support.")
ENDIF()

# Sets default linking to static if not already defined
if(NOT DEFINED MYSQLCLIENT_STATIC_LINKING)
  set(MYSQLCLIENT_STATIC_LINKING TRUE)
endif()

### Bundling of OpenSSL libraries (if needed)
# macro MYSQL_CHECK_SSL_DLLS() adapted for Shell
IF (WITH_SSL_PATH AND (APPLE OR WIN32 OR LINUX_STANDALONE))
  SET(BUNDLED_OPENSSL 1)
  MESSAGE(STATUS "WITH_SSL_PATH ${WITH_SSL_PATH}")

  # In MySQL Server 8.0.4 and up, OpenSSL is linked dynamically and
  # preferably linked against the system installed OpenSSL. But there
  # is no standard system installed OpenSSL on macOS or Windows so we
  # bundle the library copying it from the server install we link
  # against.

  IF(WIN32)
    GET_FILENAME_COMPONENT(CRYPTO_NAME "${CRYPTO_LIBRARY}" NAME_WE)
    GET_FILENAME_COMPONENT(OPENSSL_NAME "${OPENSSL_LIBRARY}" NAME_WE)

    # Different naming scheme for the matching .dll as of SSL 1.1
    # OpenSSL 3.x Look for libcrypto-3-x64.dll or libcrypto-3.dll
    # OpenSSL 1.1 Look for libcrypto-1_1-x64.dll or libcrypto-1_1.dll
    # OpenSSL 1.0 Look for libeay32.dll
    #
    # The arch suffix is not always x64: on Windows/ARM64 (e.g. the
    # arm64-windows vcpkg triplet) OpenSSL is named libcrypto-3-arm64.dll.
    # Derive it from the target architecture rather than assuming x64.
    IF(VCPKG_TARGET_TRIPLET MATCHES "arm64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "[Aa][Rr][Mm]64")
      SET(SSL_MSVC_ARCH "-arm64")
    ELSE()
      SET(SSL_MSVC_ARCH "-x64")
    ENDIF()

    SET(SSL_MSVC_VERSION_SUFFIX)
    SET(SSL_MSVC_ARCH_SUFFIX)
    IF(OPENSSL_VERSION_MINOR VERSION_EQUAL 1)
      SET(SSL_MSVC_VERSION_SUFFIX "-1_1")
      SET(SSL_MSVC_ARCH_SUFFIX "${SSL_MSVC_ARCH}")
    ENDIF()
    IF(OPENSSL_VERSION_MAJOR VERSION_EQUAL 3)
      SET(SSL_MSVC_VERSION_SUFFIX "-3")
      SET(SSL_MSVC_ARCH_SUFFIX "${SSL_MSVC_ARCH}")
    ENDIF()

    MESSAGE(STATUS "===> WITH_SSL_PATH: ${WITH_SSL_PATH}")
    MESSAGE(STATUS "===> CRYPTO_NAME: ${CRYPTO_NAME}")
    MESSAGE(STATUS "===> OPENSSL_NAME: ${OPENSSL_NAME}")
    MESSAGE(STATUS "===> SSL_MSVC_VERSION_SUFFIX: ${SSL_MSVC_VERSION_SUFFIX}")
    MESSAGE(STATUS "===> SSL_MSVC_ARCH_SUFFIX: ${SSL_MSVC_ARCH_SUFFIX}")

    FIND_FILE(HAVE_CRYPTO_DLL
      NAMES
      "${CRYPTO_NAME}${SSL_MSVC_VERSION_SUFFIX}${SSL_MSVC_ARCH_SUFFIX}.dll"
      "${CRYPTO_NAME}${SSL_MSVC_VERSION_SUFFIX}.dll"
      PATHS "${WITH_SSL_PATH}/bin"
      NO_DEFAULT_PATH
      )
    FIND_FILE(HAVE_OPENSSL_DLL
      NAMES
      "${OPENSSL_NAME}${SSL_MSVC_VERSION_SUFFIX}${SSL_MSVC_ARCH_SUFFIX}.dll"
      "${OPENSSL_NAME}${SSL_MSVC_VERSION_SUFFIX}.dll"
      PATHS "${WITH_SSL_PATH}/bin"
      NO_DEFAULT_PATH
      )

    MESSAGE(STATUS "HAVE_CRYPTO_DLL ${HAVE_CRYPTO_DLL}")
    MESSAGE(STATUS "HAVE_OPENSSL_DLL ${HAVE_OPENSSL_DLL}")

    # Fail at configure time with a clear message instead of leaking the
    # "-NOTFOUND" sentinel into the DLL-bundling copy rules (which otherwise
    # surfaces as an opaque ninja error: "HAVE_CRYPTO_DLL-NOTFOUND missing").
    IF(NOT HAVE_CRYPTO_DLL OR NOT HAVE_OPENSSL_DLL)
      MESSAGE(FATAL_ERROR
        "Could not find the OpenSSL crypto/ssl DLLs to bundle under "
        "'${WITH_SSL_PATH}/bin' (looked for "
        "${CRYPTO_NAME}${SSL_MSVC_VERSION_SUFFIX}${SSL_MSVC_ARCH_SUFFIX}.dll / "
        "${OPENSSL_NAME}${SSL_MSVC_VERSION_SUFFIX}${SSL_MSVC_ARCH_SUFFIX}.dll). "
        "Check WITH_SSL_PATH and the arch suffix (SSL_MSVC_ARCH_SUFFIX="
        "${SSL_MSVC_ARCH_SUFFIX}).")
    ENDIF()
  ELSE()
    EXECUTE_PROCESS(
      COMMAND readlink "${CRYPTO_LIBRARY}" OUTPUT_VARIABLE CRYPTO_VERSION
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    EXECUTE_PROCESS(
      COMMAND readlink "${OPENSSL_LIBRARY}" OUTPUT_VARIABLE OPENSSL_VERSION
      OUTPUT_STRIP_TRAILING_WHITESPACE)

    MESSAGE(STATUS "CRYPTO_VERSION ${CRYPTO_VERSION}")
    MESSAGE(STATUS "OPENSSL_VERSION ${OPENSSL_VERSION}")

    GET_FILENAME_COMPONENT(CRYPTO_DIRECTORY "${CRYPTO_LIBRARY}" DIRECTORY)
    GET_FILENAME_COMPONENT(OPENSSL_DIRECTORY "${OPENSSL_LIBRARY}" DIRECTORY)

    MESSAGE(STATUS "CRYPTO_DIRECTORY ${CRYPTO_DIRECTORY}")
    MESSAGE(STATUS "OPENSSL_DIRECTORY ${OPENSSL_DIRECTORY}")

    SET(CRYPTO_FULL_NAME "${CRYPTO_DIRECTORY}/${CRYPTO_VERSION}")
    SET(OPENSSL_FULL_NAME "${OPENSSL_DIRECTORY}/${OPENSSL_VERSION}")

    MESSAGE(STATUS "CRYPTO_FULL_NAME ${CRYPTO_FULL_NAME}")
    MESSAGE(STATUS "OPENSSL_FULL_NAME ${OPENSSL_FULL_NAME}")
  ENDIF()

  FIND_PATH(OPENSSL_MODULES_DIR
    NAMES ossl-modules
    NO_CMAKE_PATH
    NO_CMAKE_ENVIRONMENT_PATH
    HINTS ${WITH_SSL_PATH}
    PATH_SUFFIXES lib lib64
  )

  IF(OPENSSL_MODULES_DIR)
    SET(OPENSSL_MODULES_DIR "${OPENSSL_MODULES_DIR}/ossl-modules")
    FILE(TO_CMAKE_PATH "${OPENSSL_MODULES_DIR}" OPENSSL_MODULES_DIR)
    MESSAGE(STATUS "OPENSSL_MODULES_DIR ${OPENSSL_MODULES_DIR}")

    IF(WIN32)
      SET(_module_ext "dll")
    ELSEIF(APPLE)
      SET(_module_ext "dylib")
    ELSE()
      SET(_module_ext "so")
    ENDIF()

    FILE(GLOB OPENSSL_MODULES_LIST "${OPENSSL_MODULES_DIR}/*.${_module_ext}")
    MESSAGE(STATUS "OPENSSL_MODULES_LIST ${OPENSSL_MODULES_LIST}")

    add_definitions(-DBUNDLE_OPENSSL_MODULES)
  ENDIF()
ENDIF()

if (NOT BUILD_SOURCE_PACKAGE)
  IF (MARIADB_SOURCE_DIR)
    SET(MYSQL_INCLUDE_DIRS
      ${MARIADB_SOURCE_DIR}/libmariadb/include
      ${MARIADB_SOURCE_DIR}/include
      ${MARIADB_SOURCE_DIR}/sql
      ${MARIADB_BUILD_DIR}
      ${MARIADB_BUILD_DIR}/include
      ${MARIADB_BUILD_DIR}/libmariadb/include
    )
    # On Windows the Connector/C header (ma_global.h) defines `my_socket` as a
    # macro while the server header (my_global.h) defines it as a typedef; the
    # two clash when both are pulled into one TU. Force-include a shim into
    # every TU that reconciles them. See cmake/mariadb_win_compat.h.
    IF(WIN32)
      # Restrict to C/C++ so the flag is not passed to the resource (rc.exe) or
      # message compiler, which do not understand /FI.
      ADD_COMPILE_OPTIONS(
        "$<$<COMPILE_LANGUAGE:C,CXX>:/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/mariadb_win_compat.h>")
    ENDIF()
    # Link against the bundled MariaDB Connector/C client library, plus the
    # server's mysys/strings/dbug static libraries (the shell uses functions
    # from them directly: get_charset, my_load_defaults, my_convert, DBUG, ...).
    FIND_LIBRARY(MARIADB_CLIENT_LIBRARY
      NAMES mariadbclient libmariadbclient
      PATHS ${MARIADB_BUILD_DIR}/libmariadb/libmariadb
      NO_DEFAULT_PATH)
    IF(NOT MARIADB_CLIENT_LIBRARY)
      MESSAGE(FATAL_ERROR "Could not find libmariadbclient in "
        "${MARIADB_BUILD_DIR}/libmariadb/libmariadb")
    ENDIF()
    # Static-library naming differs by platform: libmysys.a on Unix,
    # mysys.lib on Windows/MSVC. Use CMake's own prefix/suffix variables so
    # the same paths resolve on all toolchains.
    SET(_slp "${CMAKE_STATIC_LIBRARY_PREFIX}")
    SET(_sls "${CMAKE_STATIC_LIBRARY_SUFFIX}")
    # mysys/strings/dbug have circular references; list them so the linker can
    # resolve across them (repeated as needed).
    SET(MYSQL_LIBRARIES
      ${MARIADB_CLIENT_LIBRARY}
      ${MARIADB_BUILD_DIR}/mysys/${_slp}mysys${_sls}
      ${MARIADB_BUILD_DIR}/mysys_ssl/${_slp}mysys_ssl${_sls}
      ${MARIADB_BUILD_DIR}/strings/${_slp}strings${_sls}
      ${MARIADB_BUILD_DIR}/dbug/${_slp}dbug${_sls}
      ${MARIADB_BUILD_DIR}/mysys/${_slp}mysys${_sls}
      ${MARIADB_BUILD_DIR}/strings/${_slp}strings${_sls})
    IF(WIN32)
      # The shell links mariadbclient.lib via a raw path (FIND_LIBRARY), so it
      # does not inherit the connector's exported SYSTEM_LIBS. Supply the Windows
      # system libs libmariadb needs statically: SSPI (secur32, for the Windows
      # auth plugin sspi_client.c), plus crypt32/bcrypt/shlwapi/ws2_32/advapi32
      # (TLS, path helpers, sockets). See libmariadb CMakeLists.txt (SYSTEM_LIBS).
      # Exposed as a variable (inherited by subdirectories) so executables that
      # link the connector but don't reliably inherit it transitively (e.g.
      # run_unit_tests) can link it explicitly.
      SET(MARIADB_WIN_SYSTEM_LIBS secur32 crypt32 bcrypt shlwapi ws2_32 advapi32)
      LIST(APPEND MYSQL_LIBRARIES ${MARIADB_WIN_SYSTEM_LIBS})
    ENDIF()
  ELSE()
    # Needed for protobuf header paths, which are needed to use libmysqlxclient
    INCLUDE(protobuf)
    INCLUDE(FindMySQLx)
    # Needed for X protocol error codes
    CONFIGURE_FILE(${MYSQL_SOURCE_DIR}/plugin/x/src/xpl_error.h ${CMAKE_SOURCE_DIR}/mysqlshdk/libs/db/mysqlx/xpl_error.h COPYONLY)
  ENDIF()
  INCLUDE(lz4)
  # Add lz4 library (Dependency for the X Client Lib)
  MYSQL_CHECK_LZ4()
endif()


##
## Installation location
##
SET(INSTALL_BINDIR "bin")

IF(LIBEXECDIR)
  ADD_DEFINITIONS(-DLIBEXECDIR="${LIBEXECDIR}")
  SET(INSTALL_LIBEXECDIR "${LIBEXECDIR}/mariadb-shell")
ELSE()
  ADD_DEFINITIONS(-DLIBEXECDIR="libexec")
  SET(INSTALL_LIBEXECDIR "libexec/mariadb-shell")
ENDIF()

SET(INSTALL_LIBDIR "lib/mariadb-shell")
SET(_plugin_ext "so")
IF(WIN32)
  SET(_plugin_ext "dll")
ENDIF()

SET(INSTALL_MYSQL_PLUGINS_DIR "lib/mysql/plugins")
IF(MYSQL_BUILD_DIR)
  SET(_plugin_subdirs "${MYSQL_BUILD_DIR}/plugin_output_directory")
  IF(WIN32)
    STRING(APPEND _plugin_subdirs "/${CMAKE_BUILD_TYPE}")
  ENDIF()
  FILE(GLOB AUTH_CLIENT_PLUGINS "${_plugin_subdirs}/authentication_*_client.${_plugin_ext}")
  IF(EXISTS "${_plugin_subdirs}/mysql_native_password.${_plugin_ext}")
    LIST(APPEND AUTH_CLIENT_PLUGINS "${_plugin_subdirs}/mysql_native_password.${_plugin_ext}")
  ENDIF()
ELSEIF(MARIADB_BUILD)
  SET(_plugin_subdirs "${MARIADB_BUILD_DIR}/libmariadb")
  IF(WIN32)
    STRING(APPEND _plugin_subdirs "/${CMAKE_BUILD_TYPE}")
  ENDIF()
  FILE(GLOB AUTH_CLIENT_PLUGINS "${_plugin_subdirs}/caching_sha2_password.${_plugin_ext}")
ENDIF()

IF(AUTH_CLIENT_PLUGINS)
  SET(AUTH_CLIENT_PLUGINS_COPY ${AUTH_CLIENT_PLUGINS})

  IF(MYSQL_BUILD_DIR)
    SET(EXPECTED_AUTH_CLIENT_PLUGINS
      "authentication_kerberos_client"
      "authentication_ldap_sasl_client"
      "authentication_oci_client"
      "authentication_openid_connect_client"
      "mysql_native_password"
    )
  ELSEIF(MARIADB_BUILD)
    SET(EXPECTED_AUTH_CLIENT_PLUGINS
      "caching_sha2_password"
    )
  ENDIF()

  IF(${OPENSSL_MAJOR_MINOR_FIX_VERSION} VERSION_GREATER_EQUAL "1.1.1")
    IF(${MYSQL_VERSION} VERSION_GREATER_EQUAL "8.2.0" AND NOT MARIADB_BUILD)
      LIST(APPEND EXPECTED_AUTH_CLIENT_PLUGINS "authentication_webauthn_client")
    ENDIF()
  ENDIF()

  FOREACH(_plugin ${AUTH_CLIENT_PLUGINS})
    FOREACH(_expected_plugin ${EXPECTED_AUTH_CLIENT_PLUGINS})
      IF(_plugin MATCHES ".+${_expected_plugin}\..+")
        LIST(REMOVE_ITEM EXPECTED_AUTH_CLIENT_PLUGINS "${_expected_plugin}")
        LIST(REMOVE_ITEM AUTH_CLIENT_PLUGINS_COPY "${_plugin}")

        IF(_expected_plugin STREQUAL "authentication_webauthn_client")
          SET(_library_subdirs "${MYSQL_BUILD_DIR}/library_output_directory")

          IF(WIN32)
            STRING(APPEND _library_subdirs "/${CMAKE_BUILD_TYPE}")
            FILE(GLOB AUTH_CLIENT_PLUGINS_DEPS "${_library_subdirs}/fido2.dll")
          ELSEIF(APPLE)
            FILE(GLOB AUTH_CLIENT_PLUGINS_DEPS "${_library_subdirs}/libfido2*.dylib")
          ELSE()
            FILE(GLOB AUTH_CLIENT_PLUGINS_DEPS "${_library_subdirs}/libfido2.so*")
          ENDIF()

          IF(NOT AUTH_CLIENT_PLUGINS_DEPS)
            MESSAGE(FATAL_ERROR "Couldn't find libfido2, a dependency of authentication_webauthn_client")
          ENDIF()
        ENDIF()
        BREAK()
      ENDIF()
    ENDFOREACH()
  ENDFOREACH()

  IF(AUTH_CLIENT_PLUGINS_COPY AND MAINTAINER_MODE)
    MESSAGE(FATAL_ERROR "Found unexpected client authentication plugins: ${AUTH_CLIENT_PLUGINS_COPY}")
  ENDIF()

  IF(EXPECTED_AUTH_CLIENT_PLUGINS AND MAINTAINER_MODE)
    MESSAGE(FATAL_ERROR "Didn't find expected client authentication plugins: ${EXPECTED_AUTH_CLIENT_PLUGINS}")
  ENDIF()

  ADD_DEFINITIONS(-DDEFAULT_MYSQL_PLUGIN_DIR="${INSTALL_MYSQL_PLUGINS_DIR}")
ENDIF()

SET(INSTALL_INCLUDEDIR "include/mariadb-shell")
SET(INSTALL_SHAREDIR "share/mariadb-shell")
SET(INSTALL_DOCDIR "share/mariadb-shell/Docs")
SET(INSTALL_MANDIR "share/man")

##
## Binary location
##
# Multi-config generators (e.g. Visual Studio) place binaries in a per-config
# subdir; single-config generators (Ninja, Makefiles - incl. Ninja+MSVC) do not.
# Key this off the generator, not the compiler: IF(MSVC) is also true for
# Ninja+cl, which wrongly adds a $<CONFIG> subdir so bundled files (e.g. the
# Python stdlib) land where the runtime does not look for them (bin is at
# <bld>/bin but bundles went to <bld>/<config>/lib). This matches how
# msvc.cmake / RUNTIME_OUTPUT_DIRECTORY key off CMAKE_CONFIGURATION_TYPES.
IF(CMAKE_CONFIGURATION_TYPES)
  SET(CONFIG_BINARY_DIR "${CMAKE_BINARY_DIR}/$<CONFIG>")
ELSE()
  SET(CONFIG_BINARY_DIR "${CMAKE_BINARY_DIR}")
ENDIF()

##
## Configure style system detection
##
INCLUDE(configure.cmake)

## The following definition is required to have memset_s
## available on the platforms that implement it
CHECK_FUNCTION_EXISTS (memset_s HAVE_MEMSET_S)
IF (HAVE_MEMSET_S)
  ADD_DEFINITIONS(-D__STDC_WANT_LIB_EXT1__=1)
ENDIF()
CONFIGURE_FILE(mysh_config.h.cmake   ${CMAKE_BINARY_DIR}/include/mysh_config.h)

MACRO (MY_CHECK_CXX_COMPILER_FLAG FLAG RESULT)
  CMAKE_PUSH_CHECK_STATE()
  STRING_APPEND(CMAKE_REQUIRED_FLAGS " ${FLAG}")
  CHECK_CXX_SOURCE_COMPILES("int main(void) { return 0; }" ${RESULT}
    FAIL_REGEX "unknown argument ignored"
    FAIL_REGEX "argument unused during compilation"
    FAIL_REGEX "unsupported .*option"
    FAIL_REGEX "unknown .*option"
    FAIL_REGEX "unrecognized .*option"
    FAIL_REGEX "ignoring unknown option"
    FAIL_REGEX "[Ww]arning: [Oo]ption"
    FAIL_REGEX "error: visibility"
    FAIL_REGEX "warning: visibility"
    )
  CMAKE_POP_CHECK_STATE()
ENDMACRO()

# By default the shell uses Multi-threaded Dynamic RunTime
# Only sets Static if needed
IF(WIN32)
  IF(WITH_STATIC_MSVCRT)
    FOREACH(flag_var
          CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
          CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
       IF(${flag_var} MATCHES "/MD")
          STRING(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
       ENDIF(${flag_var} MATCHES "/MD")
    ENDFOREACH(flag_var)
  ENDIF()
ENDIF()

# Set NDEBUG for non-debug project types.
FOREACH(BUILD_TYPE RELEASE RELWITHDEBINFO MINSIZEREL)
  FOREACH(LANG C CXX)
    SET(CMAKE_${LANG}_FLAGS_${BUILD_TYPE} "${CMAKE_${LANG}_FLAGS_${BUILD_TYPE}} -DNDEBUG ")
  ENDFOREACH()
ENDFOREACH()

# MySQL abolished DBUG_OFF (Bug#32216281) in favour of NDEBUG, but MariaDB's
# my_dbug.h still gates DBUG purely on the consumer's DBUG_OFF macro: without
# it the shell's DBUG_LOG/DBUG_EXECUTE_IF expand to real _db_*() calls
# (_db_keyword_, _db_set_, _db_set_init_, ...). Those symbols exist in libdbug
# only when MariaDB itself was built with DBUG enabled, i.e. a Debug build;
# every other build type (RelWithDebInfo, Release, MinSizeRel) strips dbug.c to
# a stub. The determining factor is therefore the build type of the *linked
# MariaDB*, NOT of the shell -- a Debug shell linked against a non-Debug MariaDB
# still fails to resolve _db_*. Detect the MariaDB build type from its cache and
# define DBUG_OFF (via the base language flags, so every configuration incl.
# Debug sees it) unless that MariaDB provides the DBUG runtime.
IF(MARIADB_BUILD)
  SET(_mariadb_has_dbug FALSE)
  IF(MARIADB_BUILD_DIR AND EXISTS "${MARIADB_BUILD_DIR}/CMakeCache.txt")
    FILE(STRINGS "${MARIADB_BUILD_DIR}/CMakeCache.txt" _mariadb_build_type
      REGEX "^CMAKE_BUILD_TYPE:")
    IF(_mariadb_build_type MATCHES "[Dd]ebug")
      SET(_mariadb_has_dbug TRUE)
    ENDIF()
  ENDIF()

  IF(_mariadb_has_dbug)
    MESSAGE(STATUS "Linked MariaDB is a Debug build; leaving DBUG enabled")
  ELSE()
    MESSAGE(STATUS "Linked MariaDB has no DBUG runtime; defining DBUG_OFF")
    FOREACH(LANG C CXX)
      SET(CMAKE_${LANG}_FLAGS "${CMAKE_${LANG}_FLAGS} -DDBUG_OFF ")
    ENDFOREACH()
  ENDIF()
ENDIF()

IF(NOT CMAKE_BUILD_TYPE
    AND NOT CMAKE_GENERATOR MATCHES "Visual Studio"
    AND NOT CMAKE_GENERATOR MATCHES "Xcode")
    # This is the case of no CMAKE_BUILD_TYPE choosen, typical for VS and Xcode
    # or if custom C flags are set. In VS and Xcode for non-Debug configurations
    # NDEBUG is already correctly set.
    ADD_DEFINITIONS(-DNDEBUG)
ENDIF()

# Includes common to the whole project
INCLUDE_DIRECTORIES(
            ${CMAKE_BINARY_DIR}
            ${PROJECT_SOURCE_DIR}/ext
            ${MYSQL_INCLUDE_DIRS}
            ${MYSQLX_INCLUDE_DIRS}
            ${PROTOBUF_INCLUDE_DIRS}
            ${MYSQL_SOURCE_DIR}/extra/rapidjson/include
            SYSTEM ${ANTLR4_INCLUDE_DIRS})

###
### Handle User Options
###

if(BUNDLED_SSH_DIR)
  set(libssh_DIR ${BUNDLED_SSH_DIR})
endif()

find_package(libssh 0.9.2 REQUIRED)

message(STATUS "Found libssh: ${libssh_VERSION}, using config: ${libssh_CONFIG}")

add_definitions(-DMYSH_VERSION="${MYSH_VERSION}" -DMYSH_BUILD_ID="${MYSH_BUILD_ID}" -DEXTRA_NAME_SUFFIX="${EXTRA_NAME_SUFFIX}")
if(WIN32)
  add_definitions(-DMYSH_VERSION_WIN="${MYSH_VERSION_WIN}")
endif()

##
## Prepares the python dependencies
###

# PYTHON_DEPS_PACKAGES: third-party Python packages to ship inside the bundle.
# The BUNDLED interpreter installs them itself via pip, so any C extensions are
# built/resolved for exactly its ABI (and its OpenSSL). It installs at configure
# time into a staging directory -- the source Python is never mutated -- and that
# staging directory is then handed to PYTHON_DEPS below: a folder of installed
# packages is exactly what PYTHON_DEPS already bundles (into the interpreter's
# site-packages) on every platform, so there is a single bundling path.
# Semicolon- or space-separated list of pip requirement specs, e.g.
#   -DPYTHON_DEPS_PACKAGES="certifi;PyYAML==6.0".
#
# Works for every bundled build: Windows (the located python.org install) and
# Linux/macOS (BUNDLED_PYTHON_DIR / WITH_PYTHON_SOURCE). A system-Python
# (non-bundled) unix build has nothing to bundle into, so it uses PYTHON_DEPS
# directly instead.
#
# When a bundle is being produced and the caller did not specify the list, it
# defaults to the packages the bundled shell plugins require: certifi (CA bundle),
# PyYAML, and the ANTLR4 Python runtime (antlr4-python3-runtime). Pass
# -DPYTHON_DEPS_PACKAGES="..." to override, or an empty string
# (-DPYTHON_DEPS_PACKAGES=) to install none.
#
# This is not specific to MariaDB builds: certifi is imported by the plugin
# manager itself (python/packages/mysqlsh/plugin_manager/general.py), so a bundle
# without it fails to load EVERY decorator-based plugin - including the bundled
# sandbox plugin, which then surfaces as "name 'sandbox' is not defined".

# "Are we producing a bundle that embeds a Python?" Windows always bundles the
# located python.org install; unix bundles when BUNDLED_PYTHON_DIR is set.
SET(_py_bundling FALSE)
IF(HAVE_PYTHON AND (WIN32 OR BUNDLED_PYTHON_DIR))
  SET(_py_bundling TRUE)
ENDIF()

IF(_py_bundling AND NOT DEFINED PYTHON_DEPS_PACKAGES)
  SET(PYTHON_DEPS_PACKAGES "certifi;pyyaml;antlr4-python3-runtime;mcp")
ENDIF()

# Tracks that PYTHON_DEPS below was produced by PYTHON_DEPS_PACKAGES (rather than
# supplied directly by the caller); used to gate the historical -DPYTHON_DEPS
# contract further down.
SET(_py_deps_from_packages FALSE)

IF(PYTHON_DEPS_PACKAGES)
  IF(NOT HAVE_PYTHON)
    MESSAGE(FATAL_ERROR "PYTHON_DEPS_PACKAGES set but Python support is disabled.")
  ENDIF()
  IF(NOT _py_bundling)
    MESSAGE(FATAL_ERROR
      "PYTHON_DEPS_PACKAGES requires a bundled Python. On Windows a python.org "
      "install is bundled automatically; on Linux/macOS set BUNDLED_PYTHON_DIR "
      "(or WITH_PYTHON_SOURCE to build one). For a system Python, use "
      "PYTHON_DEPS=<dir> instead.")
  ENDIF()
  IF(PYTHON_DEPS)
    MESSAGE(FATAL_ERROR
      "PYTHON_DEPS and PYTHON_DEPS_PACKAGES are alternatives (both bundle into "
      "site-packages). Put everything in the PYTHON_DEPS folder, or list "
      "everything in PYTHON_DEPS_PACKAGES -- not both.")
  ENDIF()
  IF(NOT PYTHON_EXECUTABLE)
    MESSAGE(FATAL_ERROR
      "PYTHON_DEPS_PACKAGES set but the bundled Python interpreter was not found.")
  ENDIF()

  # Accept both ';'- and space-separated lists.
  STRING(REPLACE " " ";" _py_pkg_list "${PYTHON_DEPS_PACKAGES}")

  # Install the packages into a staging directory. Treat it as a cache keyed on
  # the requested set: only (re)install when the list changes, so reconfigures do
  # not keep hitting PyPI. The stamp lives outside the staging dir so it is not
  # itself bundled.
  SET(_py_deps_staging "${CMAKE_BINARY_DIR}/bundled-python-deps")
  SET(_py_dep_stamp "${CMAKE_BINARY_DIR}/bundled-python-deps.stamp")
  SET(_py_dep_stamped "")
  IF(EXISTS "${_py_dep_stamp}")
    FILE(READ "${_py_dep_stamp}" _py_dep_stamped)
  ENDIF()

  IF(NOT _py_dep_stamped STREQUAL "${_py_pkg_list}" OR NOT EXISTS "${_py_deps_staging}")
    MESSAGE(STATUS "Installing bundled Python packages via pip: ${_py_pkg_list}")

    # The bundled interpreter must have pip. A from-source CPython built with
    # --enable-shared frequently ends up WITHOUT it: its "make install" runs
    # ensurepip as a non-fatal step ("Ignoring ensurepip failure...") that is
    # often skipped, so the prefix has no pip. Bootstrap it here before use --
    # ensurepip installs from a wheel bundled in the stdlib, so this needs no
    # network and is a near no-op when pip is already present. Doing it at the
    # point of use (rather than in bootstrap_python.cmake) also repairs an
    # already-built interpreter, which the Python bootstrap fast-path would
    # otherwise reuse as-is.
    EXECUTE_PROCESS(
      COMMAND "${PYTHON_EXECUTABLE}" -c "import pip"
      RESULT_VARIABLE _py_has_pip
      OUTPUT_QUIET ERROR_QUIET)
    IF(NOT _py_has_pip EQUAL 0)
      MESSAGE(STATUS "  bundled interpreter has no pip; bootstrapping via ensurepip")
      EXECUTE_PROCESS(
        COMMAND "${PYTHON_EXECUTABLE}" -m ensurepip --upgrade
        RESULT_VARIABLE _py_ensurepip_rc)
      IF(NOT _py_ensurepip_rc EQUAL 0)
        MESSAGE(FATAL_ERROR
          "The bundled Python interpreter has neither pip nor a working "
          "ensurepip module, so ${_py_pkg_list} cannot be installed. Rebuild "
          "the interpreter with ensurepip enabled, or supply the packages "
          "directly via -DPYTHON_DEPS=<dir>.")
      ENDIF()
    ENDIF()

    # Install into a clean target so pip does not object to pre-existing content.
    FILE(REMOVE_RECURSE "${_py_deps_staging}")
    FILE(MAKE_DIRECTORY "${_py_deps_staging}")
    EXECUTE_PROCESS(
      COMMAND "${PYTHON_EXECUTABLE}" -m pip install
              --disable-pip-version-check
              --target "${_py_deps_staging}" ${_py_pkg_list}
      RESULT_VARIABLE _py_pip_rc)
    IF(NOT _py_pip_rc EQUAL 0)
      MESSAGE(FATAL_ERROR
        "Failed to install bundled Python packages (see output above). Ensure "
        "the bundled interpreter has pip and that PyPI is reachable.")
    ENDIF()
    FILE(WRITE "${_py_dep_stamp}" "${_py_pkg_list}")
  ELSE()
    MESSAGE(STATUS "Bundled Python packages already installed: ${_py_pkg_list}")
  ENDIF()

  # Feed the staged packages into the standard PYTHON_DEPS bundling path below.
  SET(PYTHON_DEPS "${_py_deps_staging}")
  SET(_py_deps_from_packages TRUE)
ENDIF()

IF(PYTHON_DEPS)

  IF (NOT WIN32 AND NOT BUNDLED_PYTHON_DIR)
    # PYTHON_DEPS_PATH will cause the shell initialization to explicitly add the
    # bundled packages to the PYTHONPATH.
    # Only needed on this case, as when WIN32 or BUNDLED_PYTHON_DIR they are
    # automatically loaded by the site.py logic
    ADD_DEFINITIONS(-DPYTHON_DEPS_PATH="lib/python${PYTHONLIBS_MAJOR_MINOR}/site-packages")
  ENDIF()

  # -DPYTHON_DEPS advertises a curated dependency set (e.g. the oci plugin test
  # keys off it). That reflects a caller-provided PYTHON_DEPS folder, not the
  # auto-installed PYTHON_DEPS_PACKAGES default, so only define it for the former.
  IF(NOT _py_deps_from_packages)
    ADD_DEFINITIONS(-DPYTHON_DEPS)
  ENDIF()
ENDIF()

IF(NOT BUNDLED_MYSQLBINLOG AND MYSQL_BUILD_DIR)
  IF(WIN32)
    SET(BUNDLED_MYSQLBINLOG "${MYSQL_BUILD_DIR}/runtime_output_directory/${CMAKE_BUILD_TYPE}/mysqlbinlog.exe")
  ELSE()
    SET(BUNDLED_MYSQLBINLOG "${MYSQL_BUILD_DIR}/runtime_output_directory/mysqlbinlog")
  ENDIF()
ENDIF()

IF(NOT BUILD_SOURCE_PACKAGE AND BUNDLED_MYSQLBINLOG AND NOT EXISTS "${BUNDLED_MYSQLBINLOG}")
  message(FATAL_ERROR "The bundled 'mysqlbinlog' binary was not found, please compile it using: cmake --build '${MYSQL_BUILD_DIR}' --target mysqlbinlog")
ENDIF()

IF(BUNDLED_MYSQL_CONFIG_EDITOR OR BUNDLED_MYSQLBINLOG)
  add_definitions(-DHAVE_LIBEXEC_DIR)
ENDIF()

IF(WITH_TESTS)
  ###
  ### Unit-test support
  ###
  add_subdirectory(unittest)
  add_subdirectory(tests/fuzz)
ENDIF()

###
### Build Projects
###

ADD_SUBDIRECTORY(plugins/external)
ADD_SUBDIRECTORY(python)
ADD_SUBDIRECTORY(mysqlshdk)
ADD_SUBDIRECTORY(modules)
ADD_SUBDIRECTORY(res)
ADD_SUBDIRECTORY(src)
add_subdirectory(samples)
add_subdirectory(mysql-secret-store)
add_subdirectory(tests/bench)

IF(UNIX)
  ADD_SUBDIRECTORY(man)
ENDIF()

CHECK_STAN_OPTIONS()

IF(EXISTS ${CMAKE_SOURCE_DIR}/internal/CMakeLists.txt)
  ADD_SUBDIRECTORY(internal)
ENDIF()

###
### Optionally bundle Visual C++ Redistributable for Visual Studio
###

if(WIN32)
  option(BUNDLE_RUNTIME_LIBRARIES "Install Windows runtime libraries" OFF)
  if(BUNDLE_RUNTIME_LIBRARIES)
     INSTALL_VS_REDIST(main)
  endif()
endif()

#
# Create the INFO_SRC and INFO_BIN files
# =========================
#
# Will set GIT_EXECUTABLE and GIT_FOUND
FIND_PACKAGE(Git)

CONFIGURE_FILE(
    ${CMAKE_SOURCE_DIR}/cmake/info_macros.cmake.in
    ${CMAKE_BINARY_DIR}/info_macros.cmake @ONLY)

INCLUDE(info_src)
INSTALL(FILES ${CMAKE_BINARY_DIR}/Docs/INFO_SRC COMPONENT main DESTINATION ${INSTALL_DOCDIR})

IF (NOT BUILD_SOURCE_PACKAGE)
  INCLUDE(info_bin)
  INSTALL(FILES ${CMAKE_BINARY_DIR}/Docs/INFO_BIN COMPONENT main DESTINATION ${INSTALL_DOCDIR})
ENDIF()


###
### Packaging and other dependecy copying
###

INCLUDE(packaging)

if(CPACK_WIX_MYSQLSH_USE_CUSTOM_ACTION)
  add_subdirectory(packaging/wix4/custom_action)
endif()
