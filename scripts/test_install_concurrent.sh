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
# Concurrency checks for install.sh: several installers running at once into
# one prefix, installers killed mid-unpack, stale locks.
#
#   scripts/test_install_concurrent.sh <mirror-dir> [<work-dir>]
#
# <mirror-dir> holds one release's SHA256SUMS and this machine's package, so
# the many installs here read it locally instead of downloading it each time:
#
#   gh release download v26.11.0 -D <mirror-dir> \
#       -p SHA256SUMS -p '*-macos26-arm-64bit.tar.gz'
#
# The installer under test is a copy of the repository's install.sh whose
# download URL points at file://<mirror-dir>; nothing else in it changes. Every
# install goes into <work-dir> (default: a new temporary folder, removed at the
# end), never the default prefix. Exits non-zero if any check fails.
#
set -u

[ $# -ge 1 ] || { echo "usage: $0 <mirror-dir> [<work-dir>]" >&2; exit 2; }
MIRROR=$(cd "$1" && pwd) || exit 2
REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)

PKG=$(ls "$MIRROR"/mariadb-shell-*.tar.gz 2>/dev/null | head -1)
[ -n "$PKG" ] && [ -f "$MIRROR/SHA256SUMS" ] \
  || { echo "$MIRROR needs SHA256SUMS and a mariadb-shell-*.tar.gz package" >&2; exit 2; }
VERSION=$(basename "$PKG" | sed -n 's/^mariadb-shell-\([0-9][0-9.]*\)-.*/\1/p')
[ -n "$VERSION" ] || { echo "cannot read the version from $(basename "$PKG")" >&2; exit 2; }
TAG="v$VERSION"

if [ $# -ge 2 ]; then
  W=$2; mkdir -p "$W"; REMOVE_W=0
else
  W=$(mktemp -d); REMOVE_W=1
fi
W=$(cd "$W" && pwd)
trap '[ "$REMOVE_W" = 0 ] || rm -rf "$W"' EXIT

INST=$W/install.sh
sed "s#https://github.com/\$REPO/releases/download/\$MARIADB_SHELL_TAG#file://$MIRROR#" \
  "$REPO_ROOT/install.sh" > "$INST"
grep -q "file://$MIRROR" "$INST" \
  || { echo "could not point the test copy of install.sh at the mirror" >&2; exit 2; }

fail=0
ok()  { echo "PASS: $*"; }
bad() { echo "FAIL: $*"; fail=1; }

# Starts n installers together into <dir>/prefix and waits for all of them.
run() {
  _d=$1; _n=$2; _i=1; _pids=""
  while [ $_i -le $_n ]; do
    MARIADB_SHELL_PREFIX=$_d/prefix MARIADB_SHELL_BINDIR=$_d/bin MARIADB_SHELL_TAG=$TAG \
      sh "$INST" >"$_d/out.$_i" 2>&1 &
    _pids="$_pids $!"; _i=$((_i + 1))
  done
  _rc=0; for _p in $_pids; do wait "$_p" || _rc=1; done
  return $_rc
}

# One version folder per expected name, nothing else under the prefix (no
# .unpack.*, no lock), and a link that runs the installed version.
check_state() {
  _d=$1; _l=$2
  _dots=""
  for _f in "$_d"/prefix/.[!.]*; do
    [ -e "$_f" ] && _dots="$_dots ${_f##*/}"
  done
  [ -z "$_dots" ] && ok "$_l: no .unpack.* or lock left" || bad "$_l: left behind: $_dots"
  [ "$(readlink "$_d/bin/mariadb-shell")" = "$_d/prefix/$VERSION/bin/mariadb-shell" ] \
    && "$_d/bin/mariadb-shell" --version 2>/dev/null | grep -q "Ver $VERSION" \
    && ok "$_l: link runs $VERSION" || bad "$_l: link wrong or binary broken"
}

show_failed() { for _f in "$1"/out.*; do echo "--- $_f"; tail -5 "$_f"; done; }

echo "Testing $INST with $TAG from $MIRROR"

for n in 1 2 3; do
  d=$W/fresh$n; mkdir -p "$d"
  if run "$d" $n; then ok "$n install(s) into a fresh prefix exit 0"
  else bad "$n install(s) into a fresh prefix: some failed"; show_failed "$d"; fi
  check_state "$d" "fresh x$n"
  [ $n = 1 ] || rm -rf "$d"   # fresh1 is reused below
done

# A rerun keeps the folder it would only replace with the same build.
d=$W/fresh1
ino=$(ls -di "$d/prefix/$VERSION" | awk '{print $1}')
run "$d" 1
grep -q '==> Keeping' "$d/out.1" && [ "$(ls -di "$d/prefix/$VERSION" | awk '{print $1}')" = "$ino" ] \
  && ok "rerun keeps the installed folder" || bad "rerun replaced the installed folder"
rm -rf "$d"

# Three upgrades together over two older versions: the higher one is kept.
d=$W/upgrade; mkdir -p "$d/prefix/0.0.2/bin" "$d/prefix/0.0.1/bin"
run "$d" 3 && ok "3 upgrades together exit 0" || { bad "3 upgrades: some failed"; show_failed "$d"; }
check_state "$d" "upgrade x3"
[ -d "$d/prefix/0.0.2" ] && [ ! -d "$d/prefix/0.0.1" ] && ok "upgrade: kept 0.0.2, pruned 0.0.1" \
  || bad "upgrade: prune left $(ls "$d/prefix" | tr '\n' ' ')"
rm -rf "$d"

# Killed with kill -9 mid-unpack, leaving its .unpack.* folder, and a lock
# whose holder is gone: the next run clears both.
d=$W/killed; mkdir -p "$d"
MARIADB_SHELL_PREFIX=$d/prefix MARIADB_SHELL_BINDIR=$d/bin MARIADB_SHELL_TAG=$TAG \
  sh "$INST" >"$d/out.k" 2>&1 &
kp=$!
i=0
while ! ls -d "$d"/prefix/.unpack.* >/dev/null 2>&1; do
  i=$((i + 1)); [ $i -le 600 ] || break   # 60 s
  sleep 0.1
done
if [ $i -le 600 ]; then
  sleep 0.3; kill -9 $kp; wait $kp 2>/dev/null
  mkdir "$d/prefix/.install.lock"; echo "$kp" > "$d/prefix/.install.lock/pid"
  run "$d" 1 && ok "run after kill -9 and a dead holder's lock exits 0" \
    || { bad "run after kill -9 failed"; show_failed "$d"; }
  check_state "$d" "after kill"
else
  kill -9 $kp 2>/dev/null; wait $kp 2>/dev/null
  bad "the installer never started unpacking"; cat "$d/out.k"
fi
rm -rf "$d"

# A lock with no PID in it: its holder was killed between mkdir and writing it.
d=$W/nopid; mkdir -p "$d/prefix/.install.lock"
run "$d" 1 && ok "run over a lock without a PID exits 0" || { bad "run over a PID-less lock failed"; show_failed "$d"; }
check_state "$d" "lock without PID"
rm -rf "$d"

exit $fail
