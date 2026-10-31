# Project Context

## Project

MariaDB Shell (`mariadb-shell`, alias `msh`) is a fork of MySQL Shell. It is built on MariaDB Connector/C, uses the classic protocol only, and offers SQL and Python modes. Its versions use the YY.M.patch scheme (`gh release list` for the releases, all GitHub prereleases so far). The work captured here: the **reference documentation** in `docs-ref/` (GitBook Markdown for mariadb.com/docs, Enterprise Tools space, `tools/mariadb-shell`, with a Docusaurus offline preview), the built-in **MariaDB REST Service** module, and the **MCP server container image**.

## Layout

| Path | What it is |
| --- | --- |
| `docs-ref/content/` | GitBook source of the reference docs (`community/mariadb-shell/`, `community/mariadb-rest-service/`; page tree in `SUMMARY.md`) |
| `mysqlshdk/libs/mrs/antlr_grammar/` | ANTLR reference grammar of REST SQL + scripts for its railroad diagrams and the reference's rule blocks (npm `docs-ref:mrs-diagrams`, `docs-ref:mrs-grammar-docs`) |
| `docs-ref/` (rest) | Docusaurus offline preview, copied from ai-plugins; `npm run docs-ref` from the repo root |
| `MARIADB_PORT.md`, `MARIADB_DUMP_LOAD.md` | Engineering records of the port and of dump/load; sources for the docs |
| `bld/` | The local build (`bld/bin/mariadb-shell`), used to check help text and docs examples |
| `src/lsp-server/`, `cmake/lsp_server.cmake` | The lsp-server submodule (`mariadb-lsp`, the SQL language server) and its build; see [context/lsp-server.md](context/lsp-server.md) |
| `python/packages/mysqlsh/task_manager.py`, `python/plugins/task_plugin/` | Background tasks shared by every plugin, and the `task` global object; see [context/task-manager.md](context/task-manager.md) |
| `.githooks/` | pre-commit copyright hook and its `copyright-exceptions` |
| `docker/` | Build images for the release packages, and the MariaDB Shell MCP Server image `mariadb-shell-mcp-server` (`linux-debian13-mcp-server.Dockerfile`, `entrypoint-mcp-server.sh`, `README.md`) |
| `.claude/commands/checkpoint.md`, `read-checkpoint.md` | `/checkpoint` writes this context (its "In every case" rules say what belongs in these files), `/read-checkpoint` reads it back to resume |
| `.claude/commands/release.md`, `scripts/release.py` | `/release [--official] [--dry-run] [--help]`: the whole release across both repos (version bump, plugins version + sandbox index, release/candidate build + verify, publish); the script holds the checks and the run watching |

## Context Files

| File | Covers |
| --- | --- |
| [context/docker-mcp.md](context/docker-mcp.md) | The MariaDB Shell container image and the containerized multi-tenant MCP server: decisions, the plugin features it relies on (`--trustedProxies`, Keycloak login theme, `--loginTheme`), what was verified, next steps, gotchas |
| [context/docs-ref.md](context/docs-ref.md) | The reference docs: setup, decisions, how the pages were written and verified, files, next steps, gotchas |
| [context/product-issues.md](context/product-issues.md) | Open product bugs and out-of-date help/man text found while writing the docs, each with the page to update when it is fixed |
| [context/mrs-module.md](context/mrs-module.md) | The MariaDB REST Service in the shell, replacing the Python mrs_plugin's REST SQL: core library `mysqlshdk/libs/mrs/` (bison grammar, executor, metadata schema project), integration `src/mysqlsh/mrs/` (SQL handler); layout, design decisions, gotchas, state, next steps |
| [context/lsp-server.md](context/lsp-server.md) | `mariadb-lsp`, the SQL language server, built from the `src/lsp-server` submodule and installed next to `mariadb-shell`: how it is built and bundled, `WITH_LSP`, the guards, gotchas, next steps (what changes once lsp-server is public) |
| [context/task-manager.md](context/task-manager.md) | `mysqlsh.task_manager` (background tasks: owners, slots, events, the end of the process, the command-line follow) and the `task` global object; what the MCP plugin in mariadb-shell-plugins depends on; how to run both repos' tests against it |
| [context/rest-daemon-5.0.md](context/rest-daemon-5.0.md) | What the MariaDB REST Daemon (`mariadb-restd`, the rest-daemon repository) depends on in metadata schema 5.0.0, so a schema change it sees gets a daemon change: schema name/prefix/postfix, renamed `rest_daemon*` tables, roles, UUID ids, vendor names, async tasks, status counters, audit log ids not in commit order (Galera) |

## Open Items

- **API Reference not in `main`:** the hidden `--export-help` option (`471e72e9d`), `docs-ref/scripts/api_reference.py` and its 30 pages (`e4d29055d`), the docs link and CI `--check` (`f6005e443`), MariaDB naming in the built-in help (`e525a8264`), and `plugins/external-plugins.txt` (`8ba556850`). They sit on top of the pre-squash docs commits, so rebase only these five onto `main`. Re-check `8ba556850`'s choice to stop bundling mrs now that the REST Service is built in, and `e525a8264` against the help text the REST Service added.
- **Docs:** porting to mariadb-docs (`docs-ref/README.md` § Porting); the placement decision (Enterprise Tools?); whether to keep the object-storage warning. See [context/docs-ref.md](context/docs-ref.md).
- **mariadb-lsp once lsp-server is public:** fetch the submodule with `submodules: true`, and ship `mariadb-lsp` in the native `.deb`/`.rpm` packages (they build with `WITH_LSP=OFF` until then). See [context/lsp-server.md](context/lsp-server.md).
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
- **Run tests via `npm run rut`** (`RUT_FILTER="Suite.*:Other.*"`), which deploys and removes its own sandboxes. It needs the `bin` directory of a MariaDB server on `PATH`; without it every worker fails with "Could not find a MariaDB server binary". Rebuild `run_unit_tests` too, not just `mariadb-shell` (`cmake --build bld --target mariadb-shell run_unit_tests -j 10`), or in-process checks run old code. `npm run rut` is hard-wired to `bld/bin`; with another build directory run what it runs, `python3 ../../scripts/run_unit_tests.py --binary ./run_unit_tests --shell-binary ./mariadb-shell -t ../../unittest/test-execution-times.txt --gtest_filter=...`, from that directory's `bin`. The bare `run_unit_tests` binary connects to a server at startup and is no use without one.
- **`--target mariadb-shell` does not refresh the build's Python copies.** The shell loads `python/packages` and `python/plugins` from `<build>/lib/mariadb-shell/{python-packages,plugins}`, copied by the `python_extensions` target; after editing a package or a bundled plugin, build that target too, or the shell runs the old code while the files look right.
- **A Python `sys.exit()` ends the process inside `PyErr_Print()`.** Nothing after the script in `main()` runs; only `atexit` handlers do. Cleanup that must happen on that path (stopping the interrupt thread, waiting for background tasks) is registered with `atexit`, and `main()`'s own calls are made idempotent with it.
- **The pre-commit copyright hook** adds `Copyright (c) 2026, MariaDB plc.` to touched C++ files and aborts the commit; re-`git add` and commit again.
