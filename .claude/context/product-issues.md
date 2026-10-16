# Product Issues Found While Writing the Docs

Back to the index: [../PROJECT_CONTEXT.md](../PROJECT_CONTEXT.md). Context for the docs work is in [docs-ref.md](docs-ref.md).

These were found by testing a 26.9.5 build (osx, arm64, for MariaDB 13.1.0) against MariaDB 12.3.2 sandboxes, unless an entry says otherwise. The docs describe the current behavior, so **when one is fixed, update the page named in brackets and delete the entry here** (git history keeps the record). "Help" means the built-in `\?` text.

## Product bugs (behavior)

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
- **The shell can't log in to `ed25519` (or `parsec`, `auth_gssapi`) accounts.** For MariaDB it bundles only the `caching_sha2_password` client plugin (`AUTH_CLIENT_PLUGINS` in CMakeLists.txt, `_mdb_core_targets` in cmake/bootstrap_mariadb.cmake), so the login fails with `Plugin client_ed25519 could not be loaded`. Connector/C has the `client_ed25519` target; it isn't built or copied.
- **The macOS tarball has no `plaintext` credential helper**, although README.md says it exists on all platforms. [connecting/credential-store.md]

### Utilities

- **Every dump prints a stray warning:** `Charset id '33' csname 'UTF8' trying to replace existing csname 'utf8mb3'`.
- **The load progress file name in the help is wrong.** The help gives `load-progress.<server_uuid>.progress` in one place and `.json` in another. On MariaDB the file is `load-progress.<server_id>.json` (observed: `load-progress.2.json`). [dump-and-load/load-dump-utility.md uses the real name]
- **Minimum version not enforced.** "MariaDB 10.11 or newer" is stated in the help (`mod_util.cc`), but no code check enforces it. [dump-and-load/README.md repeats the help]
- **MySQL-only options accepted silently for a MariaDB source:**
  - dump: `libraries`/`includeLibraries`/`excludeLibraries`, `dataMaskingPolicies`, `allowDataMasking`, `lakehouseTarget`
  - `export_table`: `allowDataMasking`
  - copy: rejects `ocimds` as an invalid option, while dump refuses it with a reason; the two are inconsistent
- **The dump manifest's `dumper` field** reads "mysqlsh Ver 26.9.5".
- **macOS test hosts:** eight or more concurrent loads into one table can hang the MariaDB server. This is a server issue, not reproduced on Linux; it comes from the Confluence source. It isn't on the Limitations page (the server doesn't support macOS); this is a test-host note only.

### Shell, Output and Reports

- **`\show threads`:**
  - The `system.<variable>` columns read `performance_schema.variables_by_thread`, which MariaDB doesn't have.
  - The default filter compares the session's connection user/host with `PROCESSLIST_USER`/`PROCESSLIST_HOST`, so a session to `127.0.0.1` that the server records as `localhost` lists nothing (MySQL too).
  [extending-mariadb-shell/reports.md]
- **`\show thread --vars`, `--raw-locks` and `--all` fail on MariaDB**, which has no `variables_by_thread` or `data_locks` tables. The `thread` report also needs `performance_schema=ON`. [extending-mariadb-shell/reports.md]
- **A report function gets `argv`/`options` only if the report declares them**; without defaults, the call fails. [extending-mariadb-shell/reports.md]
- **`--json` prints an empty `{}` first.** [using-mariadb-shell/output-formats.md]
- **`--pym` prints "Error in my_thread_global_end(): 1 threads didn't exit"** at exit, for example on `--pym pip`.
- **`--pym pip install` from PyPI timed out** in the bundled Python, while curl reached pypi.org. Installing a local wheel with `--user` or `--target` works. On Windows, pip may be missing from the vcpkg Python (MARIADB_PORT.md §11.1). [customizing/python-module-search-paths.md]
- **An empty or invalid prompt theme** falls back to the prompt `mysql-py>` (`base_shell.cc:235`). [customizing/prompt.md]
- **The shell starts in SQL mode**, and `-e` runs SQL unless `--py` is given, while the help says the default mode is py. `defaultMode` also accepts `js`. [using-mariadb-shell/sql-and-python-modes.md]
- **`db` is always `None`.** It is only set for X sessions; README.md says it holds the default schema. [getting-started/global-objects.md]
- **`track_system_variable`** accepts only `sql_mode`, which MariaDB never reports through session tracking (MARIADB_PORT.md §13.2). It is left out of the docs.
- **Syslog records a `/command` as the `\command` it ran as** (`/source f` is logged as `\source f`), while the history keeps what was typed. [logging-and-debugging.md]
- **`shell.create_result()` cannot declare `Decimal` or `Float` columns** (`custom_result.cc` `db_type()` maps "float" to Double and knows no "decimal"), although `\? create_result` doesn't say so.
- **Tab doesn't complete a command after leading whitespace**, for either prefix: `  \co` and `  /co` stay as typed. [using-mariadb-shell/autocompletion.md]
- **Argument completion splits the line on spaces only**, so a quoted or tab-separated argument isn't completed. [using-mariadb-shell/autocompletion.md]

### MCP Server

- **uvicorn's access log in the MCP server** prints request paths with query strings, so `GET /authorize?client_id=…&state=…` shows the OAuth client ID. It is uvicorn's own logger (stdout), not `log_event`; `access_log=False` in `lib/server.py`'s `uvicorn.Config` would silence it. [mcp-server/starting-the-mcp-server.md would need a note]

### Sandboxes

- **Sandbox server packages aren't fetched automatically.** The `mariadb-*-sandbox.tar.gz` packages on the release are fetched by neither the plugin nor the installers; the docs describe them from the build workflows only. [sandbox-instances.md]

### Installation and Packaging

- **The default install skips prereleases.** `install.sh`/`install.ps1` without `MARIADB_SHELL_TAG` skip GitHub prereleases, and every release so far (v26.9.5 included) is one, so the default command fails today. [installation/*, hint]
- **Release tarballs contain no LICENSE file.** [license.md links to the repo's LICENSE]
- **The shell hard-codes `setlocale(LC_ALL, "en_US.UTF-8")`** (`src/mysqlsh/main.cc:444`) and prints "Cannot set LC_ALL to locale en_US.UTF-8" on every run where that locale isn't generated, as in slim container images; setting `LC_ALL=C.UTF-8` doesn't help. A fallback to `C.UTF-8` would fix it. [docker/linux-debian13-mcp-server.Dockerfile generates the locale as a workaround]

## Help Text and Man Page Out of Date

These items still describe MySQL Shell, or disagree with the code. The docs follow the code.

- **`mysqlx` scheme accepted at parse time** (to fix as one change across the shell and code_ext):
  - The invalid-scheme error lists `mysqlx`, and `shell.parse_uri("mysqlx://…")` succeeds; `shell.connect` and the command line prompt for the password and only then fail with `Unknown MariaDB URI type mysqlx`.
  - Cause: the default allowed set `{"mariadb","mysql","mysqlx"}` of `IConnection` (`mysqlshdk/libs/utils/connection.h`); the connect-time rejection is `Connection_options::get_session_type`.
  - Fix: build that set without `mysqlx` unless `HAVE_X_PROTOCOL`. Product code that builds a `mysqlx` scheme is already behind `HAVE_X_PROTOCOL`; three unguarded branches in `connection_options.cc` become unreachable.
  - Tests that use `mysqlx` as a sample scheme without a guard: about 142 lines in `uri_parser_t.cc`, 17 in `shell_cmdline_options_t.cc`, 6 in `connection_options_t.cc`, one weak assertion in `interactive_shell_t.cc`, and the SSH scripted tests `shell_ssh_errors_norecord.py` / `shell_ssh_import_export_norecord.py`. Swap them to `mariadb` in the MariaDB build, or guard the X-specific ones.
  - mariadb-shell-plugins: code_ext's `CONNECTION_SCHEMES` (`code_ext/src/connections/connectionUri.ts`) mirrors the shell's set and lets the connection editor save a `mysqlx://` connection; drop it there too, with its icon entry (`treeItems.ts`) and about 8 test assertions. mcp_plugin already treats an unparsable URI as "not a connection", and its tests pass either way.
- **Exposed in help but MySQL-only or meaningless:**
  - `--get-server-public-key`
  - the X values of `auth-method`
  - the OCI, WebAuthn, OpenID Connect and Kerberos connection options
  - `set_query_attributes` and `set_option_tracker_feature_id`
- **Dump and load help:**
  - it still lists `lakehouseTarget`, `dataMaskingPolicies`, `allowDataMasking`, `libraries`, `convertInnoDbVectorStore`, `heatwaveLoad`, `lakehouseSource`, `disableBulkLoad`, `analyzeTables: "histogram"` and the full HeatWave `compatibility` section
  - it refers to "Section 13.2.10.1"
  - `dump-schemas --help` lists `compatibility` and `allowDataMasking`
