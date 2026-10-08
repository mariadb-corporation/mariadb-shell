# Product Issues Found While Writing the Docs

Back to the index: [../PROJECT_CONTEXT.md](../PROJECT_CONTEXT.md). Context for the docs work is in [docs-ref.md](docs-ref.md).

These were found by testing `build/bin/mariadb-shell` 26.9.5 (osx, arm64, built for MariaDB 13.1.0) against MariaDB 12.3.2 sandboxes in October 2026. The docs describe the current behavior, so **when one is fixed, update the page named in brackets and delete the entry here** (git history keeps the record). "Help" means the built-in `\?` text.

### Found during the review fixes, not fixed

- **uvicorn's access log in the MCP server** prints request paths with query strings, so `GET /authorize?client_id=…&state=…` shows the OAuth client ID. It is uvicorn's own logger (stdout), not `log_event`; `access_log=False` in `lib/server.py`'s `uvicorn.Config` would silence it. [mcp-server/starting-the-mcp-server.md would need a note]
- **`shell.create_result()` cannot declare `Decimal` or `Float` columns** (`custom_result.cc` `db_type()` maps "float" to Double and knows no "decimal"), although `\? create_result` doesn't say so.

### Found during the slash-commands review (2026-10-07), not fixed

- **`\connect` / `/connect` lines go to history with their password.** `sql_safe_for_logging` runs only for SQL-mode lines and `\sql` statements, and the default `history.sql.ignorePattern` (`*IDENTIFIED*:*PASSWORD*`) doesn't match a URI. So `\connect root:secret@host` is saved in either mode. `using-mariadb-shell/shell-commands.md` § The Slash Prefix already says it is kept out. The user decided to fix the code in a separate PR rather than change the docs. See [slash-commands.md](slash-commands.md).

## Product bugs (behavior)

The original summary, carried over verbatim:

- **Product bugs found while writing.** The docs describe the actual behavior:
  - `mariadb+ssh://` works only via Python `shell.connect`. On the CLI and with `\connect` it fails with "Scheme extension [ssh] is not supported" (`hide_password_in_uri()`, shell_options.cc:1348).
  - `ssl-mode=REQUIRED`/`PREFERRED` silently fall back to an unencrypted connection, with no warning.
  - `util.debug.collect_*` fail on MariaDB because they read `@@server_uuid`.
  - `util.change_password({"account":…})` uses `ALTER USER … IDENTIFIED BY`, which switches ed25519 accounts to `mysql_native_password`.
  - `\show threads` always fails ('where' parameter unsupported); `\show thread --vars/--raw-locks` also fail on MariaDB.
  - `install.sh` without `MARIADB_SHELL_TAG` skips prereleases, and every release so far is a prerelease. (Reachable with `--pre-release`.)
  - Load progress file: on MariaDB it is `load-progress.<server_id>.json`, but the help says `<server_uuid>.progress`.
  - On the command line, a `where` option with `schema.table` keys is parsed as a nested key and fails.
  - Every dump warns "Charset id '33' csname 'UTF8'…".

The full list, by area:

### Connections and Authentication

- **`mariadb+ssh://` works only from Python** (`shell.connect`, `shell.open_session`). A positional URI, `--uri` and `\connect` all fail with "Scheme extension [ssh] is not supported". The cause is `hide_password_in_uri()` at `src/mysqlsh/shell_options.cc:1348`, which re-parses the URI without the extension support. [connecting/ssh-tunnels.md, which points to `--ssh` as the workaround]
- **The older `--ssh` without a user connects as `root`** in sessions opened by another user (CI, agents). `get_system_user()` asks `getlogin_r()` before `getpwuid_r()`. Only `+ssh` URIs use the new `get_effective_user()` (commit a7405461f). The same call also picks the default *database* user. [connecting/ssh-tunnels.md]
- **`ssl-mode=REQUIRED` and `PREFERRED`** fall back to an unencrypted connection, with no warning, when the server has no TLS. README.md "Server Compatibility" and MARIADB_PORT.md §3 say the shell warns; the code has no warning. [connecting/encrypted-connections.md]
- **`VERIFY_CA` behaves like `VERIFY_IDENTITY`.** Both turn on Connector/C's server certificate check, which includes the host name. [connecting/encrypted-connections.md]
- **`compression-algorithms` and `compression-level` have no effect** with Connector/C. `compression-algorithms` alone does not turn compression on: Compression showed OFF. Only zlib is documented. [connecting/compressed-connections.md]
- **An unknown option in a `[client]` group** of an option file (for example `default-character-set`) stops startup. A `loose-` prefix works around it. [connecting/option-files-and-login-paths.md]
- **Option-file groups have no priority.** Options from `[client]`, `[mysqlsh]` and `[mariadb-shell]` apply in file order. [connecting/option-files-and-login-paths.md]
- **`local-infile` is ON by default** on the client side. [connecting/connection-uris-and-options.md]
- **Passwords read with `--passwords-from-stdin` are never saved** to the credential store. [connecting/credential-store.md]
- **The macOS tarball has no `plaintext` credential helper**, although README.md says it exists on all platforms. [connecting/credential-store.md]

### Utilities

- **All three `util.debug.collect_*` collectors fail on MariaDB** with `Unknown system variable 'server_uuid'`, because `collect_diagnostics.py` queries `@@server_uuid`. Their scripted test is disabled for MariaDB builds (`#@{not __mariadb_build}`), and README.md shows the collector as working. [utilities/diagnostics-utilities.md, danger hint]
- **`util.change_password({"account": ...})` switches the authentication plugin.** It runs `ALTER USER … IDENTIFIED BY`, which turned an ed25519 account into `mysql_native_password`. Changing your own password uses `SET PASSWORD`, which keeps the plugin. `change_password` has no command-line form. Recheck after a36944d9c. [utilities/password-change-utility.md]
- **The `where` dump option fails on the command line.** The CLI argument parser reads the dot in a `schema.table` key as a nested key: `String expected, but value is Map`. The pages tell readers to pass dict options from Python. [dump-and-load/dump-utilities.md]
- **`dryRun` in a copy is inconsistent.** It prints "no locks will be acquired", then reports that it acquired the global read lock. [dump-and-load/copy-utilities.md]
- **Every dump prints a stray warning:** `Charset id '33' csname 'UTF8' trying to replace existing csname 'utf8mb3'`.
- **The load progress file name in the help is wrong.** The help gives `load-progress.<server_uuid>.progress` in one place and `.json` in another. On MariaDB the file is `load-progress.<server_id>.json` (observed: `load-progress.2.json`). [dump-and-load/load-dump-utility.md uses the real name]
- **Minimum version not enforced.** "MariaDB 10.11 or newer" is stated in the help (`mod_util.cc`), but no code check enforces it. [dump-and-load/README.md repeats the help]
- **MySQL-only options accepted silently for a MariaDB source:**
  - dump: `libraries`/`includeLibraries`/`excludeLibraries`, `dataMaskingPolicies`, `allowDataMasking`, `lakehouseTarget`
  - `export_table`: `allowDataMasking`
  - copy: rejects `ocimds` as an invalid option, while dump refuses it with a reason; the two are inconsistent
- **The dump manifest's `dumper` field** reads "mysqlsh Ver 26.9.5".
- **macOS test hosts:** eight or more concurrent loads into one table can hang the MariaDB server. This is a server issue, not reproduced on Linux; it comes from the Confluence source. Removed from the Limitations page on review (the server doesn't support macOS); kept here as a test-host note only.

### Shell, Output and Reports

- **`\show threads` always fails** with "The 'where' parameter is not supported in this build". `o.where` defaults to "TRUE" and then hits the `#ifndef HAVE_X_PROTOCOL` throw. [extending-mariadb-shell/reports.md, using-mariadb-shell/shell-commands.md]
- **`\show thread --vars`, `--raw-locks` and `--all` fail on MariaDB**, which has no `variables_by_thread` or `data_locks` tables. The `thread` report also needs `performance_schema=ON`. [extending-mariadb-shell/reports.md]
- **A report function gets `argv`/`options` only if the report declares them**; without defaults, the call fails. [extending-mariadb-shell/reports.md]
- **Command-line integration:**
  - The dotted forms `shell.options` and `util.debug` fail with exit code 10; only the space form (`shell options set-persist`, `util debug …`) works.

  [using-mariadb-shell/command-line-integration.md]
- **`--json` prints an empty `{}` first.** [using-mariadb-shell/output-formats.md]
- **`--pym` prints "Error in my_thread_global_end(): 1 threads didn't exit"** at exit, for example on `--pym pip`.
- **`--pym pip install` from PyPI timed out** in the bundled Python, while curl reached pypi.org. Installing a local wheel with `--user` or `--target` works. On Windows, pip may be missing from the vcpkg Python (MARIADB_PORT.md §11.1). [customizing/python-module-search-paths.md]
- **An empty or invalid prompt theme** falls back to the prompt `mysql-py>` (`base_shell.cc:235`). [customizing/prompt.md]
- **The shell starts in SQL mode**, and `-e` runs SQL unless `--py` is given, while the help says the default mode is py. `defaultMode` also accepts `js`. [using-mariadb-shell/sql-and-python-modes.md]
- **`db` is always `None`.** It is only set for X sessions; README.md says it holds the default schema. [getting-started/global-objects.md]
- **`track_system_variable`** accepts only `sql_mode`, which MariaDB never reports through session tracking (MARIADB_PORT.md §13.2). It is left out of the docs.

### Sandboxes

- **Sandbox server packages aren't fetched automatically.** The `mariadb-*-sandbox.tar.gz` packages on the release are fetched by neither the plugin nor the installers; the docs describe them from the build workflows only. [sandbox-instances.md]

### Installation and Packaging

- **The default install skips prereleases.** `install.sh`/`install.ps1` without `MARIADB_SHELL_TAG` skip GitHub prereleases, and every release so far (v26.9.5 included) is one, so the default command fails today. [installation/*, hint]
- **Release tarballs contain no LICENSE file.** [license.md links to the repo's LICENSE]

## Help Text and Man Page Out of Date

These items still describe MySQL Shell, or disagree with the code. The docs follow the code.

The original summary, carried over verbatim:

- **Help text still describes MySQL Shell:**
  - `\?` lists dba/mysqlx; `util` is described as "upgrade checker and JSON import"; `\connect` shows an `--mx` example; `\? cmdline` mentions mysqlsh and JS.
  - The dump/load help lists HeatWave/lakehouse options.
  - `\option -l` shows `dba.*`/`devapi.*`.
  - The manifest field `dumper` says "mysqlsh".
  - The man page lists `MARIADB_SHELL_JS_MODULE_PATH` and says `mysqlsh` is an alias.

The full list:

- **General help:**
  - `\?` still mentions the dba and mysqlx topics.
  - `\help` shows a `\h cluster` example.
  - `util` is described as "upgrade checker and JSON import".
- **`\connect`** shows a `--mx` example.
- **`\? cmdline`:**
  - it says `mysqlsh` and mentions JavaScript
  - it says `shell.options` works on the command line, but only the space form does (see above)
- **`\source`** still lists `--js`. **`\? register_report`** mentions JavaScript init.d files.
- **`\history`** says `history.autoSave` must be turned on, but it defaults to `true`.
- **Connection help:**
  - it lists only the `mysql` scheme, while the code also accepts `mariadb` and the `+ssh` forms
  - it says `tls-ciphers`, while the code accepts only `tls-ciphersuites`, which has no effect
  - it gives a 78-character login-path limit that the MariaDB build doesn't apply
  - the invalid-scheme error still lists `mysqlx`
- **Exposed in help but MySQL-only or meaningless:**
  - `--get-server-public-key`
  - the X values of `auth-method`
  - the OCI, WebAuthn, OpenID Connect and Kerberos connection options
  - `set_query_attributes` and `set_option_tracker_feature_id`
- **`\option -l`** lists `dba.*` (`connectTimeout`, `gtidWaitTimeout`, …), `devapi.dbObjectHandles` and `oci.*`.
- **Dump and load help:**
  - it still lists `lakehouseTarget`, `dataMaskingPolicies`, `allowDataMasking`, `libraries`, `convertInnoDbVectorStore`, `heatwaveLoad`, `lakehouseSource`, `disableBulkLoad`, `analyzeTables: "histogram"` and the full HeatWave `compatibility` section
  - it refers to "Section 13.2.10.1"
  - `dump-schemas --help` lists `compatibility` and `allowDataMasking`
- **`\? util.import_table`** says `SET unique_checks = 0`. On MariaDB the code keeps unique checks on unless `replaceDuplicates` is set.
- **`collect_slow_query_diagnostics`:** the help says `delay` defaults to 5 s; the code uses 15.
- **`export_table`:** `json` is not an allowed dialect. The code rejects it, and the page says so.
- **`\? create_result`:**
  - a warning `level` of "error" is rejected; only warning and note work
  - the `columns` key, list-form rows and `error` entries in multi-results are undocumented
- **`\? add_extension_object_member`:** it says `type` is required, but it is optional; the `default` and `cli` keys are undocumented.
- **Plugin manager text** says "MySQL Shell" and "Oracle", and uses `mysql-shell-plugins-manifest.json`.
- **`--help` typos:** "PREFFERED", "usingUniform", and `-V` says "Prints the version of MySQL Shell". `--auth-method` mentions the X protocol, and other options mention DevAPI.
- **Man page (`man/`):**
  - it says `mysqlsh` is an alias, but none is shipped
  - it says `-S` isn't available on Windows, where it takes a named pipe
  - it lists `MARIADB_SHELL_JS_MODULE_PATH`
- **Repo docs:** MARIADB_PORT.md §14.6 says the build "drops dump/load", which is wrong.
- **`.claude/skills/create-shell-plugin`** describes a `plugins_path` option that nothing sets.
- **`util.upgradeAuthMethod`** was still registered in the writers' build. Commit a36944d9c, on main, removes it from MariaDB builds; recheck after a rebuild.
