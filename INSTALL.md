# Build Instructions for MariaDB Shell



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

*** Debian ***

```
$ sudo apt install build-essential git cmake curl
```

*** Fedora ***

```
$ sudo dnf install gcc-c++ git cmake
```

*** MacOS ***

```
# Install the build system
$ xcode-select --install

# Install brew
$ /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install cmake and bison
$ brew install cmake bison pkg-config

# Update PATH
echo 'export PATH="$(brew --prefix bison)/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

*** Windows ***

Install Visual Studio, at least the Community Edition, specifically Desktop
development with C++.

Perform a standard install of Python 3.14 from python.org. or optionally
download the embeddable package and and use it on step 2.1.


## 2. Building Portable Packages

This is the simplest build but the slowest, it includes automatic download and
building of the required dependencies, for this reason, network access is
assumed.

### 2.1 Clone the MariaDB Shell Repository

This process is exactly the same in any platform, just make sure that in windows
all the build steps are executed in a Developer Command Prompt.

```
$ git clone --depth 1 https://github.com/mariadb-corporation/mariadb-shell.git
```

### 2.1 Configure the project

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


*** Linux/Macos ***
```
$ mkdir bld && cd bld
$ cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWITH_PYTHON_SOURCE=3.14.6 -DWITH_VCPKG_TRIPLET=<triplet>
```

*** Windows ***

If you installed the standard python package, use the following:
```
> mkdir bld && cd bld
> cmake .. -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWITH_VCPKG_TRIPLET=<triplet>
```

If you went for the embeddable package, unpack it and also add the following cmake
parameter:

`-DBUNDLED_PYTHON_DIR=<path-to-embeddable-root`

### 2.3 Build the MariaDB Shell

After the previous step completes, we are ready to build MariaDB Shell:

*** Linux/MacOS ***
```
cmake --build . -j$(nproc)
```

*** Windows ***
```
cmake --build . --parallel



==== REST IS WIP ====




## 1. Prerequisites

The following sections describe the required tooling and dependencies required to build the MariaDB Shell on each platform.

### Debian

$ sudo apt install sudo apt install build-essential git cmake libssh-dev python3-dev libssl-dev libantlr4-runtime-dev rapidjson-dev googletest libgtest-dev libgmock-dev libcurl4-openssl-dev libzstd-dev patchelf

### Fedora

$ sudo dnf install gcc-c++ git cmake libssh-devel python3-devel openssl-devel antlr4-cpp-runtime-devel rapidjson-devel gtest-devel gmock-devel libcurl-devel libzstd-devel patchelf

### Linux with vcpkg (portable build)

The system-package setup above is the recommended path for distribution packages:
the resulting DEB/RPM declare their dependencies and the system package manager
keeps them (OpenSSL, curl, ...) patched. To instead produce a **portable** build
that does not rely on matching system libraries, use vcpkg:

```
$ git clone https://github.com/microsoft/vcpkg.git && ./vcpkg/bootstrap-vcpkg.sh
$ ./vcpkg/vcpkg install openssl[tools] zlib libssh antlr4 rapidjson gtest curl[ssh,openssl] zstd --triplet=<triplet>
```

Where <triplet> is `x64-linux` / `arm64-linux` (static - vcpkg's default on Linux;
dependencies are linked into `mysqlsh`, nothing to bundle) or the `*-dynamic`
variant (shared libraries that are bundled into the package, as on Windows/macOS).
Either way the build is self-contained; with vcpkg you then own shipping security
updates for those dependencies. `patchelf` is still required for the dynamic case.

### MacOS

Two dependency sources are supported:

- **Homebrew** - the recommended setup for local development. The shell links
  against the libraries in your Homebrew prefix and runs against them in place;
  this build is **not** meant to be redistributed (its dependencies live in the
  Homebrew kegs on your machine, and bundling third-party Homebrew bottles into
  an official package is not something we do).
- **vcpkg** - use this to produce a **self-contained, redistributable** package.
  Like the Windows build, the dependencies are built by vcpkg and automatically
  bundled into the package (see step 3). Install them with:

  ```
  $ git clone https://github.com/microsoft/vcpkg.git && ./vcpkg/bootstrap-vcpkg.sh
  $ ./vcpkg/vcpkg install openssl[tools] zlib libssh antlr4 rapidjson gtest curl[ssh,openssl] zstd --triplet=<triplet>
  ```

  Where <triplet> is one of `arm64-osx` (Apple Silicon) or `x64-osx` (Intel).

For a Homebrew-based build:

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

## Linux (system packages)

$ cd mariadb-shell && mkdir bld && cd bld
$ cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo

## Linux (vcpkg, portable)

$ cd mariadb-shell && mkdir bld && cd bld
$ cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_TOOLCHAIN_FILE="<vcpkg-root-folder>/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=<triplet>

## MacOS (Homebrew, local development)

$ cd mariadb-shell && mkdir bld && cd bld
$ cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH="$(brew --prefix openssl@3);$(brew --prefix curl)"

## MacOS (vcpkg, redistributable)

$ cd mariadb-shell && mkdir bld && cd bld
$ cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_TOOLCHAIN_FILE="<vcpkg-root-folder>/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=<triplet>


## Windows

> cd mariadb-shell && mkdir bld && cd bld
> cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_TOOLCHAIN_FILE="<vcpkg-root-folder>/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=<triplet>

Where:
  <vcpkg-root-folder>: is the path where you cloned vcpkg in step 1
  <triplet>: is the triplet used to install the build dependencies with vcpkg in step 1

## Any platform (vcpkg, auto-bootstrapped)

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

## Bundled Python from source (Unix/macOS)

To ship a self-contained interpreter instead of relying on the system Python,
point the build at a Python to build and it will configure, build and install it
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

#### Third-party Python packages in the bundle

To ship extra Python packages inside the bundle, have the bundled interpreter
install them itself (so any C extensions match its exact ABI and OpenSSL):

```bash
-DPYTHON_DEPS_PACKAGES="certifi;PyYAML==6.0"
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
**defaults to `certifi;pyyaml;antlr4-python3-runtime;mcp`** — the packages the bundled shell plugins need.
Override with your own list, or pass `-DPYTHON_DEPS_PACKAGES=` (empty) to install
none.

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
