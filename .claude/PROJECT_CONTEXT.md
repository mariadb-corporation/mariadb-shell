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

## Current State

- **Done:** all 57 original pages exist and `npm run build` is clean. They are committed and pushed as `2a9fbb6fa` on `wip/docs-ref`; PR #59 to `main` is open.
- **Added 2026-10-05 (`071346cc1`):** the **MCP Server** section (6 pages in `mcp-server/`, below Sandbox Instances), plus `mcp` rows on Global Objects and `plugin_data/` on Files and Environment Variables. See [context/docs-ref.md](context/docs-ref.md) § MCP Server section.
- **Added 2026-10-05:** the **Schema Management** section (7 pages in `schema-management/`, between Sandbox Instances and MCP Server), plus an `msm` row on Global Objects. Written assuming mariadb-shell-plugins PR #36 is merged. See [context/docs-ref.md](context/docs-ref.md) § Schema Management section.
- **Open:**
  - user review
  - the placement decision (Enterprise Tools?)
  - whether to keep the object-storage warning
  - about 25 product bugs and many help-text leftovers ([context/product-issues.md](context/product-issues.md))
- **Unverified in the docs:**
  - object storage (S3/OCI/Azure)
  - cross-vendor refusal (no MySQL server was available)
  - `updateGtidSet` privileges
  - anything on Windows
  - `--pym pip install` from PyPI (timed out)
  - the minimum version "MariaDB 10.11", which comes only from help text and has no code check

## Git state

Branch `wip/docs-ref`, pushed to origin, PR #59 open; `git status --short` is clean. On top of `main`: the reference docs (`2a9fbb6fa`), Differences moved into Getting Started (`ca83b1113`), context files and the checkpoint command (`490586f71`), the MCP Server section (`071346cc1`), then the Schema Management section and this context update.
