# Reference Docs (`docs-ref/`)

Back to the index: [../PROJECT_CONTEXT.md](../PROJECT_CONTEXT.md). The product bugs that the docs work turned up are in [product-issues.md](product-issues.md).

## Architecture / key decisions

- **`docs-ref/` copies the ai-plugins setup exactly.** The source is `/Users/mzinner/git/ai-plugins/docs-ref`: same packages, lockfile, theme (`src/css/mariadb-gitbook.css`), remark extras and npm scripts. Only the names were changed: the REPO URL, the tagline, the announcement bar, and the `static/index.html` redirect to `tools/mariadb-shell/`. Keep the two copies in step.
- **`content/SUMMARY.md` is the only definition of the page tree.** `sidebars.js` generates the sidebar from it.
- **Excluded features.** Docs cover only what the build ships. JavaScript, X DevAPI/mysqlx, AdminAPI, the upgrade checker, the binlog utils, `upgradeAuthMethod`, and HeatWave options (`ocimds`, `compatibility`, lakehouse, data masking) appear only on `getting-started/differences-from-mysql-shell.md`. The `mrs` global is never documented. `mcp` and `msm` ARE documented since 2026-10-05 (user's request): `shell-release.yml` bundles `mcp_plugin`/`mrs_plugin`/`msm_plugin` from the private mariadb-shell-plugins repo into the release packages' `lib/mariadb-shell/plugins`; a source build (`build/`) lacks them, and locally they come from symlinks in `~/.mariadb-shell/plugins`.
- **MariaDB additions are documented:**
  - the `mariadb://` scheme (`mysql://` is a synonym) and `mariadb+ssh://`
  - the `sandbox` global and `%vendor%`
  - `MARIADB_SHELL_*` env vars, `~/.mariadb-shell`, and the `[mariadb-shell]` option group
  - script-based install
  - MariaDB objects in dump/load
- **Dump & load docs.** They are based on the product team's Confluence export, which the user saved in `~/Downloads/*Dump & Load*.md`; a copy is in the session scratchpad.
  - Dropped: internal status, the owner and the AIPL ticket links.
  - Kept: the warning that object storage has not been validated against MariaDB.
- **Conventions.** Python APIs are snake_case; option dict keys are camelCase. The built-in help (`\? topic`) plus the code are the authority. The MySQL Shell manual was used only to see which topics exist; it was never copied.
- **Copyright hook exception.** `.githooks/copyright-exceptions` exempts `docs-ref/content/.*\.md$`. Without it, the copyright hook inserts a MariaDB copyright line into banner samples.

### Further decisions (added later)

- **Space placement is an assumption.** The docs are in the Enterprise Tools space (`tools/mariadb-shell`) only to mirror the AI Plugins; the user has not confirmed it. Server › Clients & Utilities is the alternative.
- **MCP Server section (`mcp-server/`, 2026-10-05).** Scope is setting up, running and securing the server; tool usage stays in the ai-plugins docs (linked as absolute `https://mariadb.com/docs/tools/mariadb-ai-plugins` URLs, which won't resolve until those docs are ported). Pages: README (overview, `mcp` object, groups), Configuring Access (`mcp setup`), Automated Setup (CLI options), Starting the MCP Server (transports, `--functionGroups`, log), Connecting MCP Clients (Claude Code, Codex, VS Code, Claude Desktop, Windows `cmd /c`), Security and Session Handling.
  - Sources: mcp_plugin `README.md`, `server.py`, `lib/server.py`, `lib/general.py`, `lib/setup*.py`, and the generated `--help`.
  - Verified live on 26.9.5 in a scratch config home: `setup --show/--json/--addPaths`, every CLI refusal message, the walkthrough and menu rendering under a pty (`[Y/n]`, `[6]`), HTTP `/mcp` 200, 421 on a foreign Host, 403 on a foreign Origin, `--allowedHosts` (bare name 200, name:port 421), the 0.0.0.0 warning, transport/group errors, the port-in-use error, stdio tool sets per group (db 8, msm 12; `msm.deploy_schema` only with db), and `claude mcp add` / `codex mcp add` (`CODEX_HOME` scratch) keeping the second `--`.
  - Unverified: the VS Code and Claude Desktop JSON formats (from those products' documented formats), Windows `cmd /c mariadb-shell.cmd`, the SSH host-key prompt in `mcp setup`, and the stderr connection-event log lines (taken from the README).
  - Deviates from the ai-plugins rule "passwords only at the prompt": Automated Setup documents `--passwordEnv` and `--passwordStdin` for CI and secret managers, and names `--password` only to discourage it. Ask the user if this should be aligned.
  - `--gui` is listed once, as reserved for the VS Code extension; nothing else GUI-specific.
  - **Multi-Tenant Mode and OAuth Authentication pages** (2026-10-06) were written from `wip/mcp-multi-tenant` at `abc58808`; the plugin's later simplification pass (`176aee35`, internal) and review fixes (`fe4dbfe6`) were folded in by PR #64 (2026-10-07). The pages match mariadb-shell-plugins `main` at `a5ad8bdf`. **When mcp_plugin changes**, read its `.claude/context/multi-tenant.md` "Review round"/"Next steps" and `security-review-multi-tenant.md` (M-items) first; they list the user-visible behaviour changes, and `git log origin/main -- mcp_plugin` shows what landed.
- **Schema Management section (`schema-management/`, 2026-10-05).** Pages: README (lifecycle, quick start, interactive vs non-interactive), Schema Projects, MSM Sections (the core reference: dev/update/deployment section tables, idempotent model, stored-procedure rules, server variables, version view), Developing a Schema (SOURCE files), Preparing Releases, Deploying Schemas, Function Reference.
  - **Assumes mariadb-shell-plugins PR #36 (`wip/msm_mrs_fixes`) is merged**: section 180 is deployed on every deployment, REST views deploy, and section 010 sets `NO_AUTO_CREATE_USER`. If the PR changes, revisit MSM Sections and Deploying Schemas.
  - **Section model as the user defined it (verified):** idempotent objects come only from the target's section 150, new roles/grants only from section 170 (both run on every deployment); the update script carries only what those can't express: table changes and removals in 240, `REVOKE`/`DROP ROLE` in 270. Section 250 is documented as not deployed. The ai-plugins schema-management skills said to fill 250 and to `GRANT` in 270; corrected in ai-plugins PR #41 (`wip/skill-fixes`, with the e2e tests, docs-ref and DevHub).
  - **Documented as behaving today (warning hints):** reversed schema-name check (`allow_special_chars`), `SOURCE` slice required (`[0:]` whole file, `[:]` fails, none = copied verbatim → 1064 at deploy), `utf8mb4_0900_ai_ci` hardcoded in the deployment template, `SET PERSIST` in the local_infile error, and `StopIteration` outside a project.
  - **Passwordless-user `GRANT` is fixed in PR #36** (`948ef4eb`, section 010 now sets `NO_AUTO_CREATE_USER`). MSM Sections shows the new SQL mode and says a `GRANT` to a missing account now fails with 1133; a danger hint remains only for projects created before the fix (their dev script keeps the old section 010, and snapshots copy it), with a query to find such accounts. Preparing Releases says a `GRANT` in 270 to a role new in this version fails the deployment.
  - Verified on a 12.3.2 sandbox with the plugins working tree on `wip/msm_mrs_fixes`: Quick Start verbatim, the Developing REST example (service `/shop`, schema `/shop`, view `/product`), the update-script example, Test a Release, Correct a Release (re-preparing the same version keeps the update script), the copyright example (keyword form; positional `None` is refused), CLI deploy with a URI, `--sql -f` on the development script (runs once; CREATE TABLE fails on a rerun).
  - Unverified: the exact privileges list, the dump progress output wording, `msm.get_schema_diagram()` behavior beyond reading the code.
- **Two-level Utilities.** Utilities contains a **Dump and Load** subsection, structured after the Confluence pages:
  - overview (`README.md`), Quick Start, How Dump and Load Work
  - the dump, load and copy option references
  - What a Dump Carries, MariaDB-Specific Features, Object Storage, Limitations

  Table export/import, password change and diagnostics are pages directly under Utilities.
- **Unwanted options are named, not taught.** HeatWave and MySQL-only options that the help still lists are mentioned once, in a hint, as "not available for MariaDB sources". They are never presented as usable.
- **Bugs are documented as they behave today**, using `warning`/`danger` hints: `mariadb+ssh` only from Python, diagnostics broken, `\show threads` broken, sandbox exposure, `change_password` switching the plugin. When a bug is fixed, update its page; see [product-issues.md](product-issues.md). Reviewer pushback ("OK to document bugs?") was answered by fixing the bug instead where cheap (DECIMAL in JSON, #62).
- **No Release Notes or Bug Reports pages.** The ai-plugins docs have both. They were left out here because no verified content was available (release-notes source, issue tracker).
- **Install page.** The main command is the latest-release form. A hint explains that it fails today because every release is a prerelease, and documents `--pre-release` and `MARIADB_SHELL_TAG=v26.9.5`. `MARIADB_SHELL_TOKEN` is gone (installers dropped it in #63); `MARIADB_SHELL_REPO` is documented as "must be public".

### Review of PR #59 (mariadb-ReneRamirez, 2026-10-06/07)

Applied on the docs branch before the merge (`033274aa5`, `3b0cfe9e1`, `5a743d8d4`, `423eeec24`); every thread has a reply naming the commit or PR.

- **Renames/structure:** Command Reference → **CLI Reference** (`cli-reference.md`); "Command Reference" now means shell commands only. **Extension Objects** is a subpage of Plugins (the lower-level API the decorators build on). **Migration Tooling** is its own page after Configuring Access, which covers connections and allowed paths only. New **Appendix** section (`appendix/README.md`, `appendix/credential-helpers.md`) holds the helper internals: executables, `~/.mylogin.cnf` format, accepted password characters, running a helper directly. Credential Store keeps the helper table, the obfuscation warning and the `secret-service` setup.
- **Kept against the suggestion:** "Automated Setup" stays (the page only covers `mcp setup` without prompts); "DevOps Integration" would suit a shell-wide CLI-integration section later. Rene was told so in the thread.
- **Policy from the reviewer, adopted:** pre-releases get no migration notes; only the official version is the source of truth. The MSM danger hint for projects "created with an earlier version" was removed; don't add such notes until GA.
- **Reports:** register them in `plugins/<name>/init.py` (verified: a report registered from a plugin's `init.py` loads); `init.d` is the documented alternative, as in the MySQL manual.
- **Removed:** `MYSQL_TEST_LOGIN_FILE` everywhere; the macOS concurrent-load known issue (server doesn't support macOS); every `--login-path` mention outside Option Files and Login Paths; `MARIADB_SHELL_TOKEN`.
- **Added:** a Differences row for the MySQL client auth plugins (Kerberos, LDAP SASL, OCI, OpenID Connect) that the MariaDB build doesn't bundle (only `caching_sha2_password` in `lib/mariadb/plugins`); sandbox requirements say "server binaries, an unpacked package is enough" (`mariadbdPath` accepts the top directory).
- **MCP log examples** show no IDs: connections by URI, users by `--name`, refused tokens without the user. Matches mariadb-shell-plugins #37.
- **Still open from the review:** nothing; the `MARIADB_SHELL_TOKEN` thread was the last and is answered.

## How the pages were written

- **Seven parallel writers, one group each.** They used a shared brief, which lived only in the session scratchpad and is now gone. Its rules are summarized in `docs-ref/README.md` § Writing pages. The groups:
  - A: overview, install, getting-started, files/env, license
  - B: connecting
  - C: using-mariadb-shell
  - D1: dump-and-load concept pages
  - D2: dump/load/copy option references
  - E: utilities README, export/import, password, diagnostics, sandbox, logging, command-reference
  - F: extending, customizing
- **How examples were verified:**
  - against `build/bin/mariadb-shell` 26.9.5 (built for MariaDB 13.1.0) and MariaDB 12.3.2 sandboxes from `/opt/homebrew/bin/mariadbd`, on ports 4401–4407 and 4414/4415, all deleted afterwards
  - with `MARIADB_SHELL_USER_CONFIG_HOME` pointed at a scratch dir for isolation, which sandboxes ignore (see Gotchas)
- **Facts from real dumps, used in `how-dump-and-load-work.md`:**
  - chunk files are `schema@table@N.tsv.zst`, and the last chunk is `schema@table@@N`
  - every data file has a `.idx` next to it
  - partitions are `schema@table@partition@@0`
  - other files are `@.json`, `@.done.json`, `@.users.sql`, `@.checksums.json` and `<view>.pre.sql`
  - the load progress file is `load-progress.<server_id>.json`
- **Verified behaviors** on 12.3:
  - Recipes 2–4 of Quick Start work, including replica provisioning with `updateGtidSet: "replace"` followed by `START REPLICA`.
  - `createInvisiblePKs` works on load.
  - Without `RELOAD` the dump falls back to `LOCK TABLES`; without that as well, it fails.
  - `ocimds` and `compatibility` are refused for a MariaDB source, and `updateGtidSet` is refused while replication is running.
- **Quick Start fix to the Confluence recipe:** a full load with `loadUsers` into a fresh server stops on the existing `root` accounts, so the recipe adds `"excludeUsers": ["root"]`.
- **Confluence claim dropped.** Confluence says a dump without `RELOAD` "cannot be verified against schema changes". It was left out because `MARIADB_DUMP_LOAD.md` §4.4 and the observed output show the dump checks DDL consistency using the binary log.
- **Documented as new in 26.9.5:**
  - the terminators `\Gj`, `\GJ`, `\GT` and `\Gt`
  - in SQL mode, `source` without the backslash
- **Behaviors documented as warnings on the load page:**
  - A split load (`loadData: False`, then data only) creates the triggers first, so they fire during the data load.
  - `ignoreExistingObjects` reloads data into existing tables, so a table without a unique key gets duplicate rows.

## Files that matter

- `docs-ref/content/SUMMARY.md` -> the page tree and navigation
- `docs-ref/content/mariadb-shell/` -> the product pages (ported to mariadb-docs `tools/mariadb-shell/`)
- `docs-ref/content/README.md` -> stand-in for the Enterprise Tools space landing; only its `## MariaDB Shell` section gets ported
- `docs-ref/README.md` -> preview usage, writing rules and porting steps
- `docs-ref/docusaurus.config.js`, `sidebars.js`, `src/`, `static/` -> the preview site only, never ported
- `package.json` (root) -> the `docs-ref`, `docs-ref:build` and `docs-ref:serve` scripts
- `MARIADB_PORT.md`, `MARIADB_DUMP_LOAD.md`, `README.md`, `docs/CREDENTIAL_STORE.md` -> engineering sources for the docs
- `build/bin/mariadb-shell` -> local 26.9.5 build, used to check help text and examples
- `~/Downloads/*Dump & Load*.md` -> the Confluence export (9 files) behind the dump-and-load pages; the Confluence original is login-gated
- `python/plugins/sandbox/init.py`, `build/lib/mariadb-shell/plugins/{debug,util,sandbox}` -> built-in Python plugins (sources of the sandbox, diagnostics and prompt pages)

## Next steps

1. Port to mariadb-docs following `docs-ref/README.md` § Porting (PR #59 is merged; the pages now live on `main`).
2. Decide whether public docs keep the "object storage not validated" warning, and confirm the Enterprise Tools placement (versus Server › Clients & Utilities).
3. File or fix the remaining product bugs, especially `mariadb+ssh` on the CLI, the diagnostics collectors, the `change_password` plugin switch and the silent `ssl-mode` fallback. Update the affected pages when they are fixed. The full list is in [product-issues.md](product-issues.md). (`--login-path`, DECIMAL/FLOAT in JSON and the installer token are done.)
4. Clean up the help text that still describes MySQL Shell; see [product-issues.md](product-issues.md).
5. Once a stable (non-prerelease) release exists, drop the install-page hint about `--pre-release` being needed.
6. Keep the MCP pages in step with mariadb-shell-plugins `main` (see § MCP Server section for how); the plugin's own open items (uvicorn access log, `/register` rate limit M18, per-tool step-up M9) may need docs notes when done.
7. Rebuild `build/` from current main and recheck the pages that depend on `util` account functions: `a36944d9c` changed `changePassword` and removed `upgradeAuthMethod` after the writers' build.
8. Verify the gaps before publishing:
   - object storage
   - the cross-vendor refusal (needs a MySQL server)
   - `updateGtidSet` / `skipBinlog` privileges
   - Windows named pipes (`install.ps1` itself was verified on Windows 11 ARM64 under PowerShell 5.1 and 7 on 2026-10-07)
   - `--pym pip install` from PyPI
   - `change_password` on your own ed25519 account (the build tree lacks `client_ed25519.so`)

## Gotchas / things not to repeat

- **Backslashes in frontmatter `description:`** (for example `\option`) break the Docusaurus build ("Bad character escape sequence"). Spell commands out in descriptions instead.
- **Piping hides failures.** `npm ci | tail` reports exit 0 even when npm fails; check for `node_modules` or use `pipestatus`.
- **Chain a push only to a successful commit.** The pre-commit copyright hook can abort a commit, so don't chain `git push` after `git log`.
- **Getting help text from the binary:**
  - `-e '\?'` does not work in `--py` mode.
  - Use `printf '\\? topic\n' | build/bin/mariadb-shell --py --nw --quiet-start=2 --disable-plugins -i` and strip ANSI codes.
  - Without `--disable-plugins`, the AI Plugins' `mcp`/`mrs`/`msm` show up.
- **Sandbox location.** Sandbox tests deploy into the real `~/.mariadb-shell/sandboxes`. The user's sandboxes there (3313, `myboilerplate-*`) must not be touched.
- **The Confluence page is login-gated.** WebFetch can't read it and the Atlassian MCP was unauthorized in the session, so ask the user for an export.
- **`MARIADB_SHELL_USER_CONFIG_HOME` doesn't isolate MCP connections.** They live in the macOS keychain, so a scratch config home still shows the user's real `mariadb://root@127.0.0.1:3311`. Never add or delete MCP connections in tests.
- **The MCP Inspector mangles `mariadb-shell -- mcp start-server ...`** (the shell ended up evaluating `true` as Python), so the docs don't recommend it.
- **Credential tests use the real keychain.** On macOS they default to the user's own keychain, so pass `--credential-store-helper=plaintext` or clean up afterwards. One test stored and then deleted `app@db1:3306` there.
- **Build output is ignored.** `docs-ref/build/` and `.docusaurus/` are in `.gitignore`. A clean `npm run build` with no warnings is the check before committing, because broken links and anchors fail it.
- **GitBook `<figure><img>` screenshots** need `remarkGitBookExtras` with `{ baseUrl }` (rewrites `.gitbook/assets/` in raw HTML) and the figure CSS added in `src/css/mariadb-gitbook.css`; a Markdown image alone is rewritten by the base plugin, raw HTML is not.
- **A page that becomes a parent** (Plugins › Extension Objects, Appendix) only needs the nesting in `SUMMARY.md`; `sidebars.js` turns an entry with children into a category whose own page is the entry's target.
