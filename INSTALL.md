# Build Instructions for MariaDB Shell

This file describes the build instructions for the MariaDB Shell project on the
different platforms.

The requirements depend on the kind of package to be built, the sections below
contain the instructions for the different packages based on complexity,
including:

- Portable Package
- Developer Package

Independently of the build method, the following build dependencies are required
on each platform:

## 1. Build Tooling

The following build tooling is required on each platform.

**Debian**
```bash
sudo apt install build-essential git cmake ninja-build curl bison zip unzip tar pkg-config libncurses-dev patchelf
```

**Fedora**
```bash
sudo dnf install gcc-c++ git cmake ninja-build perl-core bison zip unzip tar pkg-config ncurses-devel patchelf
```

**MacOS**
```bash
# Install the build system
xcode-select --install

# Install brew
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install cmake and bison
brew install cmake ninja bison pkg-config ncurses

# Update PATH
echo 'export PATH="$(brew --prefix bison)/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

**Windows**

Install Visual Studio, at least the Community Edition, specifically Desktop
development with C++.

Install Python 3.14 using the MSI installer from www.python.org


## 2 Clone the MariaDB Shell Repository

This process is exactly the same in any platform, just make sure that in windows
all the build steps are executed in a Developer Command Prompt.

```bash
git clone --depth 1 https://github.com/mariadb-corporation/mariadb-shell.git
```


## 3. Building Portable Packages

This is the simplest build but the slowest, it includes automatic download and
building of the required dependencies, for this reason, network access is
assumed.

### 3.1 Configure the project

This is the crucial step to get the dev environment set, as it will automatically:

- Download and bootstrap the vcpkg package manager.
- Download the source code, build and install the required libraries to build
  the MariaDB Shell.
- Download the source code and build the Python package to be bundled in the
  final Packages
- Download the MariaDB Server source code and build the build dependencies.

On the following cmake call, the value of triplet to value that corresponds to
the platform where the MariaDB Shell is being built:

* x64-windows
* arm64-windows
* x64-osx-dynamic
* arm64-osx-dynamic
* x64-linux-dynamic
* arm64-linux-dynamic


**Linux/Macos**
```bash
mkdir bld && cd bld
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWITH_PYTHON_SOURCE=3.14.6 -DWITH_VCPKG_TRIPLET=<triplet>
```

**Windows**

Using a *Developer Command Prompt* for VS execute the following:

```bash
rem Unset VCPKG_ROOT to avoid messing up with the standard vcpkg path on the
rem installed Visual Studio
set VCPKG_ROOT=

mkdir bld && cd bld

rem Using Ninja is on  purpose, avoid conflicts resulting from the
rem build paths resulting from the multi-configuration nature of MSBuild
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWITH_VCPKG_TRIPLET=<triplet>
```

### 3.2 Build the MariaDB Shell

After the previous step completes, we are ready to build MariaDB Shell:

**all platforms**
```bash
cmake --build . --parallel
```

### 3.3 Create the Portable Package

Execute the following command on the bld directory to create a portable tar.gz
package.

**all platforms**
```bash
cpack -G TGZ -C RelWithDebInfo
```


## 4. Building Development Packages

The development packages are meant to work using dependencies available on the
system used to build the MariaDB shell, these builds are not meant to be
portable, but only to be used in a development environment

### 4.1 Additional Build Dependencies

**Debian**

```bash
$ sudo apt install build-essential git cmake libssh-dev python3-dev libssl-dev libantlr4-runtime-dev rapidjson-dev googletest libgtest-dev libgmock-dev libcurl4-openssl-dev libzstd-dev patchelf
```

**Fedora**

```bash
$ sudo dnf install gcc-c++ git cmake libssh-devel python3-devel openssl-devel antlr4-cpp-runtime-devel rapidjson-devel gtest-devel gmock-devel libcurl-devel libzstd-devel patchelf
```

**MacOS**

```bash
$ brew install libssh openssl@3 python@3.14 antlr4-cpp-runtime rapidjson googletest curl zstd
```

**Windows**

In windows, this package is identical to the portable one.


### 4.2. Configure the project

The shell has some dependencies with the MariaDB server, at this point the MariaDB server will be cloned
from github at and the required artifacts for the shell will be built, then the shell project will be
configured.

**Linux**

```bash
$ cd mariadb-shell && mkdir bld && cd bld
$ cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

**MacOS**

```bash
$ cd mariadb-shell && mkdir bld && cd bld
$ cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH="$(brew --prefix openssl@3);$(brew --prefix curl)"
```

### 4.3. Build the project

**Linux/MacOS**

```bash
$ cmake --build . -j$(nproc)
```

**Windows**

```bash
> cmake --build . --parallel
```

## 5. Optional Configuration

There are several options to change what is built and the way it is built, the
following are some examples:

### 5.1 Disabling Test Build

By default, the MariaDB Shell builds the test suite, but that can be disabled by
passing the `-DWITH_TESTS=0` to the cmake configure call.

### 5.2 Using Custom Builds for Dependencies

It is possible to use custom builds of different dependencies as described below:

**Using a custom build of the MariaDB Server

Clone the specific version of the MariaDB server, configure and build the
required dependencies and pass the source and build paths to the configure
cmake call

```bash
my-mariadb-src/bld $ cmake --build . --target mariadbclient mysys mysys_ssl caching_sha2_password GenError
my-shell-src/bld $ cmake .. -DMARIADB_SOURCE_DIR=my-mariadb-src -DMARIADB_BUILD_DIR=my-mariadb-src/bld ....
```

**Using a custom build of openssl, python, antlr4

Just as above, get the package of the dependency to be used, and unpack it,
handle it to the cmake configure call as follows:

```bash
my-shell-src/bld $ cmake .. -DBUNDLED_PYTHON_DIR=<path-to-python> \
                            -DBUNDLED_OPENSSL_DIR=<path-to-openssl> \
                            -BUNDLED_SSH_DIR=<path-to-openssl> \
                            ...
```

## 6. Configuration Details

## 6.1 Any platform (vcpkg, auto-bootstrapped)

If you don't want to clone and bootstrap vcpkg yourself (step 1), pass only the
triplet and the build will fetch vcpkg from github, run its bootstrap script,
and derive the toolchain file and triplet for you:

$ cd mariadb-shell && mkdir bld && cd bld
$ cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWITH_VCPKG_TRIPLET=<triplet>

vcpkg is cloned to `../vcpkg` (next to the shell source) by default; set
`-DVCPKG_ROOT=<path>` to clone/reuse it elsewhere (an existing checkout, or the
`VCPKG_ROOT` environment variable, is reused instead of re-cloning). This runs
once and is cached. Passing `-DCMAKE_TOOLCHAIN_FILE`/`-DVCPKG_TARGET_TRIPLET`
explicitly still works and takes precedence over `-DWITH_VCPKG_TRIPLET`.

When the vcpkg toolchain file and target triplet are supplied, on any platform,
the build detects it and makes the package self-contained: a dynamic triplet
automatically bundles the full runtime dependency closure (OpenSSL, libssh,
antlr4, curl, zlib, zstd and anything they depend on), and a static triplet
(vcpkg's default on Linux) links them into `mysqlsh` directly so there is
nothing to bundle. This is the equivalent of the `-DBUNDLED_*_DIR` options and,
on Linux, an alternative to the DEB/RPM dependency metadata used by the
system-package build. (A Homebrew-based macOS build does not bundle: it is
intended to run locally against the Homebrew libraries.)


## 6.2. Bundled Python from source (Unix/macOS)

To build and ship a self-contained interpreter of a specific version tell the
build at what Python to build and it will configure, build and install it
for you, then bundle it:

```bash
-DWITH_PYTHON_SOURCE=<path-or-version>
```

`WITH_PYTHON_SOURCE` accepts either:

- a **path** to an existing CPython source tree, or
- a **version**: `X.Y.Z` fetches the release tag `v<X.Y.Z>`, and `X.Y` fetches the
  maintenance branch (latest patch of that series), from
  `github.com/python/cpython`.

The build installs Python into `Python-<version>` (e.g. `Python-3.12.4`) next to
the shell source and sets `-DBUNDLED_PYTHON_DIR` to that folder automatically.
The install prefix (and, for the version form, the cloned source) is treated as a
cache: it is built once and reused on later configures — delete it (or set
`-DPYTHON_INSTALL_ROOT=<dir>` to relocate it) to force a rebuild.

When combined with a vcpkg build (`-DWITH_VCPKG_TRIPLET` or an explicit vcpkg
toolchain), Python is configured *after* the vcpkg dependencies are installed
and links its extension modules (`_ssl`, `zlib`, …) against the vcpkg closure
rather than the system libraries. The build type (`-DCMAKE_BUILD_TYPE`) is honoured
for the interpreter's optimization/debug-info flags. Passing `-DBUNDLED_PYTHON_DIR`
explicitly still works and takes precedence over `-DWITH_PYTHON_SOURCE`. This is
Unix/macOS only; on Windows use a python.org install (auto-detected) or a
pre-built `-DBUNDLED_PYTHON_DIR`.

## 6.3. Third-party Python packages in the bundle

To ship extra Python packages inside the bundle, have the bundled interpreter
install them itself (so any C extensions match its exact ABI and OpenSSL):

```bash
-DPYTHON_DEPS_PACKAGES="certifi;PyYAML;"
```

At configure time the bundled interpreter runs `pip install` into a staging
directory (the source Python is never mutated), and that directory is then
bundled into the packaged interpreter's `site-packages` via the same path as
`PYTHON_DEPS` — so it is picked up automatically at startup. This works for every
bundled build: **Windows** (the auto-detected python.org install) and
**Linux/macOS** (`BUNDLED_PYTHON_DIR` / `WITH_PYTHON_SOURCE`). It needs network
access to PyPI, and the set is cached — packages are (re)installed only when the
list changes.

`PYTHON_DEPS_PACKAGES` and `PYTHON_DEPS` are alternatives (both target
`site-packages`); set at most one. For a **system** (non-bundled) Python build,
use `-DPYTHON_DEPS=<dir>` (a pre-populated folder that is copied in).

When a bundled Python is in use and you don't set `PYTHON_DEPS_PACKAGES`, it
**defaults to `certifi;pyyaml;antlr4-python3-runtime;mcp`** — the packages the
bundled shell plugins need. Override with your own list, or pass
`-DPYTHON_DEPS_PACKAGES=` (empty) to install none.

## 6.4. Additional Python modules

Note that if you'd like to install additional Python modules, you must install
them in the Python runtime directories that MySQL Shell was compiled with.
To make sure that's the case, execute `pip` from mysqlsh itself.

Example:

```bash
mysqlsh --pym pip install debugpy
```

Copyright (c) 2016, 2026, Oracle and/or its affiliates.
Copyright (c) 2026, MariaDB Corporation.
