# Project Context

## Project

MariaDB Shell (`mariadb-shell`, alias `msh`) is a fork of MySQL Shell. It is built on MariaDB Connector/C, uses the classic protocol only, and offers SQL and Python modes. Its versions use the YY.M.patch scheme (`gh release list` for the releases, all GitHub prereleases so far). The work captured here: the **reference documentation** in `docs-ref/` (GitBook Markdown for mariadb.com/docs, Enterprise Tools space, `tools/mariadb-shell`, with a Docusaurus offline preview), the built-in **MariaDB REST Service** module, and the **MCP server container image**.

## Layout

| Path | What it is |
| --- | --- |
| `docs-ref/content/` | GitBook source of the reference docs (`mariadb-shell/`, `mariadb-rest-service/`; page tree in `SUMMARY.md`) |
| `mysqlshdk/libs/mrs/antlr_grammar/` | ANTLR reference grammar of REST SQL + scripts for its railroad diagrams and the reference's rule blocks (npm `docs-ref:mrs-diagrams`, `docs-ref:mrs-grammar-docs`) |
| `docs-ref/` (rest) | Docusaurus offline preview, copied from ai-plugins; `npm run docs-ref` from the repo root |
| `MARIADB_PORT.md`, `MARIADB_DUMP_LOAD.md` | Engineering records of the port and of dump/load; sources for the docs |
| `bld/` | The local build (`bld/bin/mariadb-shell`), used to check help text and docs examples |
| `.githooks/` | pre-commit copyright hook and its `copyright-exceptions` |
| `docker/` | Build images for the release packages, and the MariaDB Shell MCP Server image `mariadb-shell-mcp-server` (`linux-debian13-mcp-server.Dockerfile`, `entrypoint-mcp-server.sh`, `README.md`) |
| `.claude/commands/checkpoint.md` | The checkpoint command; its "In every case" rules say what belongs in these files |

## Context Files

| File | Covers |
| --- | --- |
| [context/docker-mcp.md](context/docker-mcp.md) | The MariaDB Shell container image and the containerized multi-tenant MCP server: decisions, the plugin features it relies on (`--trustedProxies`, Keycloak login theme, `--loginTheme`), what was verified, next steps, gotchas |
| [context/docs-ref.md](context/docs-ref.md) | The reference docs: setup, decisions, how the pages were written and verified, files, next steps, gotchas |
| [context/product-issues.md](context/product-issues.md) | Open product bugs and out-of-date help/man text found while writing the docs, each with the page to update when it is fixed |
| [context/mrs-module.md](context/mrs-module.md) | The MariaDB REST Service in the shell, replacing the Python mrs_plugin's REST SQL: core library `mysqlshdk/libs/mrs/` (bison grammar, executor, metadata schema project), integration `src/mysqlsh/mrs/` (SQL handler); layout, design decisions, gotchas, state, next steps |
| [context/rest-daemon-5.0.md](context/rest-daemon-5.0.md) | What the MariaDB REST Daemon (MySQL Router fork, not in these repos) must change to serve metadata schema 5.0.0: schema name/prefix/postfix, renamed `rest_daemon*` tables, roles, UUID ids, vendor names, async tasks, status counters, audit log ids not in commit order (Galera) |

## Open Items

- **Docs:** porting to mariadb-docs (`docs-ref/README.md` § Porting); the placement decision (Enterprise Tools?); whether to keep the object-storage warning. See [context/docs-ref.md](context/docs-ref.md).
- **Product bugs and help-text leftovers:** [context/product-issues.md](context/product-issues.md).
- **Unverified in the docs:**
  - object storage (S3/OCI/Azure)
  - cross-vendor refusal (no MySQL server was available)
  - `updateGtidSet` privileges
  - anything on Windows
  - `--pym pip install` from PyPI (timed out)
  - the minimum version "MariaDB 10.11", which comes only from help text and has no code check

## Repo-wide conventions and gotchas

- **No `Co-Authored-By` lines in commits**, in any project (the user's global rule). Squash merges via `gh pr merge --squash --subject --body` need an explicit message, or GitHub copies the lines from the branch commits.
- **PRs are squash-merged** into `main` with `Title (#N)` subjects. `main` requires the `Shell-CI-Verification` check; `shell-ci.yml` runs on PRs against any branch.
- **Scripted-test chunk names must not start with `-` or `+`.** `#@<> -foo` is parsed as "exclude this chunk", which switches the tester to only-marked mode and silently skips every chunk; the test passes without running. Check with `--show-skipped` or by breaking an assertion on purpose.
- **Run tests via `npm run rut`** (`RUT_FILTER="Suite.*:Other.*"`), which deploys and removes its own sandboxes. It needs the `bin` directory of a MariaDB server on `PATH`; without it every worker fails with "Could not find a MariaDB server binary". Rebuild `run_unit_tests` too, not just `mariadb-shell` (`cmake --build bld --target mariadb-shell run_unit_tests -j 10`), or in-process checks run old code.
- **The pre-commit copyright hook** adds `Copyright (c) 2026, MariaDB plc.` to touched C++ files and aborts the commit; re-`git add` and commit again.
