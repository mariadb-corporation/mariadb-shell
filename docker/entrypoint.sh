#!/usr/bin/env bash
# Copyright (c) 2026, MariaDB Corporation.
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

# Single source of truth: cmake configures it, cpack must package the same one
BUILD_TYPE="${BUILD_TYPE:-Release}"

# Bundled Python built from source. bootstrap_python.cmake probes PYTHON_ROOT
# for a ready Python-<version> install and skips the (slow) build when it finds
# one, so mounting just that install from a persistent cache is all it takes to
# reuse an interpreter across runs. The CPython checkout it would otherwise
# clone lands here too, but only on a rebuild, and is not worth caching.
PYTHON_VERSION="${PYTHON_VERSION:-3.14.6}"
PYTHON_ROOT="/workspace/python"

# Bind-mounted directories (the source tree, the cached vcpkg checkout) keep
# their host ownership, which is not root -- and git refuses to touch a
# repository owned by another user. This container is a disposable build
# sandbox running as root, so trust every path rather than enumerating the
# mounts: a missed one fails deep inside a cmake bootstrap, far from the cause.
git config --global --add safe.directory '*'

echo "==> OS: $(grep '^PRETTY_NAME=' /etc/os-release | cut -d'=' -f2 | tr -d '"')"
echo "==> Ninja Version: $(ninja --version)"
echo "==> Using CPU Cores: $(nproc)"

# 1. Generate Ninja build files
cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DWITH_TESTS=0 \
  -DWITH_PYTHON_SOURCE="$PYTHON_VERSION" \
  -DPYTHON_INSTALL_ROOT="$PYTHON_ROOT" \
  -DWITH_VCPKG_TRIPLET="${WITH_VCPKG_TRIPLET}"

# 2. Build directly with Ninja (parallelized across all CPU cores)
ninja -C "$BUILD_DIR" -j $(nproc)
echo "==> Ninja build finished successfully."

# 3. Export binary (cpack writes the package to its CWD, i.e. $BUILD_DIR)
cd "$BUILD_DIR"
cpack -G TGZ -C "$BUILD_TYPE"
echo "==> Packaging finished successfully."

# 4. Publish the package to the output mount & declare its name for downstream
#    consumers (e.g. CI artifact naming)
shopt -s nullglob
pkgs=("$BUILD_DIR"/*.tar.gz)
if [ ${#pkgs[@]} -ne 1 ]; then
  echo "==> Expected exactly one tarball, found ${#pkgs[@]}: ${pkgs[*]}" >&2
  exit 1
fi
mv "${pkgs[0]}" "$OUTPUT_DIR/"
basename "${pkgs[0]}" .tar.gz > "$OUTPUT_DIR/package-name.txt"
echo "==> Package: $(< "$OUTPUT_DIR/package-name.txt")"

