# MySQL Shell — MariaDB Port

This document describes the port of MySQL Shell to build and run against
**MariaDB** (server sources + MariaDB Connector/C) instead of MySQL. It covers
the build setup, what was dropped, the notable engineering decisions, and the
items that still need validation against a live MariaDB server.

The port is gated behind the **`MARIADB_BUILD`** macro (defined automatically
when `MARIADB_SOURCE_DIR` is set — see [CMakeLists.txt](CMakeLists.txt)). A
normal MySQL build is unaffected: every change is wrapped in
`#ifdef MARIADB_BUILD` / `#ifndef MARIADB_BUILD` or `IF(MARIADB_BUILD)` in CMake.

### Feature-specific gating macros

`MARIADB_BUILD` is still the master switch, but feature exclusions that are
*not* intrinsic to the build (Connector/C, mysys, Python, binlog port, …) are
gated on dedicated **`HAVE_*`** macros so the intent of each guard is explicit.
These are defined in [CMakeLists.txt](CMakeLists.txt) **exactly when
`MARIADB_BUILD` is not** (i.e. on normal MySQL builds):

| Macro | Gates | Convention |
|---|---|---|
| `HAVE_UPGRADE_CHECKER` | the Upgrade Checker | `#ifdef HAVE_UPGRADE_CHECKER` = MySQL-only code |
| `HAVE_ADMIN_API` | AdminAPI (`dba`, Cluster/ReplicaSet/ClusterSet, InnoDB Cluster, metadata) | `#ifdef HAVE_ADMIN_API` = MySQL-only code; `#ifndef HAVE_ADMIN_API` = MariaDB stub |
| `HAVE_X_PROTOCOL` | X protocol / X DevAPI (`mysqlx://`, X sessions, collections, X expr parser, `importJson`) | `#ifdef HAVE_X_PROTOCOL` = MySQL-only code; `#ifndef HAVE_X_PROTOCOL` = MariaDB stub |
| `HAVE_DUMP_AND_LOAD` | the dump/load utilities | `#ifdef HAVE_DUMP_AND_LOAD` = MySQL-only code |

A bare `#ifdef MARIADB_BUILD` / `#ifndef MARIADB_BUILD` now denotes a guard that
is **neither** AdminAPI nor X-protocol — i.e. an intrinsic build difference
(Connector/C client-API gaps, mysys lifecycle, Python macro conflicts, the
binlog port and its native GTID model, version/error-code macros, the
`.mylogin.cnf` implementation behind the login-path helper). See §10 for the
full inventory.

---

## 1. Building

### 1.1 Prerequisites

| Dependency | Notes |
|---|---|
| MariaDB server sources | provides `include/`, `sql/`, and the bundled Connector/C under `libmariadb/` |
| MariaDB build dir | must contain the built static libs: `libmariadb/libmariadb/libmariadbclient.a`, `mysys/libmysys.a`, `mysys_ssl/libmysys_ssl.a`, `strings/libstrings.a`, `dbug/libdbug.a`, and `client/mariadb-binlog` |
| OpenSSL | the shell links OpenSSL; **the Connector/C must be built with the same OpenSSL** (see §1.3) |
| RapidJSON, ANTLR4, libssh, zstd, googletest | same as the MySQL build |
| SQLite3 | needed by the bundled Python's `_sqlite3` stdlib module; supplied via the vcpkg manifest (`vcpkg.json`), not the system. See §12.4 |

### 1.2 Configure & build the shell

The build is driven by `-DMARIADB_SOURCE_DIR` / `-DMARIADB_BUILD_DIR` (instead of
`-DMYSQL_SOURCE_DIR` / `-DMYSQL_BUILD_DIR`). Example (matches `.vscode/settings.json`):

```bash
cmake -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMARIADB_SOURCE_DIR=/path/to/server \
  -DMARIADB_BUILD_DIR=/path/to/server/mariadb-build \
  -DWITH_RAPIDJSON=/path/to/rapidjson \
  -DWITH_GOOGLE_TEST=/path/to/googletest \
  -DBUNDLED_OPENSSL_DIR=/path/to/openssl/install/usr/local \
  -DBUNDLED_SSH_DIR=/path/to/libssh/.../lib/cmake/libssh \
  -DBUNDLED_ANTLR_DIR=/path/to/antlr4/.../install/usr/local \
  -DWITH_ZSTD=/path/to/zstd \
  -DHAVE_PYTHON=1 \
  -DBUNDLED_PYTHON_DIR=/path/to/Python/install/usr/local \
  <path-to-mysql-shell-source>

ninja all      # produces bin/mariadb-shell
```

> JavaScript stays off (dropped). Python is optional: pass `-DHAVE_PYTHON=1` +
> `-DBUNDLED_PYTHON_DIR=...` to enable `--py` (the flag is `HAVE_PYTHON`, not
> `WITH_PYTHON`). The Python scripting layer is not MySQL-specific and works.
> The bundled Python must have **`certifi`** installed (the plugin manager
> imports it) — otherwise plugin loading logs a non-fatal warning.

### 1.3 Rebuild MariaDB Connector/C with OpenSSL (required once)

By default the Connector/C picks **GnuTLS** on macOS/Linux
(`server/cmake/mariadb_connector_c.cmake`: `SET(CONC_WITH_SSL "GNUTLS")`), which
clashes with the shell's OpenSSL at link time (undefined `gnutls_*` symbols).
Rebuild only the connector against the shell's OpenSSL — the server's own
`WITH_SSL` (bundled WolfSSL) is irrelevant, since the shell never links the
server:

```bash
cd /path/to/server/mariadb-build
cmake -DCONC_WITH_SSL=OPENSSL \
      -DOPENSSL_ROOT_DIR=/path/to/openssl/install/usr/local .
ninja mariadbclient

# verify (should print 0 gnutls refs, and >0 OpenSSL refs):
nm libmariadb/libmariadb/libmariadbclient.a | grep -c gnutls
nm libmariadb/libmariadb/libmariadbclient.a | grep -c -iE "SSL_connect|SSL_CTX_new"
```

Then relink the shell: `ninja bin/mariadb-shell`.

> **Windows note:** the connector's TLS backend does not matter for reaching a
> MariaDB **wolfSSL** server — *both* OpenSSL and Schannel connectors fail the
> handshake against it (see §11.5.1). Schannel was tried as a fix and ruled out.
> OpenSSL remains fine to build the Windows connector with; keep it for parity
> with POSIX.

---

## 2. Dropped features

These are gated out for MariaDB (the rest of the shell — SQL mode, dump/load,
upgrade checker, etc. — is supported):

| Feature | Why | Effect on MariaDB build |
|---|---|---|
| **JavaScript** | GraalVM/Truffle not provided | `HAVE_JS` off (already the default) |
| **X protocol / X DevAPI** | libmysqlxclient + protobuf are MySQL-only | `mysqlx://`, collections, X sessions removed; `db/mysqlx/*`, `modules/devapi/*` (X parts), protobuf, lz4 excluded from the build |
| **AdminAPI** | InnoDB Cluster/ReplicaSet/ClusterSet are MySQL-specific | `dba` global, `cluster`/`rs`/`clusterset`, `modules/adminapi/*` excluded |

Only the **shared** DevAPI base classes (`base_constants`, `base_resultset`,
`dynamic_object`) are still compiled — the classic resultset/object model needs
them.

### Features that throw "not supported" at runtime
(Their backends were X/MySQL-specific; they compile but are stubbed.)

- `util.importJson` — used the X document store
- `--register-factor` — used the MySQL FIDO/WebAuthn auth plugin
- report `--where` / `--having` filtering — used the X expression parser
- cluster `--redirect-primary` / `--redirect-secondary` — used AdminAPI

---

## 3. Classic protocol → MariaDB Connector/C

The classic session ([mysqlshdk/libs/db/mysql/session.cc](mysqlshdk/libs/db/mysql/session.cc))
was adapted to libmariadb, which lacks several MySQL 8.x client APIs:

| MySQL API | MariaDB handling |
|---|---|
| `MYSQL_OPT_SSL_MODE` / `MYSQL_OPT_SSL_ENFORCE` | mapped best-effort onto `MYSQL_OPT_SSL_VERIFY_SERVER_CERT`; **`ssl-mode=REQUIRED` cannot be strictly enforced** and warns at runtime |
| query attributes (`mysql_bind_param`) | not supported — dropped, warns if used |
| MFA (`MAX_AUTH_FACTORS`, `MYSQL_OPT_USER_PASSWORD`) | primary password routed through `mysql_real_connect`; 2nd/3rd factors warn |
| `MYSQL_OPT_COMPRESSION_ALGORITHMS`, `MYSQL_OPT_TLS_CIPHERSUITES`, `MYSQL_OPT_GET_SERVER_PUBLIC_KEY` | not in libmariadb — dropped, warn if used |
| `mysql_real_escape_string_quote` | falls back to `mysql_real_escape_string` |
| `CR_*` client error codes | `#include <errmsg.h>`; MySQL-only `CR_*`/`ER_*` constants gated out |
| `MYSQL_TYPE_VECTOR` | gated out (MySQL 9 type) |

---

## 4. Binlog library port (`util.dumpBinlogs` / `util.loadBinlogs`)

The binlog utility was **ported**, not dropped. The original used MySQL's client
binlog API (`MYSQL_RPL`, `mysql_binlog_open/fetch/close`) and **libbinlogevents**
(`mysql::binlog::event`, `mysql::gtids`) — none of which exist in MariaDB.

What changed:

1. **Low-level streaming** ([db/mysql/binary_log.cc](mysqlshdk/libs/db/mysql/binary_log.cc))
   rewritten against MariaDB Connector/C's `mariadb_rpl_*` API. The parsed GTID
   (`domain/server/sequence`) is surfaced on `Binary_log_event`, so the utility
   no longer needs libbinlogevents to decode events.
2. **GTID model** ([modules/util/binlog/utils.h](modules/util/binlog/utils.h)/`.cc`):
   a native MariaDB GTID library — `Gtid` (`domain-server-seq`) and `Gtid_set`
   with interval algebra (`add`, `subtract`, `is_subset`, `contains`,
   `inplace_union`, parse/format), replacing `mysql::gtids`.
3. **dumper / loader / options** rewired to read GTIDs from the event fields and
   use the native set algebra.

> ⚠️ **Needs validation against a live MariaDB.** The GTID set semantics
> (subtraction, subset/contains, incremental-load file selection) are
> structurally correct but were not exercised against real binlog dumps. MariaDB
> GTID *position* strings (`d-s-N`, one per source) cannot express gaps the way
> MySQL ranges can — `Gtid_set::to_string()` emits the highest covered sequence
> per source.

The bundled binlog tool is `mariadb-binlog` (MariaDB's `mysqlbinlog`); the loader
invokes it by that name.

---

## 5. Build wiring (CMake)

- **Client libraries** ([CMakeLists.txt](CMakeLists.txt)): `MYSQL_LIBRARIES` =
  `libmariadbclient.a` + `libmysys.a` + `libmysys_ssl.a` + `libstrings.a` +
  `libdbug.a` (the shell calls `get_charset`, `my_load_defaults`, `my_convert`,
  DBUG, … directly — these live in the server's mysys/strings/dbug, not in the
  client lib). mysys/strings are listed twice for the linker's circular refs.
- **zstd / zlib** ([mysqlshdk/CMakeLists.txt](mysqlshdk/CMakeLists.txt)): linked
  explicitly (`libzstd_static`, `z`); X-only `lz4`/`protobuf`/`mysqlx` libs
  dropped.
- **yparser** ([mysqlshdk/libs/yparser/CMakeLists.txt](mysqlshdk/libs/yparser/CMakeLists.txt)):
  the runtime plugin *loader* (`parser.cc`) is built so `yacc::Parser` symbols
  resolve, but the per-version MySQL-grammar plugins under `mysql/` are **not**
  built (they need MySQL's `sql_yacc.yy`). Syntax-check upgrade checks therefore
  have no parser plugin to load at runtime.
- **adminapi_modules** removed from the `api_modules` / `mysqlsh` link.

---

## 6. Runtime initialization (mysys lifecycle)

libmariadb's `mysql_library_init()` does **not** initialize the *server* mysys
library the shell links separately. Handled in
[shell_init.cc](mysqlshdk/shellcore/shell_init.cc):

- `my_init()` is called early in `global_init()` (before `my_load_defaults`
  parses the command line / `my.cnf`), otherwise mysys' thread-local error state
  is null and the first mysys call segfaults.
- `my_end()` is registered via `atexit()` (and called from `global_end()`) so it
  runs on **every** exit path — including early exits like `--version` that
  bypass `global_end()` — before mysys' own safemalloc atexit handler.
- The `my_load_defaults()` argv buffer is released with `free_defaults()` after
  option parsing ([shell_options.cc](mysqlshdk/shellcore/shell_options.cc)).
  MySQL frees it via a caller `MEM_ROOT`; MariaDB requires `free_defaults()`.
  (Without this, the debug-build safemalloc leak reporter crashes at exit.)
- A stub `FI_DEFINE(mysqlx)` fault-injection handler is provided in session.cc
  (the X session normally defines it) so the `FI_SUPPRESS(mysqlx)` calls in
  shared code paths have a valid target.

### Python scripting (`-DHAVE_PYTHON=1`)

- **Macro conflict:** MariaDB's `my_config.h` and Python's `pyconfig.h` both
  define autoconf size/`HAVE_*` macros (`SIZEOF_SIZE_T`, …) with identical
  values, tripping `-Werror,-Wmacro-redefined`. Suppressed at both include
  sites: `debug.h` (around `my_global.h`) and `python_utils.h` (around
  `Python.h`) — they cover the two include orderings.
- **REPL/startup imports:** `base_shell.cc` no longer runs
  `from mysqlsh import mysqlx` at Python startup, and the bundled
  `mysqlsh/__init__.py` looks up `mysql`/`mysqlx` defensively (the X module is
  absent on MariaDB).

Also note: MariaDB has no MySQL-style **args-separator** in `my_load_defaults`,
and its signature takes 5 args (no `MEM_ROOT`). `handle_mycnf_options` was
reimplemented accordingly (it derives the config/cmdline boundary from the argc
growth instead of scanning for `----args-separator----`).

---

## 7. Verified working

- `mysqlsh --version` → `Ver 9.7.0 … for MariaDB <version>-MariaDB` (exit 0)
- `mysqlsh --help`
- Classic-protocol connect (e.g. to an unreachable host) → proper libmariadb
  error (`MySQL Error 2002: Can't connect to server`), no crash
- `ninja all` — clean

---

## 8. Open items / to validate

1. **Binlog GTID semantics** — exercise `util.dumpBinlogs` / `util.loadBinlogs`
   against a live MariaDB (see §4).
2. **`ssl-mode=REQUIRED`** — cannot be strictly enforced with this libmariadb
   (warns at runtime); confirm acceptable or use a newer Connector/C.
3. **Replication channel error mapping** — `ER_REPLICA_CHANNEL_DOES_NOT_EXIST`
   was mapped to `ER_MASTER_INFO`; verify against a live server (AdminAPI-adjacent
   path, unlikely to be hit by core flows).
4. **Scripting modes** — JavaScript is dropped (no `--js`). **Python is enabled**
   (`-DHAVE_PYTHON=1 -DBUNDLED_PYTHON_DIR=...`) and `--py` works. For clean
   plugin loading, install `certifi` into the bundled Python (otherwise a
   non-fatal "errors loading plugins" warning appears). The bundled `debug`
   plugin is also X/`certifi`-dependent and may not load.

---

## 9. Unit tests (`-DWITH_TESTS=1`)

The test tree (`unittest/`) was not gated for the port the way `modules/` was, so
enabling tests surfaced X-protocol, AdminAPI and server-header dependencies. The
build now produces `run_unit_tests` (and `mariadb-shell-rec`) cleanly. The suite has
**not** yet been run against a live MariaDB — only verified to build and load
(`run_unit_tests --gtest_list_tests`).

### Approach
- **Shared test infra → gated inline** (`#ifndef MARIADB_BUILD`):
  `test_utils/shell_test_env.{h,cc}` (X `create_mysqlx_session`, AdminAPI
  `check_min_version_skip_test`), `test_utils/mod_testutils.{h,cc}` (sandbox
  `ProvisioningInterface`/`dba::Instance`/metadata → throw "not supported"),
  `shell_script_tester.cc` (`supports_paxos_single_leader`, `LIBMYSQL_VERSION_ID`
  → `MYSQL_VERSION_ID`), `db_session_t.cc` / `modules/mod_shell_t.cc`
  (classic-only), `shell_cmdline_options_t.cc` (`SessionType::X`),
  `mysqlshdk/libs/utils/utils_general_t.cc` (`LIBMYSQL_VERSION` →
  `MYSQL_SERVER_VERSION`).
- **Whole X/AdminAPI test files → excluded** in `unittest/CMakeLists.txt` under
  `if(MARIADB_BUILD)` (mirrors the existing JS/Python `REMOVE_ITEM` blocks):
  expr/orderby/proj parsers, devapi tests, AdminAPI tests + mocks +
  `admin_api_test.cc`, the yparser `parser_t.cc` (no grammar plugins, see §5).
- **`test_main.cc` capi rename** — the test target's `-I` order resolves
  `<mysql.h>` to the *server* header, which applies `mariadb_capi_rename.h`
  (prefixes the client API with `server_*`). `detect_mysql_environment()` was
  rewritten to use the shell's classic `mysqlshdk::db::mysql::Session` instead of
  the raw client API (queries wrapped in try/catch since some vars, e.g.
  `@@mysqlx_port`, don't exist on MariaDB); `delete_sandbox()`'s raw block is
  gated out.
- **Link**: X-only libs (`MYSQLX_LIBRARIES`/`PROTOBUF_LIBRARY`/`LZ4_LIBRARY`)
  dropped from the `run_unit_tests` link.
- **Compiler flags** (MariaDB test target only): `-Wno-error=deprecated-declarations`
  (newer bundled googletest deprecates `testing::Invoke(callable)`) and
  `-Wno-macro-redefined` (Python 3.x `pyconfig.h` vs server `my_config.h`
  `SIZEOF_*`).

### Deferred — lost coverage to revisit
These were **excluded**, not ported, and remove MariaDB-relevant coverage:
`completion_frontend_t`, `mysqlshdk/shellcore/interrupt_t` (heavily X-coupled
fixtures), and `mysqlshdk/libs/mysql/{clone_t,replication_t}` +
`mysqlshdk/libs/gr/group_replication_t` (pull `clone.cc`/`repl_config.cc`, which
still reference `mysqlsh::dba::` helpers that live in the unbuilt AdminAPI).
Re-enabling them needs either an X-free rewrite or porting those `dba` helpers.

---

## 10. Leftover `MARIADB_BUILD` usage (intrinsic build differences)

After the AdminAPI / X-protocol guards were migrated to `HAVE_ADMIN_API` /
`HAVE_X_PROTOCOL` (§1), the `MARIADB_BUILD` macro remains **only** for guards
that are neither — intrinsic differences between the MySQL and MariaDB builds.
These are intentional and should stay as `MARIADB_BUILD`:

| Area | Files | Reason |
|---|---|---|
| Connector/C client API | `libs/db/mysql/session.cc` (most guards), `libs/db/mysql/result.cc`, `libs/mysql/utils.{h,cc}`, `libs/mysql/async_replication.cc`, `mod_mysql.cc`, `import_table/load_data.cc`, `dump/dumper.cc`, `load/dump_loader.cc` | libmariadb lacks MySQL 8.x client APIs (ssl-mode, query attributes, MFA, compression/cipher opts, `MYSQL_TYPE_VECTOR`, `CR_*`/`ER_*` codes, escape-quote) |
| Header include order | `dump/schema_dumper.cc`, `upgrade_checker/{custom_check,upgrade_check_creators}.cc` | `my_global.h` / `m_ctype.h` / `my_dbug.h` ordering for libmariadb |
| Secret store `.mylogin.cnf` | `mysql-secret-store/login-path/login_path_helper.h` (which header, which class backs `m_invoker`) + `login_path_helper.cc` (the mysys includes, the `store()` preamble, the `load()` body) + the new `login_file.{h,cc}` | MariaDB has no `.mylogin.cnf` support and no `mysql_config_editor`, so both halves are implemented in-tree on OpenSSL (§12.7) |
| mysys lifecycle | `shellcore/shell_init.cc`, `shellcore/shell_options.cc` (defaults), `include/shellcore/shell_options.h` | `my_init`/`my_end`/`my_load_defaults`/`free_defaults`, `MEM_ROOT` differences |
| Python macro conflicts | `include/scripting/python_utils.h`, `libs/utils/debug.h` | `pyconfig.h` vs `my_config.h` `SIZEOF_*` redefinition; DBUG API differences |
| UUID / version macros | `libs/utils/uuid_gen.cc`, `libs/utils/utils_general.cc`, `shell_script_tester.cc`, `utils_general_t.cc` | `my_rnd_*` rename; `LIBMYSQL_VERSION*` → `MYSQL_*` |
| Binlog port + GTID model | all of `modules/util/binlog/*` except the AdminAPI metadata lookups | `mariadb_rpl_*` streaming, native `Gtid`/`Gtid_set`, libbinlogevents replacement, `mariadb-binlog` tooling (§4) |
| Misc build glue | `src/mysqlsh/cmdline_shell.cc` (`STDERR_FILENO`), `src/mysqlsh/main.cc` (FIDO/WebAuthn auth plugin) | not a feature module |
| Test harness | `unittest/test_main.cc` (raw client probe) | Connector/C environment detection |

Note: a handful of binlog option files (`dump_binlogs_options.cc`,
`load_binlogs_options.cc`) mix both — their GTID/streaming guards stay
`MARIADB_BUILD` while the InnoDB-Cluster metadata lookups they perform were
migrated to `HAVE_ADMIN_API`.

The `unittest/CMakeLists.txt` yparser-grammar exclusion also stays
`IF(MARIADB_BUILD)` (a build-artifact concern — the per-MySQL-version grammar
plugins aren't built, §5 — not a feature gate), as do the MariaDB-only test
compiler flags (`-Wno-error=deprecated-declarations`, `-Wno-macro-redefined`).

---

## 11. Windows platform notes

### 11.1 Python bundling under vcpkg (CMake)

`FindPython3` may resolve vcpkg's Python, which has a **split layout** (headers
under `<prefix>/include/pythonX.Y`, runtime under `<prefix>/tools/python3`)
unlike a python.org install (self-contained root). The Windows bundling logic in
`CMakeLists.txt` derives `PYTHON_ROOT` from the **interpreter** (`dirname
Python3_EXECUTABLE`), not the headers, so both layouts work; deriving from the
headers lands on `<prefix>/include` and bundles empty `Lib`/`DLLs`. A python.org
install is still preferred (it also ships `pip`/`ensurepip`, which vcpkg's does
not — bootstrap those with `mysqlsh --pym ensurepip` / `get-pip.py`, or pre-stage
`certifi`/`pyyaml`/`antlr4-python3-runtime`/`mcp` via `-DPYTHON_DEPS=<dir>`).

### 11.2 `run_unit_tests` link — keep `PYTHON_LIBRARIES` unquoted

vcpkg installs both release and debug libs, so `FindPython3` sets
`PYTHON_LIBRARIES` to a `optimized;<rel>;debug;<dbg>` keyword list. In
`unittest/CMakeLists.txt` it must be passed to `target_link_libraries`
**unquoted** — quoting collapses the keyword list into one argument and link.exe
gets a bare `optimized`/`debug` token → `LNK1104: cannot open file
'optimized.lib'`.

### 11.3 Sandbox plugin — `mariadb-install-db.exe` options

Windows ships `mariadb-install-db.exe` as a native C++ tool (not the POSIX shell
script) with a smaller option set: it infers `basedir` from its own location and
does **not** read `--defaults-file` / `--basedir` /
`--auth-root-authentication-method` (they fail as `unknown variable ...`). The
sandbox plugin (`python/plugins/sandbox/sandboxlib.py`, `_init_data_dir`) passes
only `--datadir` on Windows. Consequence: the throwaway InnoDB-sizing option file
isn't applied at boilerplate-init time, so the per-version boilerplate is built
with default file sizes; the sandbox's own `my.cnf` still carries the sizing, so
the redo log shrinks on the sandbox's first start.

### 11.4 Sandbox plugin — SSL cert generation is config-independent

`openssl req` always loads an openssl config, falling back to the build's
compiled-in `OPENSSLDIR` (vcpkg's points at a nonexistent
`C:\Program Files\Common Files\SSL\`). `_generate_ssl_certs` writes a minimal
`openssl-req.cnf` and passes it via `-config` to every `req` call, with
`x509_extensions = v3_ca` (`basicConstraints=critical,CA:TRUE`, `keyUsage`) so the
self-signed CA is valid — the config-less path was implicitly relying on the stock
`openssl.cnf` for that, which also produced unverifiable CAs on any OpenSSL whose
default config lacks it.

### 11.5 Sandbox TLS is off by default for MariaDB on Windows (wolfSSL)

MariaDB's Windows builds bundle **wolfSSL** (`WITH_SSL=bundled`). This wolfSSL's
server-side TLS **cannot complete a handshake with the shell's OpenSSL connector
nor with a Windows Schannel client**: every ClientHello is rejected regardless of
TLS version or key-exchange group. **Both client stacks confirmed to fail
(2026-07-18):** OpenSSL via `openssl s_client` (below), and Schannel via the stock
`mariadb.exe` itself — pointed at a TLS-enabled my.cnf it fails with
`SEC_E_ILLEGAL_MESSAGE (0x80090326)`, and the shell's Schannel-built connector
fails identically (`SEC_E_INVALID_TOKEN` when pinned to TLS 1.2). `mariadb.exe`
only ever appeared to "work" because it was connecting **in plaintext**. Diagnosis
(exhaustive, all client / config levers ruled out):

- The generated certs are valid — they complete a full TLS 1.3 handshake against
  an OpenSSL `s_server` (`rsa_pss_rsae_sha256`, verify OK).
- Server-side `have_ssl=YES`; certs load; paths resolve. Not a load/config issue.
  With valid v3 certs provided, the server now reaches `ready for connections`
  with TLS enabled — the handshake wall is separate from the startup abort below.
- Every client offer fails. **Re-confirmed 2026-07-18 via `openssl s_client
  -starttls mysql`** (authoritative; the STARTTLS handshake is done correctly,
  unlike an earlier raw-ClientHello probe whose "plaintext MySQL packet" result
  was an artifact of poking a STARTTLS port with a bare ClientHello):
  - TLS 1.3 default (PQC `X25519MLKEM768`, ~1906-byte ClientHello) → `unexpected eof`
  - TLS 1.3 `-groups x25519` → `illegal parameter` (TLS alert 47)
  - TLS 1.3 `-groups secp256r1` → `wrong version number` (server replies non-TLS)
  - TLS 1.2 default → `wrong version number` (server replies non-TLS)

  All end `no peer certificate available`, `Cipher is (NONE)`. Not the OpenSSL-3.5
  post-quantum group — classical groups fail identically.

**Cert-version theory ruled out (do not re-investigate).** MariaDB's doc
`security/…/certificate-creation-with-openssl` warns that wolfSSL rejects X.509
**v1** leaf certs and needs **v3** for TLS 1.2+. Investigated 2026-07-18: not our
failure — every handshake dies at *key exchange*, **before** any certificate is
sent (`no peer certificate available`). Our CA + server certs were already v3 (CA
via `x509_extensions=v3_ca`, server via `-extfile` SAN); only the client leaf was
v1 on OpenSSL 1.x (auto-v3 on OpenSSL 3.x through SKID/AKID). `_generate_ssl_certs`
now makes the client leaf explicitly v3 (throwaway `client-ext.cnf` with
`basicConstraints`/`keyUsage`/`extendedKeyUsage=clientAuth`) — correctness
hardening for `REQUIRE X509`, unrelated to this wall.

This is a MariaDB server-side wolfSSL TLS-interop limitation; with the server
build fixed there is no cert-side, config-side, or client-group workaround.
Therefore `sandbox.deploy()` defaults `ssl` to **false** when the resolved server
is MariaDB **and** the host is Windows (prints a one-line note; `ssl:true` still
honored, useful only for a MariaDB built against OpenSSL). MySQL-on-Windows
(OpenSSL) and all POSIX deployments keep TLS on. See
`_deploy`/`_build_option_file` in `python/plugins/sandbox/sandboxlib.py`.

**`ssl:false` must disable TLS explicitly, not just omit the certs.** MariaDB
12.x brings up TLS **by default** and *aborts at startup* when it can't load a
key — even with no `ssl_*` options in the option file:

```
SSL error: Unable to get private key
[ERROR] Failed to setup SSL
[ERROR] Aborting
```

So `_build_option_file` writes `skip_ssl` into `[mysqld]` whenever the sandbox is
deployed without certificates on MariaDB (`disable_ssl` arg). Merely leaving out
`ssl_ca`/`ssl_cert`/`ssl_key` is not enough — the server still tries to
auto-enable TLS and dies. (Only done for MariaDB; MySQL auto-generates its own
certs and starts regardless.)

**`caching_sha2_password` RSA keypair (MariaDB-only).** MariaDB ships
`caching_sha2_password` (for MySQL client compat) as a built-in plugin that
initializes at startup and looks for `private_key.pem`/`public_key.pem` in the
data directory. Unlike MySQL's `--initialize`, `mariadb-install-db` does not
auto-generate them, so every start logs a spurious
`[ERROR] caching_sha2_password: failed to read private_key.pem`. The sandbox
creates root with `mysql_native_password`, so nothing depends on the keys and
auth (including non-TLS, the Windows default) works regardless — the error is
cosmetic. `_deploy` provisions the pair anyway for MariaDB
(`_generate_caching_sha2_keypair`, `openssl genrsa` + `rsa -pubout` into the
datadir): it silences the startup error and lets a user-created
`caching_sha2_password` account authenticate over a non-TLS connection.
Best-effort — needed independently of TLS, so openssl is resolved on its own and
a missing openssl or a generation failure is non-fatal (the cosmetic error just
remains). MySQL is unaffected (it auto-generates the pair during
`--initialize-insecure`).

### 11.5.1 Schannel ruled out as a fix; only wolfSSL-client remains (unverified)

**Schannel was investigated as a fix and ruled out (2026-07-18).** Because the
stock `mariadb.exe` links Schannel (`dumpbin` shows `Secur32`/`bcrypt`/`CRYPT32`,
no OpenSSL DLLs) and appeared to connect, the hypothesis was that Schannel ↔
wolfSSL works and the shell just needed its Connector/C built with
`CONC_WITH_SSL=SCHANNEL`. **Tested and false:** a Schannel-built shell connector
fails with `SEC_E_ILLEGAL_MESSAGE`, and — decisively — `mariadb.exe` itself fails
with the *same* error the moment it is pointed at a TLS-enabled my.cnf. It only
appeared to work because it was connecting in plaintext. So Schannel is not a
workaround; both OpenSSL and Schannel clients fail identically.

**Root cause confirmed — it is server-side, not a client-library mismatch
(2026-07-18).** Querying the server over a plaintext session (`mariadb.exe
--skip-ssl`, `SHOW GLOBAL STATUS/VARIABLES LIKE '%ssl%'`) shows:

```
have_ssl             = YES          version_ssl_library = WolfSSL 5.9.1
have_openssl         = NO           ssl_ca/ssl_cert/ssl_key = (our certs, loaded)
Ssl_accepts          = 13           <- 13 TLS handshakes were started
Ssl_finished_accepts = 0            <- ZERO ever completed
Ssl_cipher / Ssl_version = (empty)  <- nothing ever negotiated
```

TLS is enabled and advertised, the certs are loaded, but the server's wolfSSL
**begins every handshake and finishes none** — for OpenSSL, Schannel, and its own
`mariadb.exe` alike. This is a **server-side wolfSSL 5.9.1 handshake bug in the
MariaDB 12.3.2 Windows build**, not something any shell-side change can fix.

**Consequence:** every client-side avenue is dead, *including a wolfSSL-built
connector* (the original bundling idea) — you cannot fix a server that completes a
handshake with no one, including itself, so that effort buys nothing and is off
the table. The only real fix is server-side: rebuild MariaDB with
`-DWITH_SSL=<openssl>` (or use a Windows build with a working TLS lib). Worth an
upstream report: `Ssl_accepts` climbs while `Ssl_finished_accepts` stays 0 and the
server logs only "Got an error reading communication packets" with no SSL
diagnostic. `ssl=false` stays the MariaDB-on-Windows default.

### 11.6 Server start must use `CREATE_NO_WINDOW`, not `DETACHED_PROCESS`

`_spawn_detached` launches the sandbox on Windows with `CREATE_NO_WINDOW`.
`DETACHED_PROCESS` gives the process **no console at all**, which makes the
console-mode `mariadbd.exe` allocate its own **visible** console window on every
start. `CREATE_NO_WINDOW` runs it under a hidden console it (and its children)
inherit — no window pops up, and the server still outlives the shell.
(`DETACHED_PROCESS | CREATE_NO_WINDOW` does *not* help: `CREATE_NO_WINDOW` is
documented to be ignored when combined with `DETACHED_PROCESS`, so you get the
DETACHED behavior — the window returns.)

### 11.7 Packaging (CPack) misses find_package/vcpkg runtime DLLs

On Windows only the shell's own bundling options run `install()` for a
dependency's DLL — `WITH_CURL_PATH` (curl.cmake, `MYSQL_CHECK_CURL_DLLS`, gated
`IF (WITH_CURL_PATH AND WIN32)`) and `BUNDLED_SSH_DIR` (src/CMakeLists.txt).
When the deps come from `find_package` instead (the vcpkg toolchain case),
`vcpkg z-applocal` copies the DLLs next to the **build** output at build time, so
running from `bld/bin` works, but they never enter the **install/CPack** tree —
so the produced package is missing `libcurl.dll`, `ssh.dll`,
`antlr4-runtime.dll`, zlib (`z.dll`), and the OpenSSL DLLs
(`libcrypto-*/libssl-*`). A `WIN32` block in `src/CMakeLists.txt` (right after the
`BUNDLED_SSH_DIR` block) installs them: libssh via `$<TARGET_FILE:ssh>`, and
curl/zlib/antlr4/OpenSSL located in vcpkg's per-triplet `bin` (derived as the
`../bin` sibling of a resolved import library's dir). Each is guarded so it never
double-handles the option-based paths (`WITH_CURL_PATH`, `BUNDLED_SSH_DIR`,
`BUNDLED_OPENSSL`) and is a no-op when a DLL isn't found (statically linked deps).
Note: this covers the direct deps; if a vcpkg feature pulls extra transitive DLLs
(e.g. brotli/zstd/nghttp2 for some curl builds), extend the same block or switch
to CMake's `install(RUNTIME_DEPENDENCY_SET)` for a full scan.

### 11.8 No `socket` option on Windows

The sandbox `my.cnf` omits `socket` (in both `[mysqld]` and `[client]`) on
Windows. Windows has no Unix domain sockets; there `--socket` only names a named
pipe, and only when the server is started with `--named-pipe` (the sandbox is
not), so the value is inert — the server even reports `socket: ''` for it — and
all connections go over TCP (`127.0.0.1:port`). The old AdminAPI/mysql_gadgets
sandbox always wrote `socket = mysqld.sock` regardless of platform; the port
keeps that on Linux/macOS (where it's the primary local endpoint) but drops it on
Windows rather than writing a meaningless Unix-style path. `_socket_path` is
therefore POSIX/macOS-only now (`_build_option_file`, `_open_root_session`, and
the `delete_sandbox` socket cleanup all guard with `os.name != "nt"`).

### 11.9 vcpkg manifest — pin the *port-version*, not just the version

When building via vcpkg (`-DWITH_VCPKG_TRIPLET=…`, `vcpkg.json`), a version
override like `{ "name": "antlr4", "version": "4.13.2" }` resolves to
**port-version 0** — the version-string alone does *not* pick up later
port-versions that carry vcpkg's own patches. antlr4 `4.13.2#0` misses the
`add-include-chrono.patch` that upstream added in `4.13.2#1`: its
`runtime/Cpp/.../atn/ProfilingATNSimulator.cpp` does `using namespace
std::chrono;` and calls `high_resolution_clock::now()` without ever
`#include <chrono>`. This is a latent bug on every platform, but it only *fails
to compile* on **arm64-windows** — the arm64 MSVC STL doesn't pull `<chrono>` in
transitively the way x64 MSVC / libc++ / libstdc++ do, so the names are
undefined only there (`error C2653: 'high_resolution_clock' is not a class or
namespace name`). Fix: pin the port-version in the override —

```json
{ "name": "antlr4", "version": "4.13.2", "port-version": 1 }
```

Changing the port-version alters the port's ABI hash, so a plain reconfigure
rebuilds just that port (no cache wipe). General rule: when a vcpkg port fails
to build, check `versions/<x>-/<port>.json` in the cloned vcpkg tree for a higher
port-version — it usually already carries the fix, and pinning to it is cheaper
and more maintainable than a local overlay port. This applies to all triplets;
arm64-windows is just the canary.

Related trap: Visual Studio 2022's developer environment sets `VCPKG_ROOT` to its
own **bundled, non-git** vcpkg (`…/VC/vcpkg`). `bootstrap_vcpkg.cmake` honours
`$ENV{VCPKG_ROOT}`, then tries to `git clone` into it and fails
(`destination path already exists and is not an empty directory`). Pass
`-DVCPKG_ROOT=<sibling path>` (matching the macOS default of `../vcpkg`) or clear
the env var (`set VCPKG_ROOT=`) for the configure shell.

### 11.10 Use the Ninja generator on Windows (single-config)

Configure the shell (and therefore the auto-bootstrapped server) with
**`-G Ninja`** on Windows. The shell links the server's static libraries by
**explicit path at configure time** (`FIND_LIBRARY` +
`${MARIADB_BUILD_DIR}/mysys/mysys.lib`, … — see CMakeLists.txt ~L1247/L1262), and
the `bootstrap_mariadb.cmake` fast-path probes the same paths. A **multi-config**
generator (the Windows default, Visual Studio / MSBuild) writes every library
into a per-config subdirectory (`…/libmariadb/libmariadb/RelWithDebInfo/mariadbclient.lib`),
which those lookups don't search — configure dies with `Could not find
libmariadbclient in …/libmariadb/libmariadb`. `CMAKE_BUILD_TYPE` is also empty for
multi-config generators, so the subdir can't be derived. Ninja is single-config:
libraries land directly in the target dir where everything expects them (this is
also why the runtime `CONFIG_BINARY_DIR` handling keys off `CMAKE_CONFIGURATION_TYPES`).
Run from an **arm64 native** VS developer prompt (`vcvarsall.bat arm64`) so `cl`
and `ninja` resolve and target arm64; the generator can't be changed in an
existing build dir, so delete `bld/` when switching.

Configure-time DLL trap on the nested server build: with a **dynamic** vcpkg
triplet (arm64-windows), the server runs freshly-built **host tools** during its
own build — `comp_err` generates `mysqld_error.h`, charset generators run, etc. —
and those tools link vcpkg DLLs (zlib → `zlib1.dll`, OpenSSL → `libcrypto-3-arm64.dll`).
Those DLLs live in the vcpkg per-triplet `bin` dir, which is neither beside the
tool exes nor on `PATH`, so the loader aborts the tool with
`STATUS_DLL_NOT_FOUND` (exit `-1073741515` / `0xC0000135`) before it runs — the
nested build fails at, e.g., `GenError`. `bootstrap_mariadb.cmake` prepends that
bin dir (`<prefix>/bin` for release, `<prefix>/debug/bin` for Debug) to `PATH`
for the nested build via `cmake -E env --modify PATH=path_list_prepend:` (needs
CMake ≥ 3.25).

## 12. Linux platform notes

Verified working: a full `arm64-linux-dynamic` (vcpkg) build with a from-source
bundled Python (`-DWITH_PYTHON_SOURCE=3.14.6`) on Ubuntu (GCC 15, aarch64).
`mariadb-shell`, `mariadb-shell-rec`, and `run_unit_tests` all link;
`mariadb-shell --version`
reports the MariaDB build and the embedded interpreter imports the bundled
`certifi`/`pyyaml`/`antlr4`/`antlr4-python3-runtime`/`mcp` packages.

### 12.1 From-source `--enable-shared` Python must RUNPATH its own lib dir

`bootstrap_python.cmake` builds CPython `--enable-shared`, so the interpreter
loads `libpython<ver>.so` from `<prefix>/lib`. That dir is not on the system
loader path, and distros that ship their own system Python of the same version
(Ubuntu: `/usr/lib/<arch>/libpython3.14.so.1.0`) will win the `ld.so` lookup
unless we say otherwise. The result is a **Frankenstein interpreter**: our
executable + our stdlib on `sys.path`, but the *system* (Debian-patched)
`libpython` driving `site`/`sysconfig`. Its `getsitepackages()` returns the
Debian `dist-packages` scheme while `ensurepip`/pip install into the vanilla
`site-packages`, so the two never meet and `import pip` fails with
**`No module named pip`** even though pip was "successfully installed". `ldd`
on the interpreter is the tell — `libpython3.14.so.1.0 => /usr/lib/...` instead
of `<prefix>/lib`.

Fix: bake `<prefix>/lib` into the interpreter's `RUNPATH` (via
`-Wl,-rpath,<prefix>/lib`, ours first then the vcpkg lib dir). `DT_RUNPATH` is
searched before the ldconfig cache, so it loads its own `libpython`. NOTE: the
`RUNPATH` is set at *link* time — after changing it you must force a **relink**
(delete the install *and* build trees; `make` alone won't relink cached binaries
just because `LDFLAGS` changed). Verify with `ldd <prefix>/bin/python<ver> | grep
python` (must point into `<prefix>/lib`) and `python -m pip --version`. The
bundled shell inherits this correctly: `ldd bin/mariadb-shell` resolves libpython to
`lib/mariadb-shell/libpython3.14.so.1.0`.

(The `ensurepip` self-heal in the top-level CMakeLists — `import pip`, else
`python -m ensurepip --upgrade` before the bundled-package install — remains as
a belt-and-suspenders for interpreters that simply lack pip; it needs no network
since ensurepip installs from a stdlib-bundled wheel.)

### 12.2 Neutralize the MariaDB server C-API rename (link)

MariaDB's `include/mariadb_capi_rename.h` (pulled in by `include/mysql.h`)
rewrites the client C API with a `server_` prefix (`mysql_get_ssl_cipher` →
`server_mysql_get_ssl_cipher`, plus `mysql_init`/`_real_connect`/`_options`/…)
for the server's *own* internal use. The shell links the Connector/C, which
exports the **unprefixed** symbols, but any shell TU that transitively includes
a server header gets the rename — so a call to a renamed function links against
the missing `server_*` symbol. On Linux this surfaced as `undefined reference to
server_mysql_get_ssl_cipher` from `Session_impl::get_ssl_cipher` (the only
renamed C-API function called *inline* in `session.h`; the rest are out-of-line
in `session.cc`, a TU that pulls the connector header instead). The rename block
is guarded by `#if !defined(EMBEDDED_LIBRARY) && !defined(MYSQL_DYNAMIC_PLUGIN)`,
but neither is appropriate for the shell. Fix (CMakeLists.txt, `MARIADB_BUILD`
branch): pre-define the header's own include guard,
`ADD_DEFINITIONS(-DMARIADB_CAPI_RENAME_INCLUDED)`, so the rename is a no-op for
every shell TU and all C-API calls resolve to the connector's unprefixed
symbols. Cross-platform safe — the shell always links the connector, which is
why macOS/Windows (where that TU happened to get the connector header) never
tripped it.

### 12.3 GCC 15 `-Werror=free-nonheap-object` false positive

GCC 15 raises a false-positive `free-nonheap-object` inside libstdc++
(`new_allocator.h`) when it inlines a `std::vector<std::tuple<...>>` destructor
(seen in `unittest/json_shell_t.cc`), which `-Werror` turns into a hard failure.
`cmake/compiler.cmake` downgrades it to a warning for GCC
(`-Wno-error=free-nonheap-object`, understood by all supported GCC versions),
alongside the existing `-Wno-error=type-limits`. Not MariaDB-specific — any
GCC-15 build hits it.

### 12.4 Bundled Python needs SQLite3 from the vcpkg closure (`_sqlite3`)

CPython builds its `_sqlite3` stdlib module only if `configure` finds `sqlite3.h`
+ `-lsqlite3` at build time. `bootstrap_python.cmake` intentionally points the
Python build **only** at the vcpkg dependency closure (`CPPFLAGS=-I<vcpkg>/include`,
`LDFLAGS=-L<vcpkg>/lib -Wl,-rpath,<vcpkg>/lib`) so extension modules link the same
libraries as the shell rather than the host's. SQLite was originally absent from
that closure, so the interpreter came up without `_sqlite3` and `import sqlite3`
failed with **`ModuleNotFoundError: No module named '_sqlite3'`** (surfaced by
`unittest/scripts/auto/py_shell/scripts/sqlite_support_norecord.py`).

Fix: add `sqlite3` to `vcpkg.json` (pinned to the baseline's `3.53.3#1` per §11.9).
It then lands in `<vcpkg>/include` + `<vcpkg>/lib`, CPython's configure detects it
(via the header/`-lsqlite3` fallback — `PKG_CONFIG_PATH` is not set), and builds
`_sqlite3` with the vcpkg lib dir baked into its RUNPATH like `_ssl`/`zlib`/`_zstd`.
NOTE: the bundled Python is **cached** (`bootstrap_python.cmake` reuses an existing
`Python-<ver>` install), so adding the dep alone won't rebuild it — delete the
cached install (`$PYTHON_INSTALL_ROOT/Python-<ver>`, default alongside the shell
source) so the next configure re-runs vcpkg + the Python bootstrap. Verify with
`lib/mariadb-shell/bin/python<ver> -c "import sqlite3; print(sqlite3.sqlite_version)"`.
Cross-platform: the same manifest entry gives macOS/Windows builds `_sqlite3` too.

### 12.5 GCC 15 `-Werror=array-bounds` false positive (linenoise-ng)

GCC 15 (e.g. Fedora 44) raises a false-positive `array-bounds` in the vendored
`ext/linenoise-ng/src/ConvertUTF.cpp` — `offsetsFromUTF8[extraBytesToRead]`, where
`extraBytesToRead` is bounded to 0..5 (array size 6) but GCC's range analysis
assumes a possible subscript 6 — which `-Werror` turns into a hard failure. Same
treatment as §12.3: `cmake/compiler.cmake` adds `-Wno-error=array-bounds` for GCC
(alongside `-Wno-error=type-limits` / `-Wno-error=free-nonheap-object`) rather than
patching third-party code. Not MariaDB-specific — any GCC-15 build hits it.

### 12.6 Fedora build prerequisites — Perl modules for the vcpkg OpenSSL build

On a fresh Fedora host (44) the vcpkg OpenSSL port builds from source and runs
OpenSSL's Perl `Configure`, which needs several standard Perl modules that Fedora's
minimal Perl does not ship: the build aborts with `Can't locate IPC/Cmd.pm`
(and `FindBin`, `File::Compare`, `File::Copy`, `Pod::Html` are missing next).
Install the full standard set with `sudo dnf install -y perl-core` (Ubuntu's
`perl-modules-*` already covers this). The "openssl requires Linux kernel headers"
message is a non-fatal notice as long as `/usr/include/linux` is present
(`kernel-headers`, installed by default).

### 12.7 The credential store: native `.mylogin.cnf`, and `secret-service` notes

#### `login-path` is the Linux default and reads/writes `.mylogin.cnf` in-tree

The `login-path` helper is built for both vendors, on every platform except
Windows (upstream's own `if (WIN32)` in
[mysql-secret-store/CMakeLists.txt](mysql-secret-store/CMakeLists.txt)). It is
therefore the default credential helper on Linux, exactly as upstream, and
`get_default_helper_name()` carries **no** MariaDB guard. macOS still defaults to
`keychain` (that function tests `__APPLE__` first) with `login-path` merely
available. The helper needs no external binary and no daemon: only OpenSSL, which
the shell already links.

The difference from upstream is *how* the login file is accessed. Upstream drives
`mysql_config_editor` to write it and `my_load_defaults()` to read it; neither
works here. MariaDB ships no `mysql_config_editor`, and libmariadb has no
`.mylogin.cnf` support at all — `grep -rniE
"mylogin|login_file|LOGIN_KEY_LEN|MAX_CIPHER_STORE" mysys/ include/ libmariadb/`
in the server tree returns nothing — so `my_load_defaults()` compiles but hands
back an argv with no password, and `load()` throws `"Failed to read the secret"`.
Both halves are consequently implemented in
`mysql-secret-store/login-path/login_file.{h,cc}` (new files, so they cannot
conflict on merge). `config_editor_invoker.{h,cc}` are untouched and still back
the MySQL build.

`Login_file` exposes **exactly** the method signatures of `Config_editor_invoker`
(`validate`/`list`/`store`/`erase`/`erase_port`/`erase_socket`/`version`, plus a
`get_secret()` that the MySQL build has no use for), so every `m_invoker.foo()`
call site is unchanged and the drift in upstream files is five guards: two in
`login_path_helper.h` (which header to include, which class backs `m_invoker`)
and three in `login_path_helper.cc` (the
`<my_alloc.h>`/`<my_default.h>`/`<mysql.h>` includes, the `store()` preamble, the
`load()` body).

The include guard is not cosmetic. `my_default.h` needs `my_global.h` ahead of it
under libmariadb, so those three headers do not compile here at all; only `load()`
ever needed them, so they are excluded outright. (§10 previously listed this file
under "header include order" — that entry described an ordering which does not in
fact work, and had never been exercised because the helper was not built.)

`list()` returns the option-file *text*, masked and stripped exactly as
`mysql_config_editor print --all` piped through `str_strip()` would be, so
upstream's `parse_ini()` needs no change. Serialising to INI only to re-parse it
is silly, and deliberate: it protects the merge path. The strip is load-bearing —
`parse_ini()` indexes `line.substr(pos + 3)` with `pos == npos` on an empty
trailing line.

The two details that silently produce a corrupt-but-plausible file:

* **The 20→16 byte key fold.** `mysql_config_editor` hands a 20-byte key
  (`LOGIN_KEY_LEN`) to a 128-bit cipher; mysys folds it inside
  `my_aes_create_key()` (`mysys/my_aes.cc`) by XOR-ing every key byte into a zeroed
  16-byte buffer, cycling. Calling OpenSSL EVP directly means reproducing that.
* **Key generation.** MySQL's `generate_login_key()` fills all 20 bytes with
  `(char)((int)(my_rnd_ssl(&failed) * 100000) % 32)` — values 0-31 only. We use
  `RAND_bytes` and apply the same `% 32`.

We deliberately do **not** use MariaDB's `my_aes_crypt`: `include/mysql/service_my_crypt.h`
only exposes it through the plugin-service indirection (`my_crypt_service->my_aes_crypt`),
which is server plumbing and the wrong fit for a standalone helper executable.

Beyond format compatibility the implementation is stricter than MySQL's where it
costs nothing: the file is created `0600` (its directory `0700`), updates go to a
temporary file in the same directory and are `rename()`d over the original so an
interrupted operation cannot destroy the store, and an advisory `flock` is held
across the read-modify-write (re-checking after acquisition that the descriptor
still refers to the path, since a competing `rename()` would otherwise leave us
holding a lock on an unlinked inode). A group/other-readable file is read anyway,
as MySQL does — refusing would lock out anybody with an existing `0644` file.
There is no warning for it, because the helper protocol has no channel for one:
`Process_launcher` folds the helper's stderr into the stdout the shell parses, so
anything printed there would corrupt every command's output.

Two upstream workarounds do not apply, both being
`mysql_config_editor` bugs rather than format properties: the
`Version(m_invoker.version()) < Version(8, 0, 24)` quoting sniff, and the
78-character secret cap (the editor reads the password into a fixed 80-byte buffer
and fails to null-terminate past 78). The backslash escaping (`"\\"` → `"\\\\"`)
**stays** — the format still needs it, since a mysys reader expands `\s` to a
space. `validate_secret()`'s control-code blacklist also stays: a newline in a
secret would still break the INI structure.

Not implemented, deliberately: the `--login-path=` command-line option. It is not
standard in the MariaDB ecosystem, and `shellcore/shell_options.cc`'s
`my_load_defaults` path is untouched. The file is only the credential store's
backing file.

**Verified interoperable at the byte level** against `mysql_config_editor` from
both MySQL 9.7.1 and 26.7.0: seeded with the same login key, the two writers
produce byte-identical files for every secret tried (spaces, `#`, embedded double
and single quotes, UTF-8, empty, punctuation), and each reads what the other
writes. The one intentional divergence is a secret longer than 78 characters,
which the editor truncates to 79 and we store in full.

In-tree, the interop coverage is the five `login_path_*` cases in
`unittest/mysql_secret_store_t.cc`, which drive the real binary in both
directions. Two test-side differences from upstream: they `return` early when
`mysql_config_editor` is not on `PATH` (`has_config_editor()`) — avoiding an
external binary is the whole point, so it must not become a test requirement —
and `k_login_path_limits_secret_length` guards the 78-character expectation, as
does the `{has_login_path and not __mariadb_build}` gate in
`unittest/scripts/auto/py_shell/scripts/shell_secrets_norecord.py`.

**Caveat on that in-tree coverage:** the whole `Parametrized_helper_test` family
(`Mysql_secret_store*`, `Shell_secret_api*`, `Helper_executable_test`) skips in a
MariaDB build, because `SetUp()` requires the target server version to be ≥ the
shell version and the shell is versioned 26.x. It was exercised by temporarily
defeating that gate: 119/119 `Helper_executable_test` (including
`valid_id_characters` and `valid_secret_characters` over all 256 byte values),
37/37 `Mysql_secret_store_api_test*`, and 54/54
`Shell_secret_api*`/`Shell_all_api*`/`Mysqlsh_credential_store*` pass for
`login-path`. `unittest/scripts/auto/js_credential/` cannot be run in this build
tree at all: `HAVE_JS` is off, so `auto_script_js_t.cc` is not compiled.

#### `secret-service`: built and selectable, but not the default

`secret-service` is fully buildable and selectable on Linux; it is simply not the
default. A keyring daemon is therefore **not** a runtime requirement for
credential storage, nor a CI requirement: `get_available_helpers()` omits
`secret-service` when `check_requirements()` (a real `secret-tool search`) fails,
and `login-path` is used instead. Keeping the
`dbus-run-session` / `gnome-keyring-daemon --unlock --components=secrets` setup in
CI now buys **`secret-service` test coverage** rather than making the suites pass,
which is why it has been kept rather than dropped. End-user setup instructions:
[docs/CREDENTIAL_STORE.md](docs/CREDENTIAL_STORE.md).

The notes below still apply to `secret-service` whenever it is selected.

##### Store retry (gnome-keyring transient encryption error)

The `secret-service` helper shells out to `secret-tool`, which talks to the D-Bus
Secret Service (gnome-keyring). Newer gnome-keyring (seen with **50.0 / libsecret
0.21.7 / libgcrypt 1.12.2** on Fedora 44) intermittently fails an otherwise valid
store — roughly 0.5% of requests — with `secret-tool: Couldn't create item: The
secret was transferred or encrypted in an invalid way.` (libsecret
`SECRET_ERROR_PROTOCOL`): the per-request D-Bus session's DH-encrypted secret
transfer occasionally fails server-side. It is **not** input-specific (any id can
hit it) and **not** FIPS/crypto-policy related; the older gnome-keyring on the
Ubuntu VM does not exhibit it, so `mysql_secret_store_t`'s `valid_id_characters`
(which does ~360 store/get/erase ops) failed only on Fedora. A fresh invocation
negotiates a new session and succeeds, so `Secret_service_helper::store` now retries
the store on a `Helper_exception` with the same bounded backoff (`{200, 400}` ms) as
`get()`. The store is idempotent (`secret-tool` overwrites the item with matching
attributes), and it still throws after the retries are exhausted. Verified: 780
store ops across the full id range with 0 helper-visible failures after the change.

Related crash uncovered while testing the above: `parse_list` (the parser for
`secret-tool search --all` output) split each line on `=` and unconditionally read
the second field. A secret id containing a newline — which `store()` accepts, since
it is valid UTF-8, and which `valid_id_characters` exercises — makes secret-tool wrap
the value onto a following line with no `=`, so the parser indexed a missing field
(**out-of-bounds → SIGSEGV**). The helper died with no output, surfacing to the shell
as an empty `RuntimeError: Failed to list secrets:` and failing
`Shell_secret_api_test.store_and_check`. It only bites when such an item lingers in
the store (e.g. a transient erase failure on Fedora's flaky keyring leaves one), but
it is a self-inconsistency: the helper can create items it then cannot list. Fix:
`parse_list` now skips continuation lines (`option.size() < 2`) instead of indexing
them. Verified: listing with a newline-id item present returns cleanly and still
reports the other credentials.

### 12.8 Bundled Python extension modules need a relative `DT_RPATH` (not `RUNPATH`)

`bootstrap_python.cmake` links the bundled interpreter and its extension modules
with `-Wl,-rpath,<abs vcpkg lib dir>` so they load the same OpenSSL/zlib/zstd/
sqlite3 as the shell (§12.4). Modern linkers emit that as **`DT_RUNPATH`**, which
`ld.so` searches *after* `LD_LIBRARY_PATH`, and the path is absolute into the
*build* tree. Both properties bite:

* any `LD_LIBRARY_PATH` naming a system lib dir wins over it, and
* it resolves nothing once the build tree is cleaned, moved, or packaged.

Either way the module falls through to the ldconfig cache and loads the *system*
library of the same SONAME. For `_ssl`/`_hashlib` that is the distro's
`libcrypto.so.3`, which does not carry the symbol versions of the newer vcpkg
OpenSSL they were built against, so `import ssl` fails with **`ImportError:
/lib/x86_64-linux-gnu/libcrypto.so.3: version `OPENSSL_3.3.0' not found (required
by .../lib-dynload/_ssl.cpython-314-x86_64-linux-gnu.so)`**. This failed
`Shell_scripted/Auto_script_py.run_and_check/mysqlsh_python_norecord` on the
self-hosted Ubuntu CI runner, at the `sys.executable`/`import ssl` check
(`unittest/scripts/auto/py_shell/scripts/mysqlsh_python_norecord.py`) — a
*subprocess* of the bundled interpreter, so only that module's own search path
applies. Reproduce/confirm the mechanism with
`LD_LIBRARY_PATH=/lib/x86_64-linux-gnu lib/mariadb-shell/bin/python<ver> -c "import ssl"`
(fails) versus the same command without it (silent).

macOS already handled this (strip the build-tree `LC_RPATH`, add
`@loader_path/../../..`); the Linux side did not, and only worked as long as the
build tree stayed intact *and* `LD_LIBRARY_PATH` was unset. Fix:

* `cmake/linux_fix_bundled_rpath.cmake` + `get_force_rpath_command()`
  (`cmake/exeutils.cmake`) run `patchelf --force-rpath --set-rpath
  '$ORIGIN/../../..'` over the copied `lib-dynload/*.so` (and top-level
  `site-packages/*.so`), pointing them at `INSTALL_LIBDIR`, where the same
  libraries are bundled. The relative form is valid in the build tree *and* in the
  package.
* `get_rpath_command()` (used for every individually bundled binary — `libpython`,
  the `python<ver>` launcher, the vcpkg/libssh/krb5 libraries) now also passes
  `--force-rpath`.

`--force-rpath` writes the legacy `DT_RPATH` tag, the only one searched *before*
`LD_LIBRARY_PATH` — the same reason `add_shell_executable()` links the shell's own
executables with `-Wl,--disable-new-dtags`. Bundled copies now beat a system
library of the same SONAME regardless of the environment.

NOTE: the copy is a `make` target keyed on timestamps, so an incremental build will
not re-patch existing copies — delete `<build>/lib/mariadb-shell/lib/python<ver>` (or
rebuild from a clean tree, as CI does) to force the re-copy. Verify with
`readelf -d lib/mariadb-shell/lib/python<ver>/lib-dynload/_ssl*.so | grep -i rpath`
(expect `RPATH` — not `RUNPATH` — with `$ORIGIN/../../..`) and the
`LD_LIBRARY_PATH=/lib/x86_64-linux-gnu` import above, which must now succeed.

---

## 13. MariaDB server behaviour differences

### 13.1 `COLLATE utf8_bin` breaks once `utf8` means utf8mb4

`Query_helper::case_sensitive_compare()` forced a binary collation on
information_schema name columns with `COLLATE utf8_bin`, to compare identifiers
case-sensitively regardless of `lower_case_table_names`. That construct is not
portable: I_S name columns are **utf8mb3**, while the character set `utf8` — and
therefore the collation `utf8_bin` — is an alias for **utf8mb4** unless `old_mode`
contains `UTF8_IS_UTF8MB3`. Where it is absent, every such query fails with

```
ERROR 1253: COLLATION 'utf8mb4_bin' is not valid for CHARACTER SET 'utf8mb3'
```

which failed `Db_tests.query_helper_bug37207914` (8 assertions) on the CI runner.
This is a **server**, not a platform, difference — the shell sends the same SQL
everywhere — and it is not decided by the version string: `UTF8_IS_UTF8MB3` is
"deprecated since 13.1" (`sql/sys_vars.cc`) and `OLD_MODE_DEFAULT_VALUE` is now `0`
(`sql/sql_class.h`), but the flip landed *within* the 13.1 alpha series, so two
builds both reporting `13.1.0` can differ. Probe the build, not the version:

```bash
mariadbd --no-defaults --verbose --help | grep '^old-mode'   # empty => utf8 is utf8mb4
```

Observed: `12.3.2` and one macOS `13.1.0` tarball default to `UTF8_IS_UTF8MB3`
(queries work), a newer Linux `13.1.0` build defaults to empty (queries fail).
A server-side `old_mode=UTF8_IS_UTF8MB3` unblocks it, but the flag is deprecated,
so it only defers the problem.

Fix: compare the binary representation instead of naming a collation —
`STRCMP(CAST(<column> AS BINARY),?)` (`query_helper.cc`, and the same construct in
`dump/dump_options.cc`). It is charset-agnostic, so it works on either
`old_mode` setting and on MySQL; it keeps the comparison byte-exact (case- and
accent-sensitive); and unlike an explicit `utf8mb3_bin` it does not fail with
`1267 Illegal mix of collations` when a *filter value* holds a 4-byte character.
The `NO PAD` semantics of a binary comparison are irrelevant here because
identifiers cannot contain trailing spaces (`1102`). Verified passing against both
a `UTF8_IS_UTF8MB3` server (12.3.2) and an empty-`old_mode` server (13.1.0).

Scope note: `Query_helper`'s only product consumers are dump/load and the Upgrade
Checker, both excluded from MariaDB builds (`HAVE_DUMP_AND_LOAD`,
`HAVE_UPGRADE_CHECKER`), so today the code is exercised only by its own unit test —
but the defect is in shared library code and would resurface the moment either
feature is ported. The 66 SQL-literal expectations in the (MySQL-only)
Upgrade-Checker tests were updated to match the generated SQL.

### 13.2 `sql_mode` is never reported via session state tracking

MariaDB lists `sql_mode` in the default `session_track_system_variables`, but it
never emits an `sql_mode` entry in the OK packet's session-state tracker. Dumping
the tracker in `Session_impl::check_session_track_system_variables` against
MariaDB 13.1 shows the complete set it does emit:

```
autocommit, time_zone, character_set_client, character_set_connection,
character_set_results, redirect_url
```

`SET @@sql_mode = ...` produces no tracker at all. MySQL emits one, and the shell
relied on it.

The consequence is a silently stale cache. `ShellBaseSession::m_tracking_sql_mode`
was set from the variable *list*, so it became true, `is_sql_mode_tracking_enabled()`
returned true, and the shell trusted a tracker that never fires — leaving the cached
`m_sql_mode` stale after `SET @@sql_mode='ANSI_QUOTES'`. That broke the SQL splitter's
ANSI_QUOTES handling and auto-completion: `SELECT "acte` + TAB no longer completed to
`"actest"`, because `provider_sql`'s `update_completion_context` reads
`session->get_sql_mode()`, which re-queries only when the cache is empty.

Fix: in `ShellBaseSession::track_system_variable`
([mysqlshdk/shellcore/base_session.cc](mysqlshdk/shellcore/base_session.cc)), force
`m_tracking_sql_mode = false` when the peer is MariaDB. Note this is a **runtime
vendor check** (`get_core_session()->get_server_vendor() == ServerVendor::MariaDB`),
not a `#ifdef MARIADB_BUILD` — a MySQL-linked build can connect to a MariaDB server,
so the compile-time gate would be wrong. Callers then fall back to the explicit
refresh path: `shell_sql.cc` detects `SET ... SQL_MODE` and calls
`refresh_sql_mode()` (`SELECT @@sql_mode`), keeping splitter and completer in sync.

### 13.3 libmariadb dereferences a NULL `MYSQL*` where libmysqlclient does not

A class of port bug rather than a single defect: C-API accessors that hand `_mysql`
straight to a `mysql_*()` function crash when the session was never connected,
where libmysqlclient returned safely.

Concrete instance: `Session_impl::get_mysql_info()`
([mysqlshdk/libs/db/mysql/session.h](mysqlshdk/libs/db/mysql/session.h)) called
`mysql_info(_mysql)` with no null guard, unlike every adjacent accessor
(`get_ssl_cipher`, `get_server_version`, … all test `_mysql` first). With a mock
session (`testing::Mock_mysql_session`, which never connects, so `_mysql == nullptr`),
`Load_data_worker::execute` reached `mysql_info(nullptr)` and took a SIGSEGV,
crashing `Load_dump_mocked.chunk_scheduling_more_threads` and `_more_tables`
(rc=139). The call site already handled a null return.

The guard is unconditional — no `MARIADB_BUILD` gate — since it simply makes the
accessor consistently null-safe. **When a "this worked on MySQL" segfault appears in
`db/` session code, look first for an unguarded `_mysql` passed to a `mysql_*()`
call.**

### 13.4 `my_load_defaults()` and `--print-defaults`

MariaDB's mysys `my_load_defaults()` (the 5-arg form the shell links, from
`mysys/my_default.c`) diverges from MySQL's for `--print-defaults`:

| | MySQL mysys | MariaDB mysys |
|---|---|---|
| Return code | 0 | **4** |
| Password in output | masked to `--password=*****` | **printed in clear text** |
| After printing | `exit(0)` | returns, does not exit |

In `Shell_options::handle_mycnf_options`
([mysqlshdk/shellcore/shell_options.cc](mysqlshdk/shellcore/shell_options.cc)) the
generic `if (my_load_defaults(...)) throw` read rc 4 as failure. That produced three
faults at once: a bogus "Could not read my.cnf files" error, a leaked argv buffer
(safemalloc "bytes lost" plus a SIGSEGV at exit, because `m_mariadb_defaults_argv`
was only stored after the success check), and an unmasked password on stdout.

Fix: store `m_mariadb_defaults_argv` when rc is 0 **or** 4 so the buffer is always
reclaimed, and intercept `--print-defaults` in the shell — strip it from the argv
handed to `my_load_defaults()` so mysys stays quiet, load the defaults, then print
the list with `--password*` masked and `exit(0)`, matching MySQL.

---

## 14. Product rename: `mysqlsh` → `mariadb-shell`

The shell is renamed from `mysqlsh` to **`mariadb-shell`**. The rename is
**unconditional** — deliberately *not* gated on `MARIADB_BUILD` — so a
MySQL-linked build produces the same names. There is therefore no `#ifdef` to
maintain, and no divergence between the two builds' on-disk layouts.

### 14.1 Single source of truth

Every user-visible name derived from the product name lives in
[mysqlshdk/libs/utils/shell_naming.h](mysqlshdk/libs/utils/shell_naming.h):

| Constant | Value |
|---|---|
| `k_shell_binary_name` | `mariadb-shell` |
| `k_shell_user_config_dir_unix` | `.mariadb-shell` |
| `k_shell_config_dir_win_vendor` / `k_shell_config_dir_win` | `MariaDB` / `mariadb-shell` |
| `k_shell_global_config_dir_unix` | `/etc/mysql/mariadb-shell` |
| `k_shell_log_file_name` | `mariadb-shell.log` |
| `k_shell_sandbox_dir_name` | `sandboxes` (leaf of the default `sandboxDir`, see §14.4) |
| `k_shell_startup_script_name` | `mariadb-shellrc` |
| `k_shell_option_group` | `mariadb-shell` (my.cnf group, syslog ident, `program_name` attribute) |
| `k_shell_install_dir_name` | `mariadb-shell` (leaf of `share/`, `lib/`, `libexec/`, `include/`) |
| `k_shell_env_prefix` / `k_shell_legacy_env_prefix` | `MARIADB_SHELL_` / `MYSQLSH_` |

The CMake half of the same names is `INSTALL_LIBDIR` / `INSTALL_SHAREDIR` /
`INSTALL_LIBEXECDIR` / `INSTALL_INCLUDEDIR` in the top-level
[CMakeLists.txt](CMakeLists.txt) plus [cmake/packaging.cmake](cmake/packaging.cmake).
**These must be changed together with the header** — the C++ resolves installed
data at runtime by joining those same leaf names onto `MARIADB_SHELL_HOME`.

### 14.2 Environment variables — legacy fallback

`MYSQLSH_*` became `MARIADB_SHELL_*`, but the old names are still honoured:
`shcore::getenv_shell()` ([utils_general.cc](mysqlshdk/libs/utils/utils_general.cc))
looks up the `MARIADB_SHELL_`-prefixed name and, only if unset, retries under the
`MYSQLSH_` prefix. Options that declare an `environment_variable` route through
the same helper in
[libs/utils/options.cc](mysqlshdk/libs/utils/options.cc) when the declared name
carries the new prefix.

The my.cnf reader likewise reads `[mariadb-shell]`, `[mysqlsh]` **and** `[client]`
so existing option files keep working.

### 14.3 Compatibility aliases

`bin/mysqlsh` and `bin/msh` are installed alongside the real binary — symlinks on
Unix, `.cmd` shims on Windows (a symlink needs privileges the installer may not
have). See `SHELL_BINARY_ALIASES` in [src/CMakeLists.txt](src/CMakeLists.txt).
The install uses `install(CODE ...)` rather than `install(FILES ...)`: CPack's
DESTDIR staging dereferences symlinks, which would ship two extra full copies of
the ~12 MB binary. `man/mysqlsh.1` and `man/msh.1` are one-line `.so` stubs
pointing at `mariadb-shell.1`.

The `.deb`/`.rpm` declare `Conflicts: mysql-shell` (both packages own
`/usr/bin/mysqlsh` and `man1/mysqlsh.1`) but deliberately **not**
`Provides: mysql-shell` — this build drops AdminAPI, dump/load, X DevAPI and the
Upgrade Checker, so it must not satisfy dependencies on the upstream package.

### 14.4 Sandbox directory

Deployed sandboxes moved out of `~/mysql-sandboxes/` and under the per-user shell
directory, so everything the shell writes to `$HOME` lives in one place:

| Platform | Default `sandboxDir` |
|---|---|
| Unix / macOS | `~/.mariadb-shell/sandboxes` |
| Windows | `%userprofile%\MariaDB\mariadb-shell\sandboxes` |

The value is the default of the `sandboxDir` shell option, built from
`k_shell_user_config_dir_unix` (or the two Windows constants) plus
`k_shell_sandbox_dir_name` in
[shell_options.cc](mysqlshdk/shellcore/shell_options.cc). The sandbox plugin
normally just reads `shell.options["sandboxDir"]`, but
`default_sandbox_base_dir()` in
[sandboxlib.py](python/plugins/sandbox/sandboxlib.py) duplicates the same default
as a fallback for when the option is unavailable (unit tests, plugin loaded
outside the shell) — **the two must be changed together.**

This is not a compatibility-preserving change: sandboxes deployed by an older
build under `~/mysql-sandboxes/<port>` are not migrated and are no longer listed
or found by `mariadbSandbox.*` unless the old path is passed explicitly
(`{sandboxDir: "~/mysql-sandboxes"}`), which still works. Simply moving the
directory does **not** work: each sandbox's `my.cnf`, start script and stop
script carry absolute paths written at deploy time, and `start_sandbox()`
regenerates the scripts only when `mariadbdPath` is given or the script is
missing — never the `my.cnf`. Redeploy, or keep using the old path.

The AdminAPI `dba.*Sandbox*` help text still documents `~/mysql-sandboxes`
([mod_dba.cc](modules/adminapi/mod_dba.cc)), as does
`DEFAULT_SANDBOX_DIR` in
[mysql_gadgets/command/sandbox.py](python/packages/mysql_gadgets/command/sandbox.py);
both are excluded from MariaDB builds by `HAVE_ADMIN_API` and were left untouched
to keep the upstream diff small.

### 14.5 Deliberately not renamed

- The scripting API module **`mysqlsh`** (`from mysqlsh import ...`) and its
  package directory `python/packages/mysqlsh` — renaming it would break every
  existing plugin and user script.
- The C++ `mysqlsh` namespace and the `src/mysqlsh/` source directory.
- Test-harness-only variables: `MYSQLSH_TEST_HOME`, `MYSQLSH_S3_*`,
  `MYSQLSH_AWS_*`.

Note that the Windows installer GUIDs *were* changed
([cmake/packaging.cmake](cmake/packaging.cmake),
[cmake/WIX.template.in](cmake/WIX.template.in)): MariaDB Shell is a separate
product, so `CPACK_WIX_UPGRADE_GUID` and the `UpdatePath` component GUID both
differ from MySQL Shell's. The MSI therefore never sees an installed MySQL Shell
as an upgradeable predecessor, the two can coexist, and uninstalling one does not
strip the other's `PATH` entry or event-log registration.

### 14.6 Test-expectation fallout

Two classes of test breakage are worth knowing about when touching these names:

1. **Word wrapping.** Help text is wrapped to a fixed width, so a longer name
   shifts wrap points in unrelated lines. `~/.mariadb-shell/init.d` and
   `mariadb-shell.log` both re-flowed paragraphs in
   `unittest/scripts/auto/*/validation/shell_help_norecord.*`.
2. **The prompt.** The 256-colour themes split the product name across two
   coloured segments (`" My"` + `"SQL "` → `" Maria"` + `"DB "`), so
   `unittest/shell_prompt_t.cc` asserts the exact escape-sequence string.

Also note `mysqlshrec` → **`mariadb-shell-rec`**: the name appears both in
`prepare_mysqlsh_cmdline` (`unittest/test_utils/mod_testutils.cc`) and as a bare
string in several `unittest/scripts/` test scripts — a mismatch makes the child
shell launch hang rather than fail loudly.
