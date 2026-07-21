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
# bootstrap_vcpkg.cmake
#
# Configure-time convenience: when the caller only asks for a triplet via
# -DWITH_VCPKG_TRIPLET=<triplet>, fetch and bootstrap vcpkg automatically and
# derive the two variables the rest of the build (and the MariaDB server
# bootstrap) already understand:
#
#   CMAKE_TOOLCHAIN_FILE = <VCPKG_ROOT>/scripts/buildsystems/vcpkg.cmake
#   VCPKG_TARGET_TRIPLET = <triplet>
#
# so the user no longer has to clone vcpkg, run its bootstrap script, and pass
# the long -DCMAKE_TOOLCHAIN_FILE / -DVCPKG_TARGET_TRIPLET pair by hand.
#
# This MUST run before the first project() call (CMake reads
# CMAKE_TOOLCHAIN_FILE there) and before bootstrap_mariadb.cmake (which
# propagates the toolchain + triplet to the server tree).
#
# Explicitly passing -DCMAKE_TOOLCHAIN_FILE / -DVCPKG_TARGET_TRIPLET keeps
# working and takes precedence: WITH_VCPKG_TRIPLET is a shortcut, not a
# requirement.
#
##############################################################################

# Nothing to do unless the shortcut was requested.
IF(NOT WITH_VCPKG_TRIPLET)
  RETURN()
ENDIF()

# The triplet is what the caller ultimately wants selected. Honour an explicit
# VCPKG_TARGET_TRIPLET if one was also passed, otherwise adopt the shortcut's.
IF(NOT VCPKG_TARGET_TRIPLET)
  SET(VCPKG_TARGET_TRIPLET "${WITH_VCPKG_TRIPLET}"
    CACHE STRING "vcpkg target triplet" FORCE)
ENDIF()

# If the caller already supplied a vcpkg toolchain file, respect it and only
# make sure the triplet is set -- do not clone a second copy.
IF(CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "[Vv]cpkg")
  MESSAGE(STATUS "WITH_VCPKG_TRIPLET: reusing the vcpkg toolchain already given "
                 "(${CMAKE_TOOLCHAIN_FILE}); triplet '${VCPKG_TARGET_TRIPLET}'")
  RETURN()
ENDIF()

# Where to fetch vcpkg from, and which ref. Dependency versions are pinned by
# the repo's vcpkg.json (builtin-baseline + overrides), so the tip of the
# default branch is fine here.
SET(VCPKG_GIT_REPOSITORY "https://github.com/microsoft/vcpkg.git"
  CACHE STRING "Git URL of vcpkg to fetch when WITH_VCPKG_TRIPLET is set")
SET(VCPKG_GIT_TAG "master"
  CACHE STRING "Git ref (branch, tag, or commit) of vcpkg to fetch")

# Where vcpkg is cloned. Honour an existing VCPKG_ROOT (cache or environment)
# so a shared checkout is reused; otherwise clone next to the shell source,
# outside the build dir, so a clean rebuild does not re-clone/re-bootstrap.
IF(VCPKG_ROOT)
  SET(_vcpkg_root "${VCPKG_ROOT}")
ELSEIF(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
  SET(_vcpkg_root "$ENV{VCPKG_ROOT}")
ELSE()
  SET(_vcpkg_root "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg")
ENDIF()
GET_FILENAME_COMPONENT(_vcpkg_root "${_vcpkg_root}" ABSOLUTE)

# The bootstrap script and the executable it produces differ by host OS. WIN32
# is not set this early (before project()), so key off CMAKE_HOST_WIN32.
IF(CMAKE_HOST_WIN32)
  SET(_vcpkg_bootstrap "${_vcpkg_root}/bootstrap-vcpkg.bat")
  SET(_vcpkg_exe "${_vcpkg_root}/vcpkg.exe")
ELSE()
  SET(_vcpkg_bootstrap "${_vcpkg_root}/bootstrap-vcpkg.sh")
  SET(_vcpkg_exe "${_vcpkg_root}/vcpkg")
ENDIF()

MESSAGE(STATUS "==========================================================")
MESSAGE(STATUS "WITH_VCPKG_TRIPLET='${WITH_VCPKG_TRIPLET}' -- bootstrapping vcpkg.")
MESSAGE(STATUS "  repo    : ${VCPKG_GIT_REPOSITORY}")
MESSAGE(STATUS "  ref     : ${VCPKG_GIT_TAG}")
MESSAGE(STATUS "  location: ${_vcpkg_root}")
MESSAGE(STATUS "  (override with -DVCPKG_ROOT / -DVCPKG_GIT_TAG; this runs once)")
MESSAGE(STATUS "==========================================================")

##############################################################################
# 1. Clone (or reuse) the vcpkg checkout.
##############################################################################
IF(NOT EXISTS "${_vcpkg_root}/.git")
  FIND_PACKAGE(Git REQUIRED)
  MESSAGE(STATUS "Cloning vcpkg...")
  GET_FILENAME_COMPONENT(_vcpkg_parent "${_vcpkg_root}" DIRECTORY)
  FILE(MAKE_DIRECTORY "${_vcpkg_parent}")
  # --depth 1 + --single-branch (implied) grab only the tip; --progress keeps
  # the meter visible in non-TTY build panels.
  EXECUTE_PROCESS(
    COMMAND "${GIT_EXECUTABLE}" clone --progress
            --branch "${VCPKG_GIT_TAG}"
            "${VCPKG_GIT_REPOSITORY}" "${_vcpkg_root}"
    RESULT_VARIABLE _rc)
  IF(NOT _rc EQUAL 0)
    # --branch (and shallow) fails on a raw commit SHA; fall back to a full
    # clone + checkout so an arbitrary SHA is reachable.
    MESSAGE(STATUS "Branch/tag clone failed; cloning default and checking out ref...")
    FILE(REMOVE_RECURSE "${_vcpkg_root}")
    EXECUTE_PROCESS(
      COMMAND "${GIT_EXECUTABLE}" clone --progress
              "${VCPKG_GIT_REPOSITORY}" "${_vcpkg_root}"
      RESULT_VARIABLE _rc)
    IF(NOT _rc EQUAL 0)
      MESSAGE(FATAL_ERROR "Failed to clone ${VCPKG_GIT_REPOSITORY}")
    ENDIF()
    EXECUTE_PROCESS(
      COMMAND "${GIT_EXECUTABLE}" -C "${_vcpkg_root}" checkout "${VCPKG_GIT_TAG}"
      RESULT_VARIABLE _rc)
    IF(NOT _rc EQUAL 0)
      MESSAGE(FATAL_ERROR "Failed to check out vcpkg ref '${VCPKG_GIT_TAG}'")
    ENDIF()
  ENDIF()
ELSE()
  MESSAGE(STATUS "Reusing existing vcpkg checkout at ${_vcpkg_root}")
ENDIF()

##############################################################################
# 2. Bootstrap vcpkg (builds the vcpkg executable) unless already present.
##############################################################################
IF(NOT EXISTS "${_vcpkg_exe}")
  IF(NOT EXISTS "${_vcpkg_bootstrap}")
    MESSAGE(FATAL_ERROR "vcpkg bootstrap script not found: ${_vcpkg_bootstrap}")
  ENDIF()
  MESSAGE(STATUS "Running vcpkg bootstrap (${_vcpkg_bootstrap})...")
  IF(CMAKE_HOST_WIN32)
    EXECUTE_PROCESS(
      COMMAND cmd /c "${_vcpkg_bootstrap}" -disableMetrics
      WORKING_DIRECTORY "${_vcpkg_root}"
      RESULT_VARIABLE _rc)
  ELSE()
    EXECUTE_PROCESS(
      COMMAND "${_vcpkg_bootstrap}" -disableMetrics
      WORKING_DIRECTORY "${_vcpkg_root}"
      RESULT_VARIABLE _rc)
  ENDIF()
  IF(NOT _rc EQUAL 0)
    MESSAGE(FATAL_ERROR "vcpkg bootstrap failed (see output above)")
  ENDIF()
ELSE()
  MESSAGE(STATUS "Reusing existing vcpkg executable at ${_vcpkg_exe}")
ENDIF()

##############################################################################
# 2.5 Restrict which configuration(s) vcpkg builds, keyed off CMAKE_BUILD_TYPE.
#
#     By default vcpkg builds BOTH debug and release for every port, which
#     roughly doubles build time. vcpkg has no command-line flag for this; the
#     supported knob is VCPKG_BUILD_TYPE (release|debug) set inside a triplet.
#     So when a single-config CMAKE_BUILD_TYPE is requested we generate an
#     overlay triplet that include()s the stock triplet and appends the setting:
#
#       Debug                          -> debug   (debug only)
#       Release/RelWithDebInfo/MinSizeRel -> release (release only)
#       <empty> / multi-config         -> build both (no override)
#
#     The overlay dir is also exported as VCPKG_OVERLAY_TRIPLETS so the later
#     toolchain-driven install at project() uses the same restricted triplet.
##############################################################################
STRING(TOLOWER "${CMAKE_BUILD_TYPE}" _bt_lower)
SET(_vcpkg_build_type "")
IF(_bt_lower STREQUAL "debug")
  SET(_vcpkg_build_type "debug")
ELSEIF(_bt_lower STREQUAL "release" OR _bt_lower STREQUAL "relwithdebinfo"
       OR _bt_lower STREQUAL "minsizerel")
  # vcpkg has no RelWithDebInfo/MinSizeRel notion; its "release" config is the
  # right (and only) non-debug closure for all of these.
  SET(_vcpkg_build_type "release")
ENDIF()

SET(_vcpkg_overlay_arg "")
IF(_vcpkg_build_type)
  # The stock triplet lives either directly under triplets/ or in
  # triplets/community/. We must include() it so the overlay inherits
  # VCPKG_TARGET_ARCHITECTURE, VCPKG_CRT_LINKAGE, etc.
  SET(_base_triplet "")
  FOREACH(_dir "${_vcpkg_root}/triplets" "${_vcpkg_root}/triplets/community")
    IF(EXISTS "${_dir}/${VCPKG_TARGET_TRIPLET}.cmake")
      SET(_base_triplet "${_dir}/${VCPKG_TARGET_TRIPLET}.cmake")
      BREAK()
    ENDIF()
  ENDFOREACH()

  IF(_base_triplet)
    SET(_overlay_dir "${CMAKE_BINARY_DIR}/vcpkg-overlay-triplets")
    FILE(MAKE_DIRECTORY "${_overlay_dir}")
    FILE(WRITE "${_overlay_dir}/${VCPKG_TARGET_TRIPLET}.cmake"
      "# Generated by bootstrap_vcpkg.cmake from CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'.\n"
      "include(\"${_base_triplet}\")\n"
      "set(VCPKG_BUILD_TYPE ${_vcpkg_build_type})\n")
    SET(_vcpkg_overlay_arg "--overlay-triplets=${_overlay_dir}")
    # Propagate to the toolchain-driven install at project() so it agrees.
    SET(VCPKG_OVERLAY_TRIPLETS "${_overlay_dir}"
      CACHE PATH "vcpkg overlay triplets (restricts VCPKG_BUILD_TYPE)" FORCE)
    MESSAGE(STATUS "vcpkg: building '${_vcpkg_build_type}' only "
                   "(CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}')")
  ELSE()
    MESSAGE(WARNING "vcpkg: triplet '${VCPKG_TARGET_TRIPLET}' not found under "
                    "${_vcpkg_root}/triplets[/community]; cannot restrict to "
                    "'${_vcpkg_build_type}', building both debug and release.")
  ENDIF()
ELSE()
  MESSAGE(STATUS "vcpkg: CMAKE_BUILD_TYPE unset/multi-config -- building both "
                 "debug and release.")
ENDIF()

##############################################################################
# 3. Install the manifest dependencies NOW, before any consumer that runs at
#    configure time (notably the MariaDB server bootstrap) needs them.
#
#    In manifest mode vcpkg installs the vcpkg.json closure as a side effect of
#    the first project() call reading the toolchain file -- which happens AFTER
#    the MariaDB bootstrap is included. Doing the install explicitly here closes
#    that gap: the deps (OpenSSL, zlib, ...) are on disk before the server is
#    configured. We install into the SAME location the toolchain uses by default
#    (<build>/vcpkg_installed), so the later toolchain-driven install at
#    project() finds everything up to date and is a near no-op.
##############################################################################
SET(_vcpkg_installed_root "${CMAKE_BINARY_DIR}/vcpkg_installed")
MESSAGE(STATUS "Installing vcpkg manifest dependencies (triplet '${VCPKG_TARGET_TRIPLET}')...")
EXECUTE_PROCESS(
  COMMAND "${_vcpkg_exe}" install
          --triplet "${VCPKG_TARGET_TRIPLET}"
          --x-manifest-root "${CMAKE_CURRENT_SOURCE_DIR}"
          --x-install-root "${_vcpkg_installed_root}"
          ${_vcpkg_overlay_arg}
  RESULT_VARIABLE _rc)
IF(NOT _rc EQUAL 0)
  MESSAGE(FATAL_ERROR "vcpkg manifest install failed (see output above)")
ENDIF()

##############################################################################
# 4. Export VCPKG_ROOT + the derived toolchain/triplet for the rest of the
#    build. Cached (FORCE) so they persist across reconfigures and so the
#    first project() call below sees CMAKE_TOOLCHAIN_FILE.
##############################################################################
SET(VCPKG_ROOT "${_vcpkg_root}"
  CACHE PATH "Root of the vcpkg checkout used for this build" FORCE)

SET(CMAKE_TOOLCHAIN_FILE "${_vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
  CACHE FILEPATH "Vcpkg toolchain file (derived from WITH_VCPKG_TRIPLET)" FORCE)

IF(NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
  MESSAGE(FATAL_ERROR "vcpkg toolchain file missing: ${CMAKE_TOOLCHAIN_FILE}")
ENDIF()

# The per-triplet install prefix (headers under include/, libs under lib/,
# tools under tools/). Consumers that run at configure time before project()
# -- e.g. the MariaDB server bootstrap -- point their own dependency search at
# this so they reuse the closure just installed instead of the system.
SET(VCPKG_INSTALLED_DIR "${_vcpkg_installed_root}/${VCPKG_TARGET_TRIPLET}"
  CACHE PATH "Per-triplet vcpkg install prefix for this build" FORCE)

MESSAGE(STATUS "vcpkg bootstrap complete:")
MESSAGE(STATUS "  VCPKG_ROOT           = ${VCPKG_ROOT}")
MESSAGE(STATUS "  CMAKE_TOOLCHAIN_FILE = ${CMAKE_TOOLCHAIN_FILE}")
MESSAGE(STATUS "  VCPKG_TARGET_TRIPLET = ${VCPKG_TARGET_TRIPLET}")
MESSAGE(STATUS "  VCPKG_INSTALLED_DIR  = ${VCPKG_INSTALLED_DIR}")
