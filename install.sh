#!/bin/sh
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
#   MARIADB_SHELL_REPO    owner/repo to install from; it has to be public
#
# Options:
#   --pre-release  install the newest release even if it is a prerelease
#
set -eu

REPO="${MARIADB_SHELL_REPO:-mariadb-corporation/mariadb-shell}"
PREFIX="${MARIADB_SHELL_PREFIX:-$HOME/.local/share/mariadb-shell}"
BINDIR="${MARIADB_SHELL_BINDIR:-$HOME/.local/bin}"

# printf, not echo: these messages carry their own newlines, and echo is free to
# mangle what it is given on some shells.
die() { printf 'install.sh: %s\n' "$*" >&2; exit 1; }
info() { echo "==> $*"; }

need() {
  command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

# ---------------------------------------------------------------------------
# Options. Piped into a shell, these arrive via bash's -s:
#   curl -fsSL <url> | bash -s -- --pre-release
# ---------------------------------------------------------------------------
ENABLE_PRERELEASE=0
while [ $# -gt 0 ]; do
  case "$1" in
    --pre-release) ENABLE_PRERELEASE=1 ;;
    -h|--help)
      cat <<'USAGE'
MariaDB Shell installer.

  install.sh [--pre-release]
  curl -fsSL <url> | bash -s -- [--pre-release]

  --pre-release  install the newest release even if it is a prerelease.
                 Without it, prereleases are skipped, exactly as
                 /releases/latest/ skips them.

Environment: MARIADB_SHELL_TAG, MARIADB_SHELL_PREFIX, MARIADB_SHELL_BINDIR,
MARIADB_SHELL_REPO. A pinned tag wins over --pre-release, since it
already names the release to install.
USAGE
      exit 0 ;;
    *) die "unknown option: $1 (try --help)" ;;
  esac
  shift
done

if [ "$ENABLE_PRERELEASE" = 1 ]; then
  PRERELEASE_HINT="Prereleases are already enabled. Name a release outright if
  the one you want is not the newest:

      export MARIADB_SHELL_TAG=<tag>"
else
  PRERELEASE_HINT="A prerelease is never 'latest'. Reach the newest one with
  --pre-release, or name a release outright:

      export MARIADB_SHELL_TAG=<tag>"
fi

# A pinned tag and the latest stable release are both reachable by a plain
# download URL: /releases/latest/download/ is a permanent URL GitHub redirects to
# the newest non-prerelease release, so nothing here needs to know the version
# and this script never goes stale. Only the newest prerelease has to be looked
# up first, because it is invisible to /releases/latest: the releases are listed
# through the GitHub API and the newest one taken. That needs the listing
# parsed, hence a JSON reader; the other two paths need no such thing.
RESOLVE_PRERELEASE=0
if [ -n "${MARIADB_SHELL_TAG:-}" ]; then
  BASE="https://github.com/$REPO/releases/download/$MARIADB_SHELL_TAG"
  RELEASE_DESC="$MARIADB_SHELL_TAG"
elif [ "$ENABLE_PRERELEASE" = 1 ]; then
  RESOLVE_PRERELEASE=1
  BASE=""   # known once the release is resolved
  RELEASE_DESC="newest, prereleases included"
else
  BASE="https://github.com/$REPO/releases/latest/download"
  RELEASE_DESC="latest"
fi

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

# Reduces the release listing, newest first, to the tag of the newest release
# that is not a draft.
newest_tag() {
  if command -v jq >/dev/null 2>&1; then
    jq -er '[.[] | select(.draft == false)][0].tag_name'
  elif command -v python3 >/dev/null 2>&1; then
    python3 -c 'import json,sys
print(next(r for r in json.load(sys.stdin) if not r["draft"])["tag_name"])'
  else
    die "this install needs jq or python3 to read the release listing"
  fi
}

# Every release is public, so once the tag is known the assets are downloaded by
# name like on the other paths -- the GitHub API is only asked for the listing.
# Its anonymous requests are limited to 60 an hour per address, so the API is
# never asked for anything the plain download URLs can provide.
resolve_prerelease() {
  _url="https://api.github.com/repos/$REPO/releases?per_page=20"
  if ! curl -fsSL --retry 3 -H "Accept: application/vnd.github+json" \
            -o "$TMP/releases.json" "$_url" 2>"$TMP/api.err"; then
    # What the transport actually said, kept above the advice and set apart
    # from it -- a bare 'curl: (56)' butted against a formatted block reads
    # like the start of the message rather than evidence for it.
    if [ -s "$TMP/api.err" ]; then
      sed 's/^/  /' "$TMP/api.err" >&2
      echo "" >&2
    fi
    die "could not list the releases of $REPO.

  The GitHub API limits anonymous requests to 60 an hour per address, so a
  shared address may have used them up. Naming a release avoids the API:

      export MARIADB_SHELL_TAG=<tag>
"
  fi

  RELEASE_DESC=$(newest_tag < "$TMP/releases.json") \
    || die "could not find a published release in $REPO -- is every release a
  draft?"
  BASE="https://github.com/$REPO/releases/download/$RELEASE_DESC"
}

fetch() {
  curl -fL --retry 3 ${3:-} -o "$2" "$BASE/$1"
}

[ "$RESOLVE_PRERELEASE" = 0 ] || resolve_prerelease

info "Fetching package list from the $RELEASE_DESC release"
# Silent, and its stderr kept aside, so that what curl said is printed above the
# advice rather than mixed with its progress output.
if ! fetch SHA256SUMS "$TMP/SHA256SUMS" -sS 2>"$TMP/fetch.err"; then
  if [ -s "$TMP/fetch.err" ]; then
    sed 's/^/  /' "$TMP/fetch.err" >&2
    echo "" >&2
  fi
  # A release that was just found in the listing exists; one that was named or
  # implied may not.
  if [ "$RESOLVE_PRERELEASE" = 1 ]; then
    die "could not download SHA256SUMS from release $RELEASE_DESC of $REPO.

  The release exists but has no SHA256SUMS file. A release that is still being
  published does not have its files yet; try again in a few minutes."
  fi
  die "could not download SHA256SUMS from the $RELEASE_DESC release
  of $REPO.

  That release may not exist.

  $PRERELEASE_HINT
"
fi

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
fetch "$BEST_FILE" "$TMP/$BEST_FILE" --progress-bar \
  || die "could not download $BEST_FILE from release $RELEASE_DESC of $REPO"

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
# Unpack. The tarball holds a single top-level mariadb-shell-<ver>-<platform>
# directory, which is renamed to just the version: the platform is a property of
# the machine that unpacked it, not something worth repeating in every path a
# user types. Versions sit side by side, so an install can be undone by
# re-pointing the links below -- but only two are kept, see the prune at the end.
# ---------------------------------------------------------------------------
info "Unpacking into $PREFIX"
mkdir -p "$PREFIX"
tar -xzf "$TMP/$BEST_FILE" -C "$PREFIX"

TOPDIR=$(tar -tzf "$TMP/$BEST_FILE" | awk -F/ 'NF>1 {print $1; exit}')
[ -n "${TOPDIR:-}" ] || die "unexpected tarball layout in $BEST_FILE"
[ -d "$PREFIX/$TOPDIR" ] || die "expected $PREFIX/$TOPDIR after unpacking"

# Falls back to the directory the tarball actually carried, so an unrecognised
# name costs the tidy layout rather than the install.
VERSION=$(echo "$TOPDIR" | sed -n 's/^mariadb-shell-\([0-9][0-9.]*\)-.*/\1/p')
[ -n "${VERSION:-}" ] || VERSION="$TOPDIR"

if [ "$TOPDIR" != "$VERSION" ]; then
  rm -rf "$PREFIX/$VERSION"
  mv "$PREFIX/$TOPDIR" "$PREFIX/$VERSION"
fi

# Left behind by installers that kept a 'current' symlink: it would now dangle,
# or point at a version this install did not choose. Either way it lies.
rm -f "$PREFIX/current"

SHELL_BIN="$PREFIX/$VERSION/bin/mariadb-shell"
[ -x "$SHELL_BIN" ] || die "no executable at $SHELL_BIN after unpacking"

# msh is the package's own short alias, shipped beside the binary. Linking to it
# rather than past it to mariadb-shell means that if the build ever makes msh
# something other than a symlink, this follows along instead of bypassing it.
MSH_BIN="$PREFIX/$VERSION/bin/msh"
[ -e "$MSH_BIN" ] || MSH_BIN="$SHELL_BIN"

mkdir -p "$BINDIR"
ln -sfn "$SHELL_BIN" "$BINDIR/mariadb-shell"
ln -sfn "$MSH_BIN" "$BINDIR/msh"

info "Installed $("$SHELL_BIN" --version 2>/dev/null || echo "$VERSION")"
info "Binary: $BINDIR/mariadb-shell -> $SHELL_BIN"
info "        $BINDIR/msh -> $MSH_BIN"

# ---------------------------------------------------------------------------
# Prune. Only the version just installed and the highest of the rest survive:
# one way back is worth keeping, a museum is not. Deliberately narrow about what
# it will delete -- a name has to be purely digits and dots to be considered
# ours, so anything else under PREFIX, including directories left by an older
# layout, is left alone rather than guessed at.
# ---------------------------------------------------------------------------
is_version_dir() {
  [ -d "$1" ] || return 1
  [ -L "$1" ] && return 1
  case "${1##*/}" in
    [0-9]*) ;;
    *) return 1 ;;
  esac
  case "${1##*/}" in
    *[!0-9.]*) return 1 ;;
  esac
  return 0
}

KEEP_OTHER=""
for d in "$PREFIX"/*; do
  is_version_dir "$d" || continue
  name=${d##*/}
  [ "$name" = "$VERSION" ] && continue
  if [ -z "$KEEP_OTHER" ] || ver_le "$KEEP_OTHER" "$name"; then
    KEEP_OTHER="$name"
  fi
done

for d in "$PREFIX"/*; do
  is_version_dir "$d" || continue
  name=${d##*/}
  [ "$name" = "$VERSION" ] && continue
  [ "$name" = "$KEEP_OTHER" ] && continue
  info "Removing superseded version $name"
  rm -rf "$PREFIX/$name"
done

[ -z "$KEEP_OTHER" ] || info "Kept previous version $KEEP_OTHER"

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
