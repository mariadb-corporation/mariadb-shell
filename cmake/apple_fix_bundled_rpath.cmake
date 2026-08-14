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
#
##############################################################################
#
# apple_fix_bundled_rpath.cmake  (macOS, run via cmake -P at install time)
#
# Makes bundled Mach-O binaries relocatable by scrubbing absolute LC_RPATH
# entries that point into the *build* tree (e.g. the vcpkg per-triplet lib dir
# baked in by bootstrap_python.cmake's LDFLAGS) and, optionally, adding a
# relative @loader_path rpath so the binary finds its bundled dependencies
# within INSTALL_LIBDIR on any machine.
#
# Why this is needed: install_name_tool -change (used by
# apple_use_bundled_openssl.cmake) only rewrites LC_LOAD_DYLIB *references*; it
# never removes LC_RPATH entries. A dynamic vcpkg triplet gives its dylibs the
# id "@rpath/lib<x>.dylib", so the Python interpreter / libpython / extension
# modules are linked with -Wl,-rpath,<abs vcpkg build dir> to resolve them at
# build time -- and that absolute path would otherwise ship in the package,
# pointing at a directory that does not exist on the target.
#
# Parameters (via -D):
#   strip_prefix : delete every LC_RPATH whose path starts with this string
#   add_rpath    : (optional) rpath to add if not already present
#   binaries     : (optional) ';'- or space-separated list of Mach-O files
#   pattern      : (optional) glob expanded with GLOB_RECURSE into a file list
# Provide exactly one of 'binaries' / 'pattern'.
#
##############################################################################

IF(DEFINED pattern)
  FILE(GLOB_RECURSE _bins "${pattern}")
ELSE()
  STRING(REPLACE " " ";" _bins "${binaries}")
ENDIF()

FOREACH(_bin ${_bins})
  IF(NOT EXISTS "${_bin}")
    CONTINUE()
  ENDIF()

  # Collect the binary's current LC_RPATH entries. `otool -l` prints, per
  # LC_RPATH load command, a line "         path <value> (offset N)".
  EXECUTE_PROCESS(COMMAND otool -l "${_bin}" OUTPUT_VARIABLE _otool)
  STRING(REGEX MATCHALL "path [^\n]+ \\(offset [0-9]+\\)" _rpath_lines "${_otool}")

  FOREACH(_line ${_rpath_lines})
    STRING(REGEX REPLACE "^path (.+) \\(offset [0-9]+\\)$" "\\1" _rp "${_line}")
    STRING(FIND "${_rp}" "${strip_prefix}" _pos)
    IF(_pos EQUAL 0)
      MESSAGE(STATUS "  ${_bin}: deleting build-tree rpath ${_rp}")
      # Tolerate a concurrent/duplicate delete: a non-zero result just means the
      # entry was already gone, which is fine.
      EXECUTE_PROCESS(COMMAND install_name_tool -delete_rpath "${_rp}" "${_bin}"
                      RESULT_VARIABLE _rc ERROR_QUIET)
    ENDIF()
  ENDFOREACH()

  IF(add_rpath)
    # Only add if not already present, so re-running install is idempotent.
    STRING(FIND "${_otool}" "path ${add_rpath} (offset" _has)
    IF(_has EQUAL -1)
      MESSAGE(STATUS "  ${_bin}: adding relative rpath ${add_rpath}")
      EXECUTE_PROCESS(COMMAND install_name_tool -add_rpath "${add_rpath}" "${_bin}"
                      RESULT_VARIABLE _rc ERROR_QUIET)
    ENDIF()
  ENDIF()
ENDFOREACH()
