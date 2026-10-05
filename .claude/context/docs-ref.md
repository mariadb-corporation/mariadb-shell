# Reference Docs (`docs-ref/`)

Back to the index: [../PROJECT_CONTEXT.md](../PROJECT_CONTEXT.md). The product bugs that the docs work turned up are in [product-issues.md](product-issues.md).

## Architecture / key decisions

- **`docs-ref/` copies the ai-plugins setup exactly.** The source is `/Users/mzinner/git/ai-plugins/docs-ref`: same packages, lockfile, theme (`src/css/mariadb-gitbook.css`), remark extras and npm scripts. Only the names were changed: the REPO URL, the tagline, the announcement bar, and the `static/index.html` redirect to `tools/mariadb-shell/`. Keep the two copies in step.
- **`content/SUMMARY.md` is the only definition of the page tree.** `sidebars.js` generates the sidebar from it.
- **Excluded features.** Docs cover only what the build ships. JavaScript, X DevAPI/mysqlx, AdminAPI, the upgrade checker, the binlog utils, `upgradeAuthMethod`, and HeatWave options (`ocimds`, `compatibility`, lakehouse, data masking) appear only on `getting-started/differences-from-mysql-shell.md`. The `mcp`/`mrs`/`msm` globals come from the AI Plugins, not the shell, and are never documented.
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
- **Two-level Utilities.** Utilities contains a **Dump and Load** subsection, structured after the Confluence pages:
  - overview (`README.md`), Quick Start, How Dump and Load Work
  - the dump, load and copy option references
  - What a Dump Carries, MariaDB-Specific Features, Object Storage, Limitations

  Table export/import, password change and diagnostics are pages directly under Utilities.
- **Unwanted options are named, not taught.** HeatWave and MySQL-only options that the help still lists are mentioned once, in a hint, as "not available for MariaDB sources". They are never presented as usable.
- **Bugs are documented as they behave today**, using `warning`/`danger` hints: `mariadb+ssh` only from Python, diagnostics broken, `\show threads` broken, DECIMAL in JSON, sandbox exposure, `change_password` switching the plugin. When a bug is fixed, update its page; see [product-issues.md](product-issues.md).
- **No Release Notes or Bug Reports pages.** The ai-plugins docs have both. They were left out here because no verified content was available (release-notes source, issue tracker).
- **Install page.** The main command is the latest-release form. A hint explains that it fails today because every release is a prerelease, and documents `--pre-release` and `MARIADB_SHELL_TAG=v26.9.5`.

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

1. Have the user review the pages in the preview (`npm run docs-ref`), especially the dump & load pages against the Confluence source.
2. Decide whether public docs keep the "object storage not validated" warning, and confirm the Enterprise Tools placement (versus Server › Clients & Utilities).
3. File or fix the product bugs, especially `mariadb+ssh` on the CLI, `--login-path`, the diagnostics collectors, the `change_password` plugin switch and the silent `ssl-mode` fallback. Update the affected pages when they are fixed. The full list is in [product-issues.md](product-issues.md).
4. Clean up the help text that still describes MySQL Shell; see [product-issues.md](product-issues.md).
5. Open a PR from `wip/docs-ref`, or port to mariadb-docs following `docs-ref/README.md` § Porting.
6. Rebuild `build/` from current main and recheck the pages that depend on `util` account functions: `a36944d9c` changed `changePassword` and removed `upgradeAuthMethod` after the writers' build.
7. Verify the gaps before publishing:
   - object storage
   - the cross-vendor refusal (needs a MySQL server)
   - `updateGtidSet` / `skipBinlog` privileges
   - Windows install and named pipes
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
- **Credential tests use the real keychain.** On macOS they default to the user's own keychain, so pass `--credential-store-helper=plaintext` or clean up afterwards. One test stored and then deleted `app@db1:3306` there.
- **Build output is ignored.** `docs-ref/build/` and `.docusaurus/` are in `.gitignore`. A clean `npm run build` with no warnings is the check before committing, because broken links and anchors fail it.
