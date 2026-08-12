#!/bin/sh
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

#
# MariaDB Shell installer.
#
#   curl -fsSL https://github.com/mariadb-corporation/mariadb-shell/raw/main/install.sh | bash
#
# Detects the local OS, CPU and (on Linux) glibc version, picks the matching
# package from the release, verifies its checksum and unpacks it.
#
# POSIX sh on purpose -- this runs on whatever the target has, which may be a
# minimal container with dash and no bash. No arrays, no [[ ]], no local.
#
# Environment overrides:
#   MARIADB_SHELL_TAG     install a specific release tag instead of the newest
#   MARIADB_SHELL_PREFIX  where to unpack        (default $HOME/.local/share/mariadb-shell)
#   MARIADB_SHELL_BINDIR  where to symlink       (default $HOME/.local/bin)
#   MARIADB_SHELL_REPO    owner/repo to install from
#
set -eu

REPO="${MARIADB_SHELL_REPO:-mariadb-corporation/mariadb-shell}"
PREFIX="${MARIADB_SHELL_PREFIX:-$HOME/.local/share/mariadb-shell}"
BINDIR="${MARIADB_SHELL_BINDIR:-$HOME/.local/bin}"

# With no tag pinned, resolve through /releases/latest/download/ -- a permanent
# URL GitHub redirects to the newest non-prerelease release. Nothing here needs
# to know the version, so this script never goes stale.
if [ -n "${MARIADB_SHELL_TAG:-}" ]; then
  BASE="https://github.com/$REPO/releases/download/$MARIADB_SHELL_TAG"
  RELEASE_DESC="$MARIADB_SHELL_TAG"
else
  BASE="https://github.com/$REPO/releases/latest/download"
  RELEASE_DESC="latest"
fi

die() { echo "install.sh: $*" >&2; exit 1; }
info() { echo "==> $*"; }

need() {
  command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

need curl
need tar
need awk

# ---------------------------------------------------------------------------
# Platform tokens, matching cmake/packaging.cmake:
#   Linux   -> linux-glibc<ver>-<arch>
#   macOS   -> macos<major>-<arch>
#   arch    -> x86-64bit | arm-64bit
# ---------------------------------------------------------------------------
detect_arch() {
  case "$(uname -m)" in
    x86_64|amd64)          echo "x86-64bit" ;;
    aarch64|arm64|armv8*)  echo "arm-64bit" ;;
    *) die "unsupported CPU architecture: $(uname -m)" ;;
  esac
}

# The local "version" that packages are compared against: the glibc version on
# Linux, the macOS major version on Darwin. Both follow the same compatibility
# rule -- a package built against an older one runs on a newer one.
detect_os_and_version() {
  case "$(uname -s)" in
    Linux)
      OS="linux"
      # getconf is the same source cmake reads at package time. ldd is the
      # fallback for images where getconf is absent.
      LOCAL_VER=$(getconf GNU_LIBC_VERSION 2>/dev/null | awk '{print $2}')
      if [ -z "${LOCAL_VER:-}" ]; then
        LOCAL_VER=$(ldd --version 2>/dev/null | awk 'NR==1{print $NF}')
      fi
      [ -n "${LOCAL_VER:-}" ] || die "could not determine the glibc version (musl is not supported)"
      ;;
    Darwin)
      OS="macos"
      LOCAL_VER=$(sw_vers -productVersion 2>/dev/null | awk -F. '{print $1}')
      [ -n "${LOCAL_VER:-}" ] || die "could not determine the macOS version"
      ;;
    *)
      die "unsupported operating system: $(uname -s) (Windows: download the .zip/.tar.gz from the release page)"
      ;;
  esac
}

# Dotted numeric comparison: is $1 <= $2? Field-by-field so 2.9 < 2.34, which a
# lexical compare gets backwards -- exactly the case that matters for glibc.
ver_le() {
  awk -v a="$1" -v b="$2" '
    BEGIN {
      na = split(a, x, "."); nb = split(b, y, ".");
      n = (na > nb ? na : nb);
      for (i = 1; i <= n; i++) {
        xi = (i <= na ? x[i] + 0 : 0);
        yi = (i <= nb ? y[i] + 0 : 0);
        if (xi < yi) { exit 0 }
        if (xi > yi) { exit 1 }
      }
      exit 0
    }'
}

ARCH=$(detect_arch)
detect_os_and_version
info "Detected: $OS $LOCAL_VER, $ARCH"

# ---------------------------------------------------------------------------
# SHA256SUMS is the manifest. Reading the asset list from the same file that
# carries the checksums means there is no separate index to drift out of sync.
# ---------------------------------------------------------------------------
TMP=$(mktemp -d)
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT INT TERM

info "Fetching package list from the $RELEASE_DESC release"
curl -fsSL --retry 3 -o "$TMP/SHA256SUMS" "$BASE/SHA256SUMS" \
  || die "could not download $BASE/SHA256SUMS -- is there a published release?"

# Select the best candidate: every package whose platform version is <= the
# local one is compatible; take the highest such version. Built-older-runs-newer
# is why this is a range match and not an equality match -- a glibc 2.34 package
# is the right answer on a glibc 2.39 host when that is all that is published.
BEST_FILE=""
BEST_VER=""
while read -r _sum name; do
  [ -n "${name:-}" ] || continue
  case "$name" in
    *"-$ARCH.tar.gz") ;;
    *) continue ;;
  esac

  # Pull the platform version out of the filename.
  case "$OS" in
    linux) cand=$(echo "$name" | sed -n "s/.*-linux-glibc\([0-9.]*\)-$ARCH\.tar\.gz$/\1/p") ;;
    macos) cand=$(echo "$name" | sed -n "s/.*-macos\([0-9]*\)-$ARCH\.tar\.gz$/\1/p") ;;
  esac
  [ -n "${cand:-}" ] || continue

  # Too new for this host: built against a newer glibc/SDK than we have.
  ver_le "$cand" "$LOCAL_VER" || continue

  if [ -z "$BEST_VER" ] || ver_le "$BEST_VER" "$cand"; then
    BEST_VER="$cand"
    BEST_FILE="$name"
  fi
done < "$TMP/SHA256SUMS"

if [ -z "$BEST_FILE" ]; then
  echo "install.sh: no compatible package for $OS $LOCAL_VER / $ARCH." >&2
  echo "Available packages in the $RELEASE_DESC release:" >&2
  awk '{print "  " $2}' "$TMP/SHA256SUMS" >&2
  exit 1
fi

info "Selected $BEST_FILE"

# ---------------------------------------------------------------------------
# Download and verify
# ---------------------------------------------------------------------------
info "Downloading"
curl -fL --retry 3 --progress-bar -o "$TMP/$BEST_FILE" "$BASE/$BEST_FILE"

info "Verifying checksum"
EXPECTED=$(awk -v f="$BEST_FILE" '$2 == f {print $1}' "$TMP/SHA256SUMS")
[ -n "$EXPECTED" ] || die "no checksum for $BEST_FILE in SHA256SUMS"

if command -v sha256sum >/dev/null 2>&1; then
  ACTUAL=$(sha256sum "$TMP/$BEST_FILE" | awk '{print $1}')
elif command -v shasum >/dev/null 2>&1; then
  ACTUAL=$(shasum -a 256 "$TMP/$BEST_FILE" | awk '{print $1}')
else
  die "neither sha256sum nor shasum available; cannot verify the download"
fi

[ "$ACTUAL" = "$EXPECTED" ] || die "checksum mismatch for $BEST_FILE
  expected: $EXPECTED
  actual:   $ACTUAL"

# ---------------------------------------------------------------------------
# Unpack. The tarball contains a single top-level mariadb-shell-<ver>-<platform>
# directory; install it under PREFIX and point a stable 'current' symlink at it,
# so an upgrade is atomic from the PATH's point of view and the previous version
# stays on disk.
# ---------------------------------------------------------------------------
info "Unpacking into $PREFIX"
mkdir -p "$PREFIX"
tar -xzf "$TMP/$BEST_FILE" -C "$PREFIX"

TOPDIR=$(tar -tzf "$TMP/$BEST_FILE" | awk -F/ 'NF>1 {print $1; exit}')
[ -n "${TOPDIR:-}" ] || die "unexpected tarball layout in $BEST_FILE"
[ -d "$PREFIX/$TOPDIR" ] || die "expected $PREFIX/$TOPDIR after unpacking"

ln -sfn "$PREFIX/$TOPDIR" "$PREFIX/current"

SHELL_BIN="$PREFIX/current/bin/mariadb-shell"
[ -x "$SHELL_BIN" ] || die "no executable at $SHELL_BIN after unpacking"

mkdir -p "$BINDIR"
ln -sfn "$SHELL_BIN" "$BINDIR/mariadb-shell"

info "Installed $("$SHELL_BIN" --version 2>/dev/null || echo "$TOPDIR")"
info "Binary: $BINDIR/mariadb-shell -> $SHELL_BIN"

# Only a hint, never an edit: rewriting a user's shell rc from a piped installer
# is not this script's call to make.
case ":$PATH:" in
  *":$BINDIR:"*) ;;
  *)
    echo ""
    echo "$BINDIR is not on your PATH. Add it with:"
    echo ""
    echo "    export PATH=\"$BINDIR:\$PATH\""
    echo ""
    ;;
esac
