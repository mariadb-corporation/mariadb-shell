# Copyright (c) 2014, 2026, Oracle and/or its affiliates.
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
# This program is distributed in the hope that it will be useful,  but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
# the GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

if(WITH_TESTS)
    INCLUDE(${CMAKE_CURRENT_SOURCE_DIR}/gtest.cmake)

    add_definitions(${GMOCK_CFLAGS})
    add_definitions(-DMYSQLX_SOURCE_HOME="${CMAKE_SOURCE_DIR}")
    ADD_DEFINITIONS(-DCURL_VERSION="${CURL_VERSION_MAJOR}.${CURL_VERSION_MINOR}")

    include_directories(SYSTEM "${CMAKE_SOURCE_DIR}/ext/rapidjson/include")

    include_directories(BEFORE
                ${CMAKE_SOURCE_DIR}/unittest
                ${CMAKE_SOURCE_DIR}/ext/linenoise-ng/include
                ${CMAKE_SOURCE_DIR}/mysqlshdk/libs
                ${CMAKE_SOURCE_DIR}/mysqlshdk/include
                ${CMAKE_BINARY_DIR}/mysqlshdk/include
                ${CMAKE_SOURCE_DIR}/
                ${CMAKE_SOURCE_DIR}/src
                ${MYSQL_INCLUDE_DIRS}
                ${GMOCK_INCLUDE_DIRS})

    if(HAVE_PYTHON)
      include_directories("${PYTHON_INCLUDE_DIRS}")
    endif()

    add_subdirectory(mysql-secret-store-plaintext)
    add_subdirectory(sample-pager)

    file(GLOB mysqlsh_tests_SRC
        "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/shellcore/*.cc"
        "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/db/*.cc"
        "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/config/*.cc"
        "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/mysql/*.cc"
        "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/rest/*.cc"
        "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/storage/*.cc"
        "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/storage/in_memory/*.cc"
        "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/utils/*.cc"
        "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/aws/*.cc"
        "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/oci/*.cc"
        "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/azure/*.cc"
        "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/textui/*.cc"
        "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/gr/group_replication_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/innodbcluster/*.cc"
        "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/ssh/*.cc"
        "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/parser/mysql_parser_utils_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/yparser/parser_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/scripting/*.cc"
        "${CMAKE_SOURCE_DIR}/unittest/*_t.cc"
        "${CMAKE_SOURCE_DIR}/unittest/test_utils/*.cc"
        "${CMAKE_SOURCE_DIR}/unittest/test_utils/mocks/mysqlshdk/libs/*/*.cc"
        "${CMAKE_SOURCE_DIR}/unittest/test_utils/mocks/modules/adminapi/common/*.cc"
        "${CMAKE_SOURCE_DIR}/unittest/*/*_t.cc")

    set(mysqlsh_tests_SRC
        ${mysqlsh_tests_SRC}
        "${PROJECT_SOURCE_DIR}/unittest/modules/mysql_connection_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/mod_utils_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/dump_utils_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/dummy_dumpdir.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/adminapi/mod_dba_common_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/adminapi/mod_dba_cluster_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/adminapi/preconditions_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/adminapi/common/clone_handling_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/adminapi/common/metadata_management_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/adminapi/common/provisioning_interface_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/adminapi/common/router_options_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/adminapi/common/replication_version_compatibility_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/devapi/mod_mysqlx_collection_find_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/devapi/mod_mysqlx_table_select_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/util/common/data_masking_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/util/upgrade_checker/test_utils.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/util/upgrade_checker/upgrade_check_condition_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/util/upgrade_checker/feature_upgrade_check_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/util/upgrade_checker/upgrade_check_registry_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/util/upgrade_checker/upgrade_check_creators_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/modules/util/upgrade_checker/sysvar_upgrade_check_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/shell_cmdline_regressions_t.cc"
        "${PROJECT_SOURCE_DIR}/unittest/shell_cli_operation_t.cc"
        "${CMAKE_SOURCE_DIR}/unittest/test_main.cc"
        "${CMAKE_SOURCE_DIR}/unittest/test_utils.cc"
        "${CMAKE_SOURCE_DIR}/unittest/shell_script_tester.cc"
    )

    if(NOT HAVE_PROTOBUF)
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/mod_mysqlx_t.cc")
    endif()

    if(NOT HAVE_JS)
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/value_js_bridging_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/shell_js_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/auto_script_js_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/shell_js_mysql_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/shell_js_mysqlx_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/test_js_dev_api_samples_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/modules/api_connections_t.cc")
    endif()

    if(NOT HAVE_UPGRADE_CHECKER)
      # The Upgrade Checker is MySQL-server specific and is not built for MariaDB.
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/modules/upgrade_check_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/util/upgrade_checker/test_utils.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/util/upgrade_checker/upgrade_check_condition_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/util/upgrade_checker/feature_upgrade_check_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/util/upgrade_checker/upgrade_check_registry_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/util/upgrade_checker/upgrade_check_creators_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/util/upgrade_checker/sysvar_upgrade_check_t.cc")
    endif()

    if(NOT HAVE_X_PROTOCOL)
      # X protocol / X DevAPI tests — the X client, protobuf messages and the X
      # expression parsers are not built for MariaDB (see MARIADB_PORT.md).
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/expr_parser_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/orderby_parser_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/proj_parser_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/devapi/mysqlx_session_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/devapi/mod_mysqlx_collection_find_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/devapi/mod_mysqlx_table_select_t.cc")
    endif()

    if(NOT HAVE_ADMIN_API)
      # AdminAPI (InnoDB Cluster/ReplicaSet/ClusterSet) is not built for MariaDB.
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/adminapi/mod_dba_common_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/adminapi/mod_dba_cluster_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/adminapi/preconditions_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/adminapi/common/clone_handling_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/adminapi/common/metadata_management_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/adminapi/common/provisioning_interface_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/adminapi/common/router_options_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/adminapi/common/replication_version_compatibility_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/test_utils/admin_api_test.cc")

      # AdminAPI mocks and tests that transitively require AdminAPI symbols.
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/test_utils/mocks/modules/adminapi/common/mock_metadata_storage.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/test_utils/mocks/modules/adminapi/common/mock_precondition_checker.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/shell_cli_operation_t.cc")
      # clone.cc / repl_config.cc in mysqlshdk reference mysqlsh::dba helpers that
      # live in the (unbuilt) AdminAPI; these tests pull those objects in.
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/mysql/clone_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/mysql/replication_t.cc")
      # Group Replication is MySQL-specific (InnoDB Cluster backend) and pulls in
      # repl_config.cc, which references AdminAPI symbols.
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/gr/group_replication_t.cc")
    endif()

    if(MARIADB_BUILD)
      # The yparser SQL-grammar plugins are per-MySQL-version and are not built
      # for MariaDB (see MARIADB_PORT.md §5), so this test has nothing to load.
      # This is a build-artifact concern, not an AdminAPI/X-protocol feature gate.
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/mysqlshdk/libs/yparser/parser_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/mysqlshdk/libs/mysql/gtid_utils_t.cc")
    endif()

    if(NOT HAVE_DUMP_AND_LOAD)
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/util/dump/decimal_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/dump_utils_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/load_dump_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/compatibility_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/instance_cache_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/schema_dumper_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/instance_cache_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${PROJECT_SOURCE_DIR}/unittest/modules/checksums_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/mysqlshdk/libs/mysql/gtid_utils_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/mysqlshdk/libs/mysql/lock_service_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/mysqlshdk/libs/mysql/user_privileges_t.cc")
    endif()

    if(NOT HAVE_PYTHON)
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/shell_py_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/shell_py_mysql_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/shell_py_mysqlx_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/auto_script_py_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/value_py_bridging_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/mysqlshdk/shellcore/completer_python_t.cc")
    endif()

    if (NOT HAVE_X_PROTOCOL)
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/test_py_dev_api_samples_t.cc")
      list(REMOVE_ITEM mysqlsh_tests_SRC "${CMAKE_SOURCE_DIR}/unittest/shell_py_mysqlx_t.cc")
    endif()


    LIST(APPEND mysqlsh_tests_SRC
        ${CMAKE_SOURCE_DIR}/ext/linenoise-ng/src/linenoise.cpp
        ${CMAKE_SOURCE_DIR}/ext/linenoise-ng/src/ConvertUTF.cpp
        ${CMAKE_SOURCE_DIR}/ext/linenoise-ng/src/wcwidth.cpp)

    IF(CMAKE_COMPILER_IS_GNUCXX)
      SET_SOURCE_FILES_PROPERTIES(${CMAKE_SOURCE_DIR}/ext/linenoise-ng/src/linenoise.cpp PROPERTIES COMPILE_FLAGS "-Wno-stringop-overflow")
      SET_SOURCE_FILES_PROPERTIES(${CMAKE_SOURCE_DIR}/ext/linenoise-ng/src/ConvertUTF.cpp PROPERTIES COMPILE_FLAGS "-Wno-implicit-fallthrough -Wno-array-bounds")
      SET_SOURCE_FILES_PROPERTIES(
        "${CMAKE_SOURCE_DIR}/unittest/mysqlshdk/libs/db/db_row_decimal_t.cc"
        "${CMAKE_SOURCE_DIR}/unittest/mysqlshdk/libs/db/db_row_t.cc"
        "${CMAKE_SOURCE_DIR}/unittest/shell_history_t.cc"
        PROPERTIES COMPILE_FLAGS "-fno-var-tracking-assignments")
    ENDIF()

    IF(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      SET_SOURCE_FILES_PROPERTIES(${CMAKE_SOURCE_DIR}/ext/linenoise-ng/src/ConvertUTF.cpp PROPERTIES COMPILE_FLAGS "-Wno-keyword-macro")
    ENDIF()

    add_shell_executable(run_unit_tests "${mysqlsh_tests_SRC}" TRUE)

    # Disable debug symbols in 32 bit platforms
    IF(CMAKE_SIZEOF_VOID_P EQUAL 4)
      target_compile_options(run_unit_tests PRIVATE -g0)
    ENDIF()

    IF(MARIADB_BUILD)
      # The bundled googletest deprecates testing::Invoke(callable); with -Werror
      # that breaks the many existing Invoke() call sites. Don't fail on it.
      # GCC/Clang-only flag; MSVC rejects it (and doesn't build with /WX here).
      IF(NOT MSVC)
        target_compile_options(run_unit_tests PRIVATE -Wno-error=deprecated-declarations)
      ENDIF()
      # Python 3.x's pyconfig.h and the MariaDB server's my_config.h both define
      # SIZEOF_* macros; silence the redefinition warning (affects any test that
      # pulls in both, e.g. shell_py_t.cc and test_main.cc).
      IF(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(run_unit_tests PRIVATE -Wno-macro-redefined)
      ENDIF()
    ENDIF()

    IF(NOT HAVE_X_PROTOCOL)
      # The X protocol client, protobuf and lz4 are not built for MariaDB, so
      # drop them from the test link (LZ4_LIBRARY still names the missing
      # 'lz4_lib' target otherwise).
      set(MYSQLX_LIBRARIES "")
      set(PROTOBUF_LIBRARY "")
      set(LZ4_LIBRARY "")
    ENDIF()

    add_dependencies(run_unit_tests
            shellfe
            api_modules
            mysqlshdk-static)

    target_link_libraries(run_unit_tests
            shellfe
            api_modules
            db
            mysqlshdk-static
            shellcore-minimal
            ${GTEST_LIBRARIES}
            ${MYSQLX_LIBRARIES}
            ${PROTOBUF_LIBRARIES}
            ${LZ4_LIBRARY}
            ${SSL_LIBRARIES}
            ${SSL_LIBRARIES_DL}
            ${PYTHON_LIBRARIES}
            ${MYSQL_EXTRA_LIBRARIES}
            ${ANTLR4_LIBRARIES}
            ${ZSTD_LIBRARY}
    )

    IF(WIN32)
      # run_unit_tests pulls mariadbclient.lib but does not reliably inherit the
      # connector's Windows system libs (secur32/... for sspi_client.c) via the
      # transitive mysqlshdk-static link, so link them explicitly here.
      target_link_libraries(run_unit_tests Dbghelp.lib ${MARIADB_WIN_SYSTEM_LIBS})
    ELSE()
      target_link_libraries(run_unit_tests pthread ${GCOV_LDFLAGS})
    ENDIF()

    if(HAVE_JIT_EXECUTOR)
      target_link_libraries(run_unit_tests ${JIT_EXECUTOR_LIBRARY})
    endif()

    if(HAVE_PYTHON)
      # NB: keep PYTHON_LIBRARIES unquoted. With vcpkg (both release and debug
      # libs present) FindPython3 sets it to a debug/optimized keyword list
      # (optimized;<rel>;debug;<dbg>). Those keywords are only honored when they
      # arrive as separate arguments; quoting collapses them into one argument, so
      # CMake feeds bare "optimized"/"debug" tokens to link.exe and it fails with
      # LNK1104 "cannot open file 'optimized.lib'".
      target_link_libraries(run_unit_tests ${PYTHON_LIBRARIES})
    endif()

    # Makes things more organized in IDEs

    get_property(run_unit_tests_SOURCES TARGET run_unit_tests PROPERTY SOURCES)
    source_group(TREE "${PROJECT_SOURCE_DIR}" FILES ${run_unit_tests_SOURCES})

    IF (NOT MARIADB_BUILD)
      add_subdirectory(kdestroy)
    ENDIF()
else()
  message(WARNING "Skipping tests. To enable unit-tests use -DWITH_TESTS=1")
endif()
