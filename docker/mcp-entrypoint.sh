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

# Entrypoint of the MariaDB Shell image (docker/mariadb-shell.Dockerfile).
#
# `start-server` (the default command) configures a multi-tenant MCP server
# from the MCP_* and KEYCLOAK_* environment variables, then runs it in the
# foreground. Anything else is passed to mariadb-shell as it is, e.g.
#
#   podman exec -it mcp mariadb-shell -- mcp setup --addUser=ada@example.com
#
# The configuration is applied again only when one of those variables changes,
# so the Keycloak administrator password is needed on the first start alone and
# can be removed afterwards. See docker/README.md for the variables.

set -euo pipefail

if [ "${1:-}" != "start-server" ]; then
  exec mariadb-shell "$@"
fi
shift

msh() {
  mariadb-shell -- "$@"
}

die() {
  echo "mcp-entrypoint: $*" >&2
  exit 1
}

# The value of $1, or the contents of the file named by $1_FILE (for podman
# and docker secrets, which are mounted as files).
secret() {
  local file_var="${1}_FILE"
  if [ -n "${!file_var:-}" ]; then
    # A bind-mounted file keeps its host owner and mode, so it is a common
    # mistake that the container's user cannot read it.
    [ -r "${!file_var}" ] ||
      die "$file_var: cannot read ${!file_var} as uid $(id -u); make it readable by that user"
    cat "${!file_var}"
  else
    printf '%s' "${!1:-}"
  fi
}

MODE="${MCP_OAUTH_MODE:-none}"
STAMP="$HOME/.mariadb-shell/container-config"

# Everything that decides the configuration, and nothing secret: the stamp is
# a plain file in the volume.
fingerprint() {
  env | grep -E '^(MCP_(OAUTH_MODE|PUBLIC_URL|LOGIN_SERVERS|REQUIRED_ROLE|INITIAL_USER)|KEYCLOAK_(URL|REALM|ADMIN_REALM|ADMIN_USER|CLIENT_ID|GRANT_ROLE_TO|LOGIN_THEME))=' | sort
}

configure() {
  echo "mcp-entrypoint: applying the configuration ($MODE)" >&2
  msh mcp setup --multiTenant=true

  case "$MODE" in
    none)
      # API keys only.
      msh mcp setup-oauth --mode=none ${MCP_PUBLIC_URL:+--publicUrl="$MCP_PUBLIC_URL"}
      ;;

    builtin)
      [ -n "${MCP_PUBLIC_URL:-}" ] || die "MCP_OAUTH_MODE=builtin needs MCP_PUBLIC_URL"
      [ -n "${MCP_LOGIN_SERVERS:-}" ] || die "MCP_OAUTH_MODE=builtin needs MCP_LOGIN_SERVERS"
      msh mcp setup-oauth --mode=builtin --publicUrl="$MCP_PUBLIC_URL" \
        --addLoginServer="$MCP_LOGIN_SERVERS" \
        ${MCP_REQUIRED_ROLE+--requiredRole="$MCP_REQUIRED_ROLE"}
      ;;

    keycloak)
      local var
      for var in MCP_PUBLIC_URL KEYCLOAK_URL KEYCLOAK_REALM KEYCLOAK_ADMIN_USER; do
        [ -n "${!var:-}" ] || die "MCP_OAUTH_MODE=keycloak needs $var"
      done
      MCP_KEYCLOAK_ADMIN_PASSWORD="$(secret KEYCLOAK_ADMIN_PASSWORD)"
      [ -n "$MCP_KEYCLOAK_ADMIN_PASSWORD" ] ||
        die "preparing the Keycloak realm needs KEYCLOAK_ADMIN_PASSWORD or KEYCLOAK_ADMIN_PASSWORD_FILE"
      export MCP_KEYCLOAK_ADMIN_PASSWORD
      msh mcp setup-oauth --publicUrl="$MCP_PUBLIC_URL"
      msh mcp setup-keycloak-realm --nonInteractive \
        --server="$KEYCLOAK_URL" --realm="$KEYCLOAK_REALM" \
        --adminRealm="${KEYCLOAK_ADMIN_REALM:-master}" \
        --adminUser="$KEYCLOAK_ADMIN_USER" \
        --adminPasswordEnv=MCP_KEYCLOAK_ADMIN_PASSWORD \
        --mcpUrl="$MCP_PUBLIC_URL" \
        ${KEYCLOAK_CLIENT_ID:+--clientId="$KEYCLOAK_CLIENT_ID"} \
        ${KEYCLOAK_GRANT_ROLE_TO:+--grantRealmRoleTo="$KEYCLOAK_GRANT_ROLE_TO"} \
        ${KEYCLOAK_LOGIN_THEME:+--loginTheme="$KEYCLOAK_LOGIN_THEME"}
      unset MCP_KEYCLOAK_ADMIN_PASSWORD
      ;;

    *)
      die "MCP_OAUTH_MODE must be none, builtin or keycloak, not '$MODE'"
      ;;
  esac

  # A server with neither a user nor an OAuth2 mode that creates users at
  # sign-in refuses to start. The API key is not printed to the log; the
  # administrator reads it with --showApiKey.
  if [ -n "${MCP_INITIAL_USER:-}" ] &&
     ! msh mcp setup --show --user="$MCP_INITIAL_USER" > /dev/null 2>&1; then
    msh mcp setup --addUser="$MCP_INITIAL_USER" > /dev/null
    echo "mcp-entrypoint: user $MCP_INITIAL_USER added; show the API key with:" >&2
    echo "  <podman|docker> exec <container> mariadb-shell -- mcp setup --showApiKey=$MCP_INITIAL_USER" >&2
  fi

  fingerprint > "$STAMP"
}

mkdir -p "$(dirname "$STAMP")"
if ! fingerprint | cmp -s - "$STAMP"; then
  configure
fi

# Inside a container the server has to listen on all addresses to be reachable
# through a published port. Publish it on the host's loopback address only and
# put a TLS-terminating reverse proxy in front of it, or pass --sslCertfile and
# --sslKeyfile. Behind a proxy, MCP_TRUSTED_PROXIES names the addresses the
# proxy's requests arrive from, so that the server takes the client's address
# from X-Forwarded-For instead of seeing every client as the proxy.
exec mariadb-shell -- mcp start-server \
  --host=0.0.0.0 --port="${MCP_PORT:-8080}" \
  ${MCP_ALLOWED_HOSTS:+--allowedHosts="$MCP_ALLOWED_HOSTS"} \
  ${MCP_MAX_CONNECTIONS:+--maxConnections="$MCP_MAX_CONNECTIONS"} \
  ${MCP_TRUSTED_PROXIES:+--trustedProxies="$MCP_TRUSTED_PROXIES"} \
  "$@"
