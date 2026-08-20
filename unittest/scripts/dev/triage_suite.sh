#!/bin/zsh
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

# Runs one scripted suite and prints a compact census of its failures.
#
# The raw logs are tens of thousands of lines, which is expensive to read; this
# collapses them to one line per distinct cause, with counts and the first chunk
# each cause appeared in. Full log path is printed at the end for the cases where
# the detail is actually needed.
#
#   unittest/scripts/dev/triage_suite.sh util_dump_schemas_norecord [more...]
#
# Environment (defaults are this machine's):
#   SERVER_BIN  directory holding mariadbd, put on PATH for sandbox deploys
#   MYSQL_PORT  the main test server
#   BUILD       build directory holding bin/run_unit_tests
#   OUT         where to write the logs
set -u

REPO=${REPO:-$(cd "$(dirname "$0")/../../.." && pwd)}
BUILD=${BUILD:-$REPO/bld}
OUT=${OUT:-${TMPDIR:-/tmp}/suite-logs}
SERVER_BIN=${SERVER_BIN:-/Users/juanram/servers/12.3.2/bin}

mkdir -p $OUT
export PATH="$SERVER_BIN:$PATH"
export MYSQL_PORT=${MYSQL_PORT:-3315}
export MYSQLSH_TEST_HOME=$REPO/unittest

# A suite which aborts part way can leave a sandbox listening on one of the
# reserved ports, and the harness then refuses to start at all ("mysqld running
# on port reserved for sandbox tests"). Clear them first, so a run never fails
# for the previous run's mess. Only the sandbox ports are touched, never the
# main test server.
free_sandbox_ports() {
  local ports="3325,3335,3345,3355,3365,3375"
  local pids
  pids=$(lsof -nP -iTCP:$ports -sTCP:LISTEN 2>/dev/null | awk 'NR>1 {print $2}' | sort -u)
  [[ -z $pids ]] && return 0
  print -- "--- terminating leftover sandboxes: $(print -- $pids | tr '\n' ' ')"
  kill ${=pids} 2>/dev/null
  for i in {1..15}; do
    lsof -nP -iTCP:$ports -sTCP:LISTEN >/dev/null 2>&1 || break
    sleep 1
  done
  for p in ${(s:,:)ports}; do rm -rf ${TMPDIR:-/tmp}/$p; done
}

free_sandbox_ports

for suite in "$@"; do
  log=$OUT/$suite.log
  start=$(date +%s)
  $BUILD/bin/run_unit_tests --gtest_filter="*${suite}*" > $log 2>&1
  elapsed=$(( $(date +%s) - start ))

  blocks=$(grep -c "BEGIN FAILURE" $log)
  verdict=$(grep -oE "\[  (PASSED|FAILED)  \] [0-9]+ test" $log | head -1)

  if [[ -z $verdict ]]; then
    # the harness never got to the test: a sandbox left listening on a reserved
    # port, a missing server binary, no main server ... "0 failures" would read
    # as success here, so say what actually happened instead
    print -- "=== $suite: DID NOT RUN, ${elapsed}s"
    tail -n 12 $log | sed 's/^/    /'
    print -- "--- log: $log"
    continue
  fi

  print -- "=== $suite: $verdict, $blocks failure blocks, ${elapsed}s"

  if [[ $blocks -gt 0 ]]; then
    # one line per distinct cause: strip identifiers, numbers and paths so the
    # same problem in twenty places collapses to one line
    grep -A 8 "BEGIN FAILURE" $log \
      | grep -E "Unexpected Error|Missing output|Missing match|Missing log|Exception expected|Unexpected exception|Actual:|Error \([0-9]+\)|[A-Za-z]+Error:" \
      | sed -E 's/[0-9]{3,}/N/g; s/`[^`]*`/X/g; s/'"'"'[^'"'"']*'"'"'/Y/g; s#/[^ ]*/##g' \
      | sort | uniq -c | sort -rn | head -20

    print -- "--- first failing chunks:"
    grep -B 2 -A 4 "BEGIN FAILURE" $log | grep -oE "while executing chunk: .*|chunk \"[^\"]*\"" \
      | sed -E 's/ at .*//' | awk '!seen[$0]++' | head -8
  fi

  print -- "--- log: $log"
done
