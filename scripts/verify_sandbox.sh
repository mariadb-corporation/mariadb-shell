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
# Run the packaged sandbox verification.
#
#   scripts/verify_sandbox.sh
#
# Expects a packaged MariaDB Shell and a packaged MariaDB sandbox server to be
# on the PATH already -- the shell because that is what runs the plugin, the
# server because locating mariadbd on the PATH is the code path the sandbox
# plugin uses by default and therefore the one worth verifying.
#
# Environment:
#   SBX_DIR         sandbox directory (required)
#   SBX_PORT        port to deploy on (default: a free one)
#   SBX_PASSWORD    root password for the deployed instance
#   MARIADB_SHELL   shell to run (default: mariadb-shell, from the PATH)
#   SBX_LOG         where to write the transcript (default: ./sandbox-verification.log)
#
# Exits non-zero unless the driver both exits 0 and reports its success
# sentinel: a shell that reports success for a script that raised would
# otherwise turn a failed verification into a passing job.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DRIVER="$SCRIPT_DIR/verify_sandbox.py"
SHELL_BIN="${MARIADB_SHELL:-mariadb-shell}"
LOG="${SBX_LOG:-$PWD/sandbox-verification.log}"
PASS_SENTINEL="SANDBOX_VERIFICATION_PASSED"

[ -f "$DRIVER" ] || { echo "missing driver script: $DRIVER" >&2; exit 2; }
[ -n "${SBX_DIR:-}" ] || { echo "SBX_DIR is not set" >&2; exit 2; }

RESOLVED="$(command -v "$SHELL_BIN" 2>/dev/null)"
if [ -z "$RESOLVED" ]; then
  echo "'$SHELL_BIN' is not on the PATH" >&2
  echo "PATH=$PATH" >&2
  exit 2
fi

echo "==> shell:        $RESOLVED"
echo "==> server:       $(command -v mariadbd 2>/dev/null || command -v mysqld 2>/dev/null || echo '(none on PATH)')"
echo "==> sandbox dir:  $SBX_DIR"
echo "==> running as:   $(id -un 2>/dev/null || echo unknown)"
echo ""

# --quiet-start=2 suppresses the startup banner so the transcript is only the
# driver's own output. stderr is folded into stdout: the plugin logs warnings
# there, and they belong in the same transcript as the step they came from.
"$RESOLVED" --quiet-start=2 --py -f "$DRIVER" 2>&1 | tee "$LOG"
RC=${PIPESTATUS[0]}

if ! grep -q "$PASS_SENTINEL" "$LOG"; then
  echo ""
  echo "Verification did not report $PASS_SENTINEL." >&2
  RC=1
fi

if [ "$RC" -ne 0 ]; then
  # Whatever survived in the sandbox directory is the only record of a failure
  # that happened before the driver could read the error log itself.
  echo ""
  # Two levels only, and capped: a failed deployment leaves a whole initialized
  # data directory behind, and dumping all of it buries the part that matters
  # (which instance directories exist, and whether they hold an error log).
  echo "--- $SBX_DIR, 2 levels deep ---" >&2
  find "$SBX_DIR" -maxdepth 2 2>/dev/null | head -60 >&2 || true
  echo ""
  echo "SANDBOX VERIFICATION FAILED (exit $RC)" >&2
  exit 1
fi

echo ""
echo "SANDBOX VERIFICATION PASSED"
