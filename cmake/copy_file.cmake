# Copyright (c) 2024, Oracle and/or its affiliates.
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
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

# A workaround to 'cmake -E copy', which follows symlinks: for the versioned
# shared libraries we bundle, the link itself is what has to be copied. vcpkg
# ships libssl.dylib -> libssl.3.dylib (and libz.so -> libz.so.1, ...), the
# SONAME chain the loader walks, so flattening those into duplicate files would
# lose the structure they encode.
#
# But that only holds while the target sits next to the link. A symlink to
# anything else cannot survive being packaged: it keeps pointing at the build
# machine. The Windows Python's python3.exe is such a link -- it points at
# \\?\C:\hostedtoolcache\...\python.exe -- and preserving it shipped a package
# whose bundled interpreter was a dangling link to a directory that exists on no
# user's machine (and with no python.exe beside it to fall back on).
#
# Hence the test: a bare filename as the target means a sibling, which is copied
# alongside and stays valid. Anything with a path in it -- absolute, relative, or
# a Windows extended-length path, all of which reach outside this directory -- is
# resolved and copied as a real file under the link's own name.
if(IS_SYMLINK "${src_file}")
  file(READ_SYMLINK "${src_file}" _link_target)
  if(_link_target MATCHES "[/\\]")
    get_filename_component(_link_name "${src_file}" NAME)
    message(STATUS
      "Dereferencing ${_link_name} -> ${_link_target} (target is outside its directory)")
    # 'cmake -E copy' lets the OS follow the link, whatever form its target is
    # written in. Resolving it here with REALPATH does not work for every form:
    # the x64 runners' python3.exe points at \??\C:\..., an NT object path that
    # REALPATH hands back unresolved, so the link itself got copied and shipped.
    set(_dst_file "${dst_dir}/${_link_name}")
    file(REMOVE "${_dst_file}")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E copy "${src_file}" "${_dst_file}"
      RESULT_VARIABLE _copy_result)
    if(NOT _copy_result EQUAL 0)
      message(FATAL_ERROR
        "Cannot bundle ${src_file}: it is a symlink to '${_link_target}', which "
        "could not be copied (does it exist?). A dangling link would be packaged "
        "as-is and fail on every machine but this one.")
    endif()
    if(IS_SYMLINK "${_dst_file}")
      message(FATAL_ERROR
        "Cannot bundle ${src_file}: copying it produced a symlink rather than a "
        "file, and a link to '${_link_target}' points at the build machine.")
    endif()
    return()
  endif()
endif()

file(COPY "${src_file}" DESTINATION "${dst_dir}")
