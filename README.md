# MariaDB Shell

Copyright (c) 2016, 2026, Oracle and/or its affiliates.
Copyright (c) 2026, MariaDB plc.

MariaDB Shell is an advanced client and code editor for MariaDB. It is derived
from MySQL Shell and adapted to build and run against MariaDB server sources and
the MariaDB Connector/C.

License information can be found in the [LICENSE](LICENSE) file.
This distribution may include materials developed by third parties. For license
and attribution notices for these materials, please refer to the [LICENSE](LICENSE) file.

Contributing information  be found in the [CONTRIBUTING.md](CONTRIBUTING.md) file.

The shell installs as `mariadb-shell`; `msh` is provided as a short alias.

```
$ mariadb-shell root@localhost
MariaDB localhost:3306 ssl  SQL > SELECT VERSION();
```

## Key Features

### SQL and Python

MariaDB Shell provides an interactive console for **SQL** and **Python**, and
switches between them at runtime with `\sql` and `\py`. It connects over the
classic MariaDB client/server protocol.

The SQL mode offers multi-line editing, persistent history, autocompletion of
schema/table/column names, result formats (table, vertical, tab-separated, JSON
and NDJSON), pager support, and column type information.

The Python mode exposes the same connections as scriptable objects, so ad-hoc
scripts and reusable modules can be run with `-f`, `--pym`, `-c` or from a
start-up script.

### Scriptable API

The following global objects are available in Python mode:

| Global | Purpose |
|---|---|
| `shell` | Connections and sessions, `shell.options`, credential store, reports, SQL handlers, extension objects, prompts, pager |
| `session` / `db` | The current session and its default schema |
| `mysql` | Classic sessions, SQL parsing/tokenizing helpers, identifier and account quoting |
| `util` | `changePassword`, `upgradeAuthMethod`, and the `util.debug` diagnostics collectors |
| `sandbox` | Deploy and manage local server sandboxes (see below) |
| `plugins` | Install, list, update and remove shell plugins |

Every API is also reachable from the operating system shell through **API
Command Line integration**, so no scripting is needed for one-off calls:

```bash
$ mariadb-shell root@localhost -- util debug collect-diagnostics /tmp/diag.zip
$ mariadb-shell -- shell status
```

Run `\? cmdline` inside the shell for the full mapping rules.

### Local Server Sandboxes

The bundled `sandbox` plugin deploys and manages throwaway local server
instances — useful for testing, reproducing issues and development:

```
sandbox.deploy(3310)      sandbox.start(3310)     sandbox.stop(3310)
sandbox.kill(3310)        sandbox.delete(3310)
sandbox.vendor()          sandbox.version()
```

It supports both **MariaDB** and **MySQL** servers, auto-detecting the vendor
from the server binaries on `PATH` (or from an explicit `basedir`), and handles
the per-vendor differences in datadir initialization, authentication and TLS
certificate generation.

### Reports

Built-in reports (`query`, `thread`, `threads`) are available through
`shell.reports` and the `\show` / `\watch` commands, and custom reports can be
registered in Python from the `init.d` directory of the configuration path.

### Credential Store

Passwords can be stored in and retrieved from the platform's own secret store,
so they do not need to be retyped or embedded in scripts. The available helpers
depend on the platform:

- **macOS** — Keychain
- **Linux** — Secret Service (GNOME Keyring, KWallet)
- **Windows** — Windows Credential Manager
- **All platforms** — a `plaintext` helper, for testing only

See [docs/CREDENTIAL_STORE.md](docs/CREDENTIAL_STORE.md) for details, and
`shell.storeCredential` / `shell.listCredentials` for the API.

### Plugins

Shell functionality can be extended with plugins written in Python, loaded from
the `plugins` directory of the configuration path. The `plugins` global manages
them. See `\? plugins` for the layout an `init.py` must follow.

### Diagnostics

`util.debug.collectDiagnostics`, `collectHighLoadDiagnostics` and
`collectSlowQueryDiagnostics` gather server and shell state into a single zip
file for support and troubleshooting.

### Connections

- Classic MariaDB protocol over TCP/IP and Unix sockets
- Encrypted connections, including `--ssl-mode`, CA/certificate/CRL paths,
  cipher and TLS-version selection
- **SSH tunneling** (`--ssh`), with identity and config file support
- Protocol compression
- Connection data from URIs, individual options, or option files
  (`[mariadb-shell]`, `[mysqlsh]` and `[client]` groups)

## Features Not Available in This Build

MariaDB Shell is a MariaDB-targeted build, and the following MySQL-specific
features of MySQL Shell are deliberately excluded. Most are simply not compiled
in, so their globals and methods do not exist at all — a handful whose backends
were MySQL-specific still exist but raise a "not supported" error. Either way,
`\?` and `--help` list only what this build actually has.

| Feature | Reason |
|---|---|
| **JavaScript mode** (`--js`) | Requires the GraalVM/Truffle JIT executor, which is not shipped |
| **X Protocol / X DevAPI** | `mysqlx://` URIs, X sessions, collections and the document store depend on MySQL's `libmysqlxclient` |
| **AdminAPI** (`dba`, InnoDB Cluster / ReplicaSet / ClusterSet) | Built on MySQL Group Replication and the MySQL metadata schema |
| **Upgrade Checker** (`util.checkForServerUpgrade`) | Encodes MySQL Server upgrade rules |
| **Dump, load and copy utilities** (`util.dumpInstance`, `util.loadDump`, `util.copySchemas`, `util.importTable`, `util.dumpBinlogs`, …) | Depend on MySQL-specific DDL handling, capabilities and binlog semantics |
| **MySQL REST Service (MRS) management** | MySQL Router plugin specific |
| **`--register-factor`** | Uses the MySQL FIDO/WebAuthn authentication plugin |

For the engineering detail behind each exclusion, and the macros that gate them,
see [MARIADB_PORT.md](MARIADB_PORT.md).

## Server Compatibility

MariaDB Shell links the MariaDB Connector/C and speaks the classic protocol, so
it connects to MariaDB servers as well as to MySQL servers reachable over the
same protocol. It is built and validated against the MariaDB server version
recorded in the build output of `mariadb-shell --version`.

Note that `--ssl-mode=REQUIRED` cannot be strictly enforced by every
Connector/C version the shell may be built against; when it cannot, the shell
warns at connection time. See [MARIADB_PORT.md](MARIADB_PORT.md) for the current
list of items under validation.

## Files and Environment

| Path | Contents |
|---|---|
| `~/.mariadb-shell/` | Per-user configuration: history, `prompt.json`, `init.d/`, `plugins/`, and the log file. On Windows, `%AppData%\MariaDB\mariadb-shell` |
| `~/.mariadb-shell/mariadb-shell.log` | Shell log file; override with `--log-file` |
| `mariadb-shellrc.py` | Optional start-up script, read from the global config directory, `share/mariadb-shell/`, then the per-user directory |
| `share/mariadb-shell/`, `lib/mariadb-shell/` | Installed data files and private libraries, including the bundled Python runtime and built-in plugins |

The shell's environment variables use the `MARIADB_SHELL_` prefix —
`MARIADB_SHELL_HOME`, `MARIADB_SHELL_USER_CONFIG_HOME`,
`MARIADB_SHELL_PROMPT_THEME`, `MARIADB_SHELL_TERM_COLOR_MODE` and others. The
pre-rename `MYSQLSH_` names are still accepted, and are consulted only when the
`MARIADB_SHELL_` name is unset.

## Compiling from Source

See [INSTALL.md](INSTALL.md) for platform-by-platform build instructions, and
[MARIADB_PORT.md](MARIADB_PORT.md) for the build wiring of the MariaDB port.

## Documentation

### Built-in Help

MariaDB Shell includes a built-in help system. Reference documentation for
commands and APIs is obtained with the `\h` or `\?` built-in commands, which is
the authoritative reference for this build — it only lists what is actually
compiled in.

Example:

```text
MariaDB localhost:3306 ssl  Py > \h shell.connect
NAME
      connect - Establishes the shell global session.

SYNTAX
      shell.connect(connectionData[, password])

WHERE
      connectionData: the connection data to be used to establish the session.
      password: The password to be used when establishing the session.
...
```

The `mariadb-shell(1)` manual page documents the command-line options, files and
environment variables.

### Upstream Documentation

Because MariaDB Shell is derived from MySQL Shell, the upstream documentation
remains a useful reference for the shared functionality — bearing in mind that
it also covers the features listed as unavailable above:

https://dev.mysql.com/doc/mysql-shell/en/
