# Project Context

## Project

MariaDB Shell (`mariadb-shell`, alias `msh`) is a fork of MySQL Shell. It is built on MariaDB Connector/C, uses the classic protocol only, and offers SQL and Python modes. Its versions use the YY.M.patch scheme; the current one is 26.9.5, which is published as a GitHub prerelease. The work captured here is the **reference documentation** in `docs-ref/`. It is GitBook Markdown written for mariadb.com/docs (Enterprise Tools space, `tools/mariadb-shell`), together with a Docusaurus offline preview.

## Layout

| Path | What it is |
| --- | --- |
| `docs-ref/content/` | GitBook source of the reference docs (70 pages under `mariadb-shell/`, page tree in `SUMMARY.md`) |
| `docs-ref/` (rest) | Docusaurus offline preview, copied from ai-plugins; `npm run docs-ref` from the repo root |
| `MARIADB_PORT.md`, `MARIADB_DUMP_LOAD.md` | Engineering records of the port and of dump/load; sources for the docs |
| `build/bin/mariadb-shell` | Local 26.9.5 build, used to check help text and docs examples |
| `.githooks/` | pre-commit copyright hook and its `copyright-exceptions` |
| `.claude/commands/checkpoint.md` | This checkpoint command |

## Context Files

| File | Covers |
| --- | --- |
| [context/docs-ref.md](context/docs-ref.md) | The reference docs: setup, decisions, how the pages were written and verified, files, next steps, gotchas |
| [context/product-issues.md](context/product-issues.md) | Product bugs and out-of-date help/man text found while writing the docs, each with the page to update when it is fixed |
| [context/mrs-module.md](context/mrs-module.md) | Branch `wip/mrs_module`: the C++ `mrs` module replacing the Python mrs_plugin (bison REST SQL grammar, executor, SQL handler, tests): layout, design decisions, gotchas, state, next steps |

## Current State

- **Merged into `main` on 2026-10-07 as `5d0333dd6` (PR #59, squash).** All pages exist and `npm run build` is clean. The review by mariadb-ReneRamirez (15 threads, all answered and applied) led to: CLI Reference rename, a new Appendix › Credential Helpers page, Migration Tooling as its own MCP page, Extension Objects under Plugins, reports registered via plugins, no pre-release migration notes, `MARIADB_SHELL_TOKEN` dropped. See [context/docs-ref.md](context/docs-ref.md) § Review of PR #59.
- **Code fixes that came out of the review, all merged:** `--login-path` out of `--help` (#61), exact DECIMAL/FLOAT in the JSON formats (#62), installers without a GitHub token (#63), MCP server log without any part of an ID (mariadb-shell-plugins #37). [context/product-issues.md](context/product-issues.md) marks them fixed.
- **Added 2026-10-05 (`071346cc1`):** the **MCP Server** section (6 pages in `mcp-server/`, below Sandbox Instances), plus `mcp` rows on Global Objects and `plugin_data/` on Files and Environment Variables. See [context/docs-ref.md](context/docs-ref.md) § MCP Server section.
- **Added 2026-10-05:** the **Schema Management** section (7 pages in `schema-management/`, between Sandbox Instances and MCP Server), plus an `msm` row on Global Objects. Written assuming mariadb-shell-plugins PR #36 is merged. See [context/docs-ref.md](context/docs-ref.md) § Schema Management section.
- **PR #64 (`docs/mcp-review-sync`, merged 2026-10-07):** the MCP pages brought in line with the plugin's review fixes (`fe4dbfe6` in mariadb-shell-plugins #37, made after the pages were written): scopes apply at once, auto-provisioning servers start without users, 30 sign-ins per address per minute, `--publicUrl` on `start-server`.
- **Open:**
  - porting to mariadb-docs (`docs-ref/README.md` § Porting)
  - the placement decision (Enterprise Tools?)
  - whether to keep the object-storage warning
  - about 20 product bugs and many help-text leftovers ([context/product-issues.md](context/product-issues.md))
  - uvicorn's access log in the MCP server still prints query strings (OAuth `client_id`); one-line `access_log=False` in the plugin's `lib/server.py` plus a docs note, if wanted
- **Unverified in the docs:**
  - object storage (S3/OCI/Azure)
  - cross-vendor refusal (no MySQL server was available)
  - `updateGtidSet` privileges
  - anything on Windows
  - `--pym pip install` from PyPI (timed out)
  - the minimum version "MariaDB 10.11", which comes only from help text and has no code check

## Repo-wide conventions and gotchas

- **No `Co-Authored-By` lines in commits**, in any project on this machine (user rule, kept in `~/.claude/CLAUDE.md`). Squash merges via `gh pr merge --squash --subject --body` need an explicit message, or GitHub copies the lines from the branch commits.
- **PRs are squash-merged** into `main` with `Title (#N)` subjects. `main` requires the `Shell-CI-Verification` check; `shell-ci.yml` runs on PRs against any branch since #62.
- **Scripted-test chunk names must not start with `-` or `+`.** `#@<> -foo` is parsed as "exclude this chunk", which switches the tester to only-marked mode and silently skips every chunk; the test passes without running. `unittest/scripts/auto/py_shell/scripts/mysqlsh_mycnf_options.py` hit this. Check with `--show-skipped` or by breaking an assertion on purpose.
- **Run the scripted tests** with `MYSQL_PORT=<port> build/bin/run_unit_tests --gtest_filter='*/<name>'` against a sandbox deployed with `mariadb-shell -- sandbox deploy <port> --password= --sandboxDir=<scratch>`; rebuild `run_unit_tests` too, not just `mariadb-shell`, or in-process checks run old code.
- **Unit tests via `npm run rut`** (`RUT_FILTER="Suite.*:Other.*"`) need the MariaDB server on `PATH`: `PATH="/Users/juanram/servers/12.3.2/bin:$PATH"`. Without it every worker fails with "Could not find a MariaDB server binary". The runner deploys and removes its own sandboxes. The local build is in `bld/` (`cmake --build bld --target mariadb-shell run_unit_tests -j 10`).
- **`main` is checked out in the `~/dev/workflow-test` worktree**, so `git fetch origin main:main` is refused. Fast-forward it there with `git -C ~/dev/workflow-test merge --ff-only origin/main`.
- **The pre-commit copyright hook** adds `Copyright (c) 2026, MariaDB plc.` to touched C++ files and aborts the commit; re-`git add` and commit again.
- **GitHub returned HTTP 500 on four squash-merge attempts of #62** (2026-10-07) while the PR was clean; the user merged it later from the web UI.
- **Windows testing:** a Windows 11 ARM64 box with OpenSSH, PowerShell 5.1 and 7 is in the project memory (`windows-test-machine`); used to verify `install.ps1`.

## Git state

Checked 2026-10-08. Branch `wip/mrs_module` (tracks `origin/wip/mrs_module`), one commit on top of `main`: `88413da93` (Add the mrs module). Uncommitted on top of it (`git status --short`, summarized):

```
 M .claude/PROJECT_CONTEXT.md, .claude/context/mrs-module.md, .githooks/copyright-exceptions
D  cmake/embed_file.cmake
 M modules/CMakeLists.txt, modules/mrs/CMakeLists.txt, modules/mrs/mod_mrs.{h,cc}, modules/mrs/mrs_shell_session.cc
D  modules/mrs/core/metadata/mysql_rest_service_metadata_4.1.6.sql
 M modules/mrs/core/{mrs_ast.h, mrs_db_session.h, mrs_ddl_executor*.{h,cc}, mrs_lexer.{h,cc},
   mrs_metadata*.{h,cc}, mrs_parser.yy, mrs_parser_driver.h, mrs_sql.{h,cc}}
M  mysqlshdk/include/shellcore/shell_options.h, mysqlshdk/shellcore/shell_options.cc
 M src/mysqlsh/mysql_shell.cc
 M unittest/data/mrs/grammar_test.sql, unittest/modules/mrs/mrs_parser_t.cc
 M unittest/scripts/auto/py_shell/scripts/mrs_{auth,content,db_objects,services}_norecord.py
 M unittest/scripts/auto/py_shell/validation/plugin_cli_integration_norecord.py
?? modules/mrs/core/mrs_metadata_json.{h,cc}, modules/mrs/core/mrs_schema_deployment.{h,cc}
?? modules/mrs/db_schema/, modules/mrs/mrs_schema_deployers.{h,cc}
?? unittest/modules/mrs/mrs_schema_deployment_t.cc
?? unittest/scripts/auto/py_shell/scripts/mrs_{metadata_upgrade,plugin_extension}_norecord.py
```

