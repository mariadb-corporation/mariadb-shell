# Copyright (c) 2015, 2026, Oracle and/or its affiliates.
# Copyright (c) 2026, MariaDB plc.
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

##############################################################################
#
#  Packaging
#
##############################################################################

# The rest is mainly about CPack
if(NOT EXTRA_NAME_SUFFIX)
  set(EXTRA_NAME_SUFFIX "")
endif()
if(NOT EXTRA_NAME_SUFFIX2)
  set(EXTRA_NAME_SUFFIX2 "")
endif()

# A "portable" build ships its whole dependency closure (vcpkg toolchain or an
# explicit BUNDLED_*_DIR), so it is distributed as a generic tarball/zip rather
# than a distro-native package. System-dependency Linux builds instead produce a
# native .deb/.rpm named after the distro. USING_VCPKG / BUNDLED_OPENSSL_DIR are
# set much earlier in the top-level CMakeLists.txt.
if(USING_VCPKG OR BUNDLED_OPENSSL_DIR OR WIN32 OR APPLE)
  set(_shell_portable_build TRUE)
else()
  set(_shell_portable_build FALSE)
endif()

# MySQL-style CPU suffix used in the tarball/zip base name (MYSH_PLATFORM).
# CMAKE_SYSTEM_PROCESSOR spelling varies by OS (arm64, aarch64, ARM64), so match
# case-insensitively.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "[Aa][Rr][Mm]|[Aa][Aa][Rr][Cc][Hh]")
  set(_pkg_arch "arm-64bit")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(_pkg_arch "x86-32bit")
else()
  set(_pkg_arch "x86-64bit")
endif()

# Auto-detect the packaging platform string when it was not supplied explicitly
# on the configure line. Official builds pass -DMYSH_PLATFORM=... ; this keeps a
# plain `cpack` naming the tarball/zip correctly:
#   macOS   -> macos<major>-<arch>          (e.g. macos26-x86-64bit)
#   Windows -> windows-<arch>               (e.g. windows-x86-64bit)
#   Linux   -> linux-glibc<ver>-<arch>      (portable/vcpkg builds only)
# System-dependency Linux builds leave MYSH_PLATFORM empty; their native
# .deb/.rpm name is derived in the generator section below.
#
# Whether MYSH_PLATFORM was supplied explicitly (a real Oracle-style release
# build) or auto-detected here decides if component-based packaging is turned on
# further down. Auto-detected builds stay monolithic so the tarball keeps ALL
# installed content (bundled Python, deps, and any untagged install rules),
# matching the original package layout.
if(MYSH_PLATFORM)
  set(_mysh_platform_explicit TRUE)
else()
  set(_mysh_platform_explicit FALSE)
endif()

if(NOT MYSH_PLATFORM)
  if(APPLE)
    execute_process(COMMAND sw_vers -productVersion
                    OUTPUT_VARIABLE _macos_ver
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "^[0-9]+" _macos_major "${_macos_ver}")
    set(MYSH_PLATFORM "macos${_macos_major}-${_pkg_arch}")
  elseif(WIN32)
    set(MYSH_PLATFORM "windows-${_pkg_arch}")
  elseif(CMAKE_SYSTEM_NAME MATCHES "Linux" AND _shell_portable_build)
    execute_process(COMMAND getconf GNU_LIBC_VERSION
                    OUTPUT_VARIABLE _glibc_raw
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
    string(REGEX MATCH "[0-9]+\\.[0-9]+" _glibc_ver "${_glibc_raw}")
    if(_glibc_ver)
      set(MYSH_PLATFORM "linux-glibc${_glibc_ver}-${_pkg_arch}")
    else()
      set(MYSH_PLATFORM "linux-${_pkg_arch}")
    endif()
  endif()
endif()

set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "MariaDB Shell ${MYSH_VERSION}, a command line shell and scripting environment for MariaDB")
set(CPACK_PACKAGE_NAME                "mariadb-shell${EXTRA_NAME_SUFFIX}")
set(CPACK_PACKAGE_VENDOR              "MariaDB Corporation")
set(CPACK_PACKAGE_DESCRIPTION_FILE    "${CMAKE_SOURCE_DIR}/README.md")
if(WIN32)
  # WiX wants the license file to end in ".txt"
  configure_file(${CMAKE_SOURCE_DIR}/LICENSE
                 ${CMAKE_BINARY_DIR}/LICENSE.txt COPYONLY)
  set(CPACK_RESOURCE_FILE_LICENSE     "${CMAKE_BINARY_DIR}/LICENSE.txt")
else()
  set(CPACK_RESOURCE_FILE_LICENSE     "${CMAKE_SOURCE_DIR}/LICENSE")
endif()
set(CPACK_SOURCE_PACKAGE_FILE_NAME    "${CPACK_PACKAGE_NAME}-${MYSH_VERSION}-src")

set(CPACK_PACKAGE_INSTALL_DIRECTORY   "${CPACK_PACKAGE_NAME}-${MYSH_VERSION}${EXTRA_NAME_SUFFIX2}${PACKAGE_DRIVER_TYPE_SUFFIX}-${MYSH_PLATFORM}")

IF(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY   "${CPACK_PACKAGE_INSTALL_DIRECTORY}-debug")
ENDIF()

set(CPACK_PACKAGE_FILE_NAME           "${CPACK_PACKAGE_INSTALL_DIRECTORY}")
set(CPACK_STRIP_FILES                 "bin/mariadb-shell")

if(WIN32)
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "MariaDB/MariaDB Shell ${MYSH_BASE_VERSION}")
  IF(WITH_DEV)
    SET(CPACK_GENERATOR                 "ZIP")
  ELSE()
    set(CPACK_GENERATOR                 "ZIP;WIX")
  ENDIF()
  set(CPACK_PACKAGE_NAME              "MariaDB Shell ${MYSH_VERSION}")
  # MariaDB Shell is a separate product: this GUID must differ from MySQL
  # Shell's ("292FA6A2-8E70-4BC8-8C93-C1A374C8636C") so the MSI never treats an
  # installed MySQL Shell as an upgradeable predecessor (and vice versa).
  set(CPACK_WIX_UPGRADE_GUID          "44BE7000-D488-4C81-8EA0-4C46CB94A4F2")
  set(CPACK_WIX_TEMPLATE              "${CMAKE_SOURCE_DIR}/cmake/WIX.template.in")
  set(CPACK_WIX_PROGRAM_MENU_FOLDER   "MariaDB")
  if(NOT BUNDLE_RUNTIME_LIBRARIES)
    set(CPACK_WIX_VS_REDIST_CHECK     "1")
    if(MSVC_VERSION GREATER_EQUAL 1940 AND MSVC_VERSION LESS_EQUAL 1949)
      set(CPACK_WIX_REDIST_YEAR "2022")
      set(CPACK_WIX_REDIST_VERSION "14.40.0")
    elseif(MSVC_VERSION GREATER_EQUAL 1930 AND MSVC_VERSION LESS_EQUAL 1939)
      set(CPACK_WIX_REDIST_YEAR "2022")
      set(CPACK_WIX_REDIST_VERSION "14.30.0")
    elseif(MSVC_VERSION GREATER_EQUAL 1920 AND MSVC_VERSION LESS_EQUAL 1929)
      set(CPACK_WIX_REDIST_YEAR "2019")
      set(CPACK_WIX_REDIST_VERSION "14.20.0")
    elseif(MSVC_VERSION GREATER_EQUAL 1910 AND MSVC_VERSION LESS_EQUAL 1919)
      set(CPACK_WIX_REDIST_YEAR "2017")
      set(CPACK_WIX_REDIST_VERSION "14.10.0")
    elseif(MSVC_VERSION EQUAL 1900)
      set(CPACK_WIX_REDIST_YEAR "2015")
      set(CPACK_WIX_REDIST_VERSION "14.0.0")
    else()
      message(FATAL_ERROR "Unknown Visual Studio version: ${MSVC_VERSION}")
    endif()
  endif()
  set(CPACK_PACKAGE_EXECUTABLES       "mariadb-shell;MariaDB Shell")
  if(HAVE_PYTHON)
    set(CPACK_WIX_WITH_PYTHON "1")
    set(CPACK_WIX_PYTHON_DIR "Python${PYTHONLIBS_MAJOR_MINOR}")
  endif()
  set(CPACK_WIX_EXTENSIONS "WixUtilExtension.dll")

  set(CPACK_WIX_MYSQLSH_USE_CUSTOM_ACTION "1")
  set(CPACK_WIX_MYSQLSH_CUSTOM_ACTION_DLL "${CMAKE_BINARY_DIR}/packaging/wix4/custom_action/${CMAKE_BUILD_TYPE}/mariadb-shell.ca.dll")
else()
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "MariaDB Corporation <info@mariadb.com>")

  if(APPLE OR _shell_portable_build)
    # macOS, and portable (vcpkg/bundled) Linux builds: generic tarball only.
    # The clean name comes from CPACK_PACKAGE_FILE_NAME / MYSH_PLATFORM above.
    set(CPACK_GENERATOR                 "TGZ")
  else()
    # System-dependency Linux build: emit a distro-native .deb/.rpm whose name
    # follows the distro convention, e.g.
    #   mariadb-shell_9.7.1-1ubuntu22.04_amd64.deb
    #   mariadb-shell-9.7.1-1.fc43.x86_64.rpm
    set(_distro_id "")
    set(_distro_ver "")
    if(EXISTS "/etc/os-release")
      file(STRINGS "/etc/os-release" _osrel)
      foreach(_line ${_osrel})
        if(_line MATCHES "^ID=")
          string(REGEX REPLACE "^ID=\"?([^\"]*)\"?$" "\\1" _distro_id "${_line}")
        elseif(_line MATCHES "^VERSION_ID=")
          string(REGEX REPLACE "^VERSION_ID=\"?([^\"]*)\"?$" "\\1" _distro_ver "${_line}")
        endif()
      endforeach()
    endif()
    string(REGEX MATCH "^[0-9]+" _distro_major "${_distro_ver}")

    # Architecture as spelled by each packaging system.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "[Aa][Rr][Mm]|[Aa][Aa][Rr][Cc][Hh]")
      set(_deb_arch "arm64")
      set(_rpm_arch "aarch64")
    else()
      set(_deb_arch "amd64")
      set(_rpm_arch "x86_64")
    endif()

    if(_distro_id STREQUAL "ubuntu")
      set(_distro_family "deb")
      set(_distro_tag "ubuntu${_distro_ver}")      # e.g. 1ubuntu22.04
    elseif(_distro_id STREQUAL "debian")
      set(_distro_family "deb")
      set(_distro_tag "debian${_distro_major}")    # e.g. 1debian12
    elseif(_distro_id STREQUAL "fedora")
      set(_distro_family "rpm")
      set(_distro_tag "fc${_distro_major}")        # e.g. 1.fc43
    elseif(_distro_id MATCHES "sles|sled|opensuse")
      set(_distro_family "rpm")
      set(_distro_tag "sl${_distro_major}")        # e.g. 1.sl15
    elseif(_distro_id MATCHES "rhel|centos|rocky|almalinux|^ol$")
      set(_distro_family "rpm")
      set(_distro_tag "el${_distro_major}")        # e.g. 1.el9
    else()
      set(_distro_family "")
    endif()

    if(_distro_family STREQUAL "deb")
      set(CPACK_GENERATOR                   "DEB")
      set(CPACK_DEBIAN_FILE_NAME            "DEB-DEFAULT")
      set(CPACK_DEBIAN_PACKAGE_RELEASE      "1${_distro_tag}")
      set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${_deb_arch}")
    elseif(_distro_family STREQUAL "rpm")
      set(CPACK_GENERATOR                   "RPM")
      set(CPACK_RPM_FILE_NAME               "RPM-DEFAULT")
      set(CPACK_RPM_PACKAGE_LICENSE         "GPL")
      set(CPACK_RPM_PACKAGE_RELEASE         "1.${_distro_tag}")
      set(CPACK_RPM_PACKAGE_ARCHITECTURE    "${_rpm_arch}")
    else()
      # Unknown distro: fall back to the historical rpmbuild-probe behavior.
      FIND_PROGRAM(RPMBUILD_EXECUTABLE rpmbuild)
      if(NOT RPMBUILD_EXECUTABLE)
        set(CPACK_SET_DESTDIR             "on")
        set(CPACK_GENERATOR               "TGZ;DEB")
      else()
        set(CPACK_GENERATOR               "TGZ;RPM")
        set(CPACK_RPM_PACKAGE_LICENSE     "GPL")
      endif()
    endif()
  endif()
endif()

set(CPACK_SOURCE_IGNORE_FILES
\\\\.git/
\\\\.gitignore
CMakeCache\\\\.txt
CPackSourceConfig\\\\.cmake
CPackConfig\\\\.cmake
VersionInfo\\\\.h$
postflight$
/cmake_install\\\\.cmake
/CTestTestfile\\\\.cmake
/CMakeFiles/
/_CPack_Packages/
Makefile$
cmake/sql.*\\\\.c$
)

#------------ Installation ---------------------------

if(WIN32)

# TODO: line-ending conversions unix->dos

# install(FILES ChangeLog     DESTINATION . RENAME ChangeLog.txt)
  install(FILES README.md        DESTINATION . RENAME README.txt COMPONENT main)
# install(FILES INSTALL.md       DESTINATION . RENAME INSTALL.txt)
  install(FILES LICENSE       DESTINATION . RENAME LICENSE.txt COMPONENT main)

  # Install all .pdb files to enable debugging. Note that what build
  # type and what sub directory the binaries ends up in, like
  # "Release" and "Debug", is not determined until we run "devenv" or
  # similar. So when running "cmake" we don't know the location. We
  # can't test for the location here, a if(EXISTS ...) is run at
  # "cmake" invocation time, not when we are to install. So we do a
  # bit of a hack here until finding a better solution.
  install(DIRECTORY
    ${PROJECT_BINARY_DIR}/bin/RelWithDebInfo/
    ${PROJECT_BINARY_DIR}/bin/Debug/
    DESTINATION bin
    COMPONENT dev
    FILES_MATCHING
    PATTERN *.pdb
  )
  install(DIRECTORY
    ${PROJECT_BINARY_DIR}/lib/RelWithDebInfo/
    ${PROJECT_BINARY_DIR}/lib/Debug/
    DESTINATION lib
    COMPONENT dev
    FILES_MATCHING
    PATTERN *.pdb
  )


else()

# install(FILES ChangeLog    DESTINATION .)
  install(FILES README.md       DESTINATION share/mariadb-shell/ COMPONENT main)
  install(FILES README.md       DESTINATION share/mariadb-shell/ COMPONENT dev)
# install(FILES INSTALL.md     DESTINATION .)
  install(FILES LICENSE      DESTINATION share/mariadb-shell/ COMPONENT main)
  install(FILES LICENSE      DESTINATION share/mariadb-shell/ COMPONENT dev)

endif()

# Component-based packaging is only enabled for explicit Oracle-style release
# builds (-DMYSH_PLATFORM=... passed in). Auto-detected builds stay monolithic:
# CPack then packages the whole install tree - every component plus untagged
# rules - so the tarball is complete (bundled Python, deps, ...) and unpacks to
# a single "<file-name>/{bin,lib,share}" tree with no per-component filtering.
IF(_mysh_platform_explicit)
  IF(WITH_DEV)
      SET(CPACK_COMPONENTS_ALL main dev)
  ELSE()
    SET(CPACK_COMPONENTS_ALL main)
  ENDIF()

  set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
  set(CPACK_DEB_COMPONENT_INSTALL ON)
  set(CPACK_RPM_COMPONENT_INSTALL ON)
  set(CPACK_WIX_COMPONENT_INSTALL ON)
ENDIF()

include(CPack)
