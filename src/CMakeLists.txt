# Copyright (c) 2014, 2025, Oracle and/or its affiliates.
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

include_directories(SYSTEM "${CMAKE_SOURCE_DIR}/ext/rapidjson/include")

include_directories(BEFORE "${CMAKE_SOURCE_DIR}/mysqlshdk/libs"
                    "${CMAKE_SOURCE_DIR}/mysqlshdk/include"
                    "${CMAKE_BINARY_DIR}/mysqlshdk/include"
                    "${CMAKE_SOURCE_DIR}"
                    "${CMAKE_SOURCE_DIR}/src"
                    "${CMAKE_SOURCE_DIR}/ext/linenoise-ng/include")

IF(HAVE_PYTHON)
  INCLUDE_DIRECTORIES(${PYTHON_INCLUDE_DIRS})
ENDIF()

set(mysh_SRC
    mysqlsh/get_password.cc
    mysqlsh/cmdline_shell.cc
    mysqlsh/json_shell.cc
    mysqlsh/history.cc
    mysqlsh/mysql_shell.cc
    mysqlsh/prompt_renderer.cc
    mysqlsh/prompt_manager.cc
    mysqlsh/prompt_handler.cc
    mysqlsh/commands/command_edit.cc
    mysqlsh/commands/command_help.cc
    mysqlsh/commands/command_show.cc
    mysqlsh/commands/command_system.cc
    mysqlsh/commands/command_watch.cc
    mysqlsh/commands/command_query_attributes.cc
    ${CMAKE_SOURCE_DIR}/mysqlshdk/shellcore/interrupt_helper.cc
    ${CMAKE_SOURCE_DIR}/ext/linenoise-ng/src/linenoise.cpp
    ${CMAKE_SOURCE_DIR}/ext/linenoise-ng/src/ConvertUTF.cpp
    ${CMAKE_SOURCE_DIR}/ext/linenoise-ng/src/wcwidth.cpp
)

IF(CMAKE_COMPILER_IS_GNUCXX)
  SET_SOURCE_FILES_PROPERTIES(${CMAKE_SOURCE_DIR}/ext/linenoise-ng/src/linenoise.cpp PROPERTIES COMPILE_FLAGS "-Wno-stringop-overflow")
  SET_SOURCE_FILES_PROPERTIES(${CMAKE_SOURCE_DIR}/ext/linenoise-ng/src/ConvertUTF.cpp PROPERTIES COMPILE_FLAGS "-Wno-implicit-fallthrough -Wno-array-bounds")
ENDIF()

IF(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  SET_SOURCE_FILES_PROPERTIES(${CMAKE_SOURCE_DIR}/ext/linenoise-ng/src/ConvertUTF.cpp PROPERTIES COMPILE_FLAGS "-Wno-keyword-macro")
ENDIF()

ADD_CONVENIENCE_LIBRARY(shellfe ${mysh_SRC})
ADD_STAN_TARGET(shellfe ${mysh_SRC})
target_link_libraries(shellfe ssh)

set(mysqlsh_SRC
  mysqlsh/main.cc
)

if(WIN32)
  # On Windows it is requred to include this source file on the list of sources
  # for the main project in order to:
  # - Define the icon to be used on the application.
  # - Define the version information to be shown while looking at the file
  # properties.
  generate_rc_file(NAME "mariadb-shell.exe" DESCRIPTION "Advanced client and code editor for MariaDB." OUT_RC_FILE RC_FILE)
  set(mysqlsh_SRC
      "${mysqlsh_SRC}"
      "${RC_FILE}"
  )

  # syslog event
  set_source_files_properties("${SYSLOG_EVENT_RC}" PROPERTIES GENERATED TRUE)
  set(mysqlsh_SRC
      "${mysqlsh_SRC}"
      "${SYSLOG_EVENT_RC}"
  )
endif()

add_definitions(-DMYSH_COMMIT_ID="${MYSH_COMMIT_ID}")
add_definitions(-DMYSH_PRODUCT_ID="${MYSH_PRODUCT_ID}")

add_shell_executable(mariadb-shell "${mysqlsh_SRC}")
ADD_STAN_TARGET(mariadb-shell mysqlsh/main.cc)

if(WIN32)
  add_dependencies(mariadb-shell generate_syslog_event_files)
endif()

# Compatibility / convenience names for the shell executable. "mysqlsh" keeps
# scripts written against the pre-rename binary working; "msh" is a short alias.
# On Unix these are symlinks beside the real binary, both in the build tree and
# in an install; on Windows, where a symlink needs privileges the installer may
# not have, they are .cmd shims that forward every argument.
set(SHELL_BINARY_ALIASES mysqlsh msh)

if(WIN32)
  foreach(_alias ${SHELL_BINARY_ALIASES})
    set(_shim "${CMAKE_CURRENT_BINARY_DIR}/${_alias}.cmd")
    file(GENERATE OUTPUT "${_shim}"
         CONTENT "@echo off\r\n\"%~dp0mariadb-shell.exe\" %*\r\n")
    add_custom_command(TARGET mariadb-shell POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "${_shim}" "${CMAKE_BINARY_DIR}/${INSTALL_BINDIR}/${_alias}.cmd"
      COMMENT "Creating ${_alias}.cmd alias for mariadb-shell")
    install(PROGRAMS "${_shim}" DESTINATION "${INSTALL_BINDIR}" COMPONENT main)
  endforeach()
else()
  foreach(_alias ${SHELL_BINARY_ALIASES})
    add_custom_command(TARGET mariadb-shell POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E create_symlink
              "mariadb-shell" "${CMAKE_BINARY_DIR}/${INSTALL_BINDIR}/${_alias}"
      COMMENT "Creating ${_alias} symlink to mariadb-shell")
  endforeach()

  # install(CODE) rather than install(FILES) on the build-tree symlinks: CPack's
  # DESTDIR staging would otherwise resolve them and ship two extra full copies
  # of the binary.
  install(CODE "
    foreach(_alias ${SHELL_BINARY_ALIASES})
      execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink
        \"mariadb-shell\"
        \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${INSTALL_BINDIR}/\${_alias}\")
    endforeach()
  " COMPONENT main)
endif()

set(mysqlsh_SRC
  ${mysqlsh_SRC}
  ${CMAKE_SOURCE_DIR}/unittest/test_utils/mod_testutils.cc
  mysqlsh/debug_shell.cc
)

target_link_libraries(mariadb-shell
    shellfe
    api_modules
    mysqlshdk-static
    ssh
    ${GCOV_LDFLAGS}
    ${MYSQLX_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    ${MYSQL_EXTRA_LIBRARIES}
)

IF(WITH_TESTS)
  IF(NOT HAVE_PYTHON)
    message(FATAL_ERROR "Building test support (i.e.: mariadb-shell-rec) requires Python support enabled.")
  ENDIF()

  add_shell_executable(mariadb-shell-rec "${mysqlsh_SRC}" TRUE)
  target_compile_definitions(mariadb-shell-rec PRIVATE -DENABLE_SESSION_RECORDING)

  target_link_libraries(mariadb-shell-rec
    shellfe
    api_modules
    mysqlshdk-static
    ${GCOV_LDFLAGS}
    ${MYSQLX_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    ${MYSQL_EXTRA_LIBRARIES}
  )
ENDIF()

### Bundling of OpenSSL libraries (if needed)
if(BUNDLED_OPENSSL AND NOT WITH_PARFAIT)
  IF(WIN32)
    SET(OPENSSL_LIBS "${HAVE_CRYPTO_DLL}" "${HAVE_OPENSSL_DLL}")
  ELSE()
    SET(OPENSSL_LIBS "${CRYPTO_FULL_NAME}" "${OPENSSL_FULL_NAME}" "${CRYPTO_LIBRARY}" "${OPENSSL_LIBRARY}")
  ENDIF()

  install_bundled_binaries(
      DESCRIPTION "OpenSSL libraries"
      BINARIES ${OPENSSL_LIBS}
      DESTINATION "${INSTALL_LIBDIR}"
      TARGET mariadb-shell
  )

  IF(OPENSSL_MODULES_DIR)
    install_bundled_binaries(
        DESCRIPTION "OpenSSL modules"
        BINARIES ${OPENSSL_MODULES_LIST}
        DESTINATION "${INSTALL_LIBDIR}/ossl-modules"
        USE_LIBDIR
        TARGET mariadb-shell
    )
  ENDIF()

  # The sandbox plugin shells out to the openssl CLI to generate the sandbox SSL
  # certificates when it deploys a MariaDB instance (MariaDB, unlike MySQL, does
  # not auto-generate them at datadir init). The system openssl cannot be relied
  # on: Windows has none, and macOS ships LibreSSL, whose 'req' command is
  # incompatible with our cert-generation config (empty distinguished_name +
  # -subj aborts with "no objects specified in config file"). vcpkg keeps the tool
  # under <prefix>/tools/openssl, which the normal library-deploy step does not
  # copy, so bundle it next to the bundled OpenSSL libraries (INSTALL_LIBDIR,
  # which maps to bin/ on Windows) for an out-of-the-box experience. Runtime
  # discovery is in python/plugins/sandbox (see _shell_bundled_openssl), which
  # looks beside the shell's OpenSSL libraries.
  #
  # This is deliberately NOT conditional on MARIADB_BUILD: the server the shell
  # was built against says nothing about the server it deploys. The sandbox
  # plugin ships in both builds and deploys either vendor, so a MySQL build needs
  # the CLI just as much - without it, deploying a MariaDB sandbox fails wherever
  # the system openssl is missing or is LibreSSL.
  FIND_PROGRAM(OPENSSL_CLI_EXECUTABLE
    NAMES openssl
    HINTS "${WITH_SSL_PATH}/tools/openssl" "${WITH_SSL_PATH}/bin"
          "${OPENSSL_ROOT_DIR}/tools/openssl" "${OPENSSL_ROOT_DIR}/bin"
    NO_DEFAULT_PATH)
  IF(OPENSSL_CLI_EXECUTABLE)
    SET(_OPENSSL_CLI_EXTRA_COMMANDS "")
    IF(APPLE)
      # install_bundled_binaries rewrites the copy's LC_LOAD_DYLIB references
      # (-change) but never its LC_RPATH. The vcpkg CLI links its libs as
      # @rpath/lib<x>.dylib and ships an rpath (@loader_path/../../lib) aimed at
      # the vcpkg tools layout, not INSTALL_LIBDIR. Strip that stale rpath and
      # add @loader_path so it resolves the OpenSSL dylibs bundled right beside
      # it in INSTALL_LIBDIR.
      get_fix_bundled_rpath_command(
        PATTERN "${CMAKE_BINARY_DIR}/${INSTALL_LIBDIR}/openssl"
        STRIP_PREFIX "@loader_path/../../lib"
        ADD_RPATH "@loader_path"
        OUT_COMMAND _FIX_OPENSSL_CLI_RPATH)
      SET(_OPENSSL_CLI_EXTRA_COMMANDS COMMAND ${_FIX_OPENSSL_CLI_RPATH})
    ENDIF()
    install_bundled_binaries(
        DESCRIPTION "OpenSSL CLI (sandbox cert generation)"
        BINARIES "${OPENSSL_CLI_EXECUTABLE}"
        DESTINATION "${INSTALL_LIBDIR}"
        TARGET mariadb-shell
        EXTRA_COPY_COMMANDS ${_OPENSSL_CLI_EXTRA_COMMANDS}
    )
  ELSE()
    MESSAGE(WARNING "openssl CLI not found near the resolved OpenSSL "
      "(WITH_SSL_PATH='${WITH_SSL_PATH}', OPENSSL_ROOT_DIR='${OPENSSL_ROOT_DIR}'); "
      "the sandbox plugin will require openssl on PATH or the 'opensslPath' "
      "option to deploy a MariaDB instance.")
  ENDIF()
endif()


### Bundling of Python runtime if needed
IF(HAVE_PYTHON)
  if(WIN32)
    remove_definitions(-DUNICODE)

    install_bundled_binaries(
        DESCRIPTION "Python executable"
        BINARIES "${PYTHON_EXECUTABLE}"
        DESTINATION "lib/Python${PYTHONLIBS_MAJOR_MINOR}"
        TARGET mariadb-shell
    )

    file(GLOB PYTHON_DLLS "${PYTHON_ROOT}/python*.dll")
    install_bundled_binaries(
        DESCRIPTION "Python dlls"
        BINARIES ${PYTHON_DLLS}
        DESTINATION "bin"
        TARGET mariadb-shell
    )

    # GLOB is used here, because some of these files may not exist
    file(GLOB PYTHON_ADDITIONAL_DLLS
        "${PYTHON_ROOT}/libffi.dll"
        "${PYTHON_ROOT}/sqlite3.dll"
        "${PYTHON_ROOT}/psqlite3.dll"
    )
    install_bundled_binaries(
        DESCRIPTION "additional Python dlls"
        BINARIES ${PYTHON_ADDITIONAL_DLLS}
        DESTINATION "lib/Python${PYTHONLIBS_MAJOR_MINOR}/DLLs"
        TARGET mariadb-shell
    )

    # Python runtime
    install_bundled_directory(
        DESCRIPTION "Python library"
        DIRECTORY "${PYTHON_LIBRARY_PATH}"
        DESTINATION "lib/Python${PYTHONLIBS_MAJOR_MINOR}"
        TARGET mariadb-shell
    )
    install_bundled_directory(
        DESCRIPTION "Python library dlls"
        DIRECTORY "${PYTHON_DLLS_PATH}"
        DESTINATION "lib/Python${PYTHONLIBS_MAJOR_MINOR}"
        TARGET mariadb-shell
    )

    # Python dependencies
    IF(PYTHON_DEPS)
      # no need to install this, this is a subdirectory of the Python library directory
      install_bundled_directory(
          DESCRIPTION "extra Python dependencies"
          COPY_ONLY
          CONTENTS "${PYTHON_DEPS}"
          DESTINATION "lib/Python${PYTHONLIBS_MAJOR_MINOR}/Lib/site-packages"
          TARGET mariadb-shell
      )
    ENDIF()
  elseif(BUNDLED_PYTHON_DIR)
    set(WRITE_RPATH TRUE)
    set(EXTRA_COPY_COMMANDS "")

    # A dynamic vcpkg triplet gives its dylibs the id "@rpath/lib<x>.dylib", so
    # bootstrap_python.cmake links the interpreter/libpython/extension modules
    # with -Wl,-rpath,<abs vcpkg build dir> to resolve them at build time. That
    # absolute build-tree path must NOT ship in the package (it points at a dir
    # absent on the target). install_name_tool -change only rewrites dependency
    # references, never LC_RPATH entries, so scrub it explicitly from the bundled
    # copies and add a relative @loader_path rpath instead. Derive the same vcpkg
    # prefix bootstrap_python.cmake used (cached VCPKG_INSTALLED_PREFIX, or the
    # conventional per-triplet dir when only the toolchain was given).
    set(_PY_VCPKG_RPATH_STRIP "")
    if(APPLE AND USING_VCPKG)
      if(VCPKG_INSTALLED_PREFIX)
        set(_PY_VCPKG_RPATH_STRIP "${VCPKG_INSTALLED_PREFIX}/lib")
      elseif(VCPKG_TARGET_TRIPLET)
        set(_PY_VCPKG_RPATH_STRIP "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib")
      endif()
    endif()

    if(BUNDLED_SHARED_PYTHON)
      if(APPLE)
        set(_PYTHON_SHARED_LIB "${PYTHON_LIBRARIES}")
        get_filename_component(_PY_SHARED_LIB_NAME "${_PYTHON_SHARED_LIB}" NAME)

        # A CPython built with --enable-shared stamps libpython<ver>.dylib with an
        # ABSOLUTE install id (its build-tree <prefix>/lib path). Everything linked
        # against it -- the shell executables (via mysqlshdk-static) and CPython's
        # own python<ver> launcher -- copies that absolute path in as its
        # LC_LOAD_DYLIB. The bundled *copy* is made relocatable below (id -> @rpath,
        # rpath fixed), but the *references* to it are not, so a relocated package
        # still asks dyld for the vanished build-tree path ("Library not loaded:
        # .../libpython<ver>.dylib"). Read libpython's real id and rewrite each
        # consumer's reference to @rpath/<name>, resolved via that binary's own
        # rpath (INSTALL_LIBDIR, where libpython is bundled). otool -D prints the
        # file name on line 1 and the id on line 2. -change is a no-op if the id is
        # already @rpath, so this stays correct on non-CPython layouts.
        execute_process(COMMAND otool -D "${_PYTHON_SHARED_LIB}"
                        OUTPUT_VARIABLE _py_otool_id OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(REGEX REPLACE "^[^\n]*\n" "" _PY_LIBPYTHON_ID "${_py_otool_id}")
        string(STRIP "${_PY_LIBPYTHON_ID}" _PY_LIBPYTHON_ID)

        # add rpath to allow python executable to import dylibs
        set(WRITE_RPATH FALSE)
        set(EXTRA_COPY_COMMANDS COMMAND install_name_tool -add_rpath "@executable_path/.." "${CMAKE_BINARY_DIR}/${INSTALL_LIBDIR}/bin/python${PYTHONLIBS_MAJOR_MINOR}")
        # The bundled python<ver> launcher carries the same absolute libpython
        # reference; point it at the bundled copy via @rpath (reached by the
        # @executable_path/.. rpath added just above).
        if(_PY_LIBPYTHON_ID)
          list(APPEND EXTRA_COPY_COMMANDS COMMAND install_name_tool -change "${_PY_LIBPYTHON_ID}" "@rpath/${_PY_SHARED_LIB_NAME}" "${CMAKE_BINARY_DIR}/${INSTALL_LIBDIR}/bin/python${PYTHONLIBS_MAJOR_MINOR}")

          # Same rewrite for the shell executables that embed libpython. Only
          # mariadb-shell is shipped; mariadb-shell-rec (if built) is fixed too so a relocated
          # test tree works. run_unit_tests runs from the build tree, where the
          # absolute path still resolves, so it needs no fix.
          foreach(_py_consumer mariadb-shell mariadb-shell-rec)
            if(TARGET ${_py_consumer})
              add_custom_command(TARGET ${_py_consumer} POST_BUILD
                COMMAND install_name_tool -change "${_PY_LIBPYTHON_ID}" "@rpath/${_PY_SHARED_LIB_NAME}" "$<TARGET_FILE:${_py_consumer}>"
                COMMENT "Rewriting ${_py_consumer} libpython reference to @rpath (relocatable)")
            endif()
          endforeach()
        endif()
        # Drop the absolute vcpkg build-tree rpath baked in at build time; the
        # @executable_path/.. rpath above already reaches the bundled dylibs.
        if(_PY_VCPKG_RPATH_STRIP)
          get_fix_bundled_rpath_command(
            BINARIES "${CMAKE_BINARY_DIR}/${INSTALL_LIBDIR}/bin/python${PYTHONLIBS_MAJOR_MINOR}"
            STRIP_PREFIX "${_PY_VCPKG_RPATH_STRIP}"
            OUT_COMMAND _FIX_RPATH_CMD)
          list(APPEND EXTRA_COPY_COMMANDS COMMAND ${_FIX_RPATH_CMD})

          # libpython lands directly in INSTALL_LIBDIR next to the bundled vcpkg
          # dylibs, so @loader_path resolves them once the build-tree rpath is gone.
          get_fix_bundled_rpath_command(
            BINARIES "${CMAKE_BINARY_DIR}/${INSTALL_LIBDIR}/${_PY_SHARED_LIB_NAME}"
            STRIP_PREFIX "${_PY_VCPKG_RPATH_STRIP}"
            ADD_RPATH "@loader_path"
            OUT_COMMAND _FIX_RPATH_CMD)
          set(LIB_EXTRA_COPY_COMMANDS COMMAND ${_FIX_RPATH_CMD})
        endif()
      else()
        file(GLOB _PYTHON_SHARED_LIB "${PYTHON_LIBRARIES}.*")

        if(STRIP_LIBCRYPT_DEPS)
          get_filename_component(_PYTHON_SHARED_LIB_NAME "${_PYTHON_SHARED_LIB}" NAME)
          get_patchelf_command(ARGS --remove-needed libcrypt.so.1 "${CMAKE_BINARY_DIR}/${INSTALL_LIBDIR}/${_PYTHON_SHARED_LIB_NAME}" OUT_COMMAND COMMAND)
          set(LIB_EXTRA_COPY_COMMANDS COMMAND ${COMMAND})
        endif()
      endif()

      install_bundled_binaries(
          DESCRIPTION "Python shared library"
          BINARIES "${_PYTHON_SHARED_LIB}"
          DESTINATION "${INSTALL_LIBDIR}"
          TARGET mariadb-shell
          WRITE_RPATH FALSE
          EXTRA_COPY_COMMANDS ${LIB_EXTRA_COPY_COMMANDS}
      )
    endif()

    install_bundled_binaries(
        DESCRIPTION "Python executable"
        BINARIES "${PYTHON_EXECUTABLE}"
        DESTINATION "${INSTALL_LIBDIR}/bin"
        TARGET mariadb-shell
        WRITE_RPATH ${WRITE_RPATH}
        EXTRA_COPY_COMMANDS ${EXTRA_COPY_COMMANDS}
    )

    # Python headers
    install(DIRECTORY "${BUNDLED_PYTHON_DIR}/include/python${PYTHONLIBS_MAJOR_MINOR}" DESTINATION "${INSTALL_LIBDIR}/include" COMPONENT main)

    # Python runtime
    set(EXTRA_COPY_COMMANDS "")

    if(APPLE)
      get_use_bundled_openssl_command(PATTERN "${CMAKE_BINARY_DIR}/${INSTALL_LIBDIR}/lib/python${PYTHONLIBS_MAJOR_MINOR}/lib-dynload/_[sh]*.so" OUT_COMMAND EXTRA_COPY_COMMANDS)
      list(INSERT EXTRA_COPY_COMMANDS 0 COMMAND)

      # The OpenSSL rewrite above only touches _ssl/_hashlib. Every lib-dynload
      # extension linked against a vcpkg dylib (_zstd, zlib, binascii, ...) also
      # carries the absolute build-tree rpath; strip it and add a relative rpath
      # from lib-dynload (INSTALL_LIBDIR/lib/pythonX.Y/lib-dynload) up to
      # INSTALL_LIBDIR, where the bundled dylibs live, so imports resolve on the
      # target regardless of which executable dlopen()s them.
      if(_PY_VCPKG_RPATH_STRIP)
        get_fix_bundled_rpath_command(
          PATTERN "${CMAKE_BINARY_DIR}/${INSTALL_LIBDIR}/lib/python${PYTHONLIBS_MAJOR_MINOR}/lib-dynload/*.so"
          STRIP_PREFIX "${_PY_VCPKG_RPATH_STRIP}"
          ADD_RPATH "@loader_path/../../.."
          OUT_COMMAND _FIX_RPATH_CMD)
        list(APPEND EXTRA_COPY_COMMANDS COMMAND ${_FIX_RPATH_CMD})
      endif()
    elseif(LINUX)
      # Same problem as the macOS branch above, and it needs the same fix: the
      # lib-dynload extension modules are copied as part of a directory, so they
      # keep the absolute build-tree search path bootstrap_python.cmake linked
      # them with (the vcpkg per-triplet lib dir) and never look inside
      # INSTALL_LIBDIR, where the very same libraries are bundled. That path is
      # gone in a packaged/relocated tree, and - because it is a DT_RUNPATH, which
      # ld.so searches only *after* LD_LIBRARY_PATH - it also loses to any
      # LD_LIBRARY_PATH pointing at a system lib dir even in the build tree. Both
      # cases drop the module onto the system library of the same SONAME: for
      # _ssl/_hashlib that is the distro's libcrypto.so.3, which lacks the symbol
      # versions of the newer bundled OpenSSL, so `import ssl` dies with
      # "version `OPENSSL_3.3.0' not found" (seen in CI, on the
      # py_shell/mysqlsh_python_norecord.py sys.executable check). Replace it with
      # a relative DT_RPATH into INSTALL_LIBDIR, which is searched before
      # LD_LIBRARY_PATH and is valid in the build tree and the package alike. This
      # covers every lib-dynload module, not just the OpenSSL ones - _sqlite3,
      # _zstd, zlib and binascii link bundled vcpkg libraries too.
      get_force_rpath_command(
        PATTERN "${CMAKE_BINARY_DIR}/${INSTALL_LIBDIR}/lib/python${PYTHONLIBS_MAJOR_MINOR}/lib-dynload/*.so"
        ORIGIN_RELATIVE "../../.."
        OUT_COMMAND _FORCE_RPATH_CMD)
      set(EXTRA_COPY_COMMANDS COMMAND ${_FORCE_RPATH_CMD})
    endif()

    install_bundled_directory(
        DESCRIPTION "Python library"
        DIRECTORY "${BUNDLED_PYTHON_DIR}/lib/python${PYTHONLIBS_MAJOR_MINOR}"
        DESTINATION "${INSTALL_LIBDIR}/lib"
        TARGET mariadb-shell
        EXTRA_COPY_COMMANDS ${EXTRA_COPY_COMMANDS}
        EXTRA_INSTALL_COMMANDS PATTERN "lib/python${PYTHONLIBS_MAJOR_MINOR}/test" EXCLUDE
    )

    # Python dependencies
    IF(PYTHON_DEPS)
      set(EXTRA_COPY_COMMANDS "")

      if(APPLE)
        get_use_bundled_openssl_command(PATTERN "${CMAKE_BINARY_DIR}/${INSTALL_LIBDIR}/lib/python${PYTHONLIBS_MAJOR_MINOR}/site-packages/*.so" OUT_COMMAND EXTRA_COPY_COMMANDS)
        list(INSERT EXTRA_COPY_COMMANDS 0 COMMAND)

        # Any site-package extension linked against a vcpkg dylib carries the
        # same absolute build-tree rpath; strip it. The relative rpath assumes a
        # top-level module (site-packages/foo.so -> up 3 to INSTALL_LIBDIR),
        # matching the glob above; a nested module still resolves via the host
        # executable's own @executable_path/.. rpath, so the strip is the part
        # that matters (it removes the build-tree leak).
        if(_PY_VCPKG_RPATH_STRIP)
          get_fix_bundled_rpath_command(
            PATTERN "${CMAKE_BINARY_DIR}/${INSTALL_LIBDIR}/lib/python${PYTHONLIBS_MAJOR_MINOR}/site-packages/*.so"
            STRIP_PREFIX "${_PY_VCPKG_RPATH_STRIP}"
            ADD_RPATH "@loader_path/../../.."
            OUT_COMMAND _FIX_RPATH_CMD)
          list(APPEND EXTRA_COPY_COMMANDS COMMAND ${_FIX_RPATH_CMD})
        endif()
      elseif(LINUX)
        # Same treatment as lib-dynload above, for the same reasons; the relative
        # path assumes a top-level module (site-packages/foo.so -> up 3 to
        # INSTALL_LIBDIR), matching the glob. A nested module keeps whatever it was
        # linked with - only top-level extensions are rewritten here, and none of
        # the bundled packages ship a nested one that links a bundled library.
        get_force_rpath_command(
          PATTERN "${CMAKE_BINARY_DIR}/${INSTALL_LIBDIR}/lib/python${PYTHONLIBS_MAJOR_MINOR}/site-packages/*.so"
          ORIGIN_RELATIVE "../../.."
          OUT_COMMAND _FORCE_RPATH_CMD)
        set(EXTRA_COPY_COMMANDS COMMAND ${_FORCE_RPATH_CMD})
      endif()

      # no need to install this, this is a subdirectory of the Python library directory
      install_bundled_directory(
          DESCRIPTION "extra Python dependencies"
          COPY_ONLY
          CONTENTS "${PYTHON_DEPS}"
          DESTINATION "${INSTALL_LIBDIR}/lib/python${PYTHONLIBS_MAJOR_MINOR}/site-packages"
          TARGET mariadb-shell
          EXTRA_COPY_COMMANDS ${EXTRA_COPY_COMMANDS}
      )
    ENDIF()
  else()
    # Python dependencies
    IF(PYTHON_DEPS)
      install_bundled_directory(
          DESCRIPTION "extra Python dependencies"
          CONTENTS "${PYTHON_DEPS}"
          DESTINATION "${INSTALL_LIBDIR}/lib/python${PYTHONLIBS_MAJOR_MINOR}/site-packages"
          TARGET mariadb-shell
      )
    ENDIF()
  endif()
ENDIF(HAVE_PYTHON)

IF(BUNDLED_SSH_DIR)
  GET_TARGET_PROPERTY(_SSH_LIBRARY_LOCATION ssh LOCATION)
  GET_FILENAME_COMPONENT(_SSH_LIBRARY_DIR ${_SSH_LIBRARY_LOCATION} DIRECTORY)

  IF(WIN32)
    FILE(GLOB_RECURSE _SSH_LIBRARIES "${_SSH_LIBRARY_DIR}/*.dll")
  ELSEIF(APPLE)
    FILE(GLOB_RECURSE _SSH_LIBRARIES "${_SSH_LIBRARY_DIR}/*.dylib")
  ELSE()
    FILE(GLOB_RECURSE _SSH_LIBRARIES "${_SSH_LIBRARY_DIR}/*.so*")
  ENDIF()

  IF(NOT _SSH_LIBRARIES)
    MESSAGE(FATAL_ERROR "Could not find the bundled libssh libraries")
  ENDIF()

  install_bundled_binaries(
      DESCRIPTION "SSH libraries"
      BINARIES ${_SSH_LIBRARIES}
      DESTINATION "${INSTALL_LIBDIR}"
      TARGET mariadb-shell
  )
ENDIF()

# Bundle the full runtime dependency closure for vcpkg builds.
#
# With a vcpkg toolchain the dependencies (curl, zlib, libssh, antlr4, zstd and
# anything they pull in transitively) are resolved through find_package and only
# staged next to the build output at build time - they never make it into the
# install/CPack tree, so the produced package would be missing them. This is the
# Windows/macOS equivalent of the BUNDLED_*_DIR options and of the DEB/RPM
# dependency metadata used on Linux: it makes the package self-contained.
#
# vcpkg keeps every runtime artifact it built for the active triplet in one
# per-triplet directory (Windows: <triplet>/bin holds the DLLs, a sibling of the
# import-library <triplet>/lib; macOS/Linux: <triplet>/lib holds the dylibs/.so,
# which are the link libraries themselves; 'debug/...' for a debug build). That
# directory *is* the dependency closure, so rather than enumerating individual
# names (which silently misses new/transitive deps) we install all of it and drop
# only the pieces another bundling path already handles: OpenSSL (BUNDLED_OPENSSL),
# curl+zlib (WITH_CURL_PATH), libssh (BUNDLED_SSH_DIR) and Python (its own
# runtime layout above). Globbing only vcpkg's own tree also means we never pick
# up system libraries (glibc on Linux, /usr/lib and /System frameworks on macOS),
# which must stay unbundled.
#
# This covers dynamic triplets on all three platforms. A static triplet (vcpkg's
# default on Linux, x64-linux) links the dependencies into mariadb-shell directly, so
# the per-triplet lib dir has only .a files, the glob below finds nothing, and
# this is correctly a no-op - the binary is already self-contained.
#
# Shared-library resolution relies on the loader finding the bundled copies next
# to (Windows) or via the executable's rpath (macOS '@executable_path/..',
# Linux '$ORIGIN/..' - both set in add_shell_executable). vcpkg builds its
# dylibs/.so with @rpath / SONAME install names, and install_bundled_binaries
# rewrites each copy's id/rpath ($ORIGIN via patchelf on Linux, @rpath via
# install_name_tool on macOS) so the closure resolves within INSTALL_LIBDIR.
IF(USING_VCPKG AND (WIN32 OR APPLE OR LINUX))
  IF(WIN32)
    SET(_vcpkg_lib_glob "*.dll")
    SET(_vcpkg_dest "${INSTALL_BINDIR}")   # DLLs sit next to mariadb-shell.exe
    SET(_vcpkg_ext_re "\\.dll")
  ELSEIF(APPLE)
    SET(_vcpkg_lib_glob "*.dylib")
    SET(_vcpkg_dest "${INSTALL_LIBDIR}")   # dylibs resolve via the exe's rpath
    SET(_vcpkg_ext_re "\\.dylib")
  ELSE() # LINUX
    SET(_vcpkg_lib_glob "*.so*")
    SET(_vcpkg_dest "${INSTALL_LIBDIR}")   # .so resolve via the exe's $ORIGIN rpath
    # Match unversioned and versioned SONAMEs: libfoo.so, libfoo.so.1, libfoo.so.1.2
    SET(_vcpkg_ext_re "\\.so(\\..*)?")
  ENDIF()

  # vcpkg installs two closures per triplet: release under <triplet>/lib
  # (<triplet>/bin on Windows) and debug under <triplet>/debug/lib
  # (<triplet>/debug/bin). A Debug build links the debug one, so that is the one
  # to bundle - the configuration must not be picked from whichever tree a given
  # find module happened to resolve. Most ports keep the same file name in both
  # trees, so bundling the release closure into a Debug build appears to work
  # while silently loading release copies; curl does not (vcpkg names its debug
  # library libcurl-d / curl-d), so the loader aborts the binary before main with
  # "Library not loaded: @rpath/libcurl-d.4.dylib". Multi-config generators leave
  # CMAKE_BUILD_TYPE empty and get the release closure: FILE(GLOB) below runs at
  # configure time, so there is no per-config choice left to make here.
  IF(CMAKE_BUILD_TYPE STREQUAL "Debug")
    SET(_vcpkg_config_subdir "debug/")
  ELSE()
    SET(_vcpkg_config_subdir "")
  ENDIF()

  # Locate the per-triplet prefix. bootstrap_vcpkg.cmake exports it; a build
  # given the vcpkg toolchain directly may not have it, so fall back to deriving
  # it from any resolved vcpkg library by stripping the '[debug/]lib' tail.
  # Unquoted expansion skips the debug/optimized keyword tokens some *_LIBRARY
  # vars carry. The 'ssh' imported target is included because it points straight
  # at its own runtime library when the *_LIBRARY vars are empty.
  SET(_vcpkg_runtime_dir "")
  SET(_vcpkg_prefix "${VCPKG_INSTALLED_PREFIX}")
  IF(NOT _vcpkg_prefix)
    SET(_vcpkg_dep_libs ${CURL_LIBRARY} ${ANTLR4_LIBRARY} ${CRYPTO_LIBRARY}
                        ${OPENSSL_LIBRARY} ${ZLIB_LIBRARY} ${ZSTD_LIBRARY})
    IF(TARGET ssh)
      GET_TARGET_PROPERTY(_ssh_loc ssh LOCATION)
      IF(_ssh_loc)
        LIST(APPEND _vcpkg_dep_libs "${_ssh_loc}")
      ENDIF()
    ENDIF()

    FOREACH(_dep_lib ${_vcpkg_dep_libs})
      IF(EXISTS "${_dep_lib}")
        GET_FILENAME_COMPONENT(_dep_lib_dir "${_dep_lib}" DIRECTORY)
        IF(_dep_lib_dir MATCHES "^(.+)/debug/(lib|bin)$")
          SET(_vcpkg_prefix "${CMAKE_MATCH_1}")
        ELSEIF(_dep_lib_dir MATCHES "^(.+)/(lib|bin)$")
          SET(_vcpkg_prefix "${CMAKE_MATCH_1}")
        ELSE()
          # unrecognized layout - use the directory as found
          SET(_vcpkg_runtime_dir "${_dep_lib_dir}")
        ENDIF()
        BREAK()
      ENDIF()
    ENDFOREACH()
  ENDIF()

  IF(_vcpkg_prefix)
    # On Windows the link libraries are the import libs under <triplet>/lib and
    # the DLLs are the sibling bin dir; elsewhere lib/ holds the dylibs/.so
    # themselves, which are also the link libraries.
    IF(WIN32)
      SET(_vcpkg_runtime_dir "${_vcpkg_prefix}/${_vcpkg_config_subdir}bin")
    ELSE()
      SET(_vcpkg_runtime_dir "${_vcpkg_prefix}/${_vcpkg_config_subdir}lib")
    ENDIF()
  ENDIF()

  IF(_vcpkg_runtime_dir AND IS_DIRECTORY "${_vcpkg_runtime_dir}")
    FILE(GLOB _vcpkg_all_libs "${_vcpkg_runtime_dir}/${_vcpkg_lib_glob}")

    # Name patterns (regex, anchored on the full file name) bundled through
    # another code path. Python always has its own block regardless of where it
    # was found. ${_vcpkg_ext_re} matches the platform's library suffix,
    # including versioned Linux SONAMEs (e.g. .so.3).
    SET(_vcpkg_exclude_patterns "python.*${_vcpkg_ext_re}"
                                "libpython.*${_vcpkg_ext_re}")
    IF(BUNDLED_OPENSSL)
      LIST(APPEND _vcpkg_exclude_patterns "libcrypto.*${_vcpkg_ext_re}"
                                          "libssl.*${_vcpkg_ext_re}")
    ENDIF()
    IF(BUNDLED_SSH_DIR)
      LIST(APPEND _vcpkg_exclude_patterns "libssh.*${_vcpkg_ext_re}"
                                          "ssh.*${_vcpkg_ext_re}")
    ENDIF()
    IF(WITH_CURL_PATH)
      LIST(APPEND _vcpkg_exclude_patterns "libcurl.*${_vcpkg_ext_re}"
                                          "curl.*${_vcpkg_ext_re}"
                                          "libz.*${_vcpkg_ext_re}"
                                          "zlib.*${_vcpkg_ext_re}"
                                          "z${_vcpkg_ext_re}")
    ENDIF()

    SET(_vcpkg_bundle_libs "")
    FOREACH(_lib ${_vcpkg_all_libs})
      GET_FILENAME_COMPONENT(_lib_name "${_lib}" NAME)
      SET(_skip FALSE)
      FOREACH(_pat ${_vcpkg_exclude_patterns})
        IF(_lib_name MATCHES "^${_pat}$")
          SET(_skip TRUE)
          BREAK()
        ENDIF()
      ENDFOREACH()
      IF(NOT _skip)
        LIST(APPEND _vcpkg_bundle_libs "${_lib}")
      ENDIF()
    ENDFOREACH()

    IF(_vcpkg_bundle_libs)
      MESSAGE(STATUS "Bundling vcpkg runtime libraries from "
                     "'${_vcpkg_runtime_dir}': ${_vcpkg_bundle_libs}")
      IF(WIN32)
        # DLLs need no id/rpath rewriting; a plain install is enough.
        INSTALL(PROGRAMS ${_vcpkg_bundle_libs}
                DESTINATION "${_vcpkg_dest}" COMPONENT main)
      ELSE()
        # Route dylibs/.so through the shared bundler so each copy is made
        # relocatable (macOS: '@rpath/<name>' id via install_name_tool; Linux:
        # '$ORIGIN/..' rpath via patchelf) and its cross-references rewritten -
        # matching every other BUNDLED_* path on macOS and Linux.
        install_bundled_binaries(
            DESCRIPTION "vcpkg runtime libraries"
            BINARIES ${_vcpkg_bundle_libs}
            DESTINATION "${_vcpkg_dest}"
            TARGET mariadb-shell
        )
      ENDIF()
    ELSE()
      MESSAGE(STATUS "No vcpkg runtime libraries to bundle from "
                     "'${_vcpkg_runtime_dir}' (all handled by other paths)")
    ENDIF()
  ELSE()
    MESSAGE(STATUS "vcpkg build: could not resolve a per-triplet runtime dir; "
                   "assuming statically linked dependencies")
  ENDIF()
ENDIF()

# krb5 is an indirect dependency for libssh, platforms that bundle libssh should may optionally
# require a custom built version of the krb5 library to be also bundled in the shell packages
# to prevent the shell from having different openssl dependencies (system one/bundled one)
IF(BUNDLED_KRB5_DIR)
  IF(WIN32)
    SET(_KRB5_LIB_DIR "${BUNDLED_KRB5_DIR}/bin")
    SET(_KRB5_BUNDLED_LIBS
      "krb5_64.dll"
      "k5sprt64.dll"
      "comerr64.dll"
      "xpprof64.dll"
      "krbcc64.dll"
      "gssapi64.dll"
    )
  ELSE()
    SET(_KRB5_LIB_DIR "${BUNDLED_KRB5_DIR}/lib")
    SET(_KRB5_BUNDLED_LIBS
      "libkrb5.*"
      "libcom_err.*"
      "libgssapi_krb5.*"
      "libk5crypto.*"
      "libkrb5support.*"
    )
  ENDIF()

  SET(_KRB5_LIBRARIES "")

  FOREACH(_pattern ${_KRB5_BUNDLED_LIBS})
    FILE(GLOB _libs "${_KRB5_LIB_DIR}/${_pattern}")

    IF(NOT _libs)
      MESSAGE(FATAL_ERROR "Could not find the bundled krb5 library: ${_pattern}")
    ENDIF()

    LIST(APPEND _KRB5_LIBRARIES ${_libs})
  ENDFOREACH()

  install_bundled_binaries(
      DESCRIPTION "Kerberos libraries"
      BINARIES ${_KRB5_LIBRARIES}
      DESTINATION "${INSTALL_LIBDIR}"
      TARGET mariadb-shell
  )
ENDIF()

IF(BUNDLED_SASL_DIR)
  IF(WIN32)
    FILE(GLOB _SASL_LIBRARIES "${BUNDLED_SASL_DIR}/lib/lib*.dll")
    FILE(GLOB _SASL_PLUGINS "${BUNDLED_SASL_DIR}/lib/sasl*.dll")
  ELSEIF(APPLE)
    FILE(GLOB _SASL_LIBRARIES "${BUNDLED_SASL_DIR}/lib/libsasl2*.dylib")
    # plugins have .so extension
    FILE(GLOB _SASL_PLUGINS "${BUNDLED_SASL_DIR}/lib/sasl2/lib*.so")
  ELSE()
    FILE(GLOB _SASL_LIBRARIES "${BUNDLED_SASL_DIR}/lib/libsasl2.so*")
    FILE(GLOB _SASL_PLUGINS "${BUNDLED_SASL_DIR}/lib/sasl2/lib*.so*")
  ENDIF()

  IF(NOT _SASL_LIBRARIES)
    MESSAGE(FATAL_ERROR "Could not find the bundled sasl libraries")
  ENDIF()

  install_bundled_binaries(
      DESCRIPTION "SASL libraries"
      BINARIES ${_SASL_LIBRARIES}
      DESTINATION "${INSTALL_LIBDIR}"
      TARGET mariadb-shell
  )
  install_bundled_binaries(
      DESCRIPTION "SASL plugins"
      BINARIES ${_SASL_PLUGINS}
      DESTINATION "${INSTALL_LIBDIR}/sasl2"
      TARGET mariadb-shell
  )
ENDIF()

IF(BUNDLED_ANTLR_DIR)
  if(WIN32)
    file(GLOB_RECURSE _ANTLR4_SHARED_LIB "${ANTLR4_LIB_BASE_DIRECTORY}/*.dll")
  elseif(APPLE)
    file(GLOB_RECURSE _ANTLR4_SHARED_LIB "${ANTLR4_LIB_BASE_DIRECTORY}/*.dylib")
  else()
    file(GLOB _ANTLR4_SHARED_LIB "${ANTLR4_LIBRARY}.*")
  endif()

  install_bundled_binaries(
      DESCRIPTION "ANTLR libraries"
      BINARIES ${_ANTLR4_SHARED_LIB}
      DESTINATION "${INSTALL_LIBDIR}"
      TARGET mariadb-shell
      WRITE_RPATH FALSE
  )
ENDIF()

# If defined, installs the client authentication plugins
IF(AUTH_CLIENT_PLUGINS)
  IF(AUTH_CLIENT_PLUGINS_DEPS)
    SET(AUTH_CLIENT_PLUGINS_DEPS_OPTIONS)
    IF(LINUX_UBUNTU AND NOT BUNDLED_OPENSSL)
      # Do not add an RPATH to libfido2 itself; debugedit corrupts this
      # patched DSO in dh_strip on Ubuntu builds using the system OpenSSL
      # libraries.
      LIST(APPEND AUTH_CLIENT_PLUGINS_DEPS_OPTIONS WRITE_RPATH FALSE)
    ENDIF()

    install_bundled_binaries(
        DESCRIPTION "MySQL authentication plugins - dependencies"
        BINARIES ${AUTH_CLIENT_PLUGINS_DEPS}
        DESTINATION "${INSTALL_LIBDIR}"
        TARGET mariadb-shell
    )
  ENDIF()

  install_bundled_binaries(
      DESCRIPTION "MySQL authentication plugins"
      BINARIES ${AUTH_CLIENT_PLUGINS}
      DESTINATION "${INSTALL_MYSQL_PLUGINS_DIR}"
      TARGET mariadb-shell
  )
ELSE()
  MESSAGE(WARNING "Could not find Authentication Client Plugins!")
ENDIF()

IF(BUNDLED_PROTOBUF_LIBRARY)
  install_bundled_binaries(
      DESCRIPTION "Protobuf library"
      BINARIES ${BUNDLED_PROTOBUF_LIBRARY}
      DESTINATION "${INSTALL_LIBDIR}"
      TARGET mariadb-shell
  )

  IF(BUNDLED_ABSEIL_LIBRARIES)
    install_bundled_binaries(
        DESCRIPTION "abseil libraries"
        BINARIES ${BUNDLED_ABSEIL_LIBRARIES}
        DESTINATION "${INSTALL_LIBDIR}"
        TARGET mariadb-shell
    )
  ENDIF()
ENDIF()

IF (JIT_EXECUTOR_LIB)
  SET(INSTALL_JIT_EXECUTOR_LIBRARY "${JIT_EXECUTOR_LIBRARY}")
  IF (WIN32)
    GET_FILENAME_COMPONENT(LIB_JIT_EXECUTOR_NAME_WE "${JIT_EXECUTOR_LIBRARY}" NAME_WE)
    GET_FILENAME_COMPONENT(LIB_JIT_EXECUTOR_DIR "${JIT_EXECUTOR_LIBRARY}" DIRECTORY)
    SET(INSTALL_JIT_EXECUTOR_LIBRARY "${LIB_JIT_EXECUTOR_DIR}/${LIB_JIT_EXECUTOR_NAME_WE}.dll")
  ENDIF()
  install_bundled_binaries(
      DESCRIPTION "Jit Executor library"
      BINARIES "${INSTALL_JIT_EXECUTOR_LIBRARY}"
      DESTINATION "${INSTALL_LIBDIR}"
      TARGET mariadb-shell
      WRITE_RPATH FALSE
  )
ENDIF()
