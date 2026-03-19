# Build Instructions for MySQL Shell

## 1. Prerequisites

### Required tools and libraries
- cmake 3.5.1 (downgrade if a newer release causes issues)
- gcc 14, Visual Studio 2019, or clang 10
- zip
- python 3.10+
- libssh 0.9.2
- openssl 3.x
- antlr4-runtime (C++ runtime ≥ 4.10)
- git (if you checkout from github instead of using source packages)

#### Suggested packages for EL9-like distributions
```bash
sudo yum install cmake gcc-toolset-14 openssl-devel ncurses-devel rpcgen python3-devel libssh-devel libcurl-devel patchelf
```

Please adjust the specific package name for python3 to ensure version 3.10 or newer is installed.

You might also need to enable the developer EPEL repo for patchelf:

```bash
sudo dnf config-manager --enable ol9_developer_EPEL
```

#### Suggested packages for Ubuntu 24.04 LTS
```bash
sudo apt update
sudo apt install build-essential cmake ninja-build \
    python3-dev python3-venv python3-pip \
    libssl-dev libssh-dev libcurl4-openssl-dev \
    libprotobuf-dev protobuf-compiler \
    liblz4-dev zlib1g-dev pkg-config patchelf \
    libantlr4-runtime-dev
```

#### Build and install the ANTLR4 runtime
Many Linux distributions do not currently ship a new enough ANTLR4 C++ runtime, so
if yours doesn't, build it from source before configuring CMake.

```bash
mkdir -p ~/mysql-src
cd ~/mysql-src
git clone --depth 1 --branch 4.13.1 --sparse \
  https://github.com/antlr/antlr4.git antlr4-runtime
cd antlr4-runtime
git sparse-checkout set runtime/Cpp
mkdir -p build
cd build
scl enable gcc-toolset-14 -- cmake ../runtime/Cpp \
  -DCMAKE_BUILD_TYPE=Release -DANTLR_BUILD_CPP_TESTS=OFF
scl enable gcc-toolset-14 -- cmake --build . --parallel $(nproc --all)
make install DESTDIR=~/mysql-src
```

### Optional
- GraalVM 23.0.1 (for JavaScript support)

## 2. Check Out the Sources

Place the MySQL Server and MySQL Shell repositories under the same parent
directory so the relative paths used below remain valid.

```bash
mkdir -p ~/mysql-src
cd ~/mysql-src
git clone --depth 1 --branch mysql-9.7.0 https://github.com/mysql/mysql-server.git mysql-server
git clone --depth 1 --branch 9.7.0 https://github.com/mysql/mysql-shell.git mysql-shell
```

You may also use source archives for a specific version from
https://dev.mysql.com/downloads/mysql/ and https://dev.mysql.com/downloads/shell/.

## 3. Prepare the MySQL Server Build

The MySQL Shell relies on the MySQL client libraries. Build them once before
configuring the shell.

1. Create a dedicated build directory:
```bash
mkdir ~/mysql-src/mysql-server/bld
cd ~/mysql-src/mysql-server/bld
```
2. Configure the server:
```bash
cmake .. -DWITH_AUTHENTICATION_CLIENT_PLUGINS=YES -DWITH_TIRPC=bundled
```
3. Compile:
```bash
cmake --build . --parallel $(nproc --all)
```
or if you'd like to build only what's needed by MySQL Shell:
```bash
cmake --build . --parallel $(nproc --all) --target mysqlclient
cmake --build . --parallel $(nproc --all) --target mysqlxclient
cmake --build . --parallel $(nproc --all) --target mysqlxclient_lite
cmake --build . --parallel $(nproc --all) --target libprotobuf-lite
cmake --build . --parallel $(nproc --all) --target mysql_config_editor
cmake --build . --parallel $(nproc --all) --target mysql_binlog_event_standalone
cmake --build . --parallel $(nproc --all) --target mysqlbinlog
cmake --build . --parallel $(nproc --all) --target routing_guidelines-objects
cmake --build . --parallel $(nproc --all) --target mysql_native_password # optional
```

## 4. Build MySQL Shell

### Linux and macOS

1. Create the build directory:
```bash
mkdir ~/mysql-src/mysql-shell/bld
cd ~/mysql-src/mysql-shell/bld
```
2. Configure the project:
```bash
cmake .. \
    -DMYSQL_SOURCE_DIR=`pwd`/../../mysql-server \
    -DMYSQL_BUILD_DIR=`pwd`/../../mysql-server/bld \
    -DBUNDLED_ANTLR_DIR=~/mysql-src/usr/local\
    -DHAVE_PYTHON=1
```
3. Build and install the shell:
```bash
cmake --build . --parallel $(nproc --all)
sudo make install
```

### Windows

1. Create the build directory:
```bash
mkdir bld
cd bld
```
2. Configure with Visual Studio:
```bash
cmake .. -G "Visual Studio 16" \
    -DMYSQL_SOURCE_DIR=../../mysql-server \
    -DMYSQL_BUILD_DIR=../../mysql-server/bld \
    -DBUNDLED_ANTLR_DIR=../../antlr4-runtime/build \
    -DHAVE_PYTHON=1 \
    -DPYTHON_LIBRARY=<path_to_python_library> \
    -DPYTHON_INCLUDE_DIR=<python_src>/include
```
3. Open `mysh.sln` in Visual Studio 2019 and build the desired configuration.

All Windows dependencies must be compiled with matching settings (Release vs.
Debug and /MD vs. /MT).

## 5. Optional Features

### JavaScript support

1. Build the native Polyglot API library by following `ext/polyglot/README.txt`.
2. Provide the compiled library and include paths to CMake:
```bash
-DJIT_EXECUTOR_LIB=<path_to_polyglot_api>
```

Note: GraalVM 23.0.1 is required; newer releases may not be compatible.

### Unit tests

Enable unit test targets by adding the following option during configuration:
```bash
-DWITH_TESTS=1
```

### Additional Python modules

Note that if you'd like to install additional Python modules, you must install
them in the Python runtime directories that MySQL Shell was compiled with.
To make sure that's the case, execute `pip` from mysqlsh itself. 

Example:
```bash
mysqlsh --pym pip install certifi PyYAML
```


Copyright (c) 2016, 2026, Oracle and/or its affiliates.
