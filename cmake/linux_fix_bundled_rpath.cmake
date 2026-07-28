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

# Run as: cmake -Dpatchelf=<patchelf> [-Dpatchelf_page_size=<size>]
#              [-Dpattern=<glob> | -Dbinaries=<file;file;...>]
#              [-Dorigin_relative=<relative path to INSTALL_LIBDIR>]
#              -P linux_fix_bundled_rpath.cmake
#
# Rewrites the RPATH of already-copied bundled ELF binaries to
# '$ORIGIN[/<origin_relative>]' so they resolve their dependencies inside
# INSTALL_LIBDIR, and does so with patchelf's --force-rpath, i.e. writing the
# legacy DT_RPATH tag rather than DT_RUNPATH.
#
# Two reasons this replaces (rather than augments) whatever the binary was
# linked with:
#
# * The build-time search path is absolute and points into the build tree (for
#   the bundled Python: the vcpkg per-triplet lib dir baked in by
#   bootstrap_python.cmake's LDFLAGS). It resolves nothing on the target, and
#   nothing in a build tree that has been cleaned or moved.
# * DT_RUNPATH is searched *after* LD_LIBRARY_PATH, DT_RPATH *before* it. Any
#   LD_LIBRARY_PATH naming a system library dir therefore beats a RUNPATH-only
#   binary and substitutes the system library for the bundled one - which fails
#   outright when the bundled copy is newer, e.g. the vcpkg OpenSSL 3.x the
#   bundled Python's _ssl links against vs. a distro libcrypto.so.3 lacking its
#   symbol versions ("version `OPENSSL_3.3.0' not found"). This mirrors
#   add_shell_executable()'s -Wl,--disable-new-dtags for the shell's own
#   executables.
#
# The relative form keeps the copies valid in the build tree and in the
# installed/packaged tree alike, since both are rooted at INSTALL_LIBDIR.

if(NOT patchelf)
  message(FATAL_ERROR "linux_fix_bundled_rpath: 'patchelf' is required")
endif()

if(pattern)
  file(GLOB _targets "${pattern}")
else()
  set(_targets "${binaries}")
endif()

if(NOT _targets)
  # Not an error: a bundled directory may legitimately contain no top-level
  # shared object (e.g. pure-Python site-packages).
  message(STATUS "linux_fix_bundled_rpath: nothing to do for "
                 "'${pattern}${binaries}'")
  return()
endif()

# '$ORIGIN' is a literal here: CMake only expands '${...}', and execute_process
# runs patchelf without a shell, so the value reaches it unmangled - no escaping
# dance like the one add_custom_command needs.
set(_rpath "$ORIGIN")
if(origin_relative)
  set(_rpath "$ORIGIN/${origin_relative}")
endif()

# Re-assemble the flag the caller passed as a scalar (see get_force_rpath_command).
set(_patchelf_args "")
if(patchelf_page_size)
  set(_patchelf_args --page-size "${patchelf_page_size}")
endif()

foreach(_target ${_targets})
  # Skip symlinks - patchelf would rewrite (and materialize) the target.
  if(IS_SYMLINK "${_target}")
    continue()
  endif()

  message(STATUS "Setting RPATH of ${_target} to '${_rpath}'")
  execute_process(
    COMMAND "${patchelf}" ${_patchelf_args} --force-rpath --set-rpath "${_rpath}"
            "${_target}"
    RESULT_VARIABLE _rc
    ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "patchelf failed on ${_target}: ${_err}")
  endif()
endforeach()
