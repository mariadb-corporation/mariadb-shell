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

#
# Check that a sandbox server package is self-contained.
#
#   scripts/check_sandbox_server_deps.sh <package-root>
#
# Run on the platform that built the package, after pruning and bundling:
# Linux (in the Rocky 9 builder), macOS, or Windows (Git Bash, with the
# Visual Studio environment loaded for dumpbin).
#
# A sandbox server package is built once per platform and architecture and
# then runs on machines that never built it, so every library a binary loads
# must either be part of the operating system on every supported version of
# it, or be shipped in the package. Anything else -- OpenSSL above all -- is
# whatever the build machine happened to have: a Homebrew keg, the GitHub
# runner's preinstalled OpenSSL and VC++ runtime, Rocky 9's libssl.so.3 that
# Fedora 45 no longer has. The verification legs can't be relied on to notice,
# because their machines tend to have the same libraries the builders had.
#
# What counts as the operating system, per platform:
#   Linux    glibc itself plus libstdc++ and libgcc_s, which only ever grow
#            backward compatibly and which the builder links at the floor
#            Rocky 9 sets. Everything else, including zlib, libaio, libcrypt
#            and OpenSSL, has changed name or gone missing on some target.
#   macOS    /usr/lib and /System. Bundled libraries must be referenced as
#            @executable_path/../lib/private/<name>, where the packaging step
#            puts them, and be there.
#   Windows  DLLs in System32 and API sets (api-ms-win-*, ext-ms-win-*),
#            except the VC++ runtime and OpenSSL, which a build machine has
#            installed and a user's machine need not. Bundled DLLs must sit in
#            bin/, next to mariadbd.exe, which is where Windows looks first for
#            the server and for the plugins it loads.
#
# Exits 1 and lists every offending file when anything fails.

set -euo pipefail

ROOT="${1:?usage: $0 <package-root>}"
ROOT="$(cd "$ROOT" && pwd -P)"

failures=0
checked=0
fail() {
  echo "  FAIL $1" >&2
  failures=$((failures + 1))
}

magic() {
  head -c4 "$1" 2>/dev/null | od -An -tx1 | tr -d ' \n'
}

# ---------------------------------------------------------------------------
# Linux
# ---------------------------------------------------------------------------
LINUX_SYSTEM_RE='^(libc\.so\.6|libm\.so\.6|libpthread\.so\.0|libdl\.so\.2|librt\.so\.1|libresolv\.so\.2|libutil\.so\.1|ld-linux-[a-z0-9_-]+\.so\.[0-9]+|libstdc\+\+\.so\.6|libgcc_s\.so\.1)$'

check_linux() {
  local f="$1" rel="$2" needed ldd_out shipped name line resolved out deps

  # 1. Declared dependencies. A failing readelf, or no NEEDED entries at all
  # (every binary here links libc), must not read as "nothing to check".
  if ! out="$(readelf -d "$f" 2>&1)"; then
    fail "$rel: readelf failed:"$'\n'"$out"
    return
  fi
  deps="$(sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' <<<"$out")"
  if [ -z "$deps" ]; then
    fail "$rel: readelf listed no NEEDED entries"
    return
  fi
  while IFS= read -r needed; do
    [ -n "$needed" ] || continue
    [[ "$needed" =~ $LINUX_SYSTEM_RE ]] && continue
    [ -f "$ROOT/lib/$needed" ] && continue
    fail "$rel needs $needed, which is neither a glibc/GCC runtime library nor shipped in lib/"
  done <<<"$deps"

  # 2. What the loader actually picks. env -i keeps the builder's
  # LD_LIBRARY_PATH (gcc-toolset sets one) out of it.
  ldd_out="$(env -i /usr/bin/ldd "$f" 2>&1 || true)"
  if grep -q 'not found' <<<"$ldd_out"; then
    fail "$rel has unresolved dependencies:"$'\n'"$(grep 'not found' <<<"$ldd_out")"
  fi
  for shipped in "$ROOT"/lib/*.so*; do
    [ -f "$shipped" ] || continue
    name="$(basename "$shipped")"
    line="$(grep -E "^[[:space:]]*${name//./\\.} => " <<<"$ldd_out" || true)"
    [ -n "$line" ] || continue
    resolved="$(awk '{print $3}' <<<"$line")"
    resolved="$(readlink -f "$resolved" 2>/dev/null || echo "$resolved")"
    if [ "$resolved" != "$(readlink -f "$shipped")" ]; then
      fail "$rel resolves $name to $resolved instead of the copy in lib/"
    fi
  done
}

# ---------------------------------------------------------------------------
# macOS
# ---------------------------------------------------------------------------
check_macos() {
  local f="$1" rel="$2" ref own_id out refs
  own_id="$(otool -D "$f" 2>/dev/null | sed -n '2p')"
  # A failing otool, or no load commands at all (every binary here links
  # libSystem), must not read as "nothing to check".
  if ! out="$(otool -L "$f" 2>&1)"; then
    fail "$rel: otool failed:"$'\n'"$out"
    return
  fi
  refs="$(tail -n +2 <<<"$out" | awk '{print $1}')"
  if [ -z "$refs" ]; then
    fail "$rel: otool listed no libraries"
    return
  fi
  while IFS= read -r ref; do
    [ -n "$ref" ] || continue
    [ "$ref" = "$own_id" ] && continue
    case "$ref" in
      /usr/lib/*|/System/*) continue ;;
      @executable_path/../lib/private/*)
        [ -f "$ROOT/lib/private/${ref##*/}" ] && continue
        fail "$rel references $ref, which is not in lib/private/"
        ;;
      *)
        fail "$rel references $ref, which is neither a system library nor bundled in lib/private/"
        ;;
    esac
  done <<<"$refs"
}

# ---------------------------------------------------------------------------
# Windows
# ---------------------------------------------------------------------------
# Installed on build machines (Visual Studio, the GitHub runner image) and so
# usually present in System32 there, but not part of Windows.
WINDOWS_REDIST_RE='^(vcruntime[0-9_]*|msvcp[0-9_a-z]*|concrt[0-9]*|vccorlib[0-9]*|vcomp[0-9]*|libssl-[0-9a-z_-]*|libcrypto-[0-9a-z_-]*)\.dll$'
SYSTEM32="${SYSTEMROOT:-C:/Windows}/System32"

check_windows() {
  local f="$1" rel="$2" dll lower out deps
  # MSYS2_ARG_CONV_EXCL: Git Bash would otherwise rewrite /nologo and
  # /dependents into paths under its own install directory, dumpbin would fail
  # on them, and an empty dependency list would pass. Every PE file imports at
  # least KERNEL32.dll, so finding none is an error too.
  if ! out="$(MSYS2_ARG_CONV_EXCL='*' dumpbin /nologo /dependents "$(cygpath -w "$f")" 2>&1)"; then
    fail "$rel: dumpbin failed:"$'\n'"$out"
    return
  fi
  deps="$(tr -d '\r' <<<"$out" \
          | sed -n 's/^[[:space:]]\{1,\}\([^[:space:]]\{1,\}\.[dD][lL][lL]\)[[:space:]]*$/\1/p')"
  if [ -z "$deps" ]; then
    fail "$rel: dumpbin listed no dependencies:"$'\n'"$out"
    return
  fi
  while IFS= read -r dll; do
    [ -n "$dll" ] || continue
    lower="$(tr '[:upper:]' '[:lower:]' <<<"$dll")"
    if [ -f "$ROOT/bin/$dll" ] || [ -f "$ROOT/bin/$lower" ]; then
      continue
    fi
    case "$lower" in
      api-ms-win-*|ext-ms-win-*) continue ;;
    esac
    if [[ "$lower" =~ $WINDOWS_REDIST_RE ]]; then
      fail "$rel needs $dll, which is not part of Windows and is not shipped in bin/"
      continue
    fi
    [ -f "$SYSTEM32/$dll" ] && continue
    fail "$rel needs $dll, which is neither in System32 nor shipped in bin/"
  done <<<"$deps"
}

# ---------------------------------------------------------------------------

case "$(uname -s)" in
  Linux)  platform=linux ;;
  Darwin) platform=macos ;;
  MINGW*|MSYS*|CYGWIN*) platform=windows ;;
  *) echo "check_sandbox_server_deps: unsupported platform $(uname -s)" >&2; exit 1 ;;
esac

while IFS= read -r -d '' f; do
  m="$(magic "$f")"
  rel="${f#"$ROOT"/}"
  case "$platform:$m" in
    linux:7f454c46) check_linux "$f" "$rel" ;;
    macos:cffaedfe|macos:cefaedfe|macos:cafebabe) check_macos "$f" "$rel" ;;
    windows:4d5a*) check_windows "$f" "$rel" ;;
    *) continue ;;
  esac
  checked=$((checked + 1))
done < <(find "$ROOT" -type f -print0)

if [ "$checked" -eq 0 ]; then
  echo "check_sandbox_server_deps: no binaries found under $ROOT" >&2
  exit 1
fi
if [ "$failures" -ne 0 ]; then
  echo "check_sandbox_server_deps: $failures problem(s) in $checked binaries" >&2
  exit 1
fi
echo "==> check_sandbox_server_deps ($platform): $checked binaries, every dependency is part of the OS or shipped in the package"
