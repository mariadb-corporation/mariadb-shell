# Docker Images

This directory holds two kinds of images:

| File | Purpose |
| --- | --- |
| `mariadb-shell.Dockerfile`, `mcp-entrypoint.sh` | The **MariaDB Shell image**: runs the MariaDB MCP server in multi-tenant mode. Described below. |
| `linux-rocky9.Dockerfile`, `entrypoint.sh` | Build environment for the Linux release packages (`rocky-linux.yml`). |
| `linux-rocky9-server.Dockerfile`, `entrypoint-sandbox-server.sh` | Build environment for the sandbox server packages. |

## The MariaDB Shell Image

The image contains a MariaDB Shell release and starts a **multi-tenant MCP server** over streamable HTTP. Every request needs an API key or an OAuth2 access token, and each user has their own connections and passwords. It doesn't serve a single-user server or the `stdio` transport: an MCP client on your own machine can start `mariadb-shell -- mcp start-server --transport=stdio` directly.

The image is for `linux/amd64` and `linux/arm64`, based on `debian:13-slim`. It provides only the `db` tools, the only group a multi-tenant server serves. It contains no MariaDB server and no migration tooling.

### Build

Nothing is compiled. The build downloads the release's Linux tarball for each platform from GitHub, checks it against the release's `SHA256SUMS`, and unpacks it. A build for a foreign architecture runs under QEMU, which takes under a minute.

```bash
# One platform, loaded into the local image store
docker buildx build --platform linux/arm64 --load \
  --build-arg VERSION=26.10.1 -t mariadb-shell:26.10.1 \
  -f docker/mariadb-shell.Dockerfile docker

# Both platforms, pushed as one multi-arch image
docker buildx create --name msh --driver docker-container   # once; the default builder can't build multi-platform images
docker buildx build --builder msh --platform linux/amd64,linux/arm64 --push \
  --build-arg VERSION=26.10.1 -t <registry>/<namespace>/mariadb-shell:26.10.1 \
  -f docker/mariadb-shell.Dockerfile docker
```

The build context is `docker/`, not the repository root. `VERSION` must name a published release (tag `v<VERSION>`).

On Colima, foreign-platform builds need QEMU emulation, which Colima sets up by default. To move an image to a machine without a registry, run `docker save mariadb-shell:26.10.1 | ssh <host> podman load`.

Podman and other clients resolve short image names differently. Pull with the fully qualified name, for example `docker.io/<namespace>/mariadb-shell:26.10.1`.

### Run

The server listens on port 8080 in the container and keeps all state in the volume at `/home/mcp`. The volume holds the configuration, the users, and the credential store (`~/.mylogin.cnf`), which contains the users' API keys and connection passwords. Back it up, and don't share it.

Publish the port on the loopback address only, and put a TLS-terminating reverse proxy in front of it (see [Reverse Proxy](#reverse-proxy)). Every request carries a user's credentials, and the server in the container serves plain HTTP. At startup it warns about this, which is expected behind a proxy.

On its first start, the container configures the server from environment variables. It applies them again only when one of them changes. Choose one of three modes with `MCP_OAUTH_MODE`.

#### API keys only

```bash
docker run -d --name mcp -p 127.0.0.1:8080:8080 -v mcp-data:/home/mcp \
  -e MCP_INITIAL_USER=ada@example.com \
  mariadb-shell:26.10.1

docker exec mcp mariadb-shell -- mcp setup --showApiKey=ada@example.com
docker exec mcp mariadb-shell -- mcp setup --user=ada@example.com \
  --addConnection='mariadb://ada@db.example.com:3306' --passwordEnv=ADA_DB_PASSWORD
```

A server in this mode needs at least one user to start. `MCP_INITIAL_USER` creates one on the first start. The API key isn't written to the log; read it with `--showApiKey`. For `--passwordEnv`, pass the variable with `docker exec -e ADA_DB_PASSWORD=…`, or run `mcp setup` interactively with `docker exec -it`.

#### Built-in sign-in (standalone)

The MCP server is its own OAuth2 authorization server. Users sign in in their browser with their MariaDB user name and password, and their sessions run as that account:

```bash
docker run -d --name mcp -p 127.0.0.1:8080:8080 -v mcp-data:/home/mcp \
  -e MCP_OAUTH_MODE=builtin \
  -e MCP_PUBLIC_URL=https://mcp.example.com/mcp \
  -e MCP_LOGIN_SERVERS='mariadb://db.example.com:3306' \
  -e MCP_REQUIRED_ROLE=mcp_access \
  mariadb-shell:26.10.1
```

Users need no setup on the MCP server. They're created at their first sign-in. The login servers must accept TLS connections.

#### Keycloak

Keycloak signs users in. On the first start, the container runs `mcp setup-keycloak-realm` with the Keycloak administrator's credentials. That creates the client scope `mcp:db`, the realm role `mcp-user`, and the public client `mariadb-mcp` in an **existing** realm, and points the server at the realm:

```bash
printf '%s' "$KC_ADMIN_PASSWORD" | podman secret create kc-admin-password -
podman run -d --name mcp -p 127.0.0.1:8080:8080 -v mcp-data:/home/mcp \
  -e MCP_OAUTH_MODE=keycloak \
  -e MCP_PUBLIC_URL=https://mcp.example.com/mcp \
  -e KEYCLOAK_URL=https://kc.example.com \
  -e KEYCLOAK_REALM=mariadb \
  -e KEYCLOAK_ADMIN_USER=admin \
  -e KEYCLOAK_ADMIN_PASSWORD_FILE=/run/secrets/kc-admin-password \
  --secret kc-admin-password,uid=10001,mode=0400 \
  mariadb-shell:26.10.1
```

The server runs as uid 10001 in the container. A password file that is bind-mounted instead, as with Docker, keeps its owner and mode from the host, so it has to be readable by that user. Otherwise the container stops with `cannot read … as uid 10001`.

The password is needed for the first start only. Remove it afterwards, for example with `podman secret rm kc-admin-password`. The container starts without it, as long as the Keycloak settings stay the same. Users who hold the realm role `mcp-user` are created at their first sign-in, but without connections. Add their connections with `mcp setup --user=<email> --addConnection=…`.

The container must reach `KEYCLOAK_URL` under the same name that the realm's issuer uses. Keycloak issues tokens for that issuer, and the server reads the realm's keys from it.

### Environment Variables

| Variable | Mode | Description |
| --- | --- | --- |
| `MCP_OAUTH_MODE` | all | `none` (API keys only, the default), `builtin`, or `keycloak`. API keys work in every mode. |
| `MCP_PUBLIC_URL` | `builtin`, `keycloak` | The URL clients reach the MCP endpoint at, including `/mcp`, for example `https://mcp.example.com/mcp`. Tokens are issued for it, and its host name is accepted in the `Host` header. |
| `MCP_INITIAL_USER` | all | A user to create on the first start, named by email address or user ID. |
| `MCP_LOGIN_SERVERS` | `builtin` | Comma-separated MariaDB servers that users sign in to, as connection URIs without a user. |
| `MCP_REQUIRED_ROLE` | `builtin` | The MariaDB role that an account must hold to sign in. |
| `KEYCLOAK_URL` | `keycloak` | The Keycloak base URL, for example `https://kc.example.com`. |
| `KEYCLOAK_REALM` | `keycloak` | The realm to prepare. It must exist. |
| `KEYCLOAK_ADMIN_REALM` | `keycloak` | The realm the administrator signs in to. Default: `master`. |
| `KEYCLOAK_ADMIN_USER` | `keycloak` | The Keycloak administrator. |
| `KEYCLOAK_ADMIN_PASSWORD`, `KEYCLOAK_ADMIN_PASSWORD_FILE` | `keycloak` | The administrator's password, or a file that contains it (for Docker and Podman secrets). Prefer the file. |
| `KEYCLOAK_CLIENT_ID` | `keycloak` | The client created for MCP clients. Default: `mariadb-mcp`. |
| `KEYCLOAK_GRANT_ROLE_TO` | `keycloak` | Comma-separated Keycloak users to give the realm role `mcp-user`. |
| `KEYCLOAK_LOGIN_THEME` | `keycloak` | A login theme to select for the realm, for example `mariadb-mcp`, which gives Keycloak's sign-in pages the look of the MCP server's own. The theme must be installed on the Keycloak server; the image contains it in `/opt/mariadb-shell/lib/mariadb-shell/plugins/mcp_plugin/keycloak-theme`, with installation notes. Needs a release whose `mcp setup-keycloak-realm` has `--loginTheme` (later than 26.10.1). |
| `MCP_TRUSTED_PROXIES` | all | Comma-separated addresses or networks of the reverse proxy, whose `X-Forwarded-For` header the server trusts. See [Reverse Proxy](#reverse-proxy). Needs a release whose `mcp start-server` has `--trustedProxies` (later than 26.10.1). |
| `MCP_ALLOWED_HOSTS` | all | Additional `Host` header values to accept, for example `127.0.0.1:18080` to reach a port published as 18080 directly, without the proxy. |
| `MCP_MAX_CONNECTIONS` | all | The most database connections the server holds open, for all users together. Default: 64. |
| `MCP_PORT` | all | The port in the container. Default: 8080. |

Further arguments after `start-server` go to `mcp start-server`, for example `docker run … mariadb-shell:26.10.1 start-server --sslCertfile=… --sslKeyfile=…` to serve HTTPS from the container itself.

Any other command runs `mariadb-shell` with its arguments. This is how you administer the server:

```bash
docker exec mcp mariadb-shell -- mcp setup --show --allUsers
docker exec mcp mariadb-shell -- mcp setup-oauth --show
docker exec -it mcp mariadb-shell -- mcp setup           # interactive menu
```

A running server picks up changes to users, connections, and scopes at once. Changes to the OAuth2 configuration need a restart: `docker restart mcp`.

### Reverse Proxy

Proxy everything to the container, not just `/mcp`. The OAuth2 metadata is under `/.well-known/`, and in `builtin` mode the sign-in and token endpoints are at the root. Pass the original `Host` header on, and don't buffer responses, because the streamable HTTP transport answers with server-sent events. An example for nginx:

```nginx
location / {
    proxy_pass http://127.0.0.1:8080;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header Connection "";
    proxy_buffering off;
    proxy_read_timeout 1h;
    proxy_send_timeout 1h;
}
```

Behind a proxy, the server sees every request as coming from the proxy. That merges all clients for the per-address limits: the sign-in rate limit, the throttle on refused tokens, and `allowedClientNetworks`. Set `MCP_TRUSTED_PROXIES` to the address that the proxy's requests arrive from inside the container. The server then takes the client's address from `X-Forwarded-For`, but only on requests from that address. With a published port, this is usually an address in the container network, not the proxy's own address on the host. The server's access log shows it on every request.
