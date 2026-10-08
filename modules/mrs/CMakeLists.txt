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

# The MariaDB REST Service (MRS) module.
#
# core/ holds the parts that are meant to be shared with a server plugin: the
# REST SQL grammar (bison), its lexer and AST, and the DDL executor that runs
# the statements against the MRS metadata schema through an abstract session.
# The shell specific parts (the `mrs` global object and the SQL handler) live
# next to this file and are compiled into api_modules.

set(MRS_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${MRS_GENERATED_DIR}")

BISON_TARGET(
    mrs_parser
    "${CMAKE_CURRENT_SOURCE_DIR}/core/mrs_parser.yy"
    "${MRS_GENERATED_DIR}/mrs_parser_gen.cc"
    COMPILE_FLAGS "${BISON_FLAGS_WARNINGS} ${BISON_NO_LINE_OPT}"
    DEFINES_FILE "${MRS_GENERATED_DIR}/mrs_parser_gen.h"
)

set_property(
    SOURCE "${MRS_GENERATED_DIR}/mrs_parser_gen.cc" "${MRS_GENERATED_DIR}/mrs_parser_gen.h"
    PROPERTY GENERATED 1
)

# The generated parser sets a counter (yynerrs_) it never reads.
IF(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set_property(
      SOURCE "${MRS_GENERATED_DIR}/mrs_parser_gen.cc"
      APPEND PROPERTY COMPILE_OPTIONS "-Wno-unused-but-set-variable"
  )
ENDIF()

# The MRS metadata schema script is compiled into the binary, so that
# CONFIGURE REST METADATA needs no files at run time.
set(MRS_METADATA_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/core/metadata/mysql_rest_service_metadata_4.1.6.sql")
set(MRS_METADATA_SCRIPT_CC "${MRS_GENERATED_DIR}/mrs_metadata_schema_script.cc")
add_custom_command(
    OUTPUT "${MRS_METADATA_SCRIPT_CC}"
    COMMAND "${CMAKE_COMMAND}"
        -DINPUT=${MRS_METADATA_SCRIPT}
        -DOUTPUT=${MRS_METADATA_SCRIPT_CC}
        -DSYMBOL=k_metadata_schema_script
        -P "${CMAKE_SOURCE_DIR}/cmake/embed_file.cmake"
    DEPENDS "${MRS_METADATA_SCRIPT}" "${CMAKE_SOURCE_DIR}/cmake/embed_file.cmake"
    COMMENT "Embedding the MRS metadata schema script"
)

set(mrs_core_SOURCES
    core/mrs_db_session.cc
    core/mrs_ddl_executor.cc
    core/mrs_ddl_executor_services.cc
    core/mrs_ddl_executor_schemas.cc
    core/mrs_ddl_executor_db_objects.cc
    core/mrs_ddl_executor_auth.cc
    core/mrs_ddl_executor_content.cc
    core/mrs_json.cc
    core/mrs_lexer.cc
    core/mrs_metadata.cc
    core/mrs_metadata_auth.cc
    core/mrs_metadata_content.cc
    core/mrs_metadata_db_objects.cc
    core/mrs_metadata_schema.cc
    core/mrs_parser.cc
    core/mrs_sql.cc
    "${MRS_GENERATED_DIR}/mrs_parser_gen.cc"
    "${MRS_METADATA_SCRIPT_CC}"
)

add_convenience_library(mrs_core ${mrs_core_SOURCES})
target_include_directories(mrs_core PUBLIC "${MRS_GENERATED_DIR}")
