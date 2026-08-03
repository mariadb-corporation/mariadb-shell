# Copyright (c) 2026, Oracle and/or its affiliates.
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

function(mysql_format_list_for_error in_list out_var)
  set(result "${${in_list}}")
  string(REPLACE ";" ", " result "${result}")
  set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

function(mysql_resolve_versioned_dependency
    dependency_name parent_dir prefix out_path out_version)
  file(GLOB candidates
      LIST_DIRECTORIES true
      RELATIVE "${parent_dir}"
      "${parent_dir}/${prefix}*")

  set(directories)
  foreach(candidate IN LISTS candidates)
    if(IS_DIRECTORY "${parent_dir}/${candidate}")
      list(APPEND directories "${candidate}")
    endif()
  endforeach()

  list(SORT directories)
  list(LENGTH directories directory_count)

  if(directory_count EQUAL 0)
    message(FATAL_ERROR
        "Could not find bundled ${dependency_name} directory matching: "
        "${parent_dir}/${prefix}*")
  elseif(directory_count GREATER 1)
    mysql_format_list_for_error(directories formatted_directories)
    message(FATAL_ERROR
        "Found multiple bundled ${dependency_name} directories matching "
        "${parent_dir}/${prefix}*: ${formatted_directories}")
  endif()

  list(GET directories 0 directory)
  string(LENGTH "${prefix}" prefix_length)
  string(SUBSTRING "${directory}" ${prefix_length} -1 version)

  if(version STREQUAL "")
    message(FATAL_ERROR
        "Bundled ${dependency_name} directory does not include a version "
        "suffix: ${parent_dir}/${directory}")
  endif()

  set(resolved_path "${parent_dir}/${directory}")
  message(STATUS "Using bundled ${dependency_name} ${version}: ${resolved_path}")

  set(${out_path} "${resolved_path}" PARENT_SCOPE)
  set(${out_version} "${version}" PARENT_SCOPE)
endfunction()

function(mysql_resolve_single_file dependency_name glob_pattern out_path)
  file(GLOB candidates LIST_DIRECTORIES false "${glob_pattern}")

  set(files)
  foreach(candidate IN LISTS candidates)
    if(NOT IS_DIRECTORY "${candidate}")
      list(APPEND files "${candidate}")
    endif()
  endforeach()

  list(SORT files)
  list(LENGTH files file_count)

  if(file_count EQUAL 0)
    message(FATAL_ERROR
        "Could not find bundled ${dependency_name} file matching: "
        "${glob_pattern}")
  elseif(file_count GREATER 1)
    mysql_format_list_for_error(files formatted_files)
    message(FATAL_ERROR
        "Found multiple bundled ${dependency_name} files matching "
        "${glob_pattern}: ${formatted_files}")
  endif()

  list(GET files 0 file)
  set(${out_path} "${file}" PARENT_SCOPE)
endfunction()
