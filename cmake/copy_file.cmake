# Copyright (c) 2024, Oracle and/or its affiliates.
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
    get_filename_component(_real_file "${src_file}" REALPATH)
    if(NOT EXISTS "${_real_file}")
      message(FATAL_ERROR
        "Cannot bundle ${src_file}: it is a symlink to '${_link_target}', which "
        "does not exist. A dangling link would be packaged as-is and fail on "
        "every machine but this one.")
    endif()
    message(STATUS
      "Dereferencing ${_link_name} -> ${_link_target} (target is outside its directory)")
    # file(COPY) keeps the source's name and permissions, so copy then rename.
    file(COPY "${_real_file}" DESTINATION "${dst_dir}")
    get_filename_component(_real_name "${_real_file}" NAME)
    if(NOT _real_name STREQUAL _link_name)
      file(RENAME "${dst_dir}/${_real_name}" "${dst_dir}/${_link_name}")
    endif()
    return()
  endif()
endif()

file(COPY "${src_file}" DESTINATION "${dst_dir}")
