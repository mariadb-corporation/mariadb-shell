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
# bootstrap_python_windows.cmake
#
# The Windows half of WITH_PYTHON_SOURCE, included by bootstrap_python.cmake
# once it has resolved the CPython source tree (_py_src), its version
# (_py_version, _py_mm) and the install directory (_py_install).
#
# CPython is built with its own Visual Studio projects (PCbuild), but against
# the shell's vcpkg tree rather than the copies of OpenSSL, SQLite, zlib, ...
# that PCbuild would download: every extension module then loads the same
# library, at the same version, as the shell itself, and a dependency is
# updated in one place (vcpkg.json) on every platform. PCbuild's projects are
# left untouched: cmake/python-windows/vcpkg-deps.props repoints its library
# properties and vcpkg-deps.targets, hooked into every project, swaps the
# bundled sources for vcpkg's import libraries.
#
# Tcl/Tk (tkinter, IDLE, turtle) and the test modules are not built: nothing in
# the shell or its plugins uses them.
#
# PC\layout, CPython's own packaging script, then turns the build output into
# an install tree (python.exe, include\, libs\python<XY>.lib, Lib\, DLLs\),
# which the top-level CMakeLists.txt builds against and src/CMakeLists.txt
# bundles, exactly as it did a python.org install.
#
##############################################################################

# vcpkg is not optional here: without it there is nothing to build against.
# VCPKG_INSTALLED_PREFIX comes from bootstrap_vcpkg.cmake; a build given the
# vcpkg toolchain directly gets the conventional per-triplet prefix instead,
# as on Unix (which exists at this point only if an earlier configure filled it).
SET(_py_vcpkg "${VCPKG_INSTALLED_PREFIX}")
IF(NOT _py_vcpkg AND VCPKG_TARGET_TRIPLET AND CMAKE_TOOLCHAIN_FILE MATCHES "[Vv]cpkg")
  SET(_py_vcpkg "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")
ENDIF()
IF(NOT _py_vcpkg OR NOT EXISTS "${_py_vcpkg}/include")
  MESSAGE(FATAL_ERROR
    "WITH_PYTHON_SOURCE on Windows builds CPython against the vcpkg tree, but "
    "none was found. Configure with -DWITH_VCPKG_TRIPLET=<triplet>.")
ENDIF()

# PCbuild's platform name and output folder for the triplet's architecture.
IF(VCPKG_TARGET_TRIPLET MATCHES "^arm64-")
  SET(_py_platform "ARM64")
  SET(_py_pcbuild_out "arm64")
ELSEIF(VCPKG_TARGET_TRIPLET MATCHES "^x64-")
  SET(_py_platform "x64")
  SET(_py_pcbuild_out "amd64")
ELSE()
  MESSAGE(FATAL_ERROR
    "WITH_PYTHON_SOURCE on Windows: no CPython platform for the vcpkg triplet "
    "'${VCPKG_TARGET_TRIPLET}' (supported: x64-*, arm64-*).")
ENDIF()
SET(_py_pcbuild_dir "${_py_src}/PCbuild/${_py_pcbuild_out}")

##############################################################################
# 1. vcpkg's import libraries. Looked up by the names vcpkg's ports have used,
#    so a port renaming one fails here, by name, rather than as a link error
#    deep in the CPython build.
##############################################################################
FUNCTION(_PY_VCPKG_LIB out_var)
  FOREACH(_name ${ARGN})
    IF(EXISTS "${_py_vcpkg}/lib/${_name}")
      SET(${out_var} "${_py_vcpkg}/lib/${_name}" PARENT_SCOPE)
      RETURN()
    ENDIF()
  ENDFOREACH()
  STRING(REPLACE ";" ", " _names "${ARGN}")
  MESSAGE(FATAL_ERROR
    "WITH_PYTHON_SOURCE: none of ${_names} is in ${_py_vcpkg}/lib. Is the port "
    "in vcpkg.json enabled for Windows? With the vcpkg toolchain passed directly "
    "(-DCMAKE_TOOLCHAIN_FILE), vcpkg installs a newly added port only at "
    "project(), after this step: configure with -DWITH_VCPKG_TRIPLET=<triplet> "
    "instead, which installs the ports first.")
ENDFUNCTION()

_PY_VCPKG_LIB(_py_lib_ffi    ffi.lib libffi.lib)
_PY_VCPKG_LIB(_py_lib_zlib   libz.lib zlib.lib z.lib)
_PY_VCPKG_LIB(_py_lib_bz2    bz2.lib libbz2.lib)
_PY_VCPKG_LIB(_py_lib_lzma   lzma.lib liblzma.lib)
_PY_VCPKG_LIB(_py_lib_zstd   zstd.lib libzstd.lib)
_PY_VCPKG_LIB(_py_lib_sqlite sqlite3.lib)
_PY_VCPKG_LIB(_py_lib_mpdec  libmpdec.lib mpdec.lib)
_PY_VCPKG_LIB(_py_lib_crypto libcrypto.lib)
_PY_VCPKG_LIB(_py_lib_ssl    libssl.lib)

# PCbuild hardcodes two import library names. A copy of vcpkg's under that name
# is all it takes: the DLL a module ends up importing is recorded inside the
# import library, not taken from its file name.
SET(_py_stage "${_py_src}/PCbuild/vcpkg-stage")
FILE(MAKE_DIRECTORY "${_py_stage}")
CONFIGURE_FILE("${_py_lib_ffi}"  "${_py_stage}/libffi-8.lib" COPYONLY)
CONFIGURE_FILE("${_py_lib_zlib}" "${_py_stage}/zlib-ng.lib"  COPYONLY)
FILE(WRITE "${_py_stage}/undef_build_libmpdec.h" "#undef BUILD_LIBMPDEC\n")

##############################################################################
# 2. MSBuild. On PATH in a Visual Studio developer environment; otherwise found
#    through vswhere, which every Visual Studio installation provides.
##############################################################################
FIND_PROGRAM(PYTHON_MSBUILD NAMES MSBuild msbuild
  DOC "MSBuild used to build CPython (WITH_PYTHON_SOURCE on Windows)")
IF(NOT PYTHON_MSBUILD)
  SET(_vswhere "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer/vswhere.exe")
  IF(EXISTS "${_vswhere}")
    EXECUTE_PROCESS(
      COMMAND "${_vswhere}" -latest -products * -requires Microsoft.Component.MSBuild
              -find "MSBuild\\**\\Bin\\MSBuild.exe"
      OUTPUT_VARIABLE _msbuild_found
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    STRING(REGEX REPLACE "[\r\n].*" "" _msbuild_found "${_msbuild_found}")
    IF(_msbuild_found AND EXISTS "${_msbuild_found}")
      SET(PYTHON_MSBUILD "${_msbuild_found}" CACHE FILEPATH
        "MSBuild used to build CPython (WITH_PYTHON_SOURCE on Windows)" FORCE)
    ENDIF()
  ENDIF()
ENDIF()
IF(NOT PYTHON_MSBUILD)
  MESSAGE(FATAL_ERROR
    "WITH_PYTHON_SOURCE on Windows needs MSBuild (Visual Studio with the C++ "
    "workload). Configure from a Visual Studio developer prompt.")
ENDIF()

# Build with the toolset the developer environment selected. vcvarsall
# -vcvars_ver (ilammy/msvc-dev-cmd's toolset input in CI) exports it as
# VCToolsVersion; without the matching PlatformToolset, a newer Visual Studio's
# MSBuild would build CPython with its own default toolset instead of the one
# the shell and the vcpkg ports are built with.
SET(_py_toolset_arg "")
IF("$ENV{VCToolsVersion}" MATCHES "^14\\.([0-9])")
  IF(CMAKE_MATCH_1 GREATER_EQUAL 5)
    SET(_py_toolset_arg "/p:PlatformToolset=v145")
  ELSE()
    SET(_py_toolset_arg "/p:PlatformToolset=v143")
  ENDIF()
ENDIF()

##############################################################################
# 3. Build. Always the Release configuration, whatever CMAKE_BUILD_TYPE is: a
#    Debug CPython is a different ABI (python<XY>_d.dll, _d-suffixed modules)
#    that neither the bundling nor the pip-installed packages expect, and the
#    python.org install this replaces was a Release build too.
##############################################################################
INCLUDE(ProcessorCount)
PROCESSORCOUNT(_nproc)
IF(_nproc EQUAL 0)
  SET(_nproc 1)
ENDIF()

FILE(TO_NATIVE_PATH "${_py_vcpkg}/" _n_vcpkg)
FILE(TO_NATIVE_PATH "${_py_stage}/" _n_stage)
FILE(TO_NATIVE_PATH "${_py_src}/PCbuild/vcpkg-none/" _n_none)
FILE(TO_NATIVE_PATH "${CMAKE_CURRENT_LIST_DIR}/python-windows/vcpkg-deps.props" _n_props)
FILE(TO_NATIVE_PATH "${CMAKE_CURRENT_LIST_DIR}/python-windows/vcpkg-deps.targets" _n_targets)

MESSAGE(STATUS "Building Python ${_py_version} (PCbuild, ${_py_platform}, against vcpkg)...")
EXECUTE_PROCESS(
  COMMAND "${PYTHON_MSBUILD}" "${_py_src}/PCbuild/pcbuild.proj"
          /t:Build /m:${_nproc} /nologo /v:m
          /p:Configuration=Release "/p:Platform=${_py_platform}"
          ${_py_toolset_arg}
          # The external-library modules (_ssl, _sqlite3, _bz2, ...) are built
          # only with IncludeExternals; calling msbuild directly rather than
          # build.bat is what keeps them from being downloaded.
          /p:IncludeExternals=true
          /p:IncludeSSL=true /p:IncludeCTypes=true
          /p:IncludeTkinter=false /p:IncludeTests=false /p:IncludeUwp=false
          "/p:ExternalProps=${_n_props}"
          "/p:ForceImportAfterCppTargets=${_n_targets}"
          "/p:PyVcpkgDir=${_n_vcpkg}"
          "/p:PyDepsStage=${_n_stage}"
          "/p:PyDepsNone=${_n_none}"
          "/p:PyLibBz2=${_py_lib_bz2}"
          "/p:PyLibZstd=${_py_lib_zstd}"
          "/p:PyLibSqlite3=${_py_lib_sqlite}"
          "/p:PyLibMpdec=${_py_lib_mpdec}"
          "/p:PyLibLzma=${_py_lib_lzma}"
  WORKING_DIRECTORY "${_py_src}"
  RESULT_VARIABLE _rc)
IF(NOT _rc EQUAL 0)
  MESSAGE(FATAL_ERROR "Python build (PCbuild) failed (see output above)")
ENDIF()

##############################################################################
# 4. The vcpkg DLLs the build output needs, next to it in PCbuild's output
#    folder. Windows looks for an extension module's dependencies in the
#    module's own folder (Python loads them with LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR;
#    PATH is not searched), and for python<XY>.dll's -- zlib, linked into the
#    core -- beside python<XY>.dll. PC\layout (step 5) imports ctypes, so they
#    are needed before it runs, and it then carries every DLL of the folder into
#    the install tree's DLLs\.
#
#    The modules' imports, as patterns: the names carry versions (libmpdec-4.0.1,
#    libcrypto-3-x64), and the module check at the end fails loudly if one is
#    missed.
##############################################################################
SET(_py_module_dlls
    "bz2*.dll" "ffi*.dll" "libcrypto-3*.dll" "libssl-3*.dll" "liblzma*.dll"
    "libmpdec*.dll" "sqlite3.dll" "zstd*.dll")
FOREACH(_pat ${_py_module_dlls})
  FILE(GLOB _dlls "${_py_vcpkg}/bin/${_pat}")
  IF(NOT _dlls)
    MESSAGE(FATAL_ERROR "WITH_PYTHON_SOURCE: no ${_pat} in ${_py_vcpkg}/bin")
  ENDIF()
  FILE(COPY ${_dlls} DESTINATION "${_py_pcbuild_dir}")
ENDFOREACH()
FILE(GLOB _zlib_dlls "${_py_vcpkg}/bin/libz*.dll" "${_py_vcpkg}/bin/zlib*.dll")
IF(NOT _zlib_dlls)
  MESSAGE(FATAL_ERROR "WITH_PYTHON_SOURCE: no zlib DLL in ${_py_vcpkg}/bin")
ENDIF()
FILE(COPY ${_zlib_dlls} DESTINATION "${_py_pcbuild_dir}")

##############################################################################
# 5. Lay out the install tree with CPython's own packaging script, run by the
#    freshly built interpreter. It puts python<XY>.dll at the root and every
#    other DLL in DLLs\, so zlib gets a second copy at the root.
#
#    In the shell's package, Lib\ and DLLs\ are bundled under lib\Python<X.Y>
#    and python<XY>.dll into bin\, next to the shell's own copies of the same
#    vcpkg DLLs (the vcpkg closure); a running shell has usually loaded those
#    already, and Windows reuses a loaded DLL of the same name.
##############################################################################
FILE(REMOVE_RECURSE "${_py_install}")
MESSAGE(STATUS "Laying out Python ${_py_version} into ${_py_install}...")
EXECUTE_PROCESS(
  COMMAND "${_py_pcbuild_dir}/python.exe" "${_py_src}/PC/layout"
          --source "${_py_src}" --build "${_py_pcbuild_dir}"
          --arch "${_py_pcbuild_out}" --copy "${_py_install}"
          --include-stable --include-dev --include-pip --include-venv
  WORKING_DIRECTORY "${_py_src}"
  RESULT_VARIABLE _rc)
IF(NOT _rc EQUAL 0 OR NOT EXISTS "${_py_install}/python.exe")
  MESSAGE(FATAL_ERROR "Python layout (PC\\layout) failed (see output above)")
ENDIF()
FILE(COPY ${_zlib_dlls} DESTINATION "${_py_install}")

MESSAGE(STATUS "Checking the built Python for required extension modules...")
_PY_REQUIRE_MODULES("${_py_install}" "${_py_mm}")
