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

A bare `#ifdef MARIADB_BUILD` / `#ifndef MARIADB_BUILD` now denotes a guard that
is **neither** AdminAPI nor X-protocol — i.e. an intrinsic build difference
(Connector/C client-API gaps, mysys lifecycle, Python macro conflicts, the
binlog port and its native GTID model, version/error-code macros, the
secret-store login-path). See §10 for the full inventory.

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

ninja all      # produces bin/mysqlsh
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

Then relink the shell: `ninja bin/mysqlsh`.

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
build now produces `run_unit_tests` (and `mysqlshrec`) cleanly. The suite has
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
| Header include order | `dump/schema_dumper.cc`, `upgrade_checker/{custom_check,upgrade_check_creators}.cc`, `login-path/login_path_helper.cc` | `my_global.h` / `m_ctype.h` / `my_dbug.h` ordering for libmariadb |
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
`mysqlsh`, `mysqlshrec`, and `run_unit_tests` all link; `mysqlsh --version`
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
bundled shell inherits this correctly: `ldd bin/mysqlsh` resolves libpython to
`lib/mysqlsh/libpython3.14.so.1.0`.

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
`lib/mysqlsh/bin/python<ver> -c "import sqlite3; print(sqlite3.sqlite_version)"`.
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

### 12.7 Secret Service store retry (gnome-keyring transient encryption error)

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
