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

# Turns a file into a C++ source that holds its bytes, so it can be compiled
# into a binary. Run as a script:
#
#   cmake -DINPUT=<file> -DOUTPUT=<file.cc> -DSYMBOL=<name> -P embed_file.cmake
#
# The generated source defines, in namespace mrs::embedded:
#
#   extern const unsigned char <SYMBOL>[];
#   extern const unsigned long <SYMBOL>_size;
#
# A byte array is used instead of a string literal because MSVC limits the
# size of string literals.

if(NOT INPUT OR NOT OUTPUT OR NOT SYMBOL)
  message(FATAL_ERROR "embed_file.cmake needs INPUT, OUTPUT and SYMBOL")
endif()

file(READ "${INPUT}" _content HEX)
string(LENGTH "${_content}" _hex_length)
math(EXPR _size "${_hex_length} / 2")

# "0x" before every byte, a comma after it, a line break every 16 bytes.
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_content}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){16})" "\\1\n" _bytes "${_bytes}")

get_filename_component(_input_name "${INPUT}" NAME)

file(WRITE "${OUTPUT}" "// Generated from ${_input_name} by embed_file.cmake. Do not edit.

namespace mrs {
namespace embedded {

extern const unsigned char ${SYMBOL}[];
extern const unsigned long ${SYMBOL}_size;

const unsigned char ${SYMBOL}[] = {
${_bytes}
0x00};

const unsigned long ${SYMBOL}_size = ${_size};

}  // namespace embedded
}  // namespace mrs
")
