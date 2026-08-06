# Dump & Load on MariaDB — analysis and porting plan

> Working document for re-enabling `util.dumpInstance()` / `util.loadDump()` (and
> their siblings) in the MariaDB build. Companion to `MARIADB_PORT.md`, which
> records the *finished* port; this file records the *in-progress* analysis of a
> feature that is currently gated out.
>
> **Scope (decided):** vendor → vendor. MySQL→MySQL keeps working exactly as it
> does today; MariaDB→MariaDB must work with full fidelity, including everything
> currently suppressed by the 5.6 version remap. Cross-vendor is out of scope for
> now and should be refused explicitly rather than degraded. All version gating
> becomes vendor-aware — see §6 (decision) and §7 (the refactor it implies).
>
> Scope note: the Upgrade Checker is out of scope everywhere below. It is a
> separate MySQL-only feature (`HAVE_UPGRADE_CHECKER`) and dump only calls into
> it from the `ocimds` path, which is itself MySQL-only (§4.11).
>
> Nothing is **removed** anywhere in this plan. MySQL-only features keep their
> code, options and tests and are gated or predicated off for MariaDB builds, so
> MySQL→MySQL stays fully supported.
>
> Status: **analysis only — no code changes yet.**
> Last updated: 2026-08-06.

---

## 1. Current state

`HAVE_DUMP_AND_LOAD` is set only in the non-MariaDB branch of the top-level
[CMakeLists.txt:110](CMakeLists.txt#L110). When unset:

| Where | Effect |
|---|---|
| [modules/CMakeLists.txt:74](modules/CMakeLists.txt#L74) | ~60 sources under `util/dump`, `util/load`, `util/copy`, `util/binlog`, `util/common/dump` are dropped from `api_module_SOURCES` |
| [mysqlshdk/libs/db/CMakeLists.txt:58](mysqlshdk/libs/db/CMakeLists.txt#L58) | `db/mysql/binary_log.cc` dropped |
| [modules/util/mod_util.h:36](modules/util/mod_util.h#L36), [mod_util.cc:34](modules/util/mod_util.cc#L34) | the `util.dump*` / `util.load*` / `util.copy*` methods are not registered |
| [unittest/CMakeLists.txt:176](unittest/CMakeLists.txt#L176) | dump/load unit tests dropped; `auto_script_py_t.cc:316` and `shell_script_tester.cc:1816` skip the scripted ones |

Note `util/import_table/*` is **not** gated — `util.importTable()` and the whole
`LOAD DATA LOCAL INFILE` machinery already build and run on MariaDB. That is the
data-ingest engine `loadDump` uses, so the hardest part of the load side is
already alive.

`MARIADB_PORT.md` §2 still describes dump/load as "supported"; that line is stale
and should be corrected when this work lands.

---

## 2. The big head start: upstream already supports **dumping from** MariaDB

MySQL Shell ships a supported MariaDB→MySQL migration path, so the dumper already
detects MariaDB and degrades gracefully. This is the single most important fact
for planning.

[modules/util/common/dump/server_info.cc:195](modules/util/common/dump/server_info.cc#L195):

```cpp
if (npos != str_lower(version.number.get_extra()).find("mariadb")) {
  // we don't want the numbering used by MariaDB to interfere with various
  // conditions we have in our code, just fall-back to an old version
  version.number = Version("5.6.0-" + version.number.get_full());
  version.is_5_6      = true;
  version.is_maria_db = true;
}
```

Consequences, all of them helpful:

- Every `is_8_0` gate in the dumper turns off. That automatically disables
  `information_schema.COLUMN_STATISTICS` histograms, `VIEW_TABLE_USAGE`,
  `LIBRARIES`, `BACKUP_ADMIN`, `FLUSH_TABLES`, the 8.0 optimizer hints, etc.
- `binlog()` already special-cases MariaDB to `SHOW MASTER STATUS`
  ([server_info.cc:109](modules/util/common/dump/server_info.cc#L109)).
- `Dumper::throw_if_cannot_dump_users()`
  ([dumper.cc:6658](modules/util/dump/dumper.cc#L6658)) refuses to dump users from
  MariaDB with a dedicated error (`SHERR_DUMP_USERS_MARIA_DB_NOT_SUPPORTED`).
- The row-count estimator already knows MariaDB's `EXPLAIN FORMAT=JSON` shape:
  `/query_block/nested_loop/0/table/rows` ([dumper.cc:2644](modules/util/dump/dumper.cc#L2644)).
- `compatibility.cc` already parses MariaDB's `IDENTIFIED VIA plugin OR plugin`
  account syntax ([compatibility.cc:1995](modules/util/dump/compatibility.cc#L1995)).
- `optimizer_hints()` returns `SQL_NO_CACHE` on the non-8.0 branch
  ([dumper.cc:6707](modules/util/dump/dumper.cc#L6707)) — which is exactly right for
  MariaDB, where `SQL_NO_CACHE` still exists.

**So the first milestone is not "port dump" — it is "compile dump, and verify the
pre-existing MariaDB source path still works."** But that path is a *degraded*
MariaDB-as-old-MySQL path, and the agreed scope (§6) is full MariaDB→MariaDB
fidelity — so the remap has to go. §7 is the foundational work item, not an
optional cleanup.

Three caveats on the version remap:
- It happens in `common::server_version()`, i.e. only in the dump/load metadata
  layer. Code that calls `IInstance::get_version()` sees the *real* MariaDB
  version (11/12/13) and will take the "modern MySQL" branch. §4.2 has a concrete
  bug caused by exactly this.
- Pretending to be 5.6 suppresses everything MariaDB actually has — roles, check
  constraints, sequences, persistent statistics, `BACKUP STAGE`, `RETURNING`,
  and every other post-10.0 feature. Under vendor→vendor scope that is not a
  safe default, it is the main thing standing between us and a correct dump.
- **It is not applied on the load side at all.** See §4.0 — the asymmetry is the
  single most dangerous thing in the current code.

---

## 3. `dumpInstance` — what actually happens

Entry point: `Util::dump_instance` ([mod_util.cc:2357](modules/util/mod_util.cc#L2357))
→ `Dump_instance` ([dump/dump_instance.h](modules/util/dump/dump_instance.h)) →
`Dump_schemas` → `Ddl_dumper` → **`Dumper`**
([dump/dumper.cc](modules/util/dump/dumper.cc), ~7k lines — this is where
everything lives).

`Dump_instance` itself is a 60-line subclass that only overrides the stats label.
All the behaviour is in `Dumper::do_run()`
([dumper.cc:2852](modules/util/dump/dumper.cc#L2852)), whose leading comment block
is the authoritative description of the locking protocol — read it first.

### 3.1 Phase order

| # | Step | Method |
|---|---|---|
| 1 | Validate options, resolve output URL / storage backend | `Dump_instance_options::validate_and_configure` |
| 2 | Open session, fetch server info & sysvars | `open_session` → `fetch_server_information` |
| 3 | Refuse to dump users on MariaDB | `throw_if_cannot_dump_users` |
| 4 | Read the current account's effective privileges | `fetch_user_privileges` |
| 5 | Preflight privilege check | `validate_preflight_privileges` |
| 6 | **Acquire read locks** (FTWRL, or LOCK TABLES fallback) | `acquire_read_locks` |
| 7 | Start `REPEATABLE READ` + `START TRANSACTION WITH CONSISTENT SNAPSHOT` | `start_transaction` |
| 8 | Spawn N worker sessions, each with its own consistent snapshot | `create_worker_sessions` / `create_worker_threads` |
| 9 | Build the instance cache (all metadata) | `initialize_instance_cache` |
| 10 | **Lock instance for backup** (`LOCK INSTANCE FOR BACKUP`) | `lock_instance` |
| 11 | Release read locks | `release_read_locks` |
| 12 | Per-schema/table task planning | `create_schema_tasks`, `create_table_tasks` |
| 13 | Object-level privilege check | `validate_object_privileges` |
| 14 | HeatWave (`ocimds`) compatibility check | `validate_mds` |
| 15 | Data-masking policy check | `validate_data_masking` |
| 16 | Emit DDL (global, schemas, tables, views, routines, events, triggers, users) | `dump_ddl` + `Schema_dumper` |
| 17 | Chunk each table and dump data in parallel | `Dumper::Table_worker` |
| 18 | Optional checksums | `common::Checksums` |
| 19 | Write `@.json` / `@.done.json` / per-schema / per-table metadata | `write_dump_*_metadata` |
| 20 | Consistency re-verification (GTID / binlog position drift) | `validate_dump_consistency` |

### 3.2 Output layout

A dump directory is: `@.json` (dump manifest), `@.done.json`, `@.sql` /
`@.post.sql`, `@.users.sql`, `<schema>.json` + `<schema>.sql`,
`<schema>@<table>.json` + `.sql`, and the data files
`<schema>@<table>@<chunk>.tsv.zst` (+ `.idx`). The manifest fields are enumerated
in `write_dump_started_metadata` ([dumper.cc:5331](modules/util/dump/dumper.cc#L5331)):

```
version dumper origin schemas basenames users defaultCharacterSet tzUtc
bytesPerChunk consistent mdsCompatibility targetVersion compatibilityOptions
partialRevokes serverVersion server source hostname user checksum
gtidExecuted gtidExecutedInconsistent binlogFile binlogPosition
estimatedRowCount estimatedDataSize hasLibraryDdl hasVectorStoreTables
dataMaskingPolicies hasMaskedTableDdl maybeHasMaskedTableData vectorStore
```

Several of these are MySQL-shaped (`gtidExecuted`, `partialRevokes`,
`hasLibraryDdl`, `vectorStore`, data masking). See §6 for format policy.

---

## 4. MySQL-specific dependencies, step by step

Each item: **what the code does → why it is MySQL-specific → MariaDB
alternative**. Ordered roughly by how blocking it is.

### 4.0 The load side has no MariaDB detection at all — **most dangerous item**

The dump side and the load side obtain the server version by two completely
different routes:

| | dump (source) | load (target) |
|---|---|---|
| call | `common::server_version(session)` | `Version(query("SELECT @@version"))` |
| site | [server_info.cc:182](modules/util/common/dump/server_info.cc#L182) | [load_dump_options.cc:181](modules/util/load/load_dump_options.cc#L181) |
| MariaDB detected? | **yes** → remapped to 5.6 | **no** |
| result on MariaDB | every 8.0 feature gate turns **off** | every 8.0 feature gate turns **on** |

`Load_dump_options::on_set_session` parses `@@version` into a bare
`mysqlshdk::utils::Version` and never consults `common::server_version()`. A
MariaDB 11/12/13 target therefore satisfies *every* `target_server_version() >=
Version(8, 0, x)` test in the loader. Concretely, against a MariaDB target the
loader will currently believe it can use:

- `@@SESSION.sql_generate_invisible_primary_key` ([dump_loader.cc:2226](modules/util/load/dump_loader.cc#L2226), gated by `supports_gipks`)
- `@@SESSION.restrict_fk_on_non_standard_key` (`>= 8.4.0`, [dump_loader.cc:2234](modules/util/load/dump_loader.cc#L2234))
- `BULK LOAD` (`>= 8.0.27` / `>= 8.0.16`, [load_dump_options.cc:241](modules/util/load/load_dump_options.cc#L241))
- library DDL, PKE-as-PK, dynamic data masking ([load_dump_options.cc:268-274](modules/util/load/load_dump_options.cc#L268))
- `sql_require_primary_key` handling — which is also the null-deref of §4.10

`Version::is_mds()` returns true for any version string ending in `cloud`, which
is vendor-blind as well.

The dump side fails safe (MariaDB looks ancient, so nothing MySQL-only is
attempted); the load side fails *unsafe* (MariaDB looks like a very new MySQL, so
everything is attempted). Any work on the load path must start here. This is also
the clearest argument for §7: the fix is not to add a second remap on the load
side, it is to make both sides carry a vendor and gate on `(vendor, version)`.

### 4.1 Consistency: `LOCK INSTANCE FOR BACKUP` / `BACKUP_ADMIN` — **must replace**

`Dumper::lock_instance()` ([dumper.cc:3488](modules/util/dump/dumper.cc#L3488)) runs
`LOCK INSTANCE FOR BACKUP`, gated on the `BACKUP_ADMIN` privilege
(`fetch_user_privileges`, [dumper.cc:3131](modules/util/dump/dumper.cc#L3131)). Both
are MySQL 8.0 constructs; `m_user_has_backup_admin` is only ever set when
`version >= 8.0`, so on MariaDB (remapped to 5.6) the dumper always falls into
the degraded path: FTWRL if available, else `LOCK TABLES` on every dumped table
plus the `mysql` grant tables, else a hard error for `dumpInstance`.

That degraded path *works*, but it is exactly the "block the whole server" or
"can't guarantee consistency" behaviour MariaDB has a better answer for.

**MariaDB alternative — `BACKUP STAGE`** (10.4+), verified in the server tree:

- Stages, in order: `START`, `FLUSH`, `BLOCK_DDL`, `BLOCK_COMMIT`, `END`
  (`sql/backup.cc:45` in the MariaDB server tree).
- Requires **`RELOAD`** (`check_global_access(thd, RELOAD_ACL)`,
  `sql/sql_parse.cc:5026`) — not a
  new privilege, so no `BACKUP_ADMIN` equivalent is needed.
- `BACKUP STAGE BLOCK_DDL` is the direct analogue of `LOCK INSTANCE FOR BACKUP`:
  it blocks DDL while leaving DML running.
- `BACKUP LOCK <table>` / `BACKUP UNLOCK` (also 10.4+) is the per-table form;
  requires `RELOAD` **or** `LOCK TABLES` on that table.

Proposed mapping:

| MySQL step | MariaDB step |
|---|---|
| `FLUSH TABLES WITH READ LOCK` | `BACKUP STAGE START` + `BACKUP STAGE BLOCK_COMMIT` |
| `LOCK INSTANCE FOR BACKUP` | `BACKUP STAGE BLOCK_DDL` |
| `UNLOCK TABLES` | `BACKUP STAGE END` |
| `BACKUP_ADMIN` privilege probe | `RELOAD` privilege probe |

This is the single most valuable substitution in the whole port: it removes the
`dumpInstance`-is-fatal-without-FTWRL error and gives a genuinely better
consistency story than the MySQL path.

**Caveat to design around:** `BACKUP STAGE` is *connection-global and
non-nestable* — one backup stage at a time per server, and the session that
started it must end it. The current code holds locks in a set of side sessions
(`m_lock_sessions`); that model needs rethinking, not just a statement swap.

### 4.2 Privileges & roles — **must fix (latent bug)**

- `validate_preflight_privileges` ([dumper.cc:6162](modules/util/dump/dumper.cc#L6162))
  branches on `is_5_6` → demands `SUPER`. On MariaDB (remapped to 5.6) this is
  wrong: MariaDB 10.5+ split `SUPER` into granular privileges. It only fires when
  `dump_users()` is on, which currently throws on MariaDB anyway — but it becomes
  live the moment user dumping is implemented.
- `validate_object_privileges` ([dumper.cc:6190](modules/util/dump/dumper.cc#L6190))
  requires explicit `SELECT` on `!is_8_0` — correct and harmless for MariaDB.
- **Roles are silently dropped.** `User_privileges::read_user_roles`
  ([mysqlshdk/libs/mysql/user_privileges.cc:548](mysqlshdk/libs/mysql/user_privileges.cc#L548))
  reads `activate_all_roles_on_login`; that sysvar does not exist in MariaDB, so
  the function returns early with "Roles are not supported in this instance" and
  **no role-granted privilege is ever counted**. A MariaDB user whose `SELECT` /
  `RELOAD` comes via a role will be told they lack privileges. Note this path
  uses the *real* server version (`IInstance::get_version()` → 11/12/13), so the
  5.6 remap does not shield it.
  MariaDB alternative: `information_schema.APPLICABLE_ROLES` (present since
  10.0.5, incl. an `IS_DEFAULT` column) and `mysql.roles_mapping`. `SET ROLE` /
  default role comes from `mysql.user.default_role`, not `mysql.default_roles`.
- Privilege-name mapping for the consistency checks:
  `REPLICATION CLIENT` → **`BINLOG MONITOR`** (and `SLAVE MONITOR`),
  `SUPER`-implied checks → `BINLOG ADMIN` / `READ_ONLY ADMIN` / `FEDERATED ADMIN`.
  Confirmed present in `sql/sql_acl.cc`.

### 4.3 `mysql` system-table lock list — **must fix**

`lock_all_tables()` ([dumper.cc:3247](modules/util/dump/dumper.cc#L3247)) locks a
hardcoded MySQL list:

```
columns_priv, db, default_roles, func, global_grants,
proc, procs_priv, proxies_priv, role_edges, tables_priv, user
```

MariaDB's `mysql` schema (from `scripts/mariadb_system_tables.sql`) is:

```
column_stats columns_priv db event func general_log global_priv gtid_slave_pos
index_stats innodb_index_stats innodb_table_stats plugin proc procs_priv
proxies_priv roles_mapping servers slave_master_info slave_relay_log_info
slave_worker_info slow_log table_stats tables_priv time_zone* transaction_registry
```

Differences that matter: **`global_priv`** replaces `user` (which is a *view* in
10.4+ — locking a view is not the same thing), **`roles_mapping`** replaces
`role_edges`, and there is no `default_roles` or `global_grants`. Because the
statement is built from `SHOW TABLES IN mysql WHERE Tables_in_mysql IN (...)`,
today it silently locks a *subset* on MariaDB rather than failing — a quiet
correctness hole, not a loud one.

### 4.4 GTID and binlog position — **must adapt (both directions)**

Dump side: `common::gtid_executed()` reads `@@GLOBAL.GTID_EXECUTED`
([server_info.cc:96](modules/util/common/dump/server_info.cc#L96)) — does not
exist in MariaDB. `binlog()` is already MariaDB-aware (`SHOW MASTER STATUS`), but
the GTID string is not.

MariaDB equivalents (confirmed in `sql/sys_vars.cc`): `@@gtid_binlog_pos`,
`@@gtid_current_pos`, `@@gtid_slave_pos`, `@@gtid_binlog_state`. The format is
domain-based `d-s-seq`, **not** `uuid:n-m`, and there is no `gtid_mode`,
`GTID_PURGED`, `GTID_SUBSET()` or `GTID_SUBTRACT()`.

Load side is where this bites hardest — `Dump_loader::on_dump_end`
([dump_loader.cc:2396](modules/util/load/dump_loader.cc#L2396)) and
`check_server_version` ([dump_loader.cc:3486](modules/util/load/dump_loader.cc#L3486))
run `SET GLOBAL GTID_PURGED=?` / `CALL sys.set_gtid_purged(?)` and validate with
`GTID_SUBSET` / `GTID_SUBTRACT`. MariaDB's equivalent is
`SET GLOBAL gtid_slave_pos = '...'` (server must be a stopped replica), and there
is no set-algebra function pair — subset/difference checks have to be done
client-side over parsed `d-s-seq` triples.

**Precedent exists in this repo:** `MARIADB_PORT.md` §4 describes the binlog
utility's native-GTID rewrite for `util.dumpBinlogs`/`loadBinlogs`. Reuse that
model rather than inventing a second one.

Also note `Dumper::lock_instance()`'s fallback logic keys on `gtid_mode` being
`ON`/`ON_PERMISSIVE` to decide whether a dump can be called consistent. With
`BACKUP STAGE` (§4.1) available, that whole branch should become unreachable on
MariaDB — which is the cleanest possible fix.

### 4.5 Instance cache / `information_schema` — **mostly free, some gaps**

`Instance_cache_builder` ([dump/instance_cache.cc](modules/util/dump/instance_cache.cc))
is the metadata engine. Confirmed against MariaDB's `sql/sql_show.cc`, its
`information_schema` exposes: `SCHEMATA, TABLES, COLUMNS, STATISTICS, VIEWS,
ROUTINES, PARAMETERS, TRIGGERS, EVENTS, PARTITIONS, USER_PRIVILEGES,
TABLESPACES, CHECK_CONSTRAINTS, SEQUENCES`.

| Cache step | MariaDB status |
|---|---|
| `schemata` / `tables` / `columns` / `statistics` / `partitions` / `triggers` / `events` / `routines` / `parameters` | present — should work as-is |
| `fetch_view_metadata` → `VIEW_TABLE_USAGE` | **absent**. Already gated on `>= 8.0.13`, so the fallback (parse `VIEW_DEFINITION` with `mysqlshdk::parser::Extract_table_references`) is taken. Must verify that parser handles MariaDB view SQL. |
| `fetch_table_histograms` → `COLUMN_STATISTICS` | **absent**. Gated on `is_8_0`, so skipped today. MariaDB's engine-independent stats live in `mysql.column_stats` / `index_stats` / `table_stats` + `ANALYZE TABLE ... PERSISTENT FOR`. Under full-fidelity scope this becomes a real gap (`supports_histograms()` per §7.3), not an optional extra — though statistics are regenerable, so it ranks below sequences. |
| `libraries` / `routine_libraries` | **absent** (MySQL 9 JS libraries). Already gated on `supports_library_ddl()` — becomes vendor-aware in §7.3, nothing to remove. |
| `fetch_users` → `mysql.user`, fallback `I_S.USER_PRIVILEGES` | `mysql.user` exists as a view; `activate_all_roles_on_login` probe at [instance_cache.cc:1287](modules/util/dump/instance_cache.cc#L1287) will fail. See §4.2. |
| `fetch_ndbinfo` (`SHOW VARIABLES LIKE 'ndbinfo_version'`) | harmless no-op on MariaDB |
| `replication_topology` (`mysql_innodb_cluster_metadata`, `group_replication_*`) | InnoDB Cluster / Group Replication — always empty on MariaDB. Harmless, but the fields should be dropped from the manifest rather than written empty. |

**Missing object types MariaDB has and the dumper does not know about** — all
**in scope** (decided), since under vendor→vendor fidelity a MariaDB dump that
silently omits objects is data loss, not a limitation: `SEQUENCES` (10.3+),
`CHECK_CONSTRAINTS` as first-class I_S rows, and Oracle-mode `PACKAGE` /
`PACKAGE BODY` routines. These are net-new work, not substitutions.

#### 4.5.1 Sequences — the recipe MariaDB's own `mysqldump` uses

Worth following exactly, because the reference implementation is right there in
`client/mysqldump.cc` and it settles the awkward questions (ordering, state,
exclusion from the table path).

**The bug today:** sequences are silently dropped. `I_S.TABLES` reports them with
`TABLE_TYPE='SEQUENCE'`, and the table loop
([instance_cache.cc:455](modules/util/dump/instance_cache.cc#L455)) only routes
`BASE TABLE` and `VIEW` — anything else falls through to the `views` map or is
ignored. No warning, no error; the sequence simply is not in the dump.

**What `mysqldump` does** (`get_sequence_structure`,
`client/mysqldump.cc:3155-3198`), gated on server ≥ 10.3.0
(`FIRST_SEQUENCE_VERSION 100300`):

1. **Dump sequences before tables** — they are emitted in the DDL pass, ahead of
   the table loop (`mysqldump.cc:5977`), so a table with a `DEFAULT NEXT VALUE
   FOR seq` resolves.
2. `SHOW CREATE SEQUENCE <s>` → the DDL (with `DROP SEQUENCE IF EXISTS <s>;`
   first when dropping).
3. `SELECT next_not_cached_value FROM <s>` → the current state. Note this reads
   the sequence *as a table*; `I_S.SEQUENCES` gives the static definition
   (`START_VALUE`, `MINIMUM_VALUE`, `MAXIMUM_VALUE`, `INCREMENT`, `CYCLE_OPTION`
   — `sql/sql_show.cc:10370`) but **not** the current position.
4. Emit `DO SETVAL(<s>, <value>, 0);` to restore it. The third argument
   `is_used=0` means the next `NEXT VALUE FOR` returns exactly `<value>`.
5. **Exclude sequences from the normal table dump path** — `mysqldump` sets
   `IGNORE_SEQUENCE_TABLE` for them (`mysqldump.cc:6977`). They take no `INSERT`,
   no chunking and no locks.

Mapping onto the shell: sequences want their own `Instance_cache::Schema` map
(alongside `tables` / `views` / `events` / `routines`), their own filter options
(`excludeSequences` / `includeSequences`, for symmetry with the other object
types), a `Schema_dumper::dump_sequences`, a `sequences` count in the per-schema
metadata, and a `supports_sequences()` predicate per §7.3. Because they carry
state but no bulk data, they belong in the DDL pass — not the chunked-data
machinery — which keeps them well clear of §4.7.

### 4.6 DDL generation (`Schema_dumper`) — **partly done already**

[dump/schema_dumper.cc](modules/util/dump/schema_dumper.cc) (3.7k lines) is the
`mysqldump`-derived DDL writer. Per the `mariadb-schema-dumper-tests` memory, this
file's unit tests were already ported and pass against MariaDB 13.1 — so the core
`SHOW CREATE {TABLE,VIEW,EVENT,TRIGGER,FUNCTION,PROCEDURE}` path is known-good.
Known output differences already catalogued there: int display widths, collation
names (`utf8mb4_uca1400_*`), `sql_mode` strings, trigger DDL preservation,
`CREATE DATABASE` encryption clause, view parenthesisation.

Remaining MySQL-specific pieces:
- `SHOW CREATE LIBRARY` ([schema_dumper.cc:1333](modules/util/dump/schema_dumper.cc#L1333)) — unreachable once `supports_library_ddl()` is vendor-aware; leave the code in place.
- `ANALYZE TABLE ... UPDATE HISTOGRAM` emission ([schema_dumper.cc:1962](modules/util/dump/schema_dumper.cc#L1962)) — remap to `ANALYZE TABLE ... PERSISTENT FOR ALL` for MariaDB (see §4.5).
- `SET @@GLOBAL.GTID_PURGED` epilogue ([schema_dumper.cc:2375](modules/util/dump/schema_dumper.cc#L2375)) — see §4.4.
- Account dumping (`dump_grants`, [schema_dumper.cc:2840](modules/util/dump/schema_dumper.cc#L2840)): `SHOW CREATE USER` + `SHOW GRANTS` + `SELECT plugin FROM mysql.user` + `SET DEFAULT ROLE`. MariaDB has `SHOW CREATE USER` (10.2+) but roles are not emitted by it, auth plugins differ (`mysql_native_password` is still first-class; `ed25519`, `unix_socket`, `gssapi` have no MySQL analogue), and `IDENTIFIED VIA x OR y` multi-auth has no MySQL form. This is why `throw_if_cannot_dump_users()` exists; it is the largest single chunk of net-new DDL work.
- The `/*!NNNNN ... */` version-comment prologue/epilogue: MariaDB honours `/*!` with MySQL version numbers, so these mostly work, but anything above `50700` is silently skipped by MariaDB. Anything MariaDB-only must be written as `/*M!NNNNNN ... */`. There is precedent for this pattern in the port already — see the `mariadb-sql-fixture-overrides` note on `/*M! ... */` fixtures.

### 4.7 Data chunking & extraction — **should work unchanged**

`Dumper::Table_worker` chunks by primary key / unique index using
`SELECT ... FORCE INDEX (...) ... ORDER BY ... LIMIT n,2` and estimates row
counts from `EXPLAIN FORMAT=JSON`. All standard SQL plus:
- `optimizer_hints()` → `SQL_NO_CACHE` on the non-8.0 branch. Fine (MariaDB kept it).
  The `/*+ SET_VAR(use_secondary_engine=...) */` HeatWave branch is 8.0-only and unreachable.
- The `EXPLAIN` JSON path already has a MariaDB entry (§2). Worth re-verifying
  against 11/12/13, since MariaDB's JSON plan output changed with the optimizer
  trace rework.
- `Dump_writer` / `text_dump_writer` / `dialect_dump_writer` are pure client-side
  formatting — no server dependency.

### 4.8 Checksums — **should work unchanged**

`common::Checksums` builds `SELECT count(*), <agg> FROM ...` using `BIT_XOR` +
`SHA2` variants. Both exist in MariaDB. Verify only that the aggregate produces
identical values across MySQL and MariaDB for the same rows if cross-vendor dump
portability is a goal (it may not be — see §6).

### 4.9 Load: session setup and data ingest — **mostly free**

`Dump_loader::create_session` ([dump_loader.cc:2176](modules/util/load/dump_loader.cc#L2176))
sets `SQL_MODE`, `net_read_timeout`, `wait_timeout`, `sql_log_bin`,
`sql_quote_show_create`, `foreign_key_checks`, `unique_checks`, `NAMES`,
`TIME_ZONE` — all portable. MySQL-only bits:
- `@@SESSION.sql_generate_invisible_primary_key` (8.0.30+) — MariaDB has no
  invisible-PK generation. Turned off by a vendor-aware `supports_gipks()`; the
  `createInvisiblePKs` / `ignore_missing_pks` options stay in the MySQL build.
- `@@SESSION.restrict_fk_on_non_standard_key` (8.4+) — same treatment as above,
  together with `force_non_standard_fks`.

Data ingest itself is `import_table::Load_data_worker` → `LOAD DATA LOCAL INFILE`
with the client-side infile callbacks. **Already built and working on MariaDB**
(§1). Sub-chunking by transaction size is client-side. Nothing to do.

### 4.10 Load: post-processing tasks

| Task | Status |
|---|---|
| `Index_recreation_task` (deferred `ALTER TABLE ... ADD INDEX`) | portable; the "fulltext one at a time" workaround (BUG#34787778) may not be needed but is harmless |
| `Analyze_table_task` | `ANALYZE TABLE` is portable; the `UPDATE HISTOGRAM ON ... WITH n BUCKETS` branch is 8.0-only and already gated by `histograms_supported()` |
| `Checksum_task` | see §4.8 |
| `Bulk_load_task` | MySQL 9 `BULK LOAD` from OCI/S3 object storage. Already version-gated; **off via vendor predicate** (§4.11). |
| `Secondary_load_task` (`ALTER TABLE ... SECONDARY_LOAD`) | HeatWave. **Gated out** on MariaDB (§4.11). |
| `convert_vector_store` / `heatwave_load` | HeatWave / InnoDB `VECTOR`. Already behind `supports_vector_store_conversion()`; **off via vendor predicate** (§4.11). |
| `check_tables_without_primary_key` (`sql_require_primary_key`) | MariaDB has no `sql_require_primary_key`; the `SHOW VARIABLES LIKE` returns no rows and [dump_loader.cc:3726](modules/util/load/dump_loader.cc#L3726) does `->fetch_one()->get_string(1)` on a null row → **segfault**. Same shape as the `partial_revokes` crash already documented in the schema-dumper memory. Must guard. |

### 4.11 HeatWave / MDS — **gated out, not removed**; remote storage — **kept**

**Nothing is deleted.** MySQL→MySQL stays fully supported, so every MySQL-only
feature keeps its code, its options and its tests; it is only made unreachable on
MariaDB builds. This follows the port's existing convention
(`MARIADB_PORT.md` §0): `#ifdef HAVE_<FEATURE>` = MySQL-only code,
`#ifndef HAVE_<FEATURE>` = MariaDB stub, alongside `HAVE_ADMIN_API`,
`HAVE_X_PROTOCOL`, `HAVE_UPGRADE_CHECKER`, `HAVE_DUMP_AND_LOAD`.

The MySQL-only surface here: `ocimds`, `targetVersion`-for-MDS,
`Compatibility_option::*` (all 13 are HeatWave-shaped), `validate_mds`,
`filter_user_script_for_mds`, `lakehouse_*`, `resource_principals_info`,
`set_gtid_purged` via `sys.`, the `Capability` enum, `Bulk_load_task`,
`Secondary_load_task`, `convert_vector_store` / `heatwave_load`, and
`validate_data_masking` + `common::Data_masking` (a MySQL 9.7 *component*).

**Most of it needs no compile-time gate at all.** This is the important part, and
it falls out of §7 for free: these features are *already* runtime version-gated,
so making their predicates vendor-aware turns them off on MariaDB with no
`#ifdef` and no change to the MySQL path:

| Feature | Existing runtime gate |
|---|---|
| JS libraries / `SHOW CREATE LIBRARY` | `supports_library_ddl()` |
| invisible PKs / `createInvisiblePKs` | `supports_gipks()` |
| PKE-as-PK | `supports_pke_as_pk()` |
| dynamic data masking | `supports_dynamic_data_masking()` |
| vector store conversion | `supports_vector_store_conversion()` |
| `SET_ANY_DEFINER` / `strip_definers` note | `supports_set_any_definer_privilege()` |
| load-side histograms | `histograms_supported()` |
| `BULK LOAD` | `>= 8.0.27` / `>= 8.0.16` in `load_dump_options.cc` |
| `restrict_fk_on_non_standard_key` | `>= 8.4.0` |
| `sql_require_primary_key` | `< 8.0.13` (also the §4.10 crash) |

Convert those to vendor-aware predicates (§7.3) and the MariaDB build stops
attempting them, while MySQL behaves exactly as before. No macro, no deletion.

**What may still want a compile-time gate** is the *user-visible option surface* —
`ocimds`, the `compatibility` list, `bulkLoad*`, lakehouse options — so MariaDB
users are not offered options that can never apply. One new macro in the existing
style (`HAVE_HEATWAVE`, set beside `HAVE_DUMP_AND_LOAD`) would cover it. Treat
the exact boundary as a phase-0 finding rather than a prediction: most of this
code is client-side SQL/option handling that compiles fine against libmariadb, so
the gate is about *exposure*, not compilability. Gate only what actually needs it.

**Keep: the remote storage backends** (OCI Object Storage, S3, Azure). They cost
nothing, verified rather than assumed:

- `mysqlshdk/libs/{storage,oci,aws,azure,rest}` are built **unconditionally** —
  `ADD_SUBDIRECTORY(libs/storage)` at
  [mysqlshdk/CMakeLists.txt:45](mysqlshdk/CMakeLists.txt#L45), with no
  `HAVE_DUMP_AND_LOAD` guard anywhere in the chain. They are already compiled
  into today's MariaDB build; nothing is being switched on.
- The option surface arrives via `Storage_options`, which `Common_options`
  derives from ([common_options.h:42](modules/util/common/common_options.h#L42)) —
  independent of the HeatWave option classes.
- They are pure client-side transport: no SQL, no server feature, so vendor is
  irrelevant to them.

**One tangle to unpick when gating the rest.** `resource_principals_info` is
OCI-shaped but belongs to the *vector store*, not to storage: its only consumers
are `common/dump/vector_store_info.cc` and `Dumper::setup_vector_store_directory`
([dumper.cc:4586-4604](modules/util/dump/dumper.cc#L4586)). It is gated with the
vector store and takes nothing from the storage layer with it. Conversely
`mysqlshdk::oci::mask_any_par` — used for masking PAR URLs in dumper error
messages ([dumper.cc:2777](modules/util/dump/dumper.cc#L2777)) — belongs to the
storage layer and **stays**. Do not let the shared `oci` namespace suggest these
two travel together.

---

## 5. `loadDump` — phase order

Entry point `Util::load_dump` → `Dump_loader::run()`
([dump_loader.cc:3041](modules/util/load/dump_loader.cc#L3041)).

1. `open_dump` — read `@.json`, validate dump `version` and declared capabilities
2. `check_server_version` — source vs target major-version rules; enable `strip_removed_sql_modes` for 5.7→8.x
3. `check_tables_without_primary_key` (§4.10 — crash risk)
4. `check_existing_objects` / `check_existing_users` / duplicate-object reporting
5. `setup_progress_file` — resumability log
6. `execute_drop_ddl_tasks`
7. `execute_schema_ddl_tasks` → `execute_table_ddl_tasks` (with deferred-index extraction) → `execute_view_ddl_tasks`
8. `read_users_sql` + user script filtering
9. `execute_tasks` — parallel `LOAD DATA LOCAL INFILE` per chunk, with sub-chunking
10. Post-load: index recreation, `ANALYZE TABLE`, checksums, (bulk/secondary load — MySQL-only, §4.11)
11. `on_dump_end` — `GTID_PURGED` update (§4.4), summary

Load is *structurally* simpler to port than dump: it mostly executes SQL text the
dump produced, so most of the vendor-specificity is inherited from the dump
format rather than from the loader. The loader's own MySQL dependencies are
concentrated in steps 1–3 and 10–11.

---

## 6. Scope: vendor → vendor (**decided**)

**MySQL→MySQL keeps working exactly as it does today. MariaDB→MariaDB must work
with full fidelity — including everything currently suppressed by the 5.6
remap. Cross-vendor (MariaDB→MySQL, MySQL→MariaDB) is explicitly not a goal for
now.**

What follows from that:

1. **The 5.6 remap must go** (§7). It is the mechanism that suppresses the
   features MariaDB→MariaDB is required to carry. This stops being an optional
   cleanup and becomes the foundation everything else sits on.
2. **All version gating becomes vendor-aware.** Every predicate answers
   "does *this vendor* at *this version* support X", never "is the version
   ≥ 8.0". Inventory and mechanics in §7.
3. **The dump records its source vendor explicitly**, and the loader refuses a
   cross-vendor load with a clear error rather than trying and failing partway.
   Today the vendor survives the round trip only by accident: `serverVersion` is
   written as `number.get_full()`, which for MariaDB is the remapped
   `5.6.0-11.4.2-MariaDB-…` string, and `dump_info.cc` re-detects the vendor by
   substring-matching `mariadb` in the *extra* part
   ([dump_info.cc:100](modules/util/common/dump/dump_info.cc#L100)). Once the
   remap is removed the string changes shape, so this needs an explicit field —
   e.g. `"vendor": "mariadb" | "mysql"` — not a substring search.
4. **Cross-vendor is refused, not silently degraded.** A MySQL dump loaded into
   MariaDB should fail at `open_dump` / `check_server_version` with a specific
   error, before any DDL runs. Note that dropping the MariaDB→MySQL direction
   upstream supports is a deliberate narrowing — it can be revisited later, and
   the vendor field is what would make it possible.

The `Capability` mechanism ([dump/capability.cc](modules/util/dump/capability.cc))
already exists for "this dump uses a feature your loader may not know". It stays
useful for MariaDB-only *content* (sequences, packages, roles), but it is the
wrong tool for the vendor split itself — vendor is not a capability, it is a
precondition. Keep them separate.

Consequence for the manifest: MySQL-shaped fields (`gtidExecuted`,
`partialRevokes`, `hasLibraryDdl`, `vectorStore`, data masking) should be
*omitted* when dumping from MariaDB rather than written empty, with MariaDB
equivalents added under their own names. That is conditional emission — a MySQL
dump's manifest is unchanged, field for field.

---

## 7. Vendor-aware version gating — the foundational refactor

Per §6 this is now the load-bearing change. Three parts: carry the vendor, stop
remapping, convert every gate.

### 7.1 Carry the vendor everywhere a version is carried

`Server_version` ([server_info.h:64](modules/util/common/dump/server_info.h#L64))
already has `is_maria_db`. Three things need to change around it:

- **Stop rewriting `number` — but only in the MariaDB build.** The
  `Version("5.6.0-" + …)` line at
  [server_info.cc:210](modules/util/common/dump/server_info.cc#L210) must go for
  MariaDB→MariaDB fidelity; keep `is_maria_db = true` and replace
  `is_5_6`/`is_5_7`/`is_8_0` with predicates (§7.3).

  **This one needs a `#ifndef MARIADB_BUILD` guard rather than an outright
  removal**, and it is the one place where "remove the remap" collides with
  "don't disturb the MySQL build". `common::server_version()` is shared code, and
  the remap is what makes *upstream's* MariaDB→MySQL migration path work in the
  MySQL build (§2). Delete it unconditionally and a MySQL-built shell dumping
  from a MariaDB server silently changes behaviour — existing, shipped
  functionality on a path we are not testing. Keep the remap for MySQL builds,
  take the true version for MariaDB builds.
- **Give the load side a `Server_version`, not a bare `Version`.**
  `Load_dump_options::m_target_server_version` is a plain
  `mysqlshdk::utils::Version` populated from `SELECT @@version`
  ([load_dump_options.cc:181](modules/util/load/load_dump_options.cc#L181)) — this
  is the §4.0 hole. It must go through `common::server_version()` so the target
  carries a vendor too. This ripples into ~15 comparison sites in
  `dump_loader.cc` and `load_dump_options.cc`.
- **Record the vendor in the manifest** and re-read it in `dump_info.cc`, per §6.3.

Also audit the places that bypass the metadata layer entirely and read the raw
server version — `IInstance::get_version()`,
`ISession::get_server_version()` ([dump_loader.cc:2102](modules/util/load/dump_loader.cc#L2102)),
`Version::is_mds()`. These see the true MariaDB version today and will keep doing
so; they need the same vendor treatment rather than being left as a second,
inconsistent source of truth.

### 7.2 Full gate inventory

Small enough to enumerate completely. **14 `is_5_6`/`is_5_7`/`is_8_0` sites**
outside the Upgrade Checker:

| File | Lines | What it gates |
|---|---|---|
| `dump/instance_cache.cc` | 867 | `COLUMN_STATISTICS` histograms |
| | 1281 | `activate_all_roles_on_login` probe |
| `dump/dumper.cc` | 3772 | "consider upgrading to 8.0" note (ocimds) |
| | 6167 | preflight `SUPER` requirement (§4.2) |
| | 6177 | preflight `SELECT` on `mysql` |
| | 6208 | explicit `SELECT` requirement for object privileges |
| | 6575 | upgrade-check branch (ocimds) |
| | 6708 | `optimizer_hints()` → `SQL_NO_CACHE` vs 8.0 hints |
| `dump/schema_dumper.cc` | 2871-2872, 2916 | `SHOW GRANTS` / `SHOW CREATE USER` 5.6-vs-later variants |
| `common/dump/checksums.cc` | 270 | checksum algorithm selection |
| `common/dump/server_info.{h,cc}` | 66-68, 211-218 | the definitions themselves |

Plus **~30 raw `Version(x, y, z)` comparisons** against a server version. Dump
side: `instance_cache.cc` 575/605 (`VIEW_TABLE_USAGE` ≥ 8.0.13), 1260 (8.0.17
user fallback); `dumper.cc` 3028-3029 (`SHOW CREATE USER` autocommit 8.0.21-23),
3144/3166 (`BACKUP_ADMIN`), 3337 (`FLUSH_TABLES` 8.0.23), 3562, 6551/6592/6600
(shell-vs-server version policy). Load side: `dump_loader.cc` 1836 (8.0.27),
2102 (8.0.16), 2232 (8.4 non-standard FKs), 3402/3407-3408 (5.7→8.x sql_mode
stripping), 3502, 3544 (8.0.24), 3562, 3700 (8.0.32), 3720 (8.0.13);
`load_dump_options.cc` 241 (8.0.27), 256 (8.0.16), 543 (8.4).

The `k_shell_version`-relative checks (`dumper.cc` 6551/6592/6600 — "server newer
than shell") are a separate policy question, not a vendor gate: MariaDB version
numbers are not comparable to the shell's. See §8.

### 7.3 Convert gates to vendor-aware predicates

**Home: a new `modules/util/common/dump/server_features.h`** (decided). The six
existing `supports_*(const Version &)` functions
([compatibility.h:266-314](modules/util/dump/compatibility.h#L266)) —
`supports_set_any_definer_privilege`, `supports_library_ddl`,
`supports_vector_store_conversion`, `supports_gipks`, `supports_pke_as_pk`,
`supports_dynamic_data_masking` — already have the right shape, but
`compatibility.*` is otherwise entirely HeatWave compatibility rewriting, which
is MySQL-only surface (§4.11). Moving them out avoids a MariaDB-critical header
living inside a MySQL-only one, and gives the new MariaDB predicates somewhere
natural to land. `common/dump/` is right because both the dumper and the loader
need them (§7.1), and it already holds `server_info.h`, which defines the
`Server_version` they take.

Move, don't fork — the MySQL thresholds come across unchanged, so the MySQL build
sees the same answers from a different header:

```cpp
// before, in dump/compatibility.h
bool supports_library_ddl(const mysqlshdk::utils::Version &v);
// after, in common/dump/server_features.h
// vendor is part of the question, not an afterthought
bool supports_library_ddl(const common::Server_version &v);
```

Each converted predicate then answers for both vendors explicitly — MySQL keeps
its current version threshold, MariaDB gets `false` (feature does not exist),
a MariaDB threshold (feature exists since 10.x), or `true`. Predicates the port
needs that do not exist yet:

`supports_backup_stage` (MariaDB ≥ 10.4 · MySQL false) ·
`supports_lock_instance_for_backup` (MySQL ≥ 8.0 · MariaDB false) ·
`supports_roles` (MariaDB ≥ 10.0.5 · MySQL ≥ 8.0) ·
`supports_sequences` (MariaDB ≥ 10.3 · MySQL false) ·
`supports_check_constraints` · `supports_packages` (MariaDB Oracle mode) ·
`supports_histograms` (MySQL ≥ 8.0 · MariaDB via `mysql.column_stats`) ·
`supports_invisible_pk` (MySQL ≥ 8.0.30 · MariaDB false) ·
`supports_gtid` (different model per vendor — see §4.4) ·
`supports_require_primary_key` (MySQL ≥ 8.0.13 · MariaDB false — fixes §4.10)

**Why predicates and not `if (is_maria_db)` at each site:** the sites are not
really asking about the vendor, they are asking about a feature. Spraying vendor
checks makes MariaDB version thresholds (10.3 vs 10.4 vs 10.5) invisible and
guarantees drift. It also keeps the MySQL path textually unchanged at each call
site, which is what makes "MySQL→MySQL keeps working" reviewable.

### 7.4 The reference version: what the shell was *built against* (**decided**)

Several checks compare the active server's version against `k_shell_version` —
"is this server newer than the tool that's reading it". That works for MySQL
because the shell and MySQL Server share a version scale. MariaDB version
numbers are not on that scale at all, so the comparison is meaningless for a
MariaDB source.

**Rule:** the reference version is picked by the *active server's vendor* —
never by the build's vendor.

| Active server | Reference version | Behaviour |
|---|---|---|
| MySQL | `k_shell_version` — the shell's **own** version | unchanged from today |
| MariaDB | **the server version the shell was built against** (e.g. 13.1.0) | new |

The semantics stay identical — "this tool understands servers up to the version
it was built for" — only the yardstick changes per vendor.

Keying on the *active* server's vendor is what makes this total, with no gap for
a build/server vendor mismatch:

- **MySQL active server → `k_shell_version` is always available and always
  meaningful**, whatever source the shell was built from. The shell versions on
  the same `YY.M.R` calendar model MySQL now uses — year, month, release. The
  current `MYSQL_VERSION` in this repo is `26.8.0`, i.e. 2026-08, release 0,
  which is directly comparable to a MySQL server's own calendar version. No
  build-time constant is needed for this branch at all.
- **MariaDB active server → the build-time MariaDB server version.** A MariaDB
  build has this by construction (it was built against MariaDB sources).

The only combination without a reference is a *MySQL-built* shell meeting a
MariaDB server — and there `MARIADB_BUILD` is unset, so none of this code is
compiled in and upstream's existing behaviour (the 5.6 remap, §2) applies
unchanged. That is the MySQL→MySQL build staying exactly as it is, which is the
§6 requirement.

**Most of the plumbing already exists.** `GET_MYSQL_VERSION()` in
[version.cmake](version.cmake) already reads the linked server's version file and
is already vendor-aware: `MYSQL_VERSION` for a MySQL source, `VERSION` for a
MariaDB source. Both use the same `MYSQL_VERSION_MAJOR=` key names, so
`/Users/juanram/dev/server/VERSION` (`13`/`1`/`0`) parses today and CMake already
holds `MYSQL_VERSION = 13.1.0`. `MARIADB_BUILD` is already a compile definition
([CMakeLists.txt:63](CMakeLists.txt#L63)), so the vendor is known at compile time
too.

The one missing piece: `MYSQL_VERSION` is computed at configure time and used
only there ([CMakeLists.txt:1363](CMakeLists.txt#L1363)) — it is never exported to
C++. Add it next to the existing `MYSH_VERSION` define at
[CMakeLists.txt:1540](CMakeLists.txt#L1540):

```cmake
add_definitions(-DMYSH_BUILD_SERVER_VERSION="${MYSQL_VERSION}")
```

then expose a companion to `k_shell_version` in `version.h` and resolve it per
vendor at each comparison site.

**Sites to convert** (dump/load scope; Upgrade Checker excluded):

| Site | What it does |
|---|---|
| [dumper.cc:6534](modules/util/dump/dumper.cc#L6534) | server major > shell major → hard "upgrade the Shell first" error |
| [dumper.cc:6546](modules/util/dump/dumper.cc#L6546), [6551](modules/util/dump/dumper.cc#L6551) | server ≥ shell minor + 1 → "newer than the Shell" warning |
| [dumper.cc:116](modules/util/dump/dumper.cc#L116) | `is_unsupported_historical_dump_source_version` — bounds an 8.0-relative range, so it needs a vendor guard, not just a yardstick swap |
| [dump_options.cc:74](modules/util/dump/dump_options.cc#L74) | `Dump_options::current_version()` — the default `targetVersion` |
| [copy_operation.h:88](modules/util/copy/copy_operation.h#L88) | copy's target-version validation (also reads `@@version` into a bare `Version` — same hole as §4.0) |
| [dumper.cc:6592](modules/util/dump/dumper.cc#L6592), [6600](modules/util/dump/dumper.cc#L6600) | inside `check_for_upgrade_errors` — reached only via `ocimds`, so these disappear with §4.11 rather than being converted |

Load side: `check_server_version`'s consecutive-major-version rule
([dump_loader.cc:3402](modules/util/load/dump_loader.cc#L3402)) compares *source
dump* against *target server*. Under vendor→vendor both are the same vendor by
construction (§6.4 refuses otherwise), so the rule stays meaningful — it just
needs MariaDB-appropriate thresholds rather than the 5.7/8.0/9.0 boundaries
hardcoded today.

### 7.5 Sequencing

Do **not** fold this into the change that re-enables the build. Land
`HAVE_DUMP_AND_LOAD` first with behaviour identical to today (MariaDB still
remapped, still degraded), so there is a known-good intermediate state and a
bisectable boundary. Then do §7.1-7.3 as its own change, verifying MySQL→MySQL is
byte-identical before and after — that is the regression risk, and it is
mechanical enough to check by diffing dumps of the same MySQL instance.

---

## 8. Open questions

**Resolved:**

- Scope is vendor→vendor, manifest a superset carrying an explicit vendor field (§6).
- All version gating becomes vendor-aware (§7); the reference version for "server
  newer than the tool" checks is selected by the *active server's vendor* —
  `k_shell_version` for MySQL, the build-time server version for MariaDB (§7.4).
- **Remote storage backends stay** (§4.11) — they are already built
  unconditionally, so keeping them is genuinely free.
- **`util.copy*` is in scope** — see the note below.
- **Sequences, check constraints and Oracle-mode packages are in scope** (§4.5.1).
  A MariaDB dump that silently omits a sequence is data loss, not a limitation.
- **Nothing is removed.** MySQL-only features are gated or predicated off for
  MariaDB, keeping MySQL→MySQL fully supported (§4.11).
- **The vendor-aware predicates live in a new
  `modules/util/common/dump/server_features.h`** (§7.3).

Still open — does not block any phase:

1. **Galera / wsrep.** A Galera node needs different consistency handling
   (`wsrep_sync_wait`, desync on donor). Out of scope for v1, but the
   `BACKUP STAGE` design should not preclude it — note that `sql/backup.cc`
   already carries `#ifdef WITH_WSREP` handling.

### On `util.copy*` being in scope

Copy is dump+load with the artifacts held in memory — a third front-end over the
same two engines rather than a separate feature, which is what makes including it
cheap. It stays compiled from phase 0. Three things it adds:

- **Two conversion sites of its own, both mandatory in phase 2.**
  [copy_operation.h:88](modules/util/copy/copy_operation.h#L88) is a §7.4
  `k_shell_version` site, and the same function reads the target's `@@version`
  into a bare `Version` — the §4.0 vendor hole, reproduced verbatim outside
  `Load_dump_options`. Whatever fix §4.0 gets must be applied here too; they are
  the same defect in two places.
- **Copy is inherently cross-vendor-capable**, since source and target are two
  live servers rather than a dump on disk. Under §6 it must check *both* ends'
  vendors and refuse a mismatch — the one place where the cross-vendor refusal is
  a runtime check between two sessions rather than a manifest check.
- **It exercises the in-memory dump path**, which is a genuinely different code
  path through `Dump_writer` than the file-backed one. Worth its own smoke test
  in phase 6 rather than assuming dump+load coverage implies it.

---

## 9. Suggested phasing

Revised for the vendor→vendor decision: the gating refactor moves from phase 4 to
phase 2, because §4.0 makes the load path actively unsafe until it lands, and
because the MariaDB-native object work in phase 5 has nowhere to hang its
predicates without it.

| Phase | Goal | Notes |
|---|---|---|
| 0 | **Compile.** Turn on `HAVE_DUMP_AND_LOAD` for MariaDB. Gate — do not remove — whatever HeatWave/MDS surface actually needs it (§4.11); keep remote storage and `util/copy/*`. | Expect Connector/C gaps like `MARIADB_PORT.md` §321. **No** behaviour change on either vendor: MySQL untouched, MariaDB still remapped to 5.6 and degraded. Bisectable boundary. |
| 1 | **Stop the bleeding.** §4.0 load-side vendor detection **and its twin in `copy_operation.h`**, §4.10 `sql_require_primary_key` null deref, §4.3 `mysql` lock list, §4.2 roles. | These are crashes and silent wrong behaviour, independent of the refactor. |
| 2 | **Vendor-aware gating** (§7). Remove the 5.6 remap; export the build-time server version + vendor to C++ (§7.4); move the `supports_*` predicates to `common/dump/server_features.h` and make them vendor-aware (§7.3); convert all 14 `is_*` sites, ~30 `Version` comparisons and the 5 `k_shell_version` sites; add the vendor field to the manifest; refuse cross-vendor loads and cross-vendor copy. | The foundation, and where most of §4.11 turns itself off for free. Verify MySQL→MySQL dumps are byte-identical before/after. |
| 3 | **`BACKUP STAGE`** (§4.1) — now expressible as `supports_backup_stage()`. Removes the consistency-or-error dead end. | Needs a locking-session redesign, not a statement swap. |
| 4 | **GTID** (§4.4), reusing the binlog port's native-GTID model, both dump and load. | |
| 5 | **MariaDB-native objects**: sequences (§4.5.1), check constraints, Oracle-mode packages, users/roles/grants. | Largest chunk. Sequences first — smallest and closes a silent-data-loss gap; users/roles is the long pole and unblocks `users: true`. |
| 6 | **Tests.** Re-enable the gated suites; follow the `schema_dumper_t.cc` recipe (capture real MariaDB output, splice `#ifndef MARIADB_BUILD` into raw-string expectations). Include a `util.copy*` smoke test — the in-memory writer path is not covered by dump+load tests. | Needs a MySQL server *and* a MariaDB server in CI to hold both vendor paths. |

---

## 10. File map

**Dump** (`modules/util/dump/`) — `dumper.cc` (6970, orchestration + workers +
chunking), `schema_dumper.cc` (3693, DDL text), `compatibility.cc` (2351, DDL
rewriting for HeatWave), `instance_cache.cc` (1305, metadata queries),
`compatibility_issue.cc` (1048), `dialect_dump_writer.h` / `text_dump_writer.cc`
(output encoding), `progress_thread.cc`, `capability.cc`, `indexes.cc`,
`decimal.cc`, plus `*_options.cc` per entry point.

**Load** (`modules/util/load/`) — `dump_loader.cc` (6543, orchestration + worker
tasks), `dump_reader.cc` (2768, dump directory parsing), `load_progress_log.h`
(1024, resumability), `load_dump_options.cc` (681),
`convert_vector_store.cc` / `heatwave_load.cc` / `lakehouse_source_option.cc`
(MySQL-only, gated).

**Shared** (`modules/util/common/dump/`) — `server_info.cc` (version/GTID/binlog/
topology probing — the MariaDB detection lives here), `checksums.cc`,
`basenames.cc`, `dump_info.cc`, `dump_version.cc`, `utils.cc`,
`resource_principals_info.cc` / `vector_store_info.cc` (MySQL-only, gated).

**Reused, already built on MariaDB** — `modules/util/import_table/*`
(`LOAD DATA LOCAL INFILE`), `mysqlshdk/libs/storage/*` (local + remote backends),
`mysqlshdk/libs/mysql/user_privileges.cc` (§4.2).

**Also gated by `HAVE_DUMP_AND_LOAD`** — `modules/util/copy/*` (dump+load in
memory), `modules/util/binlog/*` (already ported per `MARIADB_PORT.md` §4 —
worth checking whether it can be un-gated independently of dump/load).
