#!/usr/bin/env bash
# Copyright (c) 2026, MariaDB plc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335 USA

set -e

SRC_DIR="/workspace/src"
BUILD_DIR="/workspace/bld"
OUTPUT_DIR="/workspace/output"
ARCH="${SANDBOX_ARCH:?SANDBOX_ARCH not set}"
CCACHE_DIR="${CCACHE_DIR:-/workspace/ccache}"

# Package name, computed up front so the install step below can use it as the
# staged directory name directly (see step 3).
#
# Mirrors cmake/packaging.cmake's MYSH_VERSION/MYSH_PLATFORM scheme from the
# shell build (shell-release.yml) and the macOS/Windows sandbox-server.yml
# "Compute Package Version & Platform" step: <product>-<version>-<platform>,
# with a trailing "-sandbox" tag distinguishing this from a regular server
# package. The version comes from the server source's own VERSION file
# (MYSQL_VERSION_MAJOR/MINOR/PATCH/EXTRA, same formula as
# cmake/mysql_version.cmake).
MAJOR=$(grep '^MYSQL_VERSION_MAJOR=' "$SRC_DIR/VERSION" | cut -d= -f2)
MINOR=$(grep '^MYSQL_VERSION_MINOR=' "$SRC_DIR/VERSION" | cut -d= -f2)
PATCH=$(grep '^MYSQL_VERSION_PATCH=' "$SRC_DIR/VERSION" | cut -d= -f2)
EXTRA=$(grep '^MYSQL_VERSION_EXTRA=' "$SRC_DIR/VERSION" | cut -d= -f2 || true)
PKG_VERSION="${MAJOR}.${MINOR}.${PATCH}${EXTRA}"

case "$ARCH" in
  x86) PKG_ARCH="x86-64bit" ;;
  arm) PKG_ARCH="arm-64bit" ;;
  *) echo "Unknown SANDBOX_ARCH: $ARCH" >&2; exit 1 ;;
esac
GLIBC_VER=$(getconf GNU_LIBC_VERSION | grep -oE '[0-9]+\.[0-9]+')

PKG_NAME="mariadb-${PKG_VERSION}-linux-glibc${GLIBC_VER}-${PKG_ARCH}-sandbox"

# Bind-mounted directories keep their host ownership, which is not root -- and
# git refuses to touch a repository owned by another user. This container is a
# disposable build sandbox running as root, so trust every path rather than
# enumerating the mounts.
git config --global --add safe.directory '*'

echo "==> OS: $(grep '^PRETTY_NAME=' /etc/os-release | cut -d'=' -f2 | tr -d '"')"
echo "==> Ninja Version: $(ninja --version)"
echo "==> Using CPU Cores: $(nproc)"

ccache --set-config=cache_dir="$CCACHE_DIR"
ccache --set-config=max_size=15G

# 1. Generate Ninja build files
cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DWITH_UNIT_TESTS=OFF \
  -DWITH_EMBEDDED_SERVER=OFF \
  -DWITH_WSREP=OFF \
  -DWITHOUT_DYNAMIC_PLUGINS=OFF \
  -DPLUGIN_ROCKSDB=NO \
  -DPLUGIN_MROONGA=NO \
  -DPLUGIN_CONNECT=NO \
  -DPLUGIN_OQGRAPH=NO \
  -DWITH_MARIABACKUP=OFF \
  -DPLUGIN_DUCKDB=NO \
  -DCOMPILATION_COMMENT="Development Sandbox" \
  -DWITH_PCRE=bundled

# 2. Build the minimal server, then the sandbox plugins/tools it needs on top
cmake --build "$BUILD_DIR" --parallel "$(nproc)" --target minbuild

cmake --build "$BUILD_DIR" --parallel "$(nproc)" --target \
  archive blackhole disks federated federatedx aria_ftdump aria_chk aria_pack \
  aria_read_log aria_dump_log sphinx spider auth_ed25519 auth_mysql_sha2 \
  auth_parsec file_key_management locales handlersocket mariadbd-safe-helper \
  metadata_lock_info password_reuse_check query_cache_info query_response_time \
  server_audit simple_password_check sql_errlog type_mysql_json innochecksum \
  resolveip

echo "==> Build finished successfully."

# 3. Install the Server component under a DESTDIR, then hand-copy the one
#    binary the component omits (my_print_defaults), exactly as the tested
#    procedure does on bare-metal Linux.
DESTDIR="$BUILD_DIR/install_root" cmake --install "$BUILD_DIR" --component Server
cp "$BUILD_DIR/extra/my_print_defaults" "$BUILD_DIR/install_root/usr/local/mysql/bin"

mv "$BUILD_DIR/install_root/usr/local/mysql" "$BUILD_DIR/$PKG_NAME"

# 4. Package and publish to the output mount, declaring the package name for
#    downstream consumers (e.g. CI artifact naming).
cd "$BUILD_DIR"
tar -czvf "${PKG_NAME}.tar.gz" "$PKG_NAME"
echo "==> Packaging finished successfully."

mv "${PKG_NAME}.tar.gz" "$OUTPUT_DIR/"
echo "$PKG_NAME" > "$OUTPUT_DIR/package-name.txt"
echo "==> Package: $(< "$OUTPUT_DIR/package-name.txt")"
