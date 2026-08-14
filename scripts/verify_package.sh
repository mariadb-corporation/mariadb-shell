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
# Verify an unpacked MariaDB Shell package.
#
#   scripts/verify_package.sh <unpacked-package-root>
#
# Run against a package on a machine that has never built it -- the point is to
# catch what only works on a build host: a dependency resolved from the build
# tree, a stdlib module the bundled Python silently dropped, a plugin that fails
# to load once it is somewhere other than plugins/external.
#
# Environment:
#   EXPECT_VERSION  assert `--version` reports exactly this version
#   CHECKS_FILE     plugin CLI checks (default: package-checks.conf beside this)
#
# Every check runs even after one fails, so a single run reports the full picture
# rather than only the first problem. Exits non-zero if any check failed.

set -uo pipefail

ROOT="${1:-}"
if [ -z "$ROOT" ]; then
  echo "usage: $0 <unpacked-package-root>" >&2
  exit 2
fi
[ -d "$ROOT" ] || { echo "not a directory: $ROOT" >&2; exit 2; }
# Absolute, no trailing slash: the dependency audit decides whether a library is
# bundled by asking whether the loader resolved it to a path under $ROOT, and a
# relative or slash-terminated root would never match one.
ROOT="$(cd "$ROOT" && pwd)"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHECKS_FILE="${CHECKS_FILE:-$SCRIPT_DIR/package-checks.conf}"
EXPECT_VERSION="${EXPECT_VERSION:-}"

PASS=0
FAIL=0
SKIP=0
FAILED_NAMES=()

pass() { printf '  \033[32mPASS\033[0m  %s\n' "$1"; PASS=$((PASS + 1)); }
skip() { printf '  \033[33mSKIP\033[0m  %s (%s)\n' "$1" "$2"; SKIP=$((SKIP + 1)); }
fail() {
  printf '  \033[31mFAIL\033[0m  %s\n' "$1"
  [ -n "${2:-}" ] && printf '        %s\n' "$2"
  FAIL=$((FAIL + 1))
  FAILED_NAMES+=("$1")
}

section() { printf '\n== %s\n' "$1"; }

# ---------------------------------------------------------------------------
# Locate the binary and the bundled plugin directory.
#
# Both paths are probed rather than assumed: the install layout differs between
# platforms, and a hard-coded path would turn a layout change into a confusing
# "plugin did not load" instead of "the package is not shaped as expected".
# ---------------------------------------------------------------------------
section "Package layout"

SHELL_BIN=""
for cand in "$ROOT/bin/mariadb-shell" "$ROOT/bin/mariadb-shell.exe"; do
  if [ -x "$cand" ] || [ -f "$cand" ]; then SHELL_BIN="$cand"; break; fi
done
if [ -z "$SHELL_BIN" ]; then
  fail "shell binary present" "no bin/mariadb-shell[.exe] under $ROOT"
  echo ""
  echo "Cannot continue without the shell binary." >&2
  exit 1
fi
pass "shell binary present ($(basename "$SHELL_BIN"))"

PLUGIN_DIR=""
for cand in \
  "$ROOT/lib/mariadb-shell/plugins" \
  "$ROOT/share/mariadb-shell/plugins" \
  "$ROOT/lib/mysqlsh/plugins"; do
  if [ -d "$cand" ]; then PLUGIN_DIR="$cand"; break; fi
done
if [ -n "$PLUGIN_DIR" ]; then
  pass "bundled plugin directory (${PLUGIN_DIR#$ROOT/})"
else
  fail "bundled plugin directory" "none of lib/mariadb-shell/plugins, share/mariadb-shell/plugins, lib/mysqlsh/plugins exist"
fi

# ---------------------------------------------------------------------------
# Run the shell and capture stdout/stderr separately.
#
# stderr is checked as well as the exit status: a plugin that fails to import
# is reported as a warning on stderr while the shell still exits 0, so an
# exit-status-only check would pass a package whose plugins are all broken.
# ---------------------------------------------------------------------------
OUT=""
ERR=""
RC=0
run_shell() {
  local out_f err_f
  out_f="$(mktemp)"; err_f="$(mktemp)"
  "$SHELL_BIN" "$@" >"$out_f" 2>"$err_f"
  RC=$?
  OUT="$(cat "$out_f")"
  ERR="$(cat "$err_f")"
  rm -f "$out_f" "$err_f"
  return $RC
}

# ---------------------------------------------------------------------------
section "Shell startup"
# ---------------------------------------------------------------------------
if run_shell --version; then
  if [ -n "$EXPECT_VERSION" ]; then
    if printf '%s' "$OUT" | grep -qF "$EXPECT_VERSION"; then
      pass "--version reports $EXPECT_VERSION"
    else
      fail "--version reports $EXPECT_VERSION" "got: $OUT"
    fi
  else
    pass "--version runs ($OUT)"
  fi
else
  fail "--version runs" "exit $RC; stderr: $ERR"
fi

# A clean startup is the broadest single signal in this script: anything the
# shell could not load on the way up lands on stderr here.
if run_shell --py -e "print('startup-ok')" && [ "$OUT" = "startup-ok" ]; then
  if [ -z "$ERR" ]; then
    pass "clean startup (nothing on stderr)"
  else
    fail "clean startup (nothing on stderr)" "stderr: $ERR"
  fi
else
  fail "python one-liner runs" "exit $RC; stdout: $OUT; stderr: $ERR"
fi

# ---------------------------------------------------------------------------
section "Bundled Python"
# ---------------------------------------------------------------------------
# Every stdlib module whose C extension needs a library we do not write
# ourselves -- i.e. the ones that go missing when a dependency is not where
# configure looked. Each has been silently dropped by a from-source CPython
# build in this project before (a missing vcpkg dependency or a mis-emitted
# rpath), and each failure only shows up at run time on a machine that is not
# the build host. That is exactly what this script is for.
#
# Same list as _PY_REQUIRED_MODULES in cmake/bootstrap_python.cmake, which
# checks it on the build host; keep the two together. curses and readline are
# deliberately absent from both -- see the curses note in that file.
for mod in ssl hashlib sqlite3 zlib binascii ctypes lzma bz2 decimal; do
  if run_shell --py -e "import $mod; print('ok')" && [ "$OUT" = "ok" ]; then
    pass "bundled Python can import $mod"
  else
    fail "bundled Python can import $mod" "exit $RC; stderr: $ERR"
  fi
done

# ---------------------------------------------------------------------------
section "Bundled plugins load"
# ---------------------------------------------------------------------------
# Assert the shell exposes what each bundled plugin directory registers.
# Enumerating the package rather than a hard-coded list means a newly bundled
# plugin is covered the moment it ships, with no change here.
#
# What a plugin registers cannot be guessed from its directory name: mrs_plugin
# registers the global 'mrs', debug/ registers 'util.debug' (a member of the
# built-in util object, not a global of its own), and util/ registers no object
# at all -- it only adds functions to util. So read the name out of the plugin's
# own init.py: the @plugin class together with its optional parent=, or, for a
# plugin that only extends an existing object, the first name it registers with
# @plugin_function. Either way the result is a name that exists only if the
# plugin actually loaded, which is the point of the check.
plugin_object() {
  local init="$1/init.py"
  [ -f "$init" ] || return 1
  awk -v q="\"'" '
    !pending && /^[[:space:]]*@plugin([[:space:]]|\(|$)/ {
      parent = ""
      if (match($0, "parent[[:space:]]*=[[:space:]]*[" q "][^" q "]+")) {
        parent = substr($0, RSTART, RLENGTH)
        sub("^parent[[:space:]]*=[[:space:]]*[" q "]", "", parent)
      }
      pending = 1
      next
    }
    pending && /^[[:space:]]*class[[:space:]]/ {
      cls = $2
      sub("[(:].*$", "", cls)
      print (parent == "" ? cls : parent "." cls)
      found = 1
      exit
    }
    !pending && /@plugin_function\(/ {
      if (match($0, "@plugin_function\\([" q "][^" q "]+")) {
        fn = substr($0, RSTART, RLENGTH)
        sub("^@plugin_function\\([" q "]", "", fn)
        print fn
        found = 1
        exit
      }
    }
    END { if (!found) exit 1 }
  ' "$init"
}

# Plugins register camelCase names, and the shell exposes those in Python as
# snake_case, so a name read out of init.py has to be converted before it can
# be looked up through --py.
py_name() {
  printf '%s' "$1" | awk '{ gsub(/[A-Z]/, "_&"); print tolower($0) }'
}

PLUGIN_OBJECTS=()
PLUGIN_COUNT=0
if [ -n "$PLUGIN_DIR" ]; then
  for d in "$PLUGIN_DIR"/*/; do
    [ -d "$d" ] || continue
    name="$(basename "$d")"
    # A JS plugin, or one this cannot parse, still gets the old guess.
    obj="$(plugin_object "${d%/}")" || obj="${name%_plugin}"
    PLUGIN_COUNT=$((PLUGIN_COUNT + 1))

    # Only a plugin that registers a global object is a CLI entry point, so
    # only those are eligible for the checks below; util.debug is reachable as
    # `-- util debug ...`, but through util, which no plugin owns.
    case "$obj" in
      *.*) ;;
      *) PLUGIN_OBJECTS+=("$obj") ;;
    esac

    if run_shell --py -e "print(type($(py_name "$obj")).__name__)"; then
      if [ -n "$ERR" ]; then
        fail "plugin '$name' registers '$obj'" "stderr: $ERR"
      else
        pass "plugin '$name' registers '$obj'"
      fi
    else
      fail "plugin '$name' registers '$obj'" "exit $RC; stderr: $ERR"
    fi
  done

  if [ "$PLUGIN_COUNT" -eq 0 ]; then
    fail "package bundles at least one plugin" "$PLUGIN_DIR is empty"
  else
    pass "package bundles $PLUGIN_COUNT plugin(s)"
  fi
fi

# ---------------------------------------------------------------------------
section "Plugin CLI checks"
# ---------------------------------------------------------------------------
has_object() {
  local want="$1" o
  for o in ${PLUGIN_OBJECTS[@]+"${PLUGIN_OBJECTS[@]}"}; do
    [ "$o" = "$want" ] && return 0
  done
  return 1
}

if [ ! -f "$CHECKS_FILE" ]; then
  fail "checks file present" "no such file: $CHECKS_FILE"
else
  # Read through `tr -d '\r'`: on Windows the conf file is checked out with CRLF
  # line endings, which would otherwise leave a carriage return on the last
  # field of every line -- a regex ending in \r matches nothing, and a blank
  # line reads as "\r" rather than as empty. Process substitution rather than a
  # pipe keeps the loop in this shell, so the pass/fail counters survive it.
  # The `|| [ -n "$obj" ]` picks up a last line with no trailing newline.
  while IFS='|' read -r obj args regex || [ -n "${obj:-}" ]; do
    obj="$(printf '%s' "${obj:-}" | tr -d '[:space:]')"
    case "$obj" in ''|'#'*) continue ;; esac
    label="$obj $args"

    # A plugin the package does not bundle is skipped: mcp_plugin is optional.
    if ! has_object "$obj"; then
      skip "$label" "plugin not bundled in this package"
      continue
    fi

    # shellcheck disable=SC2086  # args is intentionally word-split
    if run_shell -- $obj $args; then
      if [ -n "$ERR" ]; then
        fail "$label" "wrote to stderr: $ERR"
      elif printf '%s' "$OUT" | grep -Eq "$regex"; then
        pass "$label -> $OUT"
      else
        fail "$label" "output '$OUT' does not match /$regex/"
      fi
    else
      fail "$label" "exit $RC; stdout: $OUT; stderr: $ERR"
    fi
  done < <(tr -d '\r' < "$CHECKS_FILE")
fi

# ---------------------------------------------------------------------------
section "Self-contained (no external dependencies)"
# ---------------------------------------------------------------------------
# The package must run on a minimal install of a supported OS: everything it
# links, apart from what such an install is guaranteed to already have, is
# shipped inside it. Nothing above proves that. A module can import perfectly on
# a machine that happens to have the library the build host had -- which is how
# _decimal and _lzma shipped in a macOS package pointing at /opt/homebrew, and
# how two mysql-secret-store helpers shipped pointing at a path under
# /Users/runner. Both work on the build host. Neither works on a user's.
#
# So walk every binary in the package and look at what it asks the loader for.
# A dependency is acceptable only if it is resolved inside the package (a
# relative reference, or a path under the package root) or is part of the OS
# baseline below. Anything else -- a build-tree path, a package manager prefix,
# an unresolved SONAME -- is a dependency that left the package.
#
# The baseline is deliberately short. Add to it with ALLOW_SYSTEM_LIBS (an
# extended regex matched against the whole dependency string) only for
# something a minimal install genuinely always has.
EXTRA_ALLOW="${ALLOW_SYSTEM_LIBS:-}"
AUDIT_BROKEN=""

# A symlink is checked first and separately: one whose target is missing is, by
# definition, pointing outside the package. The Windows packages ship
# lib/Python3.14/python3.exe as a link into the runner's hosted tool cache, which
# no dependency scan would catch because the file cannot be read at all.
DANGLING=0
while IFS= read -r link; do
  [ -e "$link" ] && continue
  fail "symlink '${link#$ROOT/}' resolves" "dangling, target: $(readlink "$link")"
  DANGLING=$((DANGLING + 1))
done < <(find "$ROOT" -type l 2>/dev/null)
if [ "$DANGLING" -eq 0 ]; then
  pass "no dangling symlinks"
fi

case "$(uname -s)" in
  Darwin)
    BIN_RE='Mach-O'
    list_binaries() {
      # One `file` pass over the tree rather than one per candidate: a package
      # has thousands of files and only a few hundred are binaries.
      find "$ROOT" -type f -print0 | xargs -0 file 2>/dev/null \
        | grep -E "$BIN_RE" | sed 's/:[[:space:]].*//'
    }
    list_deps() { otool -L "$1" 2>/dev/null | tail -n +2 | awk '{print $1}'; }
    # /usr/lib and /System/Library are the dyld shared cache: part of macOS
    # itself, on every machine, neither installable nor removable.
    dep_allowed() {
      case "$1" in
        @rpath/*|@loader_path/*|@executable_path/*) return 0 ;;
        /usr/lib/*|/System/Library/*)               return 0 ;;
      esac
      return 1
    }
    ;;
  Linux)
    BIN_RE='ELF'
    list_binaries() {
      find "$ROOT" -type f -print0 | xargs -0 file 2>/dev/null \
        | grep -E "$BIN_RE" | sed 's/:[[:space:]].*//'
    }
    list_deps() {
      # ldd prints "libfoo.so.1 => /path/libfoo.so.1 (0x...)", "libfoo.so.1 =>
      # not found", or a bare "/lib64/ld-linux.so.2 (0x...)" / "linux-vdso.so.1
      # (0x...)". Emit the resolved path where there is one, so a dependency met
      # from inside the package is recognisable as such, and mark the ones the
      # loader could not resolve at all.
      ldd "$1" 2>/dev/null | while IFS= read -r line; do
        case "$line" in
          *"not found"*) printf '%s [UNRESOLVED]\n' "$(printf '%s' "$line" | awk '{print $1}')" ;;
          *"=>"*)        printf '%s\n' "$(printf '%s' "$line" | sed 's/.*=>[[:space:]]*//; s/[[:space:]]*(0x[0-9a-f]*)$//')" ;;
          *)             printf '%s\n' "$(printf '%s' "$line" | sed 's/[[:space:]]*(0x[0-9a-f]*)$//; s/^[[:space:]]*//')" ;;
        esac
      done
    }
    # glibc and the compiler runtime, matched on the SONAME: ldd reports these
    # as resolved paths (/lib64/libc.so.6), so the basename is what carries the
    # identity. Everything else -- libffi, xz, bzip2, sqlite, OpenSSL, ncurses
    # -- is the package's job to carry. An unresolved dependency is never
    # acceptable, not even a baseline one: it means this machine cannot run it.
    dep_allowed() {
      case "$1" in *"[UNRESOLVED]") return 1 ;; esac
      printf '%s' "${1##*/}" | grep -Eq \
        '^(linux-vdso|ld-linux[^/]*|libc|libm|libdl|libpthread|librt|libresolv|libutil|libnsl|libgcc_s|libstdc\+\+)\.so(\.[0-9.]+)?$'
    }
    ;;
  MINGW*|MSYS*|CYGWIN*)
    BIN_RE='PE'
    # By extension, not by `file`: that is not part of Git for Windows, and a PE
    # has no type marker a shell can cheaply read anyway.
    list_binaries() {
      find "$ROOT" -type f \
        \( -iname '*.exe' -o -iname '*.dll' -o -iname '*.pyd' -o -iname '*.drv' \) 2>/dev/null
    }

    # Native Windows programs cannot read the MSYS paths this script works in.
    winpath() {
      if command -v cygpath >/dev/null 2>&1; then cygpath -w "$1"; else printf '%s' "$1"; fi
    }

    # One pass over the whole tree up front, because reading a PE import table
    # needs a real program and starting it 300 times would dominate the runtime.
    # The reader is the package's own Python -- dumpbin and objdump are not on a
    # machine that has only unpacked a package. See scripts/pe_imports.py.
    PE_DEPS="$(mktemp)"
    "$SHELL_BIN" --py -f "$(winpath "$SCRIPT_DIR/pe_imports.py")" "$(winpath "$ROOT")" \
      > "$PE_DEPS" 2>/dev/null
    # No output at all means the reader did not run. Say so: an audit with no
    # dependency data to look at would otherwise report a clean package.
    if [ ! -s "$PE_DEPS" ]; then
      AUDIT_BROKEN="$SCRIPT_DIR/pe_imports.py produced no output via $SHELL_BIN"
    fi
    list_deps() { awk -F'\t' -v f="${1#$ROOT/}" '$1 == f { print $2 }' "$PE_DEPS"; }

    # A PE names its imports without a path, and the loader looks beside the
    # importing binary before anywhere else, so "shipped anywhere in the package"
    # is the right test -- Windows filenames are case-insensitive, hence the fold.
    PKG_DLLS="$(list_binaries | sed 's|.*/||' | tr 'A-Z' 'a-z' | sort -u)"

    # The Visual C++ runtime is the one dependency allowed to come from outside
    # both the package and the OS. It is not on a stock Windows, so this is not
    # an OS guarantee: it is the redistributable every MSVC-built program on the
    # platform expects, it is a documented install prerequisite, and bundling it
    # is a deliberate opt-in here (BUNDLE_RUNTIME_LIBRARIES, off by default), so
    # flagging it made every Windows package fail on a dependency nobody intends
    # to carry. The list is what vc_redist installs of the C/C++ runtime proper.
    #
    # MFC (mfc140*.dll) is NOT in it, on purpose: it is a separate redist
    # component, and the only thing that ever pulled it in was pywin32's
    # pythonwin -- which src/CMakeLists.txt excludes from the package for exactly
    # that reason. Leave it flagged so a regression there is still caught.
    VCRUNTIME_RE='^(vcruntime140|vcruntime140_1|vcruntime140_threads|msvcp140|msvcp140_1|msvcp140_2|msvcp140_atomic_wait|msvcp140_codecvt_ids|concrt140|vcomp140|vcamp140)\.dll$'

    # DLLs Windows itself provides. The api-ms-win-* / ext-ms-win-* families are
    # the API sets, and the rest is the classic Win32 surface.
    dep_allowed() {
      local d
      d="$(printf '%s' "$1" | tr 'A-Z' 'a-z')"
      printf '%s\n' "$PKG_DLLS" | grep -qxF "$d" && return 0
      printf '%s' "$d" | grep -Eq "$VCRUNTIME_RE" && return 0
      printf '%s' "$d" | grep -Eq \
        '^(api-ms-win-|ext-ms-win-)|^(aclui|activeds|advapi32|bcrypt|bcryptprimitives|cabinet|cfgmgr32|combase|comctl32|comdlg32|credui|crypt32|cryptbase|d3d9|dbghelp|dnsapi|dsound|dwmapi|gdi32|gdiplus|imagehlp|imm32|iphlpapi|kernel32|kernelbase|ktmw32|loadperf|lz32|mpr|msi|msimg32|msvcrt|mswsock|ncrypt|netapi32|normaliz|ntdll|ntdsapi|odbc32|ole32|oleacc|oleaut32|pdh|powrprof|profapi|propsys|psapi|query|rasapi32|rpcrt4|sechost|secur32|setupapi|sfc|shell32|shlwapi|urlmon|user32|userenv|usp10|uxtheme|version|win32u|winhttp|wininet|winmm|wintrust|wldap32|wsock32|wtsapi32|ws2_32)\.dll$|^winspool\.drv$'
    }
    ;;
  *)
    BIN_RE=''
    ;;
esac

if [ -n "$AUDIT_BROKEN" ]; then
  fail "dependency audit can run" "$AUDIT_BROKEN"
elif [ -z "$BIN_RE" ]; then
  skip "no dependency escapes the package" "no dependency lister for $(uname -s)"
else
  VIOLATIONS="$(mktemp)"
  SCANNED=0
  while IFS= read -r bin; do
    [ -n "$bin" ] || continue
    SCANNED=$((SCANNED + 1))
    while IFS= read -r dep; do
      [ -n "$dep" ] || continue
      # The lister could not read this one. Reported against the binary rather
      # than aggregated, because there is no dependency name to group under --
      # and staying silent would count an unread binary as a clean one.
      case "$dep" in
        '!'*) fail "dependencies of '${bin#$ROOT/}' are readable" "${dep#!}"; continue ;;
      esac
      # Resolved inside the package: fine however it got there.
      case "$dep" in "$ROOT"/*) continue ;; esac
      dep_allowed "$dep" && continue
      if [ -n "$EXTRA_ALLOW" ] && printf '%s' "$dep" | grep -Eq "$EXTRA_ALLOW"; then
        continue
      fi
      printf '%s\t%s\n' "$dep" "${bin#$ROOT/}" >> "$VIOLATIONS"
    done < <(list_deps "$bin")
  done < <(list_binaries)

  if [ "$SCANNED" -eq 0 ]; then
    fail "package contains binaries to check" "found no $BIN_RE files under $ROOT"
  elif [ ! -s "$VIOLATIONS" ]; then
    pass "all $SCANNED binaries resolve within the package or the OS baseline"
  else
    # Reported per dependency, not per binary: the thing to fix is the library,
    # and one missing DLL can be imported by every binary in the package. A
    # hundred identical failures would bury the handful of distinct causes.
    while IFS='|' read -r dep count examples; do
      fail "'$dep' is bundled or provided by the OS" "$count: $examples"
    done < <(sort "$VIOLATIONS" | awk -F'\t' '
      { n[$1]++; if (n[$1] <= 3) ex[$1] = ex[$1] (ex[$1] == "" ? "" : ", ") $2 }
      END {
        for (d in n) {
          more = (n[d] > 3) ? sprintf(", +%d more", n[d] - 3) : ""
          label = (n[d] == 1) ? "1 binary" : sprintf("%d binaries", n[d])
          printf "%s|%s|%s%s\n", d, label, ex[d], more
        }
      }' | sort)
  fi
  rm -f "$VIOLATIONS"
fi
rm -f "${PE_DEPS:-}" 2>/dev/null

# ---------------------------------------------------------------------------
printf '\n== Summary\n'
printf '  %d passed, %d failed, %d skipped\n' "$PASS" "$FAIL" "$SKIP"
if [ "$FAIL" -gt 0 ]; then
  printf '\nFailed checks:\n'
  printf '  - %s\n' "${FAILED_NAMES[@]}"
  exit 1
fi
echo "  package OK"
