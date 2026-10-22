# Docker Image and Containerized MCP Server

Back to the index: [../PROJECT_CONTEXT.md](../PROJECT_CONTEXT.md). The reference docs for this work are tracked in [docs-ref.md](docs-ref.md).

Goal: a MariaDB Shell container image, published on Docker Hub as `mariadb/mariadb-shell-mcp-server`, that runs the MCP server, working by default with Docker and Podman.

## Key decisions

- **Multi-tenant only.** The image serves `streamable-http` in multi-tenant mode: API keys (`MCP_OAUTH_MODE=none`), the built-in authorization server (`builtin`, "standalone"), or Keycloak. No single-user server, no stdio (user's call: only multi-tenant makes sense in a container). Only the `db` group; no MariaDB server, no migrator.
- **Repackage, don't compile.** `docker/linux-debian13-mcp-server.Dockerfile` downloads the release's Linux tarball (Rocky 9, glibc 2.34, bundles Python and libs) for `TARGETARCH`, checks it against the release `SHA256SUMS` (glibc floor looked up there, not hard-coded), unpacks to `/opt/mariadb-shell`. So multi-arch builds are fast even under emulation: QEMU only runs curl/tar (well under a minute per arch). ~133 MB compressed per arch. `RELEASE_TAG` covers a release whose tag isn't `v<VERSION>`.
- **Base `debian:13-slim`** (user's choice) + `ca-certificates` (bundled OpenSSL looks in `/etc/ssl/certs`), `locales` with `en_US.UTF-8` generated (the shell hard-codes it, see [product-issues.md](product-issues.md)), `tini` (PID 1 signal handling; `stop` takes 0.3 s).
- **User `mcp` uid 10001, volume = whole `/home/mcp`**, because the `login-path` credential store keeps API keys/passwords in `~/.mylogin.cnf`, outside `~/.mariadb-shell`. No Secret Service in a container. `HOME` is set and `/home/mcp` belongs to group 0 too, so another uid (`--user`, OpenShift) can set up a *new* volume; a volume stays with the uid that set it up, because the login-path store writes 0600 files (and keeps an existing file's mode) and uid 10001's files get group 10001.
- **Build context is `docker/`**, not the repo root (the root holds `build/`, `bld/`); `COPY entrypoint-mcp-server.sh`.
- **Entrypoint `docker/entrypoint-mcp-server.sh`:**
  - `start-server` (default CMD) applies the configuration from env, then `exec mariadb-shell -- mcp start-server --host=0.0.0.0 --port=$MCP_PORT …`. A program of the image (`sh`, `bash`) runs as it is; any other command runs `mariadb-shell "$@"` (admin via `exec`).
  - The configuration is applied again only when a fingerprint of the non-secret `MCP_*`/`KEYCLOAK_*` vars (the stamp `~/.mariadb-shell/container-config`) changes. The Keycloak realm is prepared again only when `MCP_OAUTH_MODE`/`MCP_PUBLIC_URL`/`KEYCLOAK_*` change, and the stamp records the realm settings as soon as `setup-keycloak-realm` succeeds (`record_realm`), so the admin password is needed once, even if a later step fails.
  - `check_required` checks every required variable, and the admin password when the realm must be prepared, before the first change, so a wrong configuration leaves the previous one in place.
  - `MCP_LOGIN_SERVERS` is authoritative: the servers of the previous stamp are removed first. Removing `MCP_REQUIRED_ROLE` (or `MCP_PUBLIC_URL` in mode `none`) clears it. What `MCP_INITIAL_USER` and the Keycloak variables created is never undone.
  - Keycloak mode runs `mcp setup-keycloak-realm --nonInteractive --adminPasswordEnv=…`, which also sets the public URL, the mode and the issuer.
  - `KEYCLOAK_ADMIN_PASSWORD(_FILE)` is unset before the server starts (tini and `docker exec` still see the container env). `*_FILE` secrets are checked for readability with a clear error. The API key of `MCP_INITIAL_USER` is never logged.
  - `MCP_TRUSTED_PROXIES` / `KEYCLOAK_LOGIN_THEME` are passed only when set, since releases up to 26.10.1 lack the options.
  - A volume of another uid stops the entrypoint with *belongs to uid …*.
- **Docker Hub repository `mariadb/mariadb-shell-mcp-server`** (https://hub.docker.com/r/mariadb/mariadb-shell-mcp-server), the name the docs use. Public. CI pushes with an organization access token (push/pull on this repository only), in the repository secrets `DOCKERHUB_USERNAME` (`mariadb`: org tokens log in as the org) and `DOCKERHUB_TOKEN`; it can't edit the description. The user has Admin on the repository through the team `mdb-shell-mcp` (set up by IT, 2026-10-22) and maintains the short description and overview by hand. Podman needs the fully qualified `docker.io/…` name.
- **Names: image `mariadb-shell-mcp-server`, file `docker/linux-debian13-mcp-server.Dockerfile`, entrypoint `docker/entrypoint-mcp-server.sh`.** The image is MCP-only, so a plain `mariadb-shell` name would mislead and stays free for a shell image. `-mcp-server`, not `-mcp`: `-mcp` images are usually stdio servers a client launches with `docker run -i`; this one is a long-running HTTP server. "shell" stays in the name to avoid confusion with the separate MariaDB MCP server project (`MariaDB/mcp`) and because tags follow the shell version. The files follow the existing pattern (base first, purpose last, as in `linux-rocky9-server.Dockerfile` / `entrypoint-sandbox-server.sh`), including the `linux-` prefix.
- **CI: `.github/workflows/mcp-server-image.yml`** (`workflow_dispatch` with `version`/`release_tag`/`dry_run`, and `workflow_call`). `publish-release.yml` calls it as job `mcp-server-image` after `publish`, skipped on dry runs: a release created with `GITHUB_TOKEN` starts no `on: release` workflow. One `ubuntu-latest` job: QEMU + buildx, per platform `--load` + `docker/smoke-test-mcp-server.sh`, then one multi-platform `--push` (rebuilt from the builder cache) and an `imagetools` check of both platforms. Tags `X.Y.Z` always, `X.Y` / `latest` only when the version is the newest published of its line / overall (prereleases count, drafts don't), so rebuilding an older release moves neither. No GHCR.
- **Keycloak can't run side by side with builtin** in one server (`lib/auth.py` `build_auth` picks one mode; API keys work in all). Two modes = two servers with two host names; user chose Keycloak only.

## Plugin features the image relies on (mariadb-shell-plugins `mcp_plugin`)

All three need a plugin release later than 26.10.1.

- **`mcp start-server --trustedProxies=<IPs/CIDRs>`**: its own outermost ASGI middleware rewrites `scope["client"]` from `X-Forwarded-For` (right to left, skip trusted, first untrusted wins; all trusted → leftmost; invalid entry or no header → unchanged) only for requests from a trusted peer; uvicorn `proxy_headers` stays off. Refuses bad entries, `/0`, and stdio. Startup log line `[mcp] server: the client address is taken from X-Forwarded-For …`. A loopback peer matches loopback networks of either IP version.
- **Keycloak login theme `mcp_plugin/keycloak-theme/mariadb-mcp`**: the look of the builtin sign-in page (parent `base`; own `template.ftl` + `login.ftl`; CSS = `_PAGE_STYLE` + generic styles; logo `lib/assets/mariadb-logo-vertical.svg`, coloured `#003545` light / `#fff` dark, heading "MCP Server"), with a README; `test_keycloak_theme.py` fails when the CSS or logo drift from `oauth_builtin.py`.
- **`setup-keycloak-realm --loginTheme`**: checks `/admin/serverinfo` before changing the realm, then PUTs `{"loginTheme": …}` only when it differs.

## Verified

- Image: both arches build and run (`--version`); API-key mode: 401 without key, `initialize` + `tools/list` with key; restart skips re-configuration.
- With rootless Podman + podman-compose behind an nginx TLS proxy, with Keycloak 26.8: builtin mode (login server with auto TLS, role `mcp_access`), Keycloak mode set up entirely by the container's first start, Claude Code sign-in via Keycloak (`--client-id mariadb-mcp`), Facebook identity provider linked to an existing MCP user with `mcp setup --addIdentity=oauth:<issuer>|<sub>`, `--trustedProxies` (log shows the client, a forged header is ignored), `KEYCLOAK_LOGIN_THEME` switching a realm from `keycloak.v2` to `mariadb-mcp`, `--passwordStdin` through `podman exec -i`.
- Entrypoint checks in real arm64 containers with rootless Podman and Keycloak 26.8: a missing variable or administrator password stops before any change and keeps the previous configuration; a failed realm step leaves the public URL unset; after a later step fails (an `--addUser` that fails through a `mariadb-shell` shim on `PATH`), the next start needs no password; a volume of uid 10001 refuses uid 10002, which can set up a new volume; in mode `none`, an authenticated request gets 421 without `MCP_ALLOWED_HOSTS` and 200 with it (without a key, 401 comes first). `setup-keycloak-realm` needs an existing realm.
- The Compose files exactly as written in `running-in-a-container.md`, both setups, end to end (tested manually by the user).
- `docker/smoke-test-mcp-server.sh` on 26.10.3: arm64 native (~6 s) and amd64 under QEMU on Colima (~50 s); one QEMU run of five got no API key from `docker exec … --showApiKey`, hence its retry.
- Not verified: Docker's (rootful) source address for published ports, any push to a registry (the CI workflow has not run yet).

## Files

- `docker/linux-debian13-mcp-server.Dockerfile`, `docker/entrypoint-mcp-server.sh`, `docker/README.md` (build, smoke test, CI, modes, env table, reverse proxy), `docker/smoke-test-mcp-server.sh <image> [<platform>]`.
- `.github/workflows/mcp-server-image.yml`, called by `publish-release.yml`; `/release` reports its tags (`.claude/commands/release.md` step 4).
- `docs-ref/content/community/mariadb-shell/mcp-server/running-in-a-container.md` — the user-facing page (see [docs-ref.md](docs-ref.md)).

## Next steps

1. First CI push: merge the workflow, then run `MCP Server Image` by hand for 26.10.3 (a `dry_run` first if wanted). Tell IT if the org token fails; it had never pushed. Then pull and run each arch from Docker Hub.
2. Write the Docker Hub short description and overview (from `docker/README.md`, linking `running-in-a-container.md`).
3. Optional: `MCP_TRUSTED_PROXIES` default for the image? (Kept explicit so far.)

## Gotchas

- **`docker buildx` default builder (`docker` driver) can't build multi-platform images**; create a `docker-container` builder (`docker buildx create --name msh --driver docker-container`). Foreign platforms need QEMU binfmt on the Docker host.
- **BuildKit timed out pulling `docker/dockerfile:1`** once; the Dockerfile has no `# syntax=` line (nothing needs it, Podman ignores it).
- **The server accepts only the `Host` names it knows** (421 otherwise): its own, the public URL's host in `builtin`/`keycloak` mode, and `MCP_ALLOWED_HOSTS`. Mode `none` has no public URL, so behind a proxy it needs `MCP_ALLOWED_HOSTS`; so does a published port that differs from the container port.
- **Bind-mounted secret files keep host owner/mode**: a 600 file is unreadable for uid 10001 → `podman unshare chown 10001:10001 <file>` (rootless Podman) or make it readable.
- **Keycloak's `data/tmp/kc-gzip-cache` survives restarts** and keeps serving the old gzip version of a changed theme stylesheet (browsers ask for gzip; `curl` without it sees the new file). Delete `kc-gzip-cache/*/login/<theme>` after changing theme resources.
- **Keycloak ignores `loginTheme=` (empty)** in a realm update; set another theme to reset.
- **Keycloak refuses anonymous dynamic client registration** (Trusted Hosts policy) → MCP clients use the pre-registered public client: `claude mcp add --client-id mariadb-mcp …`.
- **Behind a reverse proxy in rootless Podman** the peer is an address of the compose network that changes when the container is recreated → trust the subnet.
- **Without a local Node.js**, build the docs in a container: `docker run --rm -v "$PWD/docs-ref:/w" -v docsref-node-modules:/w/node_modules -w /w node:22 sh -c 'npm ci && npm run build'`.
- **A smoke test that uses an empty bash array fails on macOS** (bash 3.2 calls it unbound under `set -u`); `docker/smoke-test-mcp-server.sh` uses `${var:+"$var"}` instead.
