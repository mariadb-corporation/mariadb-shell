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

# The MSM project of the MRS metadata schema is maintained in db_schema/.
# Its project settings and release scripts are bundled into
# share/mariadb-shell/mrs/, where CONFIGURE REST METADATA deploys from:
# through the msm plugin's deploySchema() when it is loaded, otherwise by
# running the deployment script itself (metadata::Script_deployer).
set(MRS_MSM_PROJECT_NAME "mariadb_rest_service.msm.project")
set(MRS_MSM_PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/db_schema/${MRS_MSM_PROJECT_NAME}")
set(MRS_MSM_PROJECT_DESTINATION "${INSTALL_SHAREDIR}/mrs/${MRS_MSM_PROJECT_NAME}")

add_custom_target(mrs_metadata_project ALL)
install_bundled_files(
    DESCRIPTION "MRS metadata MSM project"
    FILES "${MRS_MSM_PROJECT_DIR}/msm.project.json"
    DESTINATION "${MRS_MSM_PROJECT_DESTINATION}"
    TARGET mrs_metadata_project
)
foreach(release_dir versions deployment updates)
  file(GLOB release_scripts "${MRS_MSM_PROJECT_DIR}/releases/${release_dir}/*.sql")
  # The first release has no update scripts
  if(NOT release_scripts)
    continue()
  endif()
  install_bundled_files(
      DESCRIPTION "MRS metadata ${release_dir} scripts"
      FILES ${release_scripts}
      DESTINATION "${MRS_MSM_PROJECT_DESTINATION}/releases/${release_dir}"
      TARGET mrs_metadata_project
  )
endforeach()

set(mrs_core_SOURCES
    core/mrs_db_session.cc
    core/mrs_ddl_executor.cc
    core/mrs_ddl_executor_services.cc
    core/mrs_ddl_executor_schemas.cc
    core/mrs_ddl_executor_rest_objects.cc
    core/mrs_ddl_executor_auth.cc
    core/mrs_ddl_executor_content.cc
    core/mrs_json.cc
    core/mrs_lexer.cc
    core/mrs_metadata.cc
    core/mrs_metadata_auth.cc
    core/mrs_metadata_content.cc
    core/mrs_metadata_rest_objects.cc
    core/mrs_metadata_json.cc
    core/mrs_metadata_schema.cc
    core/mrs_parser.cc
    core/mrs_schema_deployment.cc
    core/mrs_scripts.cc
    core/mrs_sql.cc
    core/mrs_strings.cc
    "${MRS_GENERATED_DIR}/mrs_parser_gen.cc"
)

add_convenience_library(mrs_core ${mrs_core_SOURCES})

# Except on APPLE, the sources are compiled in the mrs_core_objlib target,
# so that is where their include directories have to go
if(APPLE)
  set(mrs_core_compile_target mrs_core)
else()
  set(mrs_core_compile_target mrs_core_objlib)
endif()

target_include_directories(${mrs_core_compile_target} PRIVATE "${MRS_GENERATED_DIR}")

# The password hashing of REST users uses OpenSSL (the shell's bundled or
# system one; a server plugin has the server's)
if(OPENSSL_INCLUDE_DIR)
  target_include_directories(${mrs_core_compile_target} SYSTEM PRIVATE "${OPENSSL_INCLUDE_DIR}")
endif()
target_link_libraries(mrs_core ${SSL_LIBRARIES})
