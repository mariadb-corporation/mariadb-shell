# Build Instructions for MariaDB Shell


## 1. Prerequisites

The following sections describe the required tooling and dependencies required to build the MariaDB Shell on each platform.

### Debian

$ sudo apt install sudo apt install build-essential git cmake libssh-dev python3-dev libssl-dev libantlr4-runtime-dev rapidjson-dev googletest libgtest-dev libgmock-dev libcurl4-openssl-dev libzstd-dev patchelf

### Fedora

$ sudo dnf install gcc-c++ git cmake libssh-devel python3-devel openssl-devel antlr4-cpp-runtime-devel rapidjson-devel gtest-devel gmock-devel libcurl-devel libzstd-devel patchelf

### MacOS

$ xcode-select --install
$ /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
$ brew install cmake bison libssh openssl@3 python@3.14 antlr4-cpp-runtime rapidjson googletest curl zstd
$ echo 'export PATH="/opt/homebrew/opt/bison/bin:$PATH"' >> ~/.zshrc
$ source ~/.zshrc

### Windows

- Install Visual Studio, at least the Community Edition, specifically Desktop development with C++
- Install python 3.14 from python.org

Using a Developer Command Prompt:

> winget install -e --id WinFlexBison.win_flex_bison
> git clone https://github.com/microsoft/vcpkg.git
> cd vcpkg
> bootstrap-vcpkg.bat
> .\vcpkg install openssl[tools] zlib libssh openssl zlib antlr4 rapidjson gtest curl[ssh,openssl] zstd --triplet=<triplet>


Where in <triplet> we usually pick any of:
- x64-windows (64-bit Windows, dynamic/DLL linking - default on 64-bit Windows)
- arm64-windows (64-bit ARM Windows, dynamic linking)

### 2. Check Out the Sources

```bash
git clone --depth 1 https://github.com/mariadb-corporation/mariadb-shell.git
```

### 3. Configure the project

The shell has some dependencies with the MariaDB server, at this point the MariaDB server will be cloned
from github at and the required artifacts for the shell will be built, then the shell project will be
configured.

## Linux

$ cd mariadb-shell && mkdir bld && cd bld
$ cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo

## MacOS

$ cd mariadb-shell && mkdir bld && cd bld
$ cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH="$(brew --prefix openssl@3);$(brew --prefix curl)"


## Windows

> cd mariadb-shell && mkdir bld && cd bld
> cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_TOOLCHAIN_FILE="<vcpkg-root-folder>/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=<triplet>

Where:
  <vcpkg-root-folder>: is the path where you cloned vcpkg in step 1
  <triplet>: is the triplet used to install the build dependencies with vcpkg in step 1

When the vcpkg toolchain file and target triplet are supplied, the build detects
it and automatically bundles the full runtime dependency closure (OpenSSL,
libssh, antlr4, curl, zlib, zstd and anything they depend on) into the generated
package, so it is self-contained without any system package manager. This is the
Windows/macOS equivalent of the `-DBUNDLED_*_DIR` options and of the DEB/RPM
dependency metadata used on Linux.

### 4. Build the project

## Linux/MacOS

$ cmake --build . -j$(nproc)

# Windows

> cmake --build . --parallel


## 5. Optional Configuration

## Pre-built MariaDB Server

The process of cloning and building the MariaDB Server dependencies may be skipped by providing a
a custom source and build directories using:

-DMARIADB_SOURCE_DIR=<path-to-server-source>
-DMARIADB_BUILD_DIR=<path-to-server-build>




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
Copyright (c) 2026, MariaDB Corporation.
