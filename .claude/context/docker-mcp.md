# Docker Image and Containerized MCP Server

Back to the index: [../PROJECT_CONTEXT.md](../PROJECT_CONTEXT.md). The reference docs for this work are tracked in [docs-ref.md](docs-ref.md).

Started 2026-10-12. Goal: a MariaDB Shell container image (Docker Hub, later) that runs the MCP server, working by default with Docker and Podman.

## Key decisions

- **Multi-tenant only.** The image serves `streamable-http` in multi-tenant mode: API keys (`MCP_OAUTH_MODE=none`), the built-in authorization server (`builtin`, "standalone"), or Keycloak. No single-user server, no stdio (user's call: only multi-tenant makes sense in a container). Only the `db` group; no MariaDB server, no migrator.
- **Repackage, don't compile.** `docker/mariadb-shell.Dockerfile` downloads the release's Linux tarball (Rocky 9, glibc 2.34, bundles Python and libs) for `TARGETARCH`, checks it against the release `SHA256SUMS` (glibc floor looked up there, not hard-coded), unpacks to `/opt/mariadb-shell`. So multi-arch builds are fast even under emulation: QEMU only runs curl/tar (well under a minute per arch). ~133 MB compressed per arch.
- **Base `debian:13-slim`** (user's choice) + `ca-certificates` (bundled OpenSSL looks in `/etc/ssl/certs`), `locales` with `en_US.UTF-8` generated (the shell hard-codes it, see [product-issues.md](product-issues.md)), `tini` (PID 1 signal handling; `stop` takes 0.3 s).
- **User `mcp` uid 10001, volume = whole `/home/mcp`**, because the `login-path` credential store keeps API keys/passwords in `~/.mylogin.cnf`, outside `~/.mariadb-shell`. No Secret Service in a container.
- **Build context is `docker/`**, not the repo root (the root holds `build/`, `bld/`); `COPY mcp-entrypoint.sh`.
- **Entrypoint `docker/mcp-entrypoint.sh`:** `start-server` (default CMD) applies config from env, then `exec mariadb-shell -- mcp start-server --host=0.0.0.0 --port=$MCP_PORT …`; any other command runs `mariadb-shell "$@"` (admin via `exec`). Config is re-applied only when a fingerprint of the non-secret `MCP_*`/`KEYCLOAK_*` vars (stored in `~/.mariadb-shell/container-config`) changes, so the Keycloak admin password is needed on the first start only. Keycloak mode runs `mcp setup-keycloak-realm --nonInteractive --adminPasswordEnv=…`. `*_FILE` secrets are checked for readability with a clear error. `MCP_TRUSTED_PROXIES` / `KEYCLOAK_LOGIN_THEME` are passed only when set (26.10.1 lacks the options). The API key of `MCP_INITIAL_USER` is never logged.
- **Docker Hub namespace undecided** (user is asking IT). Docs use `mariadb/mariadb-shell` as a placeholder; Podman needs the fully qualified `docker.io/…` name.
- **Keycloak can't run side by side with builtin** in one server (`lib/auth.py` `build_auth` picks one mode; API keys work in all). Two modes = two servers with two host names; user chose Keycloak only.

## Plugin changes made for this (mariadb-shell-plugins PRs #46 and #47, both against `main`)

- **#46, branch `mcp-trusted-proxies`** (`daf64900`, from main `f1b3b6e2`): `mcp start-server --trustedProxies=<IPs/CIDRs>`. Own outermost ASGI middleware rewrites `scope["client"]` from `X-Forwarded-For` (right to left, skip trusted, first untrusted wins; all trusted → leftmost; invalid entry or no header → unchanged) only for requests from a trusted peer; uvicorn `proxy_headers` stays off. Refuses bad entries, `/0`, and stdio. Startup log line `[mcp] server: the client address is taken from X-Forwarded-For …`. 18 tests; the sandbox-backed e2e test has not run yet (needs a `mariadbd` on the test machine). Open question for review: a loopback peer matches loopback networks of either IP version.
- **#47, branch `mcp-keycloak-theme`** (from main): `4fd15092` adds `mcp_plugin/keycloak-theme/mariadb-mcp` (Keycloak login theme in the look of the builtin sign-in page; parent `base`; own `template.ftl` + `login.ftl`; CSS = `_PAGE_STYLE` + generic styles; seal from `lib/assets/mariadb-seal.svg`) with README and `test_keycloak_theme.py` (fails when CSS/seal drift from `oauth_builtin.py`). `6602c1f7` adds `setup-keycloak-realm --loginTheme`: checks `/admin/serverinfo` before changing the realm, then PUTs `{"loginTheme": …}` only when it differs. Full suite: 588 passed, 17 skipped.
- Both branches need a release after 26.10.1 before the image has the options. 26.10.1 also still advertises `msm` tools/scope in multi-tenant mode (fixed by plugins #42 after that release).

## Verified (2026-10-12)

- Image: both arches build and run (`--version`); API-key mode: 401 without key, `initialize` + `tools/list` with key; restart skips re-configuration.
- With rootless Podman + podman-compose behind an nginx TLS proxy, with Keycloak 26.8: builtin mode (login server with auto TLS, role `mcp_access`), Keycloak mode set up entirely by the container's first start, Claude Code sign-in via Keycloak (`--client-id mariadb-mcp`), Facebook identity provider linked to an existing MCP user with `mcp setup --addIdentity=oauth:<issuer>|<sub>`, `--trustedProxies` (log shows the client, a forged header is ignored), `KEYCLOAK_LOGIN_THEME` switching a realm from `keycloak.v2` to `mariadb-mcp`, `--passwordStdin` through `podman exec -i`.
- Not verified: the Compose files exactly as written in `running-in-a-container.md` (Docker, `mariadb:11.8`, fixed subnet `172.30.0.0/24`, `extra_hosts: host-gateway`, `kcadm` via `docker compose exec`, the password prompt in `docker compose exec`), Docker's (rootful) source address for published ports, any push to a registry.

## Files

- `docker/mariadb-shell.Dockerfile`, `docker/mcp-entrypoint.sh`, `docker/README.md` (build, modes, env table, reverse proxy) — branch `docker/mcp-image`, PR to `main`.
- `docs-ref/content/mariadb-shell/mcp-server/running-in-a-container.md` — the user-facing page (see [docs-ref.md](docs-ref.md)).

## Next steps

1. Review and merge plugins #46 (security-relevant: trusted proxies) and #47; then this repo's PR (Docker files + docs). None of them conflicts with the other open PRs (tested 2026-10-12 against plugins #41/#43/#44/#45 and shell #71/#73/#74/#75).
2. CI workflow for the image: on release published / `workflow_dispatch`, QEMU + buildx, smoke test per arch, push to Docker Hub (needs namespace + token from IT), optionally GHCR. Tags `X.Y.Z`, `X.Y`, `latest` (user: `latest` may point at prereleases).
3. Run the docs' Compose files end to end (both setups) under Docker before publishing.
4. Optional: `MCP_TRUSTED_PROXIES` default for the image? (Kept explicit so far.)

## Gotchas

- **`docker buildx` default builder (`docker` driver) can't build multi-platform images**; create a `docker-container` builder (`docker buildx create --name msh --driver docker-container`). Foreign platforms need QEMU binfmt on the Docker host.
- **BuildKit timed out pulling `docker/dockerfile:1`** once; the `# syntax=` line was dropped (nothing needs it, Podman ignores it).
- **A published port that differs from the container port needs `MCP_ALLOWED_HOSTS`** (421 otherwise); behind a proxy the public URL's host is accepted automatically.
- **Bind-mounted secret files keep host owner/mode**: a 600 file is unreadable for uid 10001 → `podman unshare chown 10001:10001 <file>` (rootless Podman) or make it readable.
- **Keycloak's `data/tmp/kc-gzip-cache` survives restarts** and keeps serving the old gzip version of a changed theme stylesheet (browsers ask for gzip; `curl` without it sees the new file). Delete `kc-gzip-cache/*/login/<theme>` after changing theme resources.
- **Keycloak ignores `loginTheme=` (empty)** in a realm update; set another theme to reset.
- **Keycloak refuses anonymous dynamic client registration** (Trusted Hosts policy) → MCP clients use the pre-registered public client: `claude mcp add --client-id mariadb-mcp …`.
- **Behind a reverse proxy in rootless Podman** the peer is an address of the compose network that changes when the container is recreated → trust the subnet.
- **Without a local Node.js**, build the docs in a container: `docker run --rm -v "$PWD/docs-ref:/w" -v docsref-node-modules:/w/node_modules -w /w node:22 sh -c 'npm ci && npm run build'`.
