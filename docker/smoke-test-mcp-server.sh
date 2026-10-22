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

# Smoke test of a MariaDB Shell MCP Server image (linux-debian13-mcp-server.Dockerfile):
# starts it in API-key mode with a throwaway volume and checks that the server
# refuses a request without a key and answers initialize and tools/list with one.
#
#   docker/smoke-test-mcp-server.sh <image> [<platform>]
#
# The platform defaults to the host's; a foreign one runs under QEMU, which is
# slower to start, hence the generous wait.

set -euo pipefail

IMAGE=${1:?usage: $0 <image> [<platform>]}
PLATFORM=${2:-}
NAME=mcp-smoke-$$
VOLUME=mcp-smoke-$$
PORT=${SMOKE_PORT:-18080}
USER_NAME=smoke@example.com

cleanup() {
  if [ "${status:-0}" -ne 0 ]; then
    echo "==> Container log:" >&2
    docker logs "$NAME" >&2 2>&1 || true
  fi
  docker rm -f "$NAME" > /dev/null 2>&1 || true
  docker volume rm "$VOLUME" > /dev/null 2>&1 || true
}
trap 'status=$?; cleanup' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

# A string, not an array: bash 3.2 (macOS) calls an empty array unbound under -u.
platform_arg=${PLATFORM:+--platform=$PLATFORM}

echo "==> $IMAGE ${PLATFORM:+($PLATFORM)}: version"
docker run --rm ${platform_arg:+"$platform_arg"} "$IMAGE" --version

echo "==> Starting the server in API-key mode"
docker run -d ${platform_arg:+"$platform_arg"} --name "$NAME" -v "$VOLUME:/home/mcp" \
  -p "127.0.0.1:$PORT:8080" \
  -e MCP_INITIAL_USER="$USER_NAME" \
  -e MCP_ALLOWED_HOSTS="localhost:$PORT" \
  "$IMAGE" > /dev/null

URL="http://localhost:$PORT/mcp"
INIT='{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"smoke-test","version":"1"}}}'

code=000
for _ in $(seq 1 90); do
  [ "$(docker inspect -f '{{.State.Running}}' "$NAME")" = true ] || fail "the container stopped"
  code=$(curl -s -o /dev/null -w '%{http_code}' -X POST "$URL" \
    -H 'Content-Type: application/json' \
    -H 'Accept: application/json, text/event-stream' -d "$INIT" || true)
  [ "$code" != 000 ] && break
  sleep 2
done
[ "$code" = 401 ] || fail "a request without an API key got $code, expected 401"
echo "    without a key: 401"

# Retried: one run under QEMU once came back without a key.
KEY=
for _ in 1 2 3; do
  shown=$(docker exec "$NAME" mariadb-shell -- mcp setup --showApiKey="$USER_NAME" 2>&1) || true
  KEY=$(grep -o 'mdbmcp_[A-Za-z0-9_-]*' <<< "$shown" | head -n 1) || true
  [ -n "$KEY" ] && break
  sleep 2
done
[ -n "$KEY" ] || fail "no API key shown for $USER_NAME: $shown"

headers=$(mktemp)
body=$(curl -s -D "$headers" -X POST "$URL" \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json, text/event-stream' \
  -H "Authorization: Bearer $KEY" -d "$INIT")
grep -q '"serverInfo"' <<< "$body" || fail "initialize with the key: $body"
SESSION=$(tr -d '\r' < "$headers" | awk 'tolower($1) == "mcp-session-id:" {print $2}')
rm -f "$headers"
echo "    initialize: ok"

session_header=${SESSION:+Mcp-Session-Id: $SESSION}
curl -s -o /dev/null -X POST "$URL" ${session_header:+-H "$session_header"} \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json, text/event-stream' \
  -H "Authorization: Bearer $KEY" \
  -d '{"jsonrpc":"2.0","method":"notifications/initialized"}'
body=$(curl -s -X POST "$URL" ${session_header:+-H "$session_header"} \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json, text/event-stream' \
  -H "Authorization: Bearer $KEY" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list"}')
grep -q '"tools"' <<< "$body" || fail "tools/list with the key: $body"
echo "    tools/list: $(grep -o '"name":"[^"]*"' <<< "$body" | wc -l | tr -d ' ') tools"

echo "==> OK"
