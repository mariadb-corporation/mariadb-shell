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
# Strip an installed MariaDB server tree down to what a sandbox actually uses.
#
#   scripts/prune_sandbox_server.sh <package-root>
#
# Run against the staged directory produced by
# `cmake --install --component Server` (plus the hand-copied binaries), before
# it is tarred up -- see sandbox-server.yml and
# docker/entrypoint-sandbox-server.sh, which is every caller.
#
# Why this exists: the sandbox packages are configured for the *build* to
# succeed, not for the package to be small. `cmake --install --component Server`
# refuses to run unless every target the Server component declares has been
# built, which is why sandbox-server.yml builds aria_chk, myisamchk,
# innochecksum, ha_spider and friends at all. None of them is a sandbox
# dependency: the mariadbSandbox plugin only ever runs mariadbd and
# mariadb-install-db (python/plugins/sandbox/sandboxlib.py), and
# mariadb-install-db in turn only runs my_print_defaults, resolveip and
# mariadbd. On macOS that is 93 MB of bin/ against 31 MB actually used.
#
# The keep-lists below are therefore the contract. Three notes on them:
#
#   - auth_mysql_sha2 is NOT optional. caching_sha2_password is not built into
#     mariadbd (verified with `strings` on 11.8.9) -- it is that plugin, and
#     _root_auth_plugin() in sandboxlib.py forces it whenever a MySQL-built
#     shell deploys a MariaDB sandbox. Dropping it breaks that pairing with an
#     unloadable-plugin error at the first connection.
#   - mysql_native_password and unix_socket ARE built in, so no auth plugin is
#     needed for the ordinary path.
#   - The seven bootstrap .sql files are all mandatory even though the plugin
#     passes --skip-test-db: mariadb-install-db stats every one of them up
#     front and exits 1 if any is missing, before it looks at that flag.
#
# Anything removed here can come back by being added to a keep-list; nothing
# here changes how the package is built. The assertions at the end fail the
# build rather than shipping a tree that cannot bootstrap, since the Windows
# layout in particular is not exercised locally.
#
# Environment:
#   PRUNE_DRY_RUN=1  report what would be removed, remove nothing
#

set -euo pipefail

ROOT="${1:-}"
if [ -z "$ROOT" ] || [ ! -d "$ROOT" ]; then
  echo "usage: $0 <package-root>" >&2
  exit 1
fi
ROOT="$(cd "$ROOT" && pwd)"
DRY_RUN="${PRUNE_DRY_RUN:-0}"

# Executables the sandbox path reaches, by basename with any .exe stripped.
# Both the MariaDB and the legacy mysql* name of every tool is listed, matching
# _MYSQLD_NAMES and _INSTALL_DB_NAMES in sandboxlib.py -- on Windows these are
# not symlinks but separate copies, and mariadb-install-db.exe there hardcodes
# 'mysqld' with no mention of mariadbd (confirmed with `strings`), so dropping
# the legacy name breaks the bootstrap with "mysqld.exe is not recognized".
# mariadb-admin is here because sandbox-server.yml hand-copies it into the
# Windows package. Every entry is kept "if present", since each platform ships
# a different subset: Windows has no resolveip and no mysqld-less layout, POSIX
# has no mysqld.exe, and Windows' mariadb-install-db is a bin/ executable
# rather than a scripts/ shell script.
KEEP_BIN="
mariadbd
mysqld
my_print_defaults
resolveip
mariadb-install-db
mysql_install_db
mariadb-admin
"

# Dynamic plugins worth shipping: the three authentication plugins a user may
# reasonably want on a sandbox account (ed25519 and parsec are MariaDB's own;
# see the auth_mysql_sha2 note above), plus the type plugin needed to read
# JSON columns written by MySQL. Every other plugin built by sandbox-server.yml
# is a storage engine or an information-schema/audit add-on that nothing loads
# unless it is asked for by name.
KEEP_PLUGINS="
auth_ed25519
auth_mysql_sha2
auth_parsec
type_mysql_json
"

# Bootstrap scripts mariadb-install-db requires to exist (all seven, see above).
REQUIRED_SQL="
fill_help_tables.sql
mariadb_system_tables.sql
mariadb_performance_tables.sql
mariadb_system_tables_data.sql
maria_add_gis_sp_bootstrap.sql
mariadb_test_db.sql
mariadb_sys_schema.sql
"

# The one message catalogue kept. mariadb-install-db is invoked without
# --language, so it bootstraps with --lc-messages=en_US and the server reads
# share/english/errmsg.sys; no sandbox option file sets lc_messages.
KEEP_LANG="english"

size_kb() { du -sk "$1" 2>/dev/null | awk '{print $1}'; }

# in_list <needle> <list>  -- the lists above are newline-separated, which the
# unquoted expansion here splits on.
in_list() {
  local needle="$1" item
  for item in $2; do
    [ "$item" = "$needle" ] && return 0
  done
  return 1
}

removed_kb=0

drop() {
  local path
  for path in "$@"; do
    [ -e "$path" ] || continue
    local kb
    kb="$(size_kb "$path")"
    removed_kb=$(( removed_kb + ${kb:-0} ))
    echo "  - ${path#"$ROOT"/}"
    [ "$DRY_RUN" = "1" ] || rm -rf "$path"
  done
}

before_kb="$(size_kb "$ROOT")"
echo "==> Pruning sandbox server package: $ROOT (${before_kb} KB)"
[ "$DRY_RUN" = "1" ] && echo "    (dry run -- nothing will be removed)"

# ---------------------------------------------------------------------------
# bin/ and sbin/: keep the handful of executables above, and keep every shared
# library unconditionally. That second rule is what makes this safe on Windows,
# where bin/ holds the runtime DLLs the server loads next to its own .exe.
# ---------------------------------------------------------------------------
echo "==> bin/"
for dir in "$ROOT/bin" "$ROOT/sbin"; do
  [ -d "$dir" ] || continue
  for path in "$dir"/*; do
    [ -e "$path" ] || continue
    base="$(basename "$path")"
    case "$base" in
      *.dll|*.so|*.so.*|*.dylib) continue ;;
    esac
    in_list "${base%.exe}" "$KEEP_BIN" && continue
    drop "$path"
  done
done

# ---------------------------------------------------------------------------
# scripts/: mariadb-install-db and nothing else.
# ---------------------------------------------------------------------------
if [ -d "$ROOT/scripts" ]; then
  echo "==> scripts/"
  for path in "$ROOT"/scripts/*; do
    [ -e "$path" ] || continue
    in_list "$(basename "$path")" "$KEEP_BIN" && continue
    drop "$path"
  done
fi

# ---------------------------------------------------------------------------
# lib/plugin/: keep-list above. auth_pam_tool_dir goes with the auth_pam
# plugins it exists to serve.
# ---------------------------------------------------------------------------
if [ -d "$ROOT/lib/plugin" ]; then
  echo "==> lib/plugin/"
  drop "$ROOT/lib/plugin/auth_pam_tool_dir"
  for path in "$ROOT"/lib/plugin/*; do
    [ -e "$path" ] || continue
    base="$(basename "$path")"
    base="${base%.so}"; base="${base%.dll}"; base="${base%.dylib}"
    in_list "$base" "$KEEP_PLUGINS" && continue
    drop "$path"
  done
fi

# ---------------------------------------------------------------------------
# share/: drop every message catalogue but English, and the few files that
# belong to the plugins dropped above or to tools the sandbox never runs.
# Everything else under share/ is left alone on purpose -- charsets/ and the
# bootstrap .sql files are load-bearing, and the unknown is cheap here.
# ---------------------------------------------------------------------------
if [ -d "$ROOT/share" ]; then
  echo "==> share/"
  for path in "$ROOT"/share/*/; do
    [ -d "$path" ] || continue
    [ -f "${path}errmsg.sys" ] || continue          # a message catalogue, and
    [ "$(basename "$path")" = "$KEEP_LANG" ] && continue
    drop "${path%/}"
  done
  drop "$ROOT/share/pam_user_map.so" \
       "$ROOT/share/user_map.conf" \
       "$ROOT/share/maria_add_gis_sp.sql" \
       "$ROOT/share/mariadb_test_data_timezone.sql"
fi

# ---------------------------------------------------------------------------
# Whole trees no sandbox reads. Listed by name rather than globbed so a layout
# that never had them is simply a no-op, and a new one is not silently caught.
# ---------------------------------------------------------------------------
echo "==> docs & support files"
drop "$ROOT/man" \
     "$ROOT/mysql-test" \
     "$ROOT/sql-bench" \
     "$ROOT/support-files" \
     "$ROOT/include" \
     "$ROOT/share/doc" \
     "$ROOT/share/man" \
     "$ROOT/share/aclocal" \
     "$ROOT/share/pkgconfig"

# ---------------------------------------------------------------------------
# Assert the result can still bootstrap a data directory. A keep-list that is
# wrong on a layout not exercised locally should fail the build here, not
# produce a package that dies in the user's first deploy().
# ---------------------------------------------------------------------------
missing=""
have_one() {
  local candidate
  for candidate in "$@"; do
    [ -e "$candidate" ] && return 0
  done
  missing="$missing
  $1"
  return 1
}

have_one "$ROOT/bin/mariadbd" "$ROOT/bin/mariadbd.exe" || true
have_one "$ROOT/bin/my_print_defaults" "$ROOT/bin/my_print_defaults.exe" || true
have_one "$ROOT/scripts/mariadb-install-db" "$ROOT/bin/mariadb-install-db.exe" || true
# The Windows install-db is the native tool, which execs bin/mysqld.exe by that
# name; mariadbd.exe alone is not enough there.
if [ -e "$ROOT/bin/mariadb-install-db.exe" ]; then
  have_one "$ROOT/bin/mysqld.exe" || true
fi
for sql in $REQUIRED_SQL; do
  have_one "$ROOT/share/$sql" || true
done
have_one "$ROOT/share/$KEEP_LANG/errmsg.sys" || true
for plugin in $KEEP_PLUGINS; do
  have_one "$ROOT/lib/plugin/$plugin.so" "$ROOT/lib/plugin/$plugin.dll" \
           "$ROOT/lib/plugin/$plugin.dylib" || true
done

if [ -n "$missing" ] && [ "$DRY_RUN" != "1" ]; then
  echo "ERROR: the pruned package is missing files the sandbox needs:$missing" >&2
  exit 1
fi

after_kb="$(size_kb "$ROOT")"
[ "$DRY_RUN" = "1" ] && after_kb=$(( before_kb - removed_kb ))
if [ "${before_kb:-0}" -gt 0 ]; then
  echo "==> Pruned: ${before_kb} KB -> ${after_kb} KB" \
       "($(( (before_kb - after_kb) * 100 / before_kb ))% smaller)"
fi
