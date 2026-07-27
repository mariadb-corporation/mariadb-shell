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
# bootstrap_python.cmake
#
# Configure-time convenience: when the caller passes -DWITH_PYTHON_SOURCE, build
# a Python from source, install it into a cached "Python-<version>" prefix, and
# derive
#
#   BUNDLED_PYTHON_DIR = <install-root>/Python-<version>
#
# so the rest of the build bundles that interpreter (see the BUNDLED_PYTHON_DIR
# handling in the top-level CMakeLists.txt and src/CMakeLists.txt). The caller no
# longer has to configure/build/install Python by hand and then pass the long
# -DBUNDLED_PYTHON_DIR path.
#
# WITH_PYTHON_SOURCE accepts either:
#   * a path to an existing CPython source tree, or
#   * a version -- "X.Y.Z" fetches the tag v<X.Y.Z>, "X.Y" fetches the maintenance
#     branch (latest patch of that series) -- from github.com/python/cpython.
#
# The install prefix (and, for the version form, the cloned source) are treated
# as a CACHE: if the interpreter is already built it is reused and the (expensive)
# clone/configure/build/install is skipped. Delete it to force a rebuild.
#
# This MUST be included AFTER bootstrap_vcpkg.cmake so the vcpkg dependency
# closure is already on disk: Python is then configured with CPPFLAGS/LDFLAGS
# (and --with-openssl) pointing at the vcpkg install prefix, so the interpreter
# and its extension modules (_ssl, zlib, ...) link the same libraries the shell
# does rather than the system ones.
#
# Explicitly passing -DBUNDLED_PYTHON_DIR keeps working and takes precedence:
# WITH_PYTHON_SOURCE is a shortcut, not a requirement. Unix/macOS only (CPython's
# autotools flow); on Windows use a python.org install or a pre-built
# -DBUNDLED_PYTHON_DIR.
#
##############################################################################

# Nothing to do unless the shortcut was requested.
IF(NOT WITH_PYTHON_SOURCE)
  RETURN()
ENDIF()

# An explicit BUNDLED_PYTHON_DIR wins: honour it and do not build a second copy.
# (On a reconfigure this is also our own cached value from a previous run, so we
# correctly reuse the already-built interpreter without doing anything.)
IF(BUNDLED_PYTHON_DIR)
  MESSAGE(STATUS "WITH_PYTHON_SOURCE: BUNDLED_PYTHON_DIR already set "
                 "(${BUNDLED_PYTHON_DIR}); not building Python from source.")
  RETURN()
ENDIF()

# Building CPython from source uses the autotools flow (configure + make), which
# is not how Python is produced on Windows (PCbuild\build.bat). On Windows the
# top-level CMakeLists.txt already locates and bundles a python.org install, so
# this shortcut is Unix/macOS only. WIN32 is not set this early (before
# project()), so key off CMAKE_HOST_WIN32.
IF(CMAKE_HOST_WIN32)
  MESSAGE(FATAL_ERROR
    "WITH_PYTHON_SOURCE (build Python from source) is only supported on "
    "Unix/macOS. On Windows use a python.org install (auto-detected) or pass "
    "-DBUNDLED_PYTHON_DIR to an already-built Python.")
ENDIF()

# A small helper reused for the "is this interpreter already built?" probe: a
# "make install" prefix has the interpreter at bin/python<maj.min> and headers
# under include/python<maj.min>.
MACRO(_PY_INSTALL_IS_BUILT dir mm out_var)
  IF(EXISTS "${dir}/bin/python${mm}"
     AND EXISTS "${dir}/include/python${mm}/Python.h")
    SET(${out_var} TRUE)
  ELSE()
    SET(${out_var} FALSE)
  ENDIF()
ENDMACRO()

##############################################################################
# 1. Where Python is installed (and, for the version form, where the CPython
#    source is cloned). Kept OUTSIDE the build dir by default (next to the shell
#    source, like the vcpkg/MariaDB bootstraps) so a clean shell rebuild does not
#    wipe the cache and force a full Python rebuild. Override -DPYTHON_INSTALL_ROOT.
##############################################################################
IF(NOT PYTHON_INSTALL_ROOT)
  SET(PYTHON_INSTALL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.."
    CACHE PATH "Directory that holds the auto-built Python-<version> install")
ENDIF()
GET_FILENAME_COMPONENT(PYTHON_INSTALL_ROOT "${PYTHON_INSTALL_ROOT}" ABSOLUTE)

##############################################################################
# 2. Resolve WITH_PYTHON_SOURCE to a CPython source tree (_py_src), fetching it
#    from GitHub when a version was requested.
##############################################################################
SET(PYTHON_GIT_REPOSITORY "https://github.com/python/cpython.git"
  CACHE STRING "Git URL of CPython to fetch when WITH_PYTHON_SOURCE is a version")

IF(WITH_PYTHON_SOURCE MATCHES "^[0-9]+\\.[0-9]+(\\.[0-9]+)?$")
  # ---------------------------------------------------------------- version form
  SET(_py_req "${WITH_PYTHON_SOURCE}")

  IF(WITH_PYTHON_SOURCE MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    # Full X.Y.Z: an exact release tag, and the install dir name is known up
    # front -- honour the build cache before we even clone.
    SET(_py_ref "v${_py_req}")
    STRING(REGEX REPLACE "^([0-9]+)\\.([0-9]+).*" "\\1.\\2" _mm "${_py_req}")
    _PY_INSTALL_IS_BUILT("${PYTHON_INSTALL_ROOT}/Python-${_py_req}" "${_mm}" _built)
    IF(_built)
      MESSAGE(STATUS "Python ${_py_req} already built at "
        "${PYTHON_INSTALL_ROOT}/Python-${_py_req}; reusing it.")
      SET(BUNDLED_PYTHON_DIR "${PYTHON_INSTALL_ROOT}/Python-${_py_req}"
        CACHE PATH "Bundled Python install (built from WITH_PYTHON_SOURCE)" FORCE)
      RETURN()
    ENDIF()
  ELSE()
    # Major.minor only: the CPython maintenance branch (latest patch level). We
    # cannot know the exact patch without fetching, so reuse an existing
    # Python-X.Y.* install if one is present rather than re-fetching.
    SET(_py_ref "${_py_req}")
    FILE(GLOB _existing "${PYTHON_INSTALL_ROOT}/Python-${_py_req}.*")
    LIST(SORT _existing)
    IF(_existing)
      LIST(REVERSE _existing)  # newest patch level first (lexical is fine here)
      FOREACH(_cand ${_existing})
        _PY_INSTALL_IS_BUILT("${_cand}" "${_py_req}" _built)
        IF(_built)
          MESSAGE(STATUS "Python ${_py_req}.x already built at ${_cand}; reusing "
            "it. Delete it to fetch/build a newer patch level.")
          SET(BUNDLED_PYTHON_DIR "${_cand}"
            CACHE PATH "Bundled Python install (built from WITH_PYTHON_SOURCE)" FORCE)
          RETURN()
        ENDIF()
      ENDFOREACH()
    ENDIF()
  ENDIF()

  # Not built yet: fetch the source into a cached checkout and build below.
  SET(_py_src "${PYTHON_INSTALL_ROOT}/cpython-${_py_req}")
  IF(EXISTS "${_py_src}/.git" AND EXISTS "${_py_src}/configure")
    MESSAGE(STATUS "Reusing existing CPython checkout at ${_py_src}")
  ELSE()
    FIND_PACKAGE(Git REQUIRED)
    MESSAGE(STATUS "Fetching CPython '${_py_ref}' from ${PYTHON_GIT_REPOSITORY}...")
    FILE(REMOVE_RECURSE "${_py_src}")
    FILE(MAKE_DIRECTORY "${PYTHON_INSTALL_ROOT}")
    # --depth 1 + --branch grabs only the tip of the tag/branch. CPython commits
    # a generated 'configure' to the repo, so no autoconf step is needed.
    EXECUTE_PROCESS(
      COMMAND "${GIT_EXECUTABLE}" clone --progress --depth 1 --no-tags
              --branch "${_py_ref}"
              "${PYTHON_GIT_REPOSITORY}" "${_py_src}"
      RESULT_VARIABLE _rc)
    IF(NOT _rc EQUAL 0)
      MESSAGE(FATAL_ERROR
        "Failed to fetch CPython '${_py_ref}'. Check that the version exists "
        "(a full X.Y.Z maps to the tag v<X.Y.Z>; X.Y maps to the maintenance "
        "branch), or pass a path to a CPython source tree instead.")
    ENDIF()
  ENDIF()
ELSE()
  # ------------------------------------------------------------------- path form
  GET_FILENAME_COMPONENT(_py_src "${WITH_PYTHON_SOURCE}" ABSOLUTE)
  IF(NOT EXISTS "${_py_src}/configure")
    MESSAGE(FATAL_ERROR
      "WITH_PYTHON_SOURCE='${WITH_PYTHON_SOURCE}' is neither a version "
      "(X.Y or X.Y.Z) nor a CPython source tree (no 'configure' script under "
      "'${_py_src}').")
  ENDIF()
ENDIF()

##############################################################################
# 3. Determine the Python version from the (now-resolved) source, so the install
#    prefix can be named "Python-<version>" and the major.minor derived.
##############################################################################
SET(_patchlevel "${_py_src}/Include/patchlevel.h")
IF(NOT EXISTS "${_patchlevel}")
  FILE(GLOB_RECURSE _patchlevel "${_py_src}/patchlevel.h")
  LIST(GET _patchlevel 0 _patchlevel)
ENDIF()
IF(NOT _patchlevel OR NOT EXISTS "${_patchlevel}")
  MESSAGE(FATAL_ERROR "Could not find patchlevel.h under ${_py_src}")
ENDIF()

FILE(STRINGS "${_patchlevel}" _py_version_line
     REGEX "^#define[ \t]+PY_VERSION[ \t]+\"[^\"]+\"")
STRING(REGEX REPLACE "^#define[ \t]+PY_VERSION[ \t]+\"([^\"]+)\".*" "\\1"
                     _py_version "${_py_version_line}")
IF(NOT _py_version)
  MESSAGE(FATAL_ERROR "Could not parse PY_VERSION from ${_patchlevel}")
ENDIF()
STRING(REGEX REPLACE "^([0-9]+)\\.([0-9]+).*" "\\1.\\2" _py_mm "${_py_version}")

SET(_py_install "${PYTHON_INSTALL_ROOT}/Python-${_py_version}")

# Guard against the pathological case where the install prefix would collide
# with the source tree itself.
IF(_py_install STREQUAL _py_src)
  MESSAGE(FATAL_ERROR
    "The Python install prefix (${_py_install}) collides with the source tree. "
    "Set -DPYTHON_INSTALL_ROOT to a different directory.")
ENDIF()

##############################################################################
# 4. Fast path: already built (covers the path form, and the version forms that
#    could not be resolved to an exact dir above).
##############################################################################
_PY_INSTALL_IS_BUILT("${_py_install}" "${_py_mm}" _built)
IF(_built)
  MESSAGE(STATUS "Python ${_py_version} already built at ${_py_install}; reusing "
    "it. Delete that directory to force a rebuild.")
  SET(BUNDLED_PYTHON_DIR "${_py_install}"
    CACHE PATH "Bundled Python install (built from WITH_PYTHON_SOURCE)" FORCE)
  RETURN()
ENDIF()

MESSAGE(STATUS "==========================================================")
MESSAGE(STATUS "WITH_PYTHON_SOURCE='${WITH_PYTHON_SOURCE}' -- building Python ${_py_version}.")
MESSAGE(STATUS "  source  : ${_py_src}")
MESSAGE(STATUS "  install : ${_py_install}")
MESSAGE(STATUS "  (override the location with -DPYTHON_INSTALL_ROOT; runs once)")
MESSAGE(STATUS "==========================================================")

##############################################################################
# 5. Point the Python build at the vcpkg dependency closure that bootstrap_vcpkg
#    just installed, so _ssl / zlib / ... link the same libraries as the shell.
#    VCPKG_INSTALLED_PREFIX is the per-triplet prefix exported by
#    bootstrap_vcpkg.cmake (headers directly under include/); when the caller
#    supplied the vcpkg toolchain directly (not via WITH_VCPKG_TRIPLET) it is not
#    set, so derive the conventional per-triplet prefix instead.
##############################################################################
SET(_vcpkg_prefix "")
IF(VCPKG_INSTALLED_PREFIX)
  SET(_vcpkg_prefix "${VCPKG_INSTALLED_PREFIX}")
ELSEIF(VCPKG_TARGET_TRIPLET AND CMAKE_TOOLCHAIN_FILE MATCHES "[Vv]cpkg")
  SET(_vcpkg_prefix "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")
ENDIF()

# --enable-shared links the interpreter against libpython<ver>.{so,dylib} in
# <prefix>/lib. That dir is not on the system loader path, and some distros ship
# their OWN system libpython of the same version in the default path (e.g.
# Ubuntu: /usr/lib/<arch>/libpython3.14.so.1.0). Without an explicit RUNPATH the
# loader then resolves the SYSTEM libpython instead of ours -> a Frankenstein
# interpreter (our executable + our stdlib on sys.path, but the system libpython
# driving site/sysconfig) whose package machinery is broken: ensurepip installs
# pip into <prefix>/lib/pythonX.Y/site-packages while the system libpython's
# (Debian-patched) site.py searches dist-packages, so "import pip" fails with
# "No module named pip". DT_RUNPATH is searched before the ldconfig cache, so
# baking our own lib dir in makes our libpython win. Always needed, even without
# vcpkg; keep it first so it also takes precedence for anything else we ship.
SET(_rpath_dirs "${_py_install}/lib")

SET(_cppflags "")
SET(_ldflags "")
SET(_configure_extra "")
IF(_vcpkg_prefix AND EXISTS "${_vcpkg_prefix}")
  SET(_cppflags "-I${_vcpkg_prefix}/include")
  # -L is link-time search only. vcpkg's dylibs carry install-name
  # @rpath/lib<x>.dylib (dynamic triplets), so the extension modules also need a
  # runtime search path baked in via -Wl,-rpath, or dyld cannot resolve them at
  # import time and CPython's build silently removes _ssl/_hashlib/_zstd/zlib
  # ("built successfully but removed because they could not be imported").
  SET(_ldflags  "-L${_vcpkg_prefix}/lib")
  LIST(APPEND _rpath_dirs "${_vcpkg_prefix}/lib")
  # Let the _ssl/_hashlib modules find OpenSSL in the vcpkg tree explicitly, and
  # bake the vcpkg lib dir into their rpath too (belt-and-suspenders with the
  # LDFLAGS rpath below; --with-openssl-rpath only covers the OpenSSL modules).
  SET(_configure_extra "--with-openssl=${_vcpkg_prefix}"
                       "--with-openssl-rpath=${_vcpkg_prefix}/lib")
  MESSAGE(STATUS "  vcpkg prefix for Python build: ${_vcpkg_prefix}")
ELSE()
  MESSAGE(STATUS "  no vcpkg prefix found; Python will build against system libs")
ENDIF()

# Bake the runtime search path in. macOS's linker accepts only ONE directory per
# -Wl,-rpath: a ':'-joined list (the GNU-ld / DT_RUNPATH convention, valid on
# Linux) collapses into a single bogus LC_RPATH on macOS, so dyld never finds the
# vcpkg dylibs and CPython's build-time import check silently drops every module
# that links them (zlib, binascii, _zstd, _sqlite3, ...). Emit one -Wl,-rpath
# flag per directory instead -- correct on macOS, and GNU ld accumulates them too.
# Our own lib dir comes first, then vcpkg.
FOREACH(_d ${_rpath_dirs})
  SET(_ldflags "${_ldflags} -Wl,-rpath,${_d}")
ENDFOREACH()

##############################################################################
# 6. Translate CMAKE_BUILD_TYPE into Python build flags. The ABI flags (and thus
#    the library/interpreter names the bundling logic probes for) are left at the
#    defaults -- we only steer optimization and debug info via CFLAGS so a Debug
#    shell gets a matching, debuggable Python without renaming the artifacts
#    (i.e. no --with-pydebug 'd' ABI suffix).
##############################################################################
STRING(TOUPPER "${CMAKE_BUILD_TYPE}" _bt_upper)
SET(_py_cflags "")
IF(_bt_upper STREQUAL "DEBUG")
  SET(_py_cflags "-O0 -g")
ELSEIF(_bt_upper STREQUAL "RELWITHDEBINFO")
  SET(_py_cflags "-O2 -g")
ELSEIF(_bt_upper STREQUAL "MINSIZEREL")
  SET(_py_cflags "-Os")
ELSEIF(_bt_upper STREQUAL "RELEASE")
  SET(_py_cflags "-O2")
ENDIF()
IF(_py_cflags)
  MESSAGE(STATUS "  Python CFLAGS (CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'): ${_py_cflags}")
ENDIF()

##############################################################################
# 7. Configure. Run in the source tree (in-tree build); this is a one-time,
#    cached operation. CPPFLAGS/LDFLAGS are passed through the environment via
#    "cmake -E env" so the compiler/linker see the vcpkg include and lib dirs;
#    CFLAGS is passed on the configure command line (configure appends it to its
#    own optimization flags). --enable-shared produces libpython<ver>.{so,dylib},
#    which is the layout the bundling logic (BUNDLED_SHARED_PYTHON) expects.
##############################################################################
MESSAGE(STATUS "Configuring Python ${_py_version}...")
EXECUTE_PROCESS(
  COMMAND "${CMAKE_COMMAND}" -E env
          "CPPFLAGS=${_cppflags}"
          "LDFLAGS=${_ldflags}"
          "${_py_src}/configure"
          "--prefix=${_py_install}"
          --enable-shared
          "CFLAGS=${_py_cflags}"
          ${_configure_extra}
  WORKING_DIRECTORY "${_py_src}"
  RESULT_VARIABLE _rc)
IF(NOT _rc EQUAL 0)
  MESSAGE(FATAL_ERROR "Python configure failed (see output above)")
ENDIF()

##############################################################################
# 8. Build and install.
##############################################################################
INCLUDE(ProcessorCount)
PROCESSORCOUNT(_nproc)
IF(_nproc EQUAL 0)
  SET(_nproc 1)
ENDIF()

MESSAGE(STATUS "Building Python ${_py_version} (make -j${_nproc})...")
EXECUTE_PROCESS(
  COMMAND make -j${_nproc}
  WORKING_DIRECTORY "${_py_src}"
  RESULT_VARIABLE _rc)
IF(NOT _rc EQUAL 0)
  MESSAGE(FATAL_ERROR "Python build (make) failed (see output above)")
ENDIF()

MESSAGE(STATUS "Installing Python ${_py_version} into ${_py_install}...")
EXECUTE_PROCESS(
  COMMAND make install
  WORKING_DIRECTORY "${_py_src}"
  RESULT_VARIABLE _rc)
IF(NOT _rc EQUAL 0)
  MESSAGE(FATAL_ERROR "Python install (make install) failed (see output above)")
ENDIF()

IF(NOT EXISTS "${_py_install}/bin/python${_py_mm}")
  MESSAGE(FATAL_ERROR
    "Python install did not produce ${_py_install}/bin/python${_py_mm}")
ENDIF()

##############################################################################
# 9. Export the derived BUNDLED_PYTHON_DIR for the rest of the build.
##############################################################################
SET(BUNDLED_PYTHON_DIR "${_py_install}"
  CACHE PATH "Bundled Python install (built from WITH_PYTHON_SOURCE)" FORCE)

MESSAGE(STATUS "Python bootstrap complete:")
MESSAGE(STATUS "  BUNDLED_PYTHON_DIR = ${BUNDLED_PYTHON_DIR}")
