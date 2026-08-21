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
> Status: **phases 0-4 and 5a-5d done** (§11-§20). Dump/load builds for MariaDB,
> a dumpInstance -> loadDump round-trip works against a live server with no
> `ignoreVersion`, the 5.6 remap is gone from the MariaDB build, all version
> gating is vendor-aware, a consistent dump holds a real backup lock
> (`BACKUP STAGE BLOCK_DDL`) instead of running with DDL wide open, a dump
> carries MariaDB's GTID position and can restore it into a target so a replica
> can be provisioned from a dump, and MariaDB's own object types - sequences,
> CHECK constraint enforcement, Oracle-mode packages, and users, roles and
> grants - all round-trip; the MySQL build is verified unaffected (§11.7, §12.5,
> §13.7, §14.4, §15.6, §16.4, §19.4, §20.5). **Phase 6 (tests) is in progress**
> (§21): the component-level tests are green on both vendors at last - the four
> failures every earlier phase carried are fixed or gated - and five of the twelve
> scripted end-to-end suites pass on MariaDB, the four big `util_dump_*` ones being
> what is left. §4.5's `mysql.column_stats` is the last unported object.
> Last updated: 2026-08-19.

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

Note `util/import_table/*` is **not** gated — the whole `LOAD DATA LOCAL INFILE`
machinery already compiled on MariaDB. That is the data-ingest engine `loadDump`
uses, so the hardest part of the load side was already alive. (Correction found
in phase 0: the *sources* were compiled, but `util.importTable()` itself was
never **exposed** — its `expose()` call sat inside the same
`#ifdef HAVE_DUMP_AND_LOAD` block in `mod_util.cc`, so the method did not exist
on the MariaDB build. Removing the gate restored it.)

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
| 10 | **Lock instance for backup** (`LOCK INSTANCE FOR BACKUP`; `BACKUP STAGE BLOCK_DDL` on MariaDB, §14) | `lock_instance` |
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

### 4.0 The load side has no MariaDB detection at all — **fixed in phases 1 and 2**

> **Phase 1:** both holes are closed. `Load_dump_options` and
> `copy_operation.h` now carry the target's vendor, obtained from
> `ISession::get_server_vendor()` — which is cached off the client-side
> handshake string (`mysql_get_server_info()`), so it costs no round trip and
> needs no `SELECT @@version`.
>
> **Phase 2** replaced the bare `Version` with a `common::Server_version`
> throughout (§13.4), so vendor and version travel together on both sides. The
> analysis below is kept as the record of what the hole was.


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

### 4.1 Consistency: `LOCK INSTANCE FOR BACKUP` / `BACKUP_ADMIN` — **fixed in phase 3**

> **Phase 3** replaced it with `BACKUP STAGE BLOCK_DDL` held in a session of its
> own — see §14. The mapping below turned out to be half right: `BLOCK_DDL` is
> indeed the analogue of `LOCK INSTANCE FOR BACKUP`, but `FTWRL` →
> `START` + `BLOCK_COMMIT` is not implementable, because stages only move
> forward and there is no way back from `BLOCK_COMMIT` to `BLOCK_DDL`. §14.1 has
> the measured stage semantics.

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

### 4.2 Privileges & roles — **fixed in phases 1 and 5d**

- ~~`validate_preflight_privileges` branches on `is_5_6` → demands `SUPER`.~~
  **Done in phase 5d** (§20.3): the `SUPER` check stays MySQL-5.6-only, and the
  `SELECT`-on-`mysql` check it sits beside is now asked for every MariaDB too,
  which is what `SHOW CREATE USER` / `SHOW GRANTS FOR` need there.
- `validate_object_privileges` ([dumper.cc:6190](modules/util/dump/dumper.cc#L6190))
  requires explicit `SELECT` on `!is_8_0` — correct and harmless for MariaDB.
- ~~**Roles are silently dropped.**~~ **Fixed in phase 1** — see §12.2 for
  reading role-granted privileges, §20 for dumping the roles themselves.
- Privilege-name mapping for the consistency checks:
  `REPLICATION CLIENT` → **`BINLOG MONITOR`** — **done in phase 1** (§12.3).
  The `SUPER`-implied checks (`BINLOG ADMIN` / `READ_ONLY ADMIN` /
  `FEDERATED ADMIN`) and `SLAVE MONITOR` were expected to matter for the
  user-dumping work; §20 found they do not — they belong to the MDS
  restricted-grant machinery, which is MySQL-only by §4.11.

### 4.3 `mysql` system-table lock list — **fixed in phase 1** (§12.1)

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

### 4.4 GTID and binlog position — **done in phase 4** (§15)

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
`ON`/`ON_PERMISSIVE` to decide whether a dump can be called consistent. Phase 3
made that branch unreachable for any MariaDB account with `RELOAD` — which is
the same privilege FTWRL needs, so in practice the branch is now only reached by
an account that could not have taken a consistent dump under any design.

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
silently omits objects is data loss, not a limitation: ~~`SEQUENCES` (10.3+)~~
**done, §16**, ~~`CHECK_CONSTRAINTS` as first-class I_S rows~~ **see §17 — this
one was wrong**, and ~~Oracle-mode `PACKAGE` / `PACKAGE BODY` routines~~
**done, §19**.

On packages specifically: they were not omitted either. `I_S.ROUTINES` reports
them, so the routine loop cached them as *functions* — losing one of two objects
of the same name, since packages have their own namespace — and then aborted the
whole dump on `SHOW CREATE FUNCTION`. §19 records what was measured.

On check constraints specifically: they are *not* a missing object type and the
dumper needs no `I_S.CHECK_CONSTRAINTS` at all. `SHOW CREATE TABLE` already
carries them, and §17 verifies the DDL round trips byte-identically. What MariaDB
does need is a load-side session switch, for a reason that has nothing to do with
metadata.

#### 4.5.1 Sequences — the recipe MariaDB's own `mysqldump` uses

Worth following exactly, because the reference implementation is right there in
`client/mysqldump.cc` and it settles the awkward questions (ordering, state,
exclusion from the table path).

**The bug today** — measured, and worse than "silently dropped": a schema
containing a sequence **cannot be dumped at all**. `I_S.TABLES` reports sequences
with `TABLE_TYPE='SEQUENCE'`, and the table loop
([instance_cache.cc:455](modules/util/dump/instance_cache.cc#L455)) routes
`BASE TABLE` to `tables` and *everything else* to `views`. So a sequence is
cached as a view, counted as one (`1 table, 2 out of 0 views` for a schema with
two sequences — the totals query does filter on `TABLE_TYPE`, the routing does
not), and then `dump_view_ddl` looks it up in `I_S.VIEWS`, finds nothing and
throws `unordered_map::at: key not found`, which aborts the dump. Fixed in §16.

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
(alongside `tables` / `views` / `events` / `routines`), a
`Schema_dumper::dump_sequences`, a `sequences` list in the per-schema metadata,
and a `supports_sequences()` predicate per §7.3. Because they carry state but no
bulk data, they belong in the DDL pass — not the chunked-data machinery — which
keeps them well clear of §4.7.

They do **not** want filter options of their own (`excludeSequences` /
`includeSequences`), which is what this section originally proposed: a sequence
shares the *table* namespace — `CREATE TABLE s` fails if sequence `s` exists — so
the table filters are the ones that have to select it, which is also what
`mysqldump` does (`include_table()` is applied to the sequence list at
`mysqldump.cc:5949`). See §16.2.

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
- ~~Account dumping (`dump_grants`)~~ — **done in phase 5d** (§20). The prediction here was wrong in every particular worth noting: `SHOW CREATE USER` does not merely omit a role, it **fails** for one (error 1133); the differing auth plugins and `IDENTIFIED VIA x OR y` round-trip **verbatim** with no rewriting at all; and what actually needed designing was that `SHOW GRANTS FOR` a role is transitive, that a role needs `CREATE ROLE`/`DROP ROLE` and a hostless name, and that the default role arrives as a statement rather than a clause. `throw_if_cannot_dump_users()` survives, narrowed to the MySQL-shaped dump BUG#34049624 wrote it for.
- The `/*!NNNNN ... */` version-comment prologue/epilogue: MariaDB honours `/*!` with MySQL version numbers, so these mostly work, but anything above `50700` is silently skipped by MariaDB. Anything MariaDB-only must be written as `/*M!NNNNNN ... */`. There is precedent for this pattern in the port already — see the `mariadb-sql-fixture-overrides` note on `/*M! ... */` fixtures. **One caveat, found in §19:** the shell's own `SQL_iterator` knows `/*!` and `/*+` but *not* `/*M!`, which it spans as an ordinary comment — so a statement written inside one is invisible to the loader's statement filters. Emit MariaDB-only DDL bare unless a MySQL reader actually has to skip it.

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

**All converted in phase 2** (§13.2); the inventory is kept as the record of
what had to be found. Small enough to enumerate completely. **14 `is_5_6`/`is_5_7`/`is_8_0` sites**
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
~~`supports_sequences` (MariaDB ≥ 10.3 · MySQL false)~~ **added in §16** ·
~~`supports_check_constraints`~~ **added in §17 as
`supports_check_constraint_checks` (MariaDB ≥ 10.2 · MySQL false) — the question
worth asking turned out to be about the session variable, not the object** ·
~~`supports_packages` (MariaDB Oracle mode)~~ **added in §19 as MariaDB ≥ 10.3 ·
MySQL false — the question is whether the server *has* the routine type, not
which `sql_mode` created it** ·
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
  All three are **done** — sequences (§16), check constraints (§17), packages
  (§19).
- **Nothing is removed.** MySQL-only features are gated or predicated off for
  MariaDB, keeping MySQL→MySQL fully supported (§4.11).
- **The vendor-aware predicates live in a new
  `modules/util/common/dump/server_features.h`** (§7.3).

Still open — does not block any phase:

1. **Galera / wsrep.** A Galera node needs different consistency handling
   (`wsrep_sync_wait`, desync on donor). Out of scope for v1, but the
   `BACKUP STAGE` design should not preclude it — note that `sql/backup.cc`
   already carries `#ifdef WITH_WSREP` handling.
2. ~~**`deferTableIndexes` never finishes on MariaDB.**~~ **FIXED (§18)** —
   found while testing §17, diagnosed in §17.5, fixed in §18. It was three
   defects stacked: a MySQL-only status variable read with
   `fetch_one_or_throw()`, a monitoring thread that a single throwing monitor
   ended for good, and an index stage whose completion depended on that thread
   staying alive.

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
| 0 | ~~**Compile.**~~ **DONE** (§11) — `HAVE_DUMP_AND_LOAD` removed entirely; binlog split out to `HAVE_BINLOG_UTILS`; 4 defects fixed (3 crashes); round-trip verified. | MySQL build still needs compiling (§11.6). |
| 1 | ~~**Stop the bleeding.**~~ **DONE** (§12) — §4.0 vendor detection for `copy_operation.h`, §4.3 `mysql` lock list, §4.2 roles and the `REPLICATION CLIENT` privilege name. Vendor detection now goes through the cached `ISession::get_server_vendor()`. | |
| 2 | ~~**Vendor-aware gating**~~ **DONE** (§13) — 5.6 remap gone from the MariaDB build; `common/dump/server_features.h` holds 28 vendor-aware predicates; every `is_*` and `Version` gate converted; manifest carries `vendor`; cross-vendor load, copy and `ocimds` refused. | The foundation, and where most of §4.11 turns itself off for free. |
| 3 | ~~**`BACKUP STAGE`**~~ **DONE** (§14) — `BACKUP STAGE START` + `BLOCK_DDL` in a dedicated session is the backup lock on MariaDB, guarded by `RELOAD`; `FLUSH TABLES WITH READ LOCK` is kept for the snapshot window. | The locking-session redesign was the work, as predicted. |
| 4 | ~~**GTID**~~ **DONE** (§15) — the dump carries `gtid_current_pos`, `updateGtidSet` restores it into `gtid_slave_pos`, and the domain-position set algebra the loader's checks need is in `mysqlshdk/libs/mysql/mariadb_gtid.h`. | There was no binlog-port GTID model to reuse: §11.2 found that port was never written. |
| 5 | **MariaDB-native objects.** Largest chunk, so it is split by object type — each part is independent and ships on its own. | Sequences first, they are the smallest; users/roles/grants is the long pole and unblocks `users: true`. |
| 5a | ~~**Sequences** (§4.5.1)~~ **DONE** (§16) — enumerated and filtered as tables, dumped as DDL with the position restored by `DO SETVAL`, dropped and duplicate-checked on load. | The gap turned out to abort the dump, not merely lose data. |
| 5b | ~~**Check constraints** (§4.5)~~ **DONE** (§17) — the DDL already round-tripped; the load now switches `check_constraint_checks` off, so a table holding rows its own constraints reject can be restored. | Not an object-metadata problem at all: MySQL's non-enforcement is per-constraint DDL, MariaDB's is a session variable, so only the restoring side had a gap. |
| 5c | ~~**Oracle-mode packages** — `PACKAGE` / `PACKAGE BODY` routines (§4.5).~~ **DONE** (§19) — dumped by the routine pass in mysqldump's order, filtered as routines, dropped and duplicate-checked on load. | Same failure as sequences, not the predicted one: a package was cached as a *function*, so the dump aborted on `SHOW CREATE FUNCTION`. |
| 5d | ~~**Users, roles and grants** (§4.2, §4.6)~~ **DONE** (§20) — `users: true` works on a MariaDB-dialect dump; roles get `CREATE ROLE` / `DROP ROLE` of their own, `SHOW GRANTS` output is trimmed to its own grantee and the default role moves to its own block. 52037 stays, narrowed to the MySQL-shaped dump it was written for. | Nothing predicted here was the problem. `SHOW CREATE USER` does not *omit* a role, it **fails** for one; `IDENTIFIED VIA x OR y` round-trips verbatim; and the auth plugins only matter if the target lacks one. What did bite: `SHOW GRANTS FOR` a role is *transitive*. |
| 6 | **Tests — in progress** (§21). The end-to-end suites, deferred on MariaDB until here (§12.6). The component-level tests are green on both vendors, and the `util.copy*` suites this row asked for — the in-memory writer path — are the first ones passing; the four big `util_dump_*` suites are what remains (§21.8). | Needs a MySQL server *and* a MariaDB server in CI to hold both vendor paths. Component-level unit tests are *not* deferred to here; they are tracked per phase. |

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
topology probing — the MariaDB detection lives here), `server_features.cc`
(the vendor-aware feature predicates, §7.3/§13.2), `checksums.cc`,
`basenames.cc`, `dump_info.cc`, `dump_version.cc`, `utils.cc`,
`resource_principals_info.cc` / `vector_store_info.cc` (MySQL-only, gated).

**Reused, already built on MariaDB** — `modules/util/import_table/*`
(`LOAD DATA LOCAL INFILE`), `mysqlshdk/libs/storage/*` (local + remote backends),
`mysqlshdk/libs/mysql/user_privileges.cc` (§4.2),
`mysqlshdk/libs/mysql/mariadb_gtid.cc` (domain GTID positions, §15.2 — the
counterpart of the MySQL-only `gtid_utils.cc`).

**Formerly gated by `HAVE_DUMP_AND_LOAD`** — `modules/util/copy/*` (dump+load in
memory) now builds for both vendors; `modules/util/binlog/*` moved to its own
`HAVE_BINLOG_UTILS` gate and stays MySQL-only (§11.2).

---

## 11. Phase 0 — done

Landed 2026-08-06. Goal was "compile, link, register, no behaviour change".
Achieved, plus a verified round-trip; four real defects were found on the way,
three of them crashes.

### 11.1 `HAVE_DUMP_AND_LOAD` is gone

Dump/load is supported on both vendors, so the macro was a no-op gate and was
**removed** rather than flipped — CMake variable, compile definition, and all
twelve `#ifdef`/`IF()` sites. `MARIADB_PORT.md` §0 and `.continue/rules/` are
updated.

Also un-gated, because dump/load needs them and they are vendor-neutral:
`mysqlshdk/libs/mysql/user_privileges.cc` (was `IF(NOT MARIADB_BUILD)`, though
it already carried a MariaDB-specific fix), and `IInstance::get_user_privileges`
/ `get_current_user_privileges`.

### 11.2 New gate: `HAVE_BINLOG_UTILS` (MySQL-only)

`util.dumpBinlogs()` / `util.loadBinlogs()` shared the old gate but are a
*separate* feature. **`MARIADB_PORT.md` §4 claims the binlog library was ported
to `mariadb_rpl_*`; it was not.** `git log --all -S mariadb_rpl` finds the string
only in `MARIADB_PORT.md` itself — no commit on any branch touches
`modules/util/binlog/*` or `db/mysql/binary_log.cc`, and those files still
`#include <mysql/binlog/event/binlog_event.h>`, `<mysql/gtids/gtids.h>` and
`<sql/rpl_constants.h>`. §4 of that document describes work that was never
committed and should be corrected.

So binlog got its own gate covering `modules/util/binlog/*`,
`db/mysql/binary_log.cc`, `mysqlshdk/libs/mysql/{gtid_utils,binlog_utils}.cc`,
and the two `Dumper` call sites that replay the binlog to verify no DDL ran
during a dump (`check_if_transactions_are_ddl_safe`). On MariaDB that
verification now prints a note and treats the dump as unverified — honest
degradation until §4.4.

One helper had to move: `get_binary_logs_keyword()` lived in the AdminAPI-only
`replication.cc` but is needed by `server_info.cc` on both vendors. It is now in
`mysqlshdk/libs/mysql/utils.{h,cc}`, unchanged.

### 11.3 Defects found and fixed

| # | Symptom | Cause | Fix |
|---|---|---|---|
| 1 | **SIGSEGV** in every dump, during data dump | `is_supported_collation()` lazily initializes the charset table via `get_charset(0,0)` on whichever thread reaches it first — a worker. MariaDB's mysys then stats a charset index file, and on failure writes `my_errno`, a **thread-local that worker threads never initialized**. Same failure class `MARIADB_PORT.md` §6 documents for the main thread. | New `mysqlshdk::utils::Mysys_thread_scope` (`libs/utils/mysys_thread.{h,cc}`), instantiated in `detail::spawn_scoped_thread` — the single choke point for every shell worker thread. No-op on MySQL. Declared free of mysys headers because `scoped_contexts.h` is included very widely. |
| 2 | **SIGSEGV** in every load, after "Checking for pre-existing objects" | `check_tables_without_primary_key()` does `fetch_one()->get_string(1)` on `SHOW VARIABLES LIKE 'sql_require_primary_key'`; the variable does not exist on MariaDB, so `fetch_one()` returns nullptr. **This is exactly the defect predicted in §4.10 before any code was built.** | Null-row check. Vendor-neutral: on MySQL the row exists and behaviour is unchanged. |
| 3 | Load aborts: `Unknown system variable 'server_uuid'` | MySQL-only variable, queried unconditionally to name the progress file. | Runtime vendor branch → `@@server_id` on MariaDB (an integer, so `get_uint`). |
| 4 | Load aborts: `Unknown system variable 'innodb_parallel_read_threads'` | §4.0 exactly: `m_target_server_version >= Version(8, 0, 27)` is **true** for MariaDB 12.3, so the loader probes MySQL-only variables. | Down-payment on §7.1: `Load_dump_options` now carries `m_target_is_maria_db`, detected at run time via `common::server_version()`. Used to suppress the 8.0.27 / 8.0.16 probes and the MLE / library-DDL / PKE-as-PK / data-masking feature flags. |

Also gated, as MySQL-only rather than removed: the `ER_BULK_EXECUTOR_ERROR` /
`ER_BULK_LOAD_RESOURCE` retry path (codes absent from libmariadb; BULK LOAD never
runs on MariaDB anyway), and `Dumper::check_for_upgrade_errors()` (reachable only
from `ocimds`).

`schema_dumper.cc` needed the `MARIADB_PORT.md` §322 include-order treatment
(`mysql.h` → `my_global.h` → `m_ctype.h`, with the Clang anonymous-struct
diagnostics suppressed) and a pair of accessors, because `CHARSET_INFO` spells
the names differently per vendor: MariaDB `LEX_CSTRING cs_name`/`coll_name`,
MySQL `const char *csname`/`m_coll_name`.

### 11.4 Verified

Against a live MariaDB 12.3.2 (sandbox on 3312):

- `util` now exposes `dump_instance`, `dump_schemas`, `dump_tables`,
  `export_table`, `load_dump`, `copy_instance`, `copy_schemas`, `copy_tables`,
  `import_table`. `dump_binlogs` / `load_binlogs` and `import_json` are correctly
  absent.
- `dumpInstance` → `loadDump` round-trip of 2 schemas / 2 tables / 1 view /
  5 rows, with data, view definition and `DECIMAL` values all verified after
  reload.
- `throw_if_cannot_dump_users` fires as designed (`users: false` needed).

### 11.5 Live confirmation of the §7.4 asymmetry

The best evidence yet for the phase-2 refactor, from a real load:

```
Target is MySQL 12.3.2-MariaDB-debug. Dump was produced from MySQL 5.6.0-12.3.2-MariaDB-debug
ERROR: Destination MySQL version is newer ... non-consecutive major MySQL versions
```

The *same server*, dumped and reloaded, is rejected as a non-consecutive major
version jump — because the dump side applies the 5.6 remap and the load side does
not. `ignoreVersion: true` was required for any MariaDB→MariaDB load.
**Phase 2 removed this** — see §13.7.

### 11.6 Known-unfixed / follow-ups

- ~~The MySQL build was not compiled.~~ **Verified** — see §11.7.
- `Error in my_thread_global_end(): 1 threads didn't exit` at exit. **Pre-existing** —
  it also appears on `--version`, which spawns no workers, and the count stays 1
  during a 4-thread dump, so the guard in 11.3(1) is pairing correctly. It is the
  main thread's `my_thread_end()` accounting.
- `__have_dump_and_load` is still defined for scripted tests, now always true.
  Removing it means editing ~33 conditionals and their validation files — test
  content, deferred to phase 6.
- The dump/load unit-test suites are now compiled for MariaDB but **have not been
  run**; `lock_service_t.cc` and `gtid_utils_t.cc` are excluded there (their
  sources are MySQL-only).
- ~~`Load_dump_options::on_set_session` now issues one extra
  `SELECT @@GLOBAL.VERSION` on both vendors.~~ **Fixed in phase 1** — it went
  through `common::server_version()` purely to learn the vendor; that is now
  `ISession::get_server_vendor()`, which costs nothing. The extra query had also
  broken the whole `Load_dump_mocked` suite in the MySQL build (§12.4).

### 11.7 MySQL build verified unaffected

Built clean against MySQL 26.7.0 (`MYSQL_SOURCE_DIR=/Users/juanram/dev/mysql-server`,
`MYSQL_BUILD_DIR=.../build`) in a separate `bld-mysql/` tree: **934/934 targets,
zero failures**, `mariadb-shell`, `mariadb-shell-rec` and `run_unit_tests` all
linked.

This covers what the MariaDB build structurally cannot:

- `modules/adminapi/*` compiles here, so the previously unverified hunk — the
  `mysqlshdk/libs/mysql/utils.h` include added to `adminapi/common/common.cc` for
  the relocated `get_binary_logs_keyword()` — is confirmed. All four call sites
  (`adminapi/common/common.cc`, `adminapi/cluster/cluster_impl.cc`,
  `dump/dumper.cc`, `common/dump/server_info.cc`) compile and link.
- `HAVE_BINLOG_UTILS` is defined throughout; every `util/binlog/*` object builds.

Functional check — the MySQL `util` surface is complete and unchanged:

```
change_password, check_for_server_upgrade, copy_instance, copy_schemas,
copy_tables, debug, dump_binlogs, dump_instance, dump_schemas, dump_tables,
export_table, help, import_json, import_table, load_binlogs, load_dump,
upgrade_auth_method
```

`check_for_server_upgrade` (Upgrade Checker), `dump_binlogs`/`load_binlogs`
(`HAVE_BINLOG_UTILS`), and `import_json` (X protocol) are all still there, and
`dba` still exposes its 19 functions. The MariaDB build correctly lacks exactly
those four and nothing else.

---

## 12. Phase 1 — done

Landed 2026-08-06. Goal was "stop the bleeding": the crashes and the silently
wrong behaviour that are independent of the phase-2 refactor.

**The vendor is now obtained from `ISession::get_server_vendor()`**
([db/session.h:120](mysqlshdk/libs/db/session.h#L120),
[db/mysql/session.h:180](mysqlshdk/libs/db/mysql/session.h#L180)). It already
existed and is the right primitive: it reads `mysql_get_server_info()` — the
handshake string the client already holds — and caches the answer, so it costs
no round trip and no `SELECT @@version`. Every vendor decision below is
therefore made from the *server actually connected to*, never from
`MARIADB_BUILD`; a MySQL-linked shell pointed at MariaDB takes the same
branches.

It was not null-safe: `std::string info = get_server_info()` constructs a
`std::string` from `nullptr` when the session is not connected. It now throws
`std::runtime_error("Not connected")`, matching the convention already used in
`session.cc`. `Mock_mysql_session` mocks the method, since a mock never
connects and has to state its own vendor.

### 12.1 The `mysql` system-table lock list (§4.3)

`Dumper::lock_all_tables()` ([dumper.cc:3247](modules/util/dump/dumper.cc#L3247))
picks the table list by `m_server_version.is_maria_db`. MariaDB gets
`global_priv` (the real account table — `user` is only a view over it since
10.4, so locking `user` locks nothing that matters), `roles_mapping` instead of
`role_edges`, no `default_roles` / `global_grants`, and `event`, which MySQL
does not need because 8.0 keeps events in the data dictionary. Verified against
a live server: the emitted statement is exactly that list.

Because the statement is built from `SHOW TABLES IN mysql WHERE ... IN (...)`,
the old behaviour was to silently lock a subset rather than fail — a quiet
correctness hole, which is why it survived phase 0's round-trip test.

### 12.2 Roles (§4.2)

`User_privileges::read_user_roles` returned early on MariaDB because
`activate_all_roles_on_login` does not exist there, so **no role-granted
privilege was ever counted** and an account whose `SELECT` / `RELOAD` comes via
a role was told it lacked privileges.

MariaDB's role model is different enough to need its own path, not a translated
one:

- No mandatory roles, and no way to activate every granted role on login —
  exactly one role, the account's default role, is enabled when it connects.
- Enabling a role implicitly enables everything granted to *that* role, so the
  role graph does not have to be walked client-side.
- Roles are hostless.
- `SHOW GRANTS` has **no `USING` clause**. Its bare form (`current_user_and_
  current_role` in the grammar) reports the account's own grants, the full
  transitive closure of its active role, and the `PUBLIC` role in one
  statement, and needs no privileges on the `mysql` schema. For any other
  account, `SHOW GRANTS FOR <role>` is transitive as well
  (`traverse_role_graph_down`, `sql/sql_acl.cc:11431`).

So: roles come from `information_schema.APPLICABLE_ROLES` (current account, no
privileges needed) or `mysql.user.default_role` (any other account), and
`parse_user_grants` issues the bare `SHOW GRANTS` or a `SHOW GRANTS FOR <role>`
per role rather than a `USING` clause.

Two smaller fixes in the same file:

- `parse_grant` accepts `DENY` (MariaDB 12.0+, `DENY privileges ON level TO
  account` — a revoke which keeps GRANT's token order) instead of throwing
  `std::logic_error`. The `SET DEFAULT ROLE` case it already skipped lost its
  `#ifdef MARIADB_BUILD`: MySQL's `SHOW GRANTS` never emits either statement,
  so neither needs a build gate.
- `get_mandatory_roles` did `fetch_one()->get_string(1)` on
  `SHOW GLOBAL VARIABLES LIKE 'mandatory_roles'`, the same null-row crash shape
  as §4.10 and the `partial_revokes` one. Unreachable on MariaDB now, guarded
  anyway.

`User_privileges_test.validate_role_privileges_direct` is a live-server test
which exercises exactly this (nested roles, non-current account). It **fails
before this change and passes after** — verified by reverting just this file.

### 12.3 `REPLICATION CLIENT` does not exist in MariaDB (§4.2)

Found while testing 12.1. MariaDB 10.5 renamed the privilege to `BINLOG
MONITOR` and `SHOW PRIVILEGES` does not report the old name, so
`User_privileges::validate({"REPLICATION CLIENT", "SUPER"})`
([dumper.cc:3530](modules/util/dump/dumper.cc#L3530)) hit
`validate_privileges()`'s unknown-privilege check and threw. The result was that
**every `dumpSchemas` / `dumpInstance` on MariaDB aborted** with
`Invalid privilege in the privileges list: REPLICATION CLIENT` as soon as FTWRL
was unavailable — i.e. whenever the account lacks `RELOAD`. The name and the two
user-facing messages that quote it are now vendor-selected.

The remaining `validate()` call sites were audited: `BACKUP_ADMIN` and
`FLUSH_TABLES` are behind `>= 8.0` version gates that the 5.6 remap turns off,
`MANAGE_DATA_MASKING_POLICY` and `SET_ANY_DEFINER` sit behind MySQL-only feature
gates, and the rest (`SELECT`, `LOCK TABLES`, `EVENT`, `TRIGGER`, `SUPER`,
`RELOAD`) exist in MariaDB. Phase 2 must re-check the first two when the remap
goes.

### 12.4 `util.copy*` (§4.0) and the loader's extra query

`copy_operation.h` read the target's `@@version` into a bare `Version` — §4.0's
hole reproduced verbatim outside `Load_dump_options`. Against a MariaDB target
`is_supported_server(12.3.2)` is false, so every `copyInstance` / `copySchemas`
printed a bogus *"Target MySQL version '12.3.2' is not supported by this version
of MySQL Shell"*. The MDS check, the target-version validation and the 8.4
`mysql_native_password` probe are now skipped for a MariaDB target; the MySQL
path is textually unchanged.

Separately, phase 0's `Load_dump_options::on_set_session` called
`common::server_version(session)` purely to learn the vendor, which issued an
extra `SELECT @@GLOBAL.VERSION`. That query **aborted the entire
`Load_dump_mocked` suite** in both builds (the mocked session scripts its
queries). Replacing it with `get_server_vendor()` removes the query and the
failure.

### 12.5 Verified

Against a live MariaDB 12.3.2 (sandboxes on 3312 and 3313), using an account
whose `SELECT` / `SHOW VIEW` / `EVENT` / `TRIGGER` / `LOCK TABLES` / `RELOAD`
are reachable only through a *nested* role (`baserole` -> `dumprole` -> user):

- `dumpSchemas` succeeds — previously it was refused for missing privileges.
  `SHOW GRANTS FOR` that account lists only `USAGE`, so the role expansion is
  doing the work.
- With `RELOAD` revoked so FTWRL is unavailable, the `LOCK TABLES` fallback runs
  and emits the MariaDB system-table list (12.1) instead of aborting on
  `REPLICATION CLIENT` (12.3).
- `copySchemas` completes MariaDB -> MariaDB with no spurious version warning,
  which also covers the in-memory `Dump_writer` path phase 0 did not exercise.
  `ignoreVersion: true` is still required — that is §11.5, removed in phase 2.

Unit tests, MariaDB build (`Compatibility_test`, `Dump_utils`, `Dump_scheduler`,
`Load_dump`, `Load_dump_mocked`, `Schema_dumper_test`, `User_privileges_test`):
68 passed, 7 failed (§12.6). Before this change the same filter aborted in
`Load_dump_mocked` and, with that suite excluded, reported 59 passed / 8 failed.
Net: `Load_dump_mocked` (6 tests) runs again and
`validate_role_privileges_direct` passes; nothing regressed.

MySQL build, same filter against MySQL 26.7.0: **78/78 passed**, one skip
(`dump_filtered_grants_super_priv`, "SUPER has been deprecated in 8.0").

### 12.6 Known-unfixed

**Testing policy (decided).** Component-level unit tests — `Schema_dumper_test`,
`User_privileges_test`, `Compatibility_test` and the like — count on both
vendors and are tracked per phase; they are tied to a specific piece of code, so
a MariaDB failure there is a real signal. The end-to-end **dump/load** suites
are the exception: they are deferred on MariaDB until the port is complete
(phase 6), because until the vendor-aware gating of phase 2 lands they mostly
re-report the same handful of known gaps. The **MySQL** suites are a gate at
every phase without exception — they are what proves MySQL -> MySQL is
untouched.

Failing on the MariaDB build, all **pre-existing** (confirmed by running the
same filter on a clean tree):

- `Schema_dumper_test.dump_libraries` — MySQL 9 JS libraries; MariaDB has no
  `LIBRARY` object at all. This wants a gate on the **active server's vendor**,
  not new expected output — the test-side counterpart of `supports_library_ddl()`
  becoming vendor-aware in §7.3, so it belongs with phase 2 rather than phase 6.
- `User_privileges_test.partial_revokes` — sets a MySQL-only system variable on
  a MariaDB server; same treatment, same phase.
- ~~`Schema_dumper_test.{dump_grants, dump_filtered_grants, opt_mysqlaas,
  compat_ddl, unknown_collations}` — genuine expected-output differences,
  needing the `schema_dumper_t.cc` recipe from the
  `mariadb-schema-dumper-tests` note. Phase 6.~~ **All resolved in phase 6**
  (§21.5): the grant ones were fixed along the way by §20, and the three mysqlaas
  ones are skipped — two on the server's vendor, `unknown_collations` on the
  build's, because it runs off a mock session and only the linked charset table
  decides.

~~Also still open: `Version::is_mds()` is vendor-blind (§7.1).~~ **Fixed in
phase 2** (§13.4).

---

## 13. Phase 2 — done

Landed 2026-08-10. Goal was §7: carry the vendor everywhere a version is
carried, stop remapping, and convert every gate. §11.5 — the same server being
rejected as a non-consecutive major-version jump when dumped and reloaded — is
fixed; `ignoreVersion` is no longer needed for MariaDB → MariaDB.

### 13.1 The 5.6 remap is gone from the MariaDB build

[server_info.cc](modules/util/common/dump/server_info.cc) still detects MariaDB
by substring, but now only sets `is_maria_db`. The `Version("5.6.0-" + …)`
rewrite is under `#ifndef MARIADB_BUILD`, exactly as §7.1 required: a MySQL
build keeps it, so upstream's MariaDB → MySQL migration path is untouched
(verified live, §13.7).

`Server_version`'s `is_5_6` / `is_5_7` / `is_8_0` now mean **MySQL** 5.6 / 5.7 /
8.0 and are all false for MariaDB. That is what makes the conversion
mechanical: every `!is_8_0` site keeps the answer it had under the remap, and
only the three `is_5_6` sites change meaning — which is precisely where the
remap was lying.

A second overload was added:

```cpp
Server_version server_version(const Version &number, bool is_maria_db);
```

No detection, no remapping — for the places where the vendor comes from
somewhere other than the version string (the session handshake, the manifest, a
user-supplied `targetVersion`).

### 13.2 `common/dump/server_features.h`

The new home decided in §7.3. 28 predicates, all taking a `Server_version`. The
six that moved out of `dump/compatibility.h` kept their MySQL thresholds
unchanged, so the MySQL build sees the same answers from a different header.

Most are the `mysql_only(v, since)` shape — a MySQL threshold, false for
MariaDB. The ones that are not:

| Predicate | MariaDB answer |
|---|---|
| `requires_explicit_select_privilege` | **true** — only MySQL 8.0's data dictionary reports what the account cannot see |
| `supports_show_create_user` | **true** since 10.2 |
| `requires_super_to_dump_users` | false (was true under the remap — MariaDB 10.5+ split `SUPER`) |
| `supports_optimizer_hints` / `supports_wide_bit_xor` | false → `SQL_NO_CACHE`, sliced checksums; both correct for MariaDB |
| `supports_role_dumping` / `supports_column_statistics` | false **for now** — MariaDB has both, but dumping them is phase 5 |

Two more that are not feature questions but belong with them:

- `reference_version(is_maria_db)` — §7.4's yardstick: `k_shell_version` for a
  MySQL server, `k_build_server_version` for a MariaDB one.
- `is_maria_db_dialect()` / `produces_maria_db_dialect()` — see §13.5.

### 13.3 The build-time server version reaches C++

`MYSQL_VERSION` (already computed per vendor by `GET_MYSQL_VERSION()`) is now
exported as `-DMYSH_BUILD_SERVER_VERSION`
([CMakeLists.txt](CMakeLists.txt)), and `mysqlshdk::utils::k_build_server_version`
([version.h](mysqlshdk/libs/utils/version.h)) exposes it, falling back to
`k_shell_version` when no server source tree was configured.

Five `k_shell_version` sites in `Dumper::fetch_server_information()` and
`Dump_options::current_version()` now go through `reference_version()`, and the
"unsupported/newer server" messages name the actual vendor. The default
`targetVersion` for a MariaDB dump is therefore the MariaDB release this Shell
was built from (13.1.0 here), not the Shell's own 26.8.0.

`Dump_options::set_target_version()` no longer validates inline — `targetVersion`
is unpacked *before* the session is set, so the vendor is not known yet. The
check moved to `on_validate()`, where MariaDB gets a "newer than what this Shell
was built against" rule instead of the MDS minimum and the supported-MySQL list.

### 13.4 Both sides now carry a vendor

- **Dump.** `Dump_options` captures the source vendor in `on_set_session()` from
  `ISession::get_server_vendor()` and exposes `target_server_version()`, a
  `Server_version` pairing `targetVersion` with that vendor. `Schema_dumper`'s
  `m_target_version` became a `Server_version` too, taking its vendor from the
  instance cache — the setter signature is unchanged, so no test churn.
- **Load.** `Load_dump_options::m_target_server_version` (a bare `Version`) and
  the phase-1 `m_target_is_maria_db` collapsed into one
  `common::Server_version m_target_server`, built from the existing
  `SELECT @@version` plus the cached handshake vendor — no extra round trip, and
  **no remap on the load side ever**. `Dump_reader` gained `source_server()`.

Three vendor-blind reads called out in §7.1 were fixed with it:
`Version::is_mds()` (now `!maria && …`), `ISession::get_server_version()` at
[dump_loader.cc:2109](modules/util/load/dump_loader.cc#L2109) — which would have
run MySQL-only `PS_CURRENT_THREAD_ID()` against MariaDB — and copy's target
probe.

### 13.5 Cross-vendor is refused — by dialect, not by source vendor

`check_server_version()` refuses before any DDL runs (`SHERR_LOAD_VENDOR_MISMATCH`,
53039), and `util.copy*` refuses between its two live sessions.

The check is on the dump's **dialect**, not on where it came from, because §6.4
("drop MariaDB → MySQL") and §7.1 ("do not disturb the MySQL build") pull in
opposite directions otherwise. A MySQL build remaps a MariaDB source to 5.6,
which suppresses everything MariaDB-specific and produces a MySQL-shaped dump —
so `is_maria_db && !is_5_6` is exactly "this dump is MariaDB-dialect". The
outcomes:

| Dump produced by | Source | Target | Result |
|---|---|---|---|
| MariaDB build | MariaDB | MariaDB | loads |
| MariaDB build | MariaDB | MySQL | refused |
| MySQL build | MariaDB | MySQL | **loads** — upstream's migration path, unchanged |
| either | MySQL | MariaDB | refused |

The manifest now records `"vendor": "mariadb" \| "mysql"` inside `source`
(§6.3), and `server_info()` lets it override the substring detection. Old dumps
without the field still work — the version string carries the answer.

`ocimds` is refused for a MariaDB source as well: every rewrite it performs
targets MySQL DDL, and MySQL HeatWave Service is a MySQL product.
`updateGtidSet` was refused too, pending phase 4; phase 4 implemented it and
that refusal (`SHERR_LOAD_UPDATE_GTID_UNSUPPORTED_VENDOR`) is gone — see §15.

### 13.6 One real bug the remap had been hiding

`Instance_cache_builder::fetch_view_metadata()` gates
`information_schema.VIEW_TABLE_USAGE` on `>= 8.0.13`. Under the remap MariaDB
looked like 5.6 and took the parse-`VIEW_DEFINITION` fallback; with the real
version it satisfied the threshold and every dump containing a view died with
`Unknown table 'view_table_usage' in information_schema`. Now
`supports_view_table_usage()`.

This is the whole argument for §7.3 in one example: the site was not asking
about a version, it was asking about a table that MySQL 8.0.13 happens to have.

### 13.7 Verified

Live, against MariaDB 12.3.2 sandboxes (3312, 3313) and MySQL 26.7.0 (3310):

- `dumpSchemas` → `loadDump`, MariaDB → MariaDB, **without `ignoreVersion`** —
  "Target is MariaDB 12.3.2-MariaDB-debug. Dump was produced from MariaDB
  12.3.2-MariaDB-debug". Data and view verified after reload. This is §11.5
  closed.
- Manifest: `vendor: mariadb`, `serverVersion: 12.3.2-MariaDB-debug` (no remap),
  `targetVersion: 13.1.0` (the build-time server version).
- `copySchemas` MariaDB → MariaDB.
- Refusals fire with their own messages: MariaDB dump → MySQL target, MySQL dump
  → MariaDB target, `copySchemas` MariaDB → MySQL, `ocimds: true` from MariaDB.
- MySQL build: `dumpSchemas` → `loadDump` MySQL → MySQL round trip; manifest
  `vendor: mysql`, `targetVersion: 26.8.0`.
- MySQL build dumping **from** MariaDB: manifest still says
  `serverVersion: 5.6.0-12.3.2-MariaDB-debug`, the dialect check passes, and it
  loads into MySQL with `ignoreVersion` — which is what a 5.6-shaped dump has
  always needed. Upstream's path is intact.

Unit tests (`Compatibility_test`, `Dump_utils`, `Dump_scheduler`, `Load_dump`,
`Load_dump_mocked`, `Schema_dumper_test`, `User_privileges_test`,
`Instance_cache_test`, `Checksums_test`):

- **MySQL build: 95/95 passed**, 2 pre-existing skips.
- **MariaDB build: 86 passed, 4 failed**, all four pre-existing (verified by
  running the same filter on a clean tree). `Schema_dumper_test.dump_grants` and
  `dump_filtered_grants` — listed as failing in §12.6 — now **pass**, because
  without the remap the dumper takes the `SHOW CREATE USER` path MariaDB
  actually supports.

Three tests were gated on the **active server's vendor**, the test-side
counterpart of §7.3 (`Shell_test_env::target_server_is_maria_db()` is the new
helper):

- `Schema_dumper_test.dump_libraries` and `User_privileges_test.partial_revokes`
  — the two §12.6 called out.
- `Schema_dumper_test.strip_restricted_grants_set_any_definer` — sets MySQL
  target versions on a Schema_dumper built against a MariaDB server, which no
  longer means anything now that the target carries the source's vendor.

### 13.8 Known-unfixed

- Still failing on the MariaDB build, all pre-existing:
  `Instance_cache_test.table_columns` (a column-type mapping difference),
  `Schema_dumper_test.{opt_mysqlaas, compat_ddl, unknown_collations}` (expected
  output, phase 6).
- `Instance_cache_builder::fetch_view_metadata()` hands the real MariaDB version
  to `mysqlshdk::parser::Parser_config`, so MariaDB view definitions are parsed
  with the newest MySQL grammar rather than 5.6's. Closer than before, but
  MariaDB-only view syntax is still not understood — relevant once §4.5's view
  handling is revisited.
- `dumper.cc` keeps two raw `is_8_0` reads on purpose, both on user-dumping /
  `ocimds` paths that are unreachable on MariaDB today; they are marked with the
  phase that owns them.
- MySQL → MySQL dumps are **not** byte-identical before and after, as §7.5
  suggested checking: the manifest gained the `vendor` field. That is the only
  difference, and it is the §6.3 requirement.
- **`targetVersion` error messages lost their `Argument #N:` prefix on the MySQL
  build** — found while running the phase-3 gate (§14.4), present before phase 3
  and absent before phase 2. Moving the validation out of the option unpacker
  into `on_validate()` (the trap recorded above) also moved it out of the
  unpacker's error wrapper, so `Target MySQL version '26.9.0' is not
  supported…` no longer carries the argument position. It fails 12 assertions in
  `Shell_scripted/Auto_script_py.run_and_check/util_dump_instance_norecord`, and
  those 12 are the *only* failures in that suite. Not restored: the check is
  vendor-dependent and the vendor comes with the session, so it cannot go back
  into the unpacker, and carrying the argument position into `on_validate()` would
  mean threading it through 10 call sites for a message decoration. The
  expectations were pinned to the prefix-less output instead — §21.9.

---

## 14. Phase 3 — done

Landed 2026-08-11. Goal was §4.1: give MariaDB a real backup lock, so that a
consistent dump no longer means "block the whole server with FTWRL for the
duration, or give up".

Before this, `m_user_has_backup_admin` could only ever be true on MySQL 8.0, so
every MariaDB dump printed *"Backup lock is not supported in MariaDB 12.3 and
DDL changes will not be blocked"* and ran with DDL wide open from the moment
`UNLOCK TABLES` released the global read lock.

### 14.1 What `BACKUP STAGE` actually does

Measured on MariaDB 12.3.2 with a session parked at `BACKUP STAGE BLOCK_DDL`
while another session probed it. This is the table the design rests on, and
three of the rows contradict what §4.1 predicted:

| Operation in another session | Under `BLOCK_DDL` |
|---|---|
| DDL (`CREATE TABLE`) | **blocked** — the point of the exercise |
| InnoDB DML | runs |
| Aria DML (`transactional=1`, the default) | runs |
| MyISAM DML | **blocked** |
| `FLUSH TABLES WITH READ LOCK` | succeeds |
| `LOCK TABLES … READ` | succeeds |
| `CREATE USER` / account management | **runs — not blocked** |
| a second `BACKUP STAGE START` | blocked |

So "blocks DDL while leaving DML running" (§4.1) is true only for transactional
tables: writes to genuinely non-transactional tables wait for the whole dump.
That is a real behavioural cost with no MySQL counterpart —
`LOCK INSTANCE FOR BACKUP` blocks no DML at all — so `start_backup_stage()`
`log_info`s it rather than letting it be a surprise in the server's process
list.

Three structural constraints, all from the server source
(`sql/backup.cc`, `sql/mdl.cc`, `sql/sql_parse.cc`):

- **Stages only move forward.** `run_backup_stage()` raises
  `ER_BACKUP_WRONG_STAGE` for any stage `<=` the current one. There is no path
  back from `BLOCK_COMMIT` to `BLOCK_DDL`, which is what kills §4.1's
  FTWRL → `START` + `BLOCK_COMMIT` mapping: the commit block could be taken for
  the snapshot window but never released without ending the backup.
- **It is a server-wide singleton.** `backup_flush_ticket` is a file-static, and
  `MDL_BACKUP_START` is incompatible with itself. One backup per server, held by
  the connection that started it.
- **`SQLCOM_BACKUP` is `CF_AUTO_COMMIT_TRANS`**, and `backup_start()` refuses to
  run in a connection with `has_read_only_protection()` — i.e. one holding
  FTWRL. Either alone forces the stage out of the main session; together they
  make it non-negotiable.

### 14.2 The design

FTWRL is kept for the snapshot window and `BACKUP STAGE` becomes the backup
lock, which is exactly the shape MySQL already has:

| Step | MySQL | MariaDB |
|---|---|---|
| snapshot window | `FLUSH TABLES WITH READ LOCK` | unchanged — FTWRL works and needs the same `RELOAD` |
| backup lock, held for the dump | `LOCK INSTANCE FOR BACKUP` (main session) | `BACKUP STAGE START` + `BLOCK_DDL` (**dedicated session**) |
| release | session close | `BACKUP STAGE END`, then close |
| privilege probe | `BACKUP_ADMIN` | `RELOAD` |

The dedicated session is `m_backup_stage_session`, created in
`Dumper::start_backup_stage()` and torn down in `Dumper::unlock_instance()`,
which is wired into the same `on_leave_scope` in `do_run()` that closes the main
session — so an interrupt, an exception or a hard kill all release the
server-wide backup. (A killed connection releases it too: `backup_end()` runs on
THD teardown. Verified.)

`m_user_has_backup_admin` became `m_backup_lock_available`, and the privilege
name moved behind `Dumper::backup_lock_privilege()`, which returns
`"BACKUP_ADMIN"`, `"RELOAD"` or `nullptr` (no backup lock on this server at
all). The three message sites that used to hardcode `BACKUP_ADMIN` or gate on
`supports_lock_instance_for_backup()` now go through it, so MySQL's output is
byte-identical and MariaDB's says `RELOAD`.

`supports_backup_stage()` is the new predicate in
[server_features.h](modules/util/common/dump/server_features.h) — MariaDB 10.4+.

### 14.3 The one place the two locks are not interchangeable

`lock_all_tables()` skipped locking the `mysql` system tables when the backup
lock was held, because MySQL's backup lock blocks account management. MariaDB's
does not (§14.1) — the grant tables are transactional Aria and stay writable —
so that skip is now conditional on `supports_lock_instance_for_backup()` and
MariaDB keeps locking them.

This path needs `RELOAD` present *and* FTWRL to have failed with an access
error, which is close to unreachable, but the asymmetry is real and silently
loses grant consistency if it is ever hit.

### 14.4 Verified

Live, MariaDB 12.3.2 (3312 → 3313) and MySQL 26.7.0 (3310):

- `dumpInstance` on MariaDB now prints "Locking instance for backup"; the
  "not supported in MariaDB" note is gone.
- `--log-sql=all` shows the exact intended sequence, and shows it on a
  *different connection* from the main session:
  `tid=2451 FLUSH TABLES WITH READ LOCK` → `tid=2457 BACKUP STAGE START` →
  `tid=2457 BACKUP STAGE BLOCK_DDL` → `tid=2451 UNLOCK TABLES` →
  `tid=2457 BACKUP STAGE END`.
- During a live throttled dump: `CREATE TABLE` from another session times out,
  InnoDB `INSERT` succeeds. After the dump, and after killing a dump mid-flight,
  DDL works again — nothing leaks.
- `dumpInstance` → `loadDump`, MariaDB → MariaDB, 300k rows, no warnings.

### 14.5 Not done here

- The `consistent: false` path is untouched — no locks, as before.
- Dry runs still skip the lock entirely, matching what MySQL does with
  `LOCK INSTANCE FOR BACKUP`; the `RELOAD` privilege is checked but the stage is
  never entered.
- `BACKUP LOCK <table>` (§4.1) is not used. It would be the only way to give the
  no-`RELOAD` account any DDL protection, but it is one table per connection
  (`thd->mdl_backup_lock` is a single ticket), so covering a dump would need one
  connection per table.
- `lock_wait_timeout` is left at the server default while entering the stage,
  the same as MySQL's `LOCK INSTANCE FOR BACKUP`. On MariaDB that default is
  86400, so a long-running `ALTER` will stall the dump at "Locking instance for
  backup" rather than failing it. Worth revisiting with the phase 6 tests.

---

## 15. Phase 4 — done

Landed 2026-08-11. Goal was §4.4: make the dump carry a GTID position MariaDB
understands, and make `updateGtidSet` able to put it back.

§4.4 said to "reuse the binlog port's native-GTID model". There is none —
§11.2 established that the `mariadb_rpl_*` port `MARIADB_PORT.md` §4 describes
was never written, and `mysqlshdk/libs/mysql/gtid_utils.*` is MySQL-only both
in model (`uuid:n-m`) and in mechanism (every comparison is a round trip to
`GTID_SUBSET()` / `GTID_SUBTRACT()`). So the model is new here, and small.

### 15.1 What MariaDB's GTIDs are, measured

On MariaDB 12.3.2 with `log_bin` on, `server_id=100`, three transactions:

| | value |
|---|---|
| `@@gtid_binlog_pos` | `0-100-3` |
| `@@gtid_current_pos` | `0-100-3` |
| `@@gtid_slave_pos` | *(empty)* |
| `SHOW MASTER STATUS` | `binlog.000001`, `853`, ``, ``, **`0-100-3`** |

Three things follow, all of which shaped the code:

- A GTID is `domain-server-sequence`, and a *position* is a comma-separated
  list with **at most one entry per replication domain** — the last sequence
  seen there. `0-100-3` therefore means "every transaction of domain 0 up to
  sequence 3", which is what makes client-side subset/intersection tractable.
- `SHOW MASTER STATUS` on a current MariaDB **does** have a fifth column
  (`Gtid_Binlog_Pos`, `sql/sql_repl.cc`), so the existing `size() > 4` branch
  in `common::binlog()` was already picking something up. It picks up
  `gtid_binlog_pos`, which is the wrong one for a server that replicates
  without `log_slave_updates`.
- `gtid_current_pos` is the union of the binlog and slave positions, and is
  what mariabackup records (`extra/mariabackup/backup_mysql.cc:1657`). That is
  the value the dump now stores, fetched explicitly rather than read off the
  fifth column, so old and new servers behave alike.

### 15.2 `Mariadb_gtid_position`

[mysqlshdk/libs/mysql/mariadb_gtid.h](mysqlshdk/libs/mysql/mariadb_gtid.h) — a
`map<domain, {server_id, sequence}>` with `parse`, `str` (canonical, domains
ascending), `contains`, `intersects` and `merge`. It is a pure value type: no
session, no round trips, which is the whole difference from `Gtid_set`.

`intersects()` is "share a domain with a non-zero sequence", because a position
covers every sequence below the one it names. `contains()` ignores `server_id`
— the domain and the sequence are what say which transactions are covered.
Built for both vendors and covered by
[mariadb_gtid_t.cc](unittest/mysqlshdk/libs/mysql/mariadb_gtid_t.cc)
(5 tests, run in both builds).

### 15.3 Dump side

- `common::gtid_executed()` takes the `Server_version` and reads
  `@@GLOBAL.gtid_current_pos` for a MariaDB-dialect server, `@@GLOBAL.GTID_EXECUTED`
  otherwise. `common::binlog()` calls it for every MariaDB server, not only in
  the access-denied fallback.
- `Dumper::fetch_server_information()` no longer asks MariaDB for `gtid_mode`,
  which does not exist and always read as `OFF` — leaving `m_gtid_enabled`
  false and every consistency check on the binlog-file branch. On MariaDB
  GTIDs come with the binary log, so `m_gtid_enabled = m_binlog_enabled`.
- The two `check_if_transactions_are_ddl_safe()` call sites in
  `validate_dump_consistency()` are now guarded at *run time*, not only by
  `#ifdef HAVE_BINLOG_UTILS`: a MySQL build pointed at a MariaDB server would
  otherwise have sent it `GTID_SUBTRACT()` and read its binary log with
  libbinlogevents. Both fall back to the "treating the dump as not verified"
  note.
- The no-FTWRL warning in `lock_instance()` no longer advises a MariaDB user to
  set `gtid_mode`, and names `SHOW MASTER STATUS` rather than
  `SHOW BINARY LOG STATUS` — `binlog_status_keyword()` in `server_info.h` now
  holds the keyword choice that `common::binlog()` had inline.

The gate is `is_maria_db_dialect()`, not `is_maria_db`: a MySQL build dumping
*from* MariaDB remaps the source to 5.6 and produces a MySQL-shaped dump, and a
domain position has no meaning in one. That path keeps reading
`GTID_EXECUTED`, failing, and recording nothing — exactly as before.

### 15.4 Load side

`updateGtidSet` works on MariaDB. `Dump_loader::update_maria_db_gtid_position()`
assigns to `gtid_slave_pos` — mariabackup's mechanism as well, and the one that
makes `CHANGE MASTER TO ... master_use_gtid = slave_pos` resume correctly:

| | MySQL | MariaDB |
|---|---|---|
| `replace` | `SET GLOBAL GTID_PURGED = <dump>` | `SET GLOBAL gtid_slave_pos = <dump>` |
| `append` | `SET GLOBAL GTID_PURGED = '+' + <dump>` | union computed client side, then assigned whole — there is no `+` form |
| blocked by | group replication running | any replica thread running (the server rejects the assignment) |

The upstream MySQL block moved unchanged into
`Dump_loader::validate_update_gtid_set()`; the MariaDB one is beside it. Its
two checks are the domain-position counterparts of upstream's:

- `replace` requires the dumped position to *contain* the target's current
  `gtid_slave_pos` — nothing already replicated gets forgotten. Upstream spells
  this `GTID_SUBSET(gtid_purged, dump)`.
- `append` requires the two not to *intersect*, i.e. to share no domain.

Three error codes replace the phase-2 refusal, which is deleted rather than
left as a no-op: 53040 `..._REPLICATION_IS_RUNNING`, 53041
`..._REPLACE_REQUIRES_SUPERSET_POSITION`, 53042
`..._APPEND_POSITIONS_INTERSECT`. `SHERR_LOAD_LAST` is 53042.

Replication state is read with `SHOW ALL SLAVES STATUS` (multi-source aware,
where `SHOW SLAVE STATUS` reports the default connection only), matching
columns by name so an absent column is a skipped check rather than a crash.

`util.copy*` gets all of this for free — it shares the loader — and its help
text, like `loadDump`'s, now documents the MariaDB behaviour.

### 15.5 Verified

Live, MariaDB 12.3.2 with `log_bin` on (a new sandbox on 3314; the existing
3312/3313 pair has no binary log, which is why none of this could have been
exercised before):

- `dumpSchemas` from 3314 writes `"gtidExecuted": "0-100-3"` and
  `"vendor": "mariadb"` into `@.json`.
- **Full provisioning round trip.** Dump at `0-100-3`; two more transactions on
  the source; `loadDump` into 3313 with `updateGtidSet: "replace"` sets
  `gtid_slave_pos` to `0-100-3`; `START SLAVE` then replicates exactly the two
  missing rows and lands on `0-100-6`. This is the thing phase 4 exists for.
- `append` against a target sitting at `1-200-5` produces `0-100-3,1-200-5`.
- All three refusals fire: 53042 when appending onto a position that already
  holds domain 0, 53041 when replacing would drop `1-200-5`, and 53040 while
  the target is replicating.
- The `consistent: false` dump still marks `gtidExecutedInconsistent: true`,
  and now records a real position with it.
- Lock-less dump (an account with neither `RELOAD` nor `BINLOG MONITOR`): the
  note is now *"The DDL consistency will be checked using the binary log"*
  instead of the `gtid_mode` advice, and with a writer running concurrently it
  reports the position change (`0-100-198` → `0-100-199`) and degrades to
  "treating the dump as not verified" rather than trying MySQL's binlog reader.

### 15.6 MySQL build unaffected

- `Compatibility_test`, `Dump_utils`, `Dump_scheduler`, `Load_dump`,
  `Load_dump_mocked`, `Schema_dumper_test`, `User_privileges_test`,
  `Instance_cache_test`, `Checksums_test` plus the new parser tests:
  **100/100 passed**, 2 pre-existing skips.
- MariaDB build, same filter: **91 passed, 4 failed** — the same four §13.7
  lists as pre-existing (`Instance_cache_test.table_columns`,
  `Schema_dumper_test.opt_mysqlaas` / `compat_ddl` / `unknown_collations`).
- `util_help_norecord`: the help-text edit is consistent in both builds. The
  suite still fails on each for reasons that predate this phase — verified by
  re-running the MySQL one with the change stashed: the same single
  `copy_instance` spacing mismatch, and on MariaDB ten about `dump_binlogs` and
  `skipUpgradeChecks` being absent from that build.

### 15.7 Not done here

- **`gtid_binlog_state` is never written.** Setting it needs `RESET MASTER`,
  which would throw away the target's own binary log; `gtid_slave_pos` is the
  documented provisioning path and is what mariabackup uses.
- **Galera is not detected.** The MySQL branch refuses `updateGtidSet` while
  group replication runs; the MariaDB branch checks replica threads only. On a
  Galera node `wsrep_gtid_domain_id` makes the position cluster-wide and
  assigning it is a node-local action — worth a check, but it needs a Galera
  cluster to design against.
- **`Schema_dumper::process_set_gtid_purged()` was left alone.** §4.6 lists its
  `SET @@GLOBAL.GTID_PURGED` epilogue as MySQL-specific, and it is, but the
  function is **dead code** — inherited from mysqldump, declared and defined,
  called from nowhere. The shell carries the GTID set in the manifest instead.
- `Dump_reader::show_metadata()` still labels the value `Executed_GTID_set` for
  both vendors.

---

## 16. Phase 5a — done

Landed 2026-08-17. Goal was §4.5.1: make the dump carry MariaDB sequences, and
make the load put them back where they were.

**Are sequences supported now? Yes** — on the vendor → vendor path this port is
for (MariaDB build, MariaDB source, MariaDB target) they are a first-class object
type: dumped with both their definition and their current position, restored to
that position, selected by the table filters, dropped by `dropExistingObjects`,
and reported by the pre-existing-object check. There is one case that still
leaves them out, and it is out of scope by §6 rather than unfinished: a **MySQL
build** reading a MariaDB server, which by design writes MySQL-shaped dumps and
cannot express a sequence at all (§16.4).

The gap this closes was worse than §4.5.1 described. Sequences were not silently
dropped — they were cached as **views**, and `dumpSchemas` of any schema holding
one died with `unordered_map::at: key not found`, aborting the whole dump.
§4.5.1 now records what was measured.

### 16.1 What a sequence is, measured

On MariaDB 12.3.2. A sequence is a one-row table wrapped in sequence semantics,
so it is visible in three places at once, and each one tells a different part of
the story:

| Source | What it gives | What it does not |
|---|---|---|
| `I_S.TABLES` | the sequence exists (`TABLE_TYPE='SEQUENCE'`, `ENGINE`, `TABLE_ROWS=1`) | anything about the sequence itself |
| `I_S.SEQUENCES` | the static definition — `START_VALUE`, `MINIMUM_VALUE`, `MAXIMUM_VALUE`, `INCREMENT`, `CYCLE_OPTION` | **the current position**, and `CACHE` |
| the sequence read as a table | `next_not_cached_value`, `cycle_count`, and the definition | — |

So the dump needs two queries, which is exactly what `mysqldump` does: `SHOW
CREATE SEQUENCE` for the definition (it prints `cache`/`nocache` and
`cycle`/`nocycle`, which `I_S.SEQUENCES` does not), and `SELECT
next_not_cached_value FROM <seq>` for the position.

**`next_not_cached_value` is ahead of the value last handed out, by design.** A
sequence with `CACHE 1000 INCREMENT BY 5` that has produced exactly one value
(`100`) reports `5100`: the cache reserved a thousand values up front. Restoring
`5100` is therefore correct rather than lossy — it is the same value the server
itself would resume from after a restart, and it can only skip values, never
repeat one. Fixtures that want a predictable number use `NOCACHE`.

`DO SETVAL(<seq>, <value>, 0)` is what `mysqldump` emits and what this port
emits. `ALTER SEQUENCE <seq> RESTART WITH <value>` was measured to be
equivalent here (both leave `START_VALUE` alone and make the next `NEXT VALUE
FOR` return `<value>` exactly), and it would have needed no loader-side parsing
at all — but `SETVAL` only ever moves a sequence **forward**, while `RESTART
WITH` also moves it back. Loading into a target whose sequence has run further
should not rewind it, so `SETVAL` is the safer primitive as well as the
compatible one.

### 16.2 Design

**Sequences are enumerated and filtered as tables.** They are already in
`I_S.TABLES`, they share the table namespace, and `mysqldump` filters them with
its table list — so `filter_tables()` routes `TABLE_TYPE='SEQUENCE'` into a new
`Instance_cache::Schema::sequences`, and `excludeTables` / `includeTables`
select them. No new user-facing option, and therefore no new help text. What
they never do again is land in `views`.

**They are DDL, and they travel with the schema.**
`Schema_dumper::dump_sequences_ddl()` writes them from the DDL pass, into the
schema's own script rather than a file of their own. That is not a shortcut: a
table can carry `DEFAULT NEXT VALUE FOR <seq>`, so the sequences have to exist
before any `CREATE TABLE` runs, and the schema script is the first thing the
loader executes — before table DDL, in the first of the three schema-DDL waves.
It also means the multifile-DDL layout (`Capability::MULTIFILE_SCHEMA_DDL`, which
only libraries trigger and which therefore can never happen on MariaDB) needs no
new member. They take no `INSERT`, no chunk and no lock, matching
`IGNORE_SEQUENCE_TABLE`.

**`dump_sequences()` on `Dump_options` says who dumps them**, not whether the
user wants them: `true` for `dumpSchemas` / `dumpInstance` / `dumpTables`,
`false` for `exportTable`. There is deliberately no `sequences: false` toggle
next to `events` / `routines` / `libraries` — a sequence is part of the
structure of a schema the way a view is, and a table that defaults to one cannot
be restored without it. `dumpTables` dumps a sequence named on its command line,
which is what `mysqldump db seq` does.

**Load side needs the name list, not a script.** The per-schema metadata gains a
`sequences` array — written only when it is non-empty, so a dump from a server
with no sequences is byte-identical to before. From it the loader gets `DROP
SEQUENCE IF EXISTS` for `dropExistingObjects` (scheduled after tables, which may
depend on a sequence) and the pre-existing-object check, which has to go to
`I_S.SEQUENCES`: the existing tables query cannot see them, because a dumped
sequence is never in the list of tables.

**One parser extension.** `Sql_transform::add_execution_condition()` recognises
`CREATE|ALTER|DROP SEQUENCE` for free once `SEQUENCE` joins its type list, but
`DO SETVAL(...)` is neither a CREATE nor a DROP. Left alone, excluding a
sequence on load would drop its `CREATE` and then run its `SETVAL` against a
sequence that does not exist. So a `DO` + `SETVAL` statement is now reported as
a `SEQUENCE` statement carrying the name inside the parentheses, and the two are
filtered together.

### 16.3 Verified

Live, MariaDB 12.3.2 (sandbox on 3313) and MySQL 9.7.1 (3314):

- **Round trip with the position intact.** A schema with `s1` (`START WITH 100
  INCREMENT BY 5 NOCACHE`, three rows inserted through `DEFAULT (NEXT VALUE FOR
  s1)`), a cycling `s2`, and the table: dump, `DROP DATABASE`, load. `s1` comes
  back at `115` and the next insert takes `115` — no gap, no duplicate. The
  table's `DEFAULT nextval(seqtest.s1)` resolves at `CREATE TABLE` time, which
  is the ordering claim above being exercised.
- Definitions survive exactly: `START_VALUE`, `MINIMUM_VALUE`, `MAXIMUM_VALUE`,
  `INCREMENT`, `CYCLE_OPTION` and `CACHE` all match across seven sequences.
- A cycling sequence sitting past its maximum (`next_not_cached_value` 1001 with
  `MAXVALUE 1000`) restores to 1001 rather than erroring.
- `dropExistingObjects` drops and recreates them; a second load without it
  reports ``Schema `seqtest` already contains a sequence named `s1` ``.
- Filtering works from both ends: `--exclude-tables=seqtest.q1,seqtest.q2` on
  the dump reports `3 out of 5 sequences` and writes neither; on the load it
  suppresses the `CREATE` **and** the `DO SETVAL`, with no error from the
  orphaned statement.
- `dumpTables seqtest s1` dumps the sequence (`0 tables and 0 views and 1
  sequences`); `dumpTables seqtest t1` is unchanged.
- Unit tests: `Instance_cache_test.filter_sequences` (routing, table filters,
  counts), `Schema_dumper_test.dump_sequences` (expected output, including a
  name created under `sql_mode=ANSI` that needs quoting) and eight new
  `Load_dump.add_execution_condition` cases. `Schema_dumper_test.dump_and_load`
  now round-trips sequences too — it enumerates with `SHOW FULL TABLES` and
  hands the `SEQUENCE` rows to `dump_sequences_ddl`.
- MariaDB build, all dump/load suites: **43 passed, 4 failed** — the same four
  §13.7 lists as pre-existing (`Instance_cache_test.table_columns`,
  `Schema_dumper_test.opt_mysqlaas` / `compat_ddl` / `unknown_collations`),
  confirmed by re-running the same filter with the change stashed. The suites
  need a *clean* server: the 3313 sandbox holds an unrelated schema that trips
  the parser bug below, which fails every `Schema_dumper_test` before this
  phase as well.

### 16.4 MySQL build unaffected

- MySQL build against MySQL, same filter: **49 passed, 0 failed**, with the two
  sequence tests skipped (they require MariaDB ≥ 10.3).
- A MySQL-server dump is unchanged in shape: no `sequences` key in the
  per-schema metadata, no `Dumping sequences` comment block
  (`dump_sequences_ddl()` returns before writing anything), and no change to the
  object counts.
- A **MySQL build reading a MariaDB server** keeps producing MySQL-shaped dumps:
  the 5.6 remap makes `supports_sequences()` false, so `filter_tables()` records
  no sequence and nothing about them reaches the dump. That path used to abort;
  it now completes and omits them, which is upstream's lossy MariaDB → MySQL
  migration path behaving as documented.

### 16.5 Not done here

Nothing outstanding on the MariaDB → MariaDB path; these are the edges around it.

- **A MySQL build omits sequences without saying so.** Correct for MariaDB →
  MySQL, which cannot express one — but it deserves a `Compatibility_issue`
  rather than silence. That needs the compatibility machinery, which is
  MySQL-only surface (§4.11), and the migration direction is outside the port's
  vendor → vendor scope (§6).
- **No dump capability is claimed.** Sequence DDL is ordinary SQL inside the
  schema script, so an older shell loading such a dump still creates them; it
  only misses the drop and duplicate-object handling the `sequences` metadata
  drives. Nothing in the layout requires a `Capability` entry.
- **`cycle_count` is not carried.** It records how many times a cycling sequence
  has wrapped; `mysqldump` does not restore it either, and `SETVAL` cannot set
  it.
- ~~**`dumpTables` of a table whose `DEFAULT` names a sequence does not pull the
  sequence in.** The table filters select exactly what was asked for, as they do
  for every other dependency; the resulting dump fails to load unless the
  sequence is named too.~~ **Addressed in §24**, where it turned out to be the
  smaller half of the problem: the reference also carried the source schema's
  name. The filter still selects exactly what was asked for, but it now says so.
- ~~**Unrelated bug found while testing (pre-existing, not sequences).** A view
  whose column alias contains embedded backticks — `` `format_name`(t1.f_name,
  t1.l_name) ``, which the server stores as an alias containing doubled
  backticks — makes `dumpSchemas` of that schema throw ``mismatched input
  '`format_name`'``. This is the §7.3 `supports_view_table_usage` fallback:
  MariaDB has no `I_S.VIEW_TABLE_USAGE`, so the shell parses `VIEW_DEFINITION`
  itself and its parser rejects that alias. Reproduces with this phase
  stashed.~~ **FIXED (§22)** — the lexer, not the fallback: its quoted-identifier
  rule was written from the string-literal rules.

---

## 17. Phase 5b — done

Landed 2026-08-18. Goal was check constraints, which §4.5 listed as a missing
object type needing "net-new work" on `I_S.CHECK_CONSTRAINTS`.

**That framing was wrong, and the real gap is on the other side of the port.**
Check-constraint DDL already round trips byte-identically (§17.2), so the dumper
needs nothing — no cache map, no metadata, no filters, no `I_S.CHECK_CONSTRAINTS`
query. What was broken is the **load**: `util.loadDump` failed with
`MySQL Error 4025 (23000): CONSTRAINT 't1.a' failed` on a dump it had produced
itself, minutes earlier, from a perfectly legal MariaDB table.

### 17.1 Why MySQL never needed this, and MariaDB does

Both vendors have CHECK constraints. What differs is the shape of the escape
hatch — measured on MySQL 9.7.1 and MariaDB 12.3.2:

| | MySQL 9.7.1 | MariaDB 12.3.2 |
|---|---|---|
| Session switch for enforcement | **none** — no `%check%constraint%` variable exists | `check_constraint_checks` |
| `INSERT` of a row violating an enforced constraint | refused, error 3819 | **accepted** while the switch is off |
| `ALTER TABLE ... ADD CONSTRAINT` over violating rows | refused, error 3819 | **accepted** while the switch is off |
| Per-constraint escape hatch | `[NOT] ENFORCED`, emitted by `SHOW CREATE TABLE` as `/*!80016 NOT ENFORCED */` | **no such syntax** (error 1064) |
| Turning enforcement back on | validates existing rows, refuses if any violate | n/a |

**MySQL's escape hatch is DDL.** Non-enforcement lives in the constraint itself,
so it travels inside the dumped `CREATE TABLE` and a reload reproduces the exact
enforcement state for free. And since an enforced constraint can never be
violated, MySQL guarantees the invariant *every row in a dump satisfies every
enforced constraint in that same dump*. There is nothing for a dump tool to do,
which is why there is no upstream code here to port.

**MariaDB's escape hatch is a session variable**, and there is no way to say "this
constraint is not enforced" in DDL at all. So a MariaDB table can sit,
legitimately and indefinitely, holding rows that its own `SHOW CREATE TABLE`
rejects — and nothing in the dump can record that. The only place the state can
be restored is the loading session. MariaDB's own `mariadb-import` does exactly
this, unconditionally, for every import
(`client/mysqlimport.cc:938`: `/*M!100200 set check_constraint_checks=0*/`),
right beside the `foreign_key_checks` and `unique_checks` the Shell already
mirrors.

### 17.2 What already worked — verified, not assumed

Round-tripped a table carrying every check-constraint shape MariaDB emits:
column-level (`` `a` int(11) DEFAULT NULL CHECK (`a` > 0) ``), named table-level,
an anonymous one (which the server names `CONSTRAINT_1`), a clause containing a
function call (`json_valid`), and a clause containing commas, nested parentheses
and the quoted keyword `'KEY'` — alongside a virtual generated column, a
`UNIQUE KEY` and a secondary index. `SHOW CREATE TABLE` before and after
`dumpSchemas` + `loadDump` is **byte-identical**, so:

- no `I_S.CHECK_CONSTRAINTS` is needed, and MariaDB's `LEVEL` column
  (`'Column'` / `'Table'`, which MySQL's copy of that table lacks) is not needed
  either;
- `compatibility::check_create_table_for_indexes()` — the one place the Shell
  re-parses `CREATE TABLE` text — leaves check constraints alone, deferring only
  real indexes;
- the `opt_mysqlaas` / `opt_force_innodb` DDL rewriting does not strip them.

### 17.3 The change

`supports_check_constraint_checks()` (MariaDB ≥ 10.2 · MySQL false — the same
threshold `mariadb-import` gates on) joins §7.3, and two sessions act on it:

- **`Dump_loader::create_session()`** — the single factory for the loader's main
  and worker sessions, so the switch covers the data load and any `ALTER TABLE`
  the loader issues. Sent only to a MariaDB target: verified with `--log-sql=all`
  that a MariaDB target receives it six times (once per session, matching
  `unique_checks = 0`) and a MySQL target receives it zero times while still
  getting `unique_checks = 0` six times.
- **`import_table::Load_data_worker::init_session()`** — `util.importTable` is
  the Shell's `mariadb-import`, and imported data has the same problem. This is
  also the session the loader's MySQL-only `BULK LOAD` path uses. The vendor and
  version come from the session handshake, which is cached client side, so the
  gate costs no round trip.

Documented in `importTable`'s help next to the other two switches, following the
§15 precedent of stating MariaDB behaviour inline rather than forking the text.

### 17.4 Verified

Live, MariaDB 12.3.2 (3313) and MySQL 9.7.1 (3314):

- **The failing case now passes.** A table with a column-level and two
  table-level constraints plus one row inserted under
  `check_constraint_checks=0`: `dumpSchemas` then `loadDump` completes with zero
  errors, both rows are back — the violating one included — and the DDL is
  intact. Before the change this was `ERROR 4025` and an aborted load.
- `util.importTable` of a row that violates two constraints now succeeds
  (`Records: 1 ... Warnings: 0`) instead of failing.
- MariaDB build, all dump/load suites: **59 passed, 4 failed** — the same four
  §13.7 lists as pre-existing. MySQL build: **65 passed, 0 failed**, with the
  MariaDB-only tests skipped.
- New tests: `Load_dump.supports_check_constraint_checks` pins both halves of the
  predicate (no MySQL version, including 8.0.16 which does have CHECK
  constraints, may be sent the variable) and
  `Schema_dumper_test.dump_table_check_constraints` pins that the dumper emits
  the clauses verbatim even with the compatibility rewriting on.

### 17.5 Not done here

- **`deferTableIndexes` hangs on MariaDB, and it is unrelated to this phase.**
  **Fixed straight after this phase — see §18.** Found while checking whether
  deferred index rebuilding mangles check constraints. It does not — but `loadDump ... --defer-table-indexes=all` never
  returns on MariaDB, while the same dump loads in ~5s on MySQL. Diagnosed:
  every server connection is idle, so the wait is client side, and the debug log
  ends with `Monitoring thread: Query returned fewer rows than expected`. The
  monitoring thread runs
  `SELECT CAST(VARIABLE_VALUE AS UNSIGNED) FROM performance_schema.global_status
  WHERE VARIABLE_NAME='Innodb_rows_inserted'` through `fetch_one_or_throw()`, and
  **MariaDB has no `Innodb_rows_inserted` status variable at all** — not in
  `I_S.GLOBAL_STATUS`, not in `performance_schema.global_status` (which is
  populated: 384 rows), not in `SHOW GLOBAL STATUS`, and there is no
  `INNODB_METRICS` counter for it either. So the throw kills the monitoring
  thread and the load never completes. This is the same
  `fetch_one_or_throw`-on-a-variable-MariaDB-lacks class as the phase 0 crashes:
  the fix is to tolerate the absent row and let the throughput label degrade,
  not to find an equivalent counter. Recorded as §8 "still open" item 2.
- **No warning when a restored table holds violating rows.** The load reproduces
  the source faithfully and silently, which is the §6 fidelity goal and matches
  `foreign_key_checks`; but unlike a foreign key, nothing will ever re-validate
  it. A note listing such tables would be an improvement.
- **`NOT ENFORCED` is untranslated**, deliberately. A MySQL dump carrying
  `/*!80016 NOT ENFORCED */` cannot load into MariaDB, which has no such syntax
  — cross-vendor, refused up front since phase 2 (§13.5), so no rewriting is
  attempted.

---

## 18. `deferTableIndexes` hung the load — fixed

Landed 2026-08-18, between phases 5b and 5c. Not an object-type phase: a defect
§17 tripped over, fixed on its own because the symptom is a hang.

`util.loadDump` with `deferTableIndexes: "all"` never returned on MariaDB. It
did all the work first — schema DDL, table DDL, data, and the indexes
themselves, all correct — printed `Building indexes...`, and then sat forever.
The same dump loaded in about five seconds on MySQL.

### 18.1 Three defects stacked

A `sample` of the hung process shows the deadlock rather than a slow query, and
naming all three parts matters because only the first is MariaDB-specific:

1. **A MySQL-only status variable, read with `fetch_one_or_throw()`.** The
   "Loading data" stage registers a monitor which reads
   `Innodb_rows_inserted` from `performance_schema.global_status` to compute
   rows/s. **No MariaDB has that status variable** — 12.3.2 exposes
   `Rows_read`, `Rows_sent`, `Rows_tmp_read` and the `Innodb_row_lock_*` family,
   but no InnoDB DML row counters anywhere: not in `I_S.GLOBAL_STATUS` (560
   rows), not in `performance_schema.global_status` (384 rows, so the table is
   populated), not in `SHOW GLOBAL STATUS`, and there is no `INNODB_METRICS`
   counter either. The query therefore returns no row and
   `fetch_one_or_throw()` throws. Same class as the phase 0 crashes: a
   MySQL-shaped probe for something MariaDB does not have.
2. **One throwing monitor ended the monitoring thread for good.**
   `Monitoring::monitoring_thread()` wrapped its whole `while (!m_terminating)`
   loop in a single `try`, so the first throw escaped the loop, was logged once
   as `Monitoring thread: ...`, and the thread returned. That also silently
   disabled `kill_queries()` — the mechanism a hard interrupt uses to cancel
   worker queries — for the rest of the load.
3. **The index stage's completion depended on that thread.** A numeric progress
   stage finishes *itself* when `current >= total`
   (`Spinner_progress::on_update()` → `finish(false)`), and "Building indexes"
   had `current` reading `m_indexes_recreated` — a mirror of the real counter
   which only the monitoring thread published. With the thread dead the mirror
   stayed at 0 while `total` was 2, so the stage never finished. The progress
   thread runs stages **serially from a queue**, so it stayed inside that
   stage's `display()` loop and never reached the "Executing view DDL" stage
   queued behind it, while the main thread blocked in that stage's `finish()` →
   `wait_for_display_done()`. Deadlock, with every server connection idle.

### 18.2 The fixes, and why none of them is vendor-gated

- **Fall back to counting rows on the client** when `Innodb_rows_inserted`
  cannot be read — which is exactly what the `BULK LOAD` branch beside it
  already does for its own reason (that path does not update the variable
  either). The source is chosen on the first tick and then left alone: a
  *transient* failure after a successful sample skips one sample instead of
  switching source mid-load, so the rate is never computed from two different
  scales. MariaDB now reports a real throughput figure rather than none.
- **Drop a monitor which throws, do not end the thread.** Per-monitor
  `try`/`catch` in `Monitoring::monitor()`, logging once and erasing the
  offender, so one unreadable variable cannot take progress reporting *and*
  interrupt handling down with it.
- **Finish the index stage from the counter the workers keep.** `current` now
  reads `m_indexes_completed` under its own mutex, and `m_indexes_recreated` is
  deleted — it was only ever a lagged copy of that counter, and `config.current`
  was its only reader (the summary line already used `m_indexes_completed`).

**All three are vendor-neutral, and two of them fix MySQL too.** Only defect 1
is about MariaDB; 2 and 3 are shared-code fragility. Under MySQL's own defaults
nothing changes — verified below — but:

- a MySQL server running with `performance_schema` **off** has no such table, so
  defect 1's `fetch_one_or_throw()` throws there as well and MySQL hangs in the
  identical way;
- the mirror in defect 3 was only advanced when the *percentage* increased
  (`if (updated_progress > m_indexes_progress)`), and that percentage includes a
  server-side estimate for in-flight statements. If the estimate ever reaches
  100.0 before the last statement completes, the final update is
  `100.0 > 100.0` — false — and MySQL deadlocks exactly as MariaDB did.

Gating either behind a vendor check would have left MySQL holding those, which
is why the fix is in the shared path.

### 18.3 Verified

- **MariaDB 12.3.2**: `loadDump --defer-table-indexes=all` now finishes.
  `Building indexes - done`, `2 indexes were built in 0 sec.`, indexes and rows
  correct. With 200k rows: completes in 13s and reports `17.17K rows/s` from the
  new client-side counting. The log carries exactly one
  `The server does not expose Innodb_rows_inserted, row throughput will be
  counted on the client side.` and **no** `Monitoring thread:` failure.
- **MySQL 9.7.1 is unchanged**: the same deferred load finishes in ~4s with
  `Building indexes - done`, 200k rows report `200.00K rows/s`, and the fallback
  message never appears — it is still reading `Innodb_rows_inserted`. The
  default (non-deferred) load path reports throughput on both vendors.
- MariaDB build, all dump/load suites: **59 passed, 4 failed** — the same four
  §13.7 pre-existing. MySQL build: **65 passed, 0 failed**. Unchanged from §17.

### 18.4 Not done here

- **No regression test.** Reproducing it needs a load that defers indexes
  against a server without `Innodb_rows_inserted` — an end-to-end scenario, and
  those suites are deferred on MariaDB until phase 6, which is where it belongs.
  The `deferTableIndexes` case should be in that set explicitly, since a hang is
  invisible to a suite that only checks results.
- **The index progress *percentage* is still monitor-driven** and still
  disables itself on error (`m_query_index_progress`), which is pre-existing and
  correct: it is a label, and now nothing else depends on it.

---

## 19. Phase 5c — done

Landed 2026-08-18. Goal was §4.5's last missing MariaDB object type:
Oracle-mode `PACKAGE` / `PACKAGE BODY` routines.

**The gap was the same shape as §16's, not the "silently omitted" one §4.5
described: a schema holding a package could not be dumped at all.**
`I_S.ROUTINES` reports a package with `ROUTINE_TYPE='PACKAGE'`, and the routine
loop routed `PROCEDURE` to `procedures` and *everything else* to `functions`. So
a package was cached as a function and `dump_routines_for_db` then ran
`SHOW CREATE FUNCTION` against it:

```
ERROR: Could not execute 'SHOW CREATE FUNCTION `pkgtest`.`pkg1`':
       MySQL Error 1305 (42000): FUNCTION pkg1 does not exist
ERROR: MYSQLSH 52006: While 'Writing schema metadata': Fatal error during dump
```

**Are packages supported now? Yes** — on the vendor → vendor path, both halves
are dumped with their `sql_mode`, character set and definer, restored in an
order that works, selected by the routine filters, dropped by
`dropExistingObjects` and reported by the pre-existing-object check. A **MySQL
build** reading a MariaDB server omits them, which is §16.4's behaviour and out
of scope by §6.

### 19.1 What a package is, measured

On MariaDB 12.3.2. A package has two halves — a specification and an optional
body — and the pair behaves less like one object than it first looks:

| | Measured |
|---|---|
| Where they show up | `I_S.ROUTINES`, `ROUTINE_TYPE` `'PACKAGE'` and `'PACKAGE BODY'`, with the same `SQL_MODE` / `DEFINER` / `CHARACTER_SET_CLIENT` / `COLLATION_CONNECTION` / `DATABASE_COLLATION` columns every routine has |
| `I_S.PARAMETERS` | **no rows** — a package declares no parameters of its own |
| `SHOW CREATE PACKAGE [BODY]` | works in **any** `sql_mode`, and returns the same six columns, in the same order, as `SHOW CREATE PROCEDURE` |
| `CREATE PACKAGE [BODY]` | needs `sql_mode=ORACLE` — error 1064 otherwise |
| `DROP PACKAGE [BODY] IF EXISTS` | works in **any** `sql_mode`; a missing one is a *note*, not an error |
| `DROP PACKAGE` with a body present | drops **both** — the body does not survive its specification |
| Namespace | its **own**: `CREATE FUNCTION pkg1` and `CREATE PACKAGE pkg1` coexist in one schema |
| A specification without a body | legal, and `SHOW CREATE PACKAGE` still returns it |

The two that shaped the design are the last three. Because the namespaces are
separate, a package cannot share the `functions` map keyed by name — which is
what the old routing did, and why one of two objects named `pkg1` was silently
lost even before `SHOW CREATE FUNCTION` failed. And because `SHOW CREATE` and
`DROP` both work outside Oracle mode, only the `CREATE` needs the `sql_mode`
switch the routine dumper already emits.

### 19.2 Design

**Packages are routines, and are dumped by the routine pass.** They are in
`I_S.ROUTINES`, the routine filters select them, and they are counted as
routines. `Schema_dumper::dump_routines_for_db()` gains two entries in its type
list rather than a dump function of its own.

**The order is mysqldump's, and it is not alphabetical.** Following
`routine_dump_param_array` (`client/mysqldump.cc:2950`), the four types are
emitted **PACKAGE, FUNCTION, PROCEDURE, PACKAGE BODY**: a specification may
declare public data types the standalone routines use, so it goes first, and a
body may call those routines, so it goes last. Verified in the dump output and
pinned by a test.

**No `excludePackages` / `includePackages`.** A package is a routine and
`excludeRoutines` selects it, which is also what `mysqldump` does — it has no
package option either. The consequence is that a filter is by *name*, so
excluding `db.pkg1` excludes the package, its body **and** a standalone function
of that name. That is the §16.2 trade again: one filter per namespace collision,
not one option per object type.

**Two name sets in the cache, not `Routine` maps.** `Instance_cache::Schema`
gains `packages` and `package_bodies` as `unordered_set<string>`, because a
package has no parameters and no library references — the two things
`Instance_cache::Routine` exists to carry. `dump_routines_for_db` therefore
skips the parameter/return-value collation handling and the library-dependency
check for them, which is also the only reason `is_package` exists in that loop.

**The DROP is written bare**, `DROP PACKAGE IF EXISTS x;`, not wrapped in a
version comment the way `/*!50003 DROP FUNCTION ... */` is. Three reasons, and
the third is the one that matters: the `CREATE` beside it is written bare too,
so a MySQL server could not read the dump either way; a cross-vendor load has
been refused up front since phase 2 (§13.5); and the shell's own `SQL_iterator`
does not know `/*M!` — it spans it as an ordinary comment — so a `/*M! ... */`
wrapper would hide the statement from `add_execution_condition`, and an excluded
package would have been dropped from the target anyway. (`mysqldump` writes
`/*!50003 DROP PACKAGE ... */`, which a MySQL server would try to execute and
fail on, so there was no correct precedent to copy.)

**Load side takes the name lists.** The per-schema metadata gains `packages` and
`packageBodies`, written only when non-empty, so a dump from a server with no
package is byte-identical to before. From them the loader gets the
pre-existing-object check — which has to ask `I_S.ROUTINES` for the type
explicitly, since a package and a function may share a name and the function
query would report the wrong object — and `DROP PACKAGE [BODY] IF EXISTS` for
`dropExistingObjects`, with the bodies scheduled *before* the specifications so
the cascade does not make the second statement a no-op.

**One parser extension.** `Sql_transform::add_execution_condition()` reads the
object type as a single token, and `PACKAGE BODY` is two. It now consumes the
`BODY` when it follows `PACKAGE` and reports the type as `PACKAGE BODY`; an
unquoted `BODY` can only be the keyword, because a package actually named `body`
is written quoted. Both types map to `include_routine`.

### 19.3 Verified

Live, MariaDB 12.3.2 (sandboxes on 3313 and 3315) and MySQL 9.7.1 (3314):

- **The failing case now passes.** The schema that aborted the dump — `pkg1`
  specification + body, a `specOnly` specification with no body, a standalone
  function *also* named `pkg1`, plus a plain function, procedure and table —
  dumps, and after `DROP DATABASE` + `loadDump` all six routines are back.
  `pkgtest.pkg1.f1(5)` returns `15` (the body's package variable survived) and
  `pkgtest.pkg1(7)` returns `7`, so the two namespaces came back separate.
- `dropExistingObjects` drops and recreates them; a second load without it
  reports ``already contains a package named `pkg1` ``, ``a package named
  `specOnly` `` and ``a package body named `pkg1` `` alongside the table,
  function and procedure.
- Filtering works from both ends. On the dump, `--exclude-routines=pkgtest.pkg1`
  reports `3 out of 6 routines` and writes neither the package, its body, nor
  the function. On the load it suppresses the `CREATE` **and** the `DROP`:
  verified by planting an unrelated `pkg1` package in the target first and
  confirming it was still callable afterwards.
- `util.copySchemas` carries them: 3313 → 3315 with a schema rename reproduces
  all six routines and the package call still returns `15`.
- Unit tests: `Instance_cache_test.filter_packages` (routing, the shared routine
  filter, the function/package name collision, counts),
  `Schema_dumper_test.dump_packages` (expected output for a specification, a
  body, a specification-only package under a name needing quoting, and the
  PACKAGE → routines → PACKAGE BODY ordering), eight new
  `Load_dump.add_execution_condition` cases and a filtering block beside them.
  `Schema_dumper_test.dump_and_load` now replays the package DDL through
  `mysqlsh --sql -f` as well — it asserts nothing about packages specifically
  (it only checks tables), but it does catch DDL that will not reload. The
  fixture itself survives the client-side splitter only because that splitter
  spans `/*M! ... */` as one comment, semicolons and all.
- MariaDB build, all dump/load suites: **78 passed, 4 failed** — the same four
  §13.7 lists as pre-existing (`Instance_cache_test.table_columns`,
  `Schema_dumper_test.opt_mysqlaas` / `compat_ddl` / `unknown_collations`).

### 19.4 MySQL build unaffected

- MySQL build against MySQL, same filter: **87 passed, 0 failed**, with the two
  package tests skipped (they require MariaDB ≥ 10.3).
- A MySQL-server dump is unchanged in shape: no `packages` or `packageBodies`
  key in the per-schema metadata, no change to the routine section, no change to
  the object counts.
- A **MySQL build reading a MariaDB server** keeps producing MySQL-shaped dumps:
  the 5.6 remap makes `supports_packages()` false, so `routines()` records no
  package and nothing about them reaches the dump. That path used to abort; it
  now completes and reports `1 out of 4 routines`, which is upstream's lossy
  MariaDB → MySQL migration path behaving as documented.

### 19.5 Not done here

- **A MySQL build omits packages without saying so**, and its routine count goes
  from `4 routines` to `1 out of 4 routines` without naming what the other three
  were. Correct for MariaDB → MySQL, which cannot express a package, but it
  deserves a `Compatibility_issue` — the same §16.5 item, and blocked on the same
  MySQL-only compatibility machinery (§4.11).
- **`SQL_iterator` still does not know `/*M!`.** It spans one as an ordinary
  comment, so anything the port writes inside a MariaDB version comment is
  invisible to the statement filters. That is why §19.2 writes the package DROP
  bare. Teaching the lexer would be the deeper fix, but `/*M! ... */` really *is*
  an ordinary comment to a MySQL server, so the change cannot simply be made in
  the shared path — it would need the same vendor plumbing §7.1 gave the dumper.
- ~~**`GRANT EXECUTE ON PACKAGE` is not carried**, because no grant is~~ —
  **done in §20**, and it needed a parser fix of its own (§20.3).
- **A package with no body is restored with no body**, which is faithful. But
  the target then has a specification nothing implements, and neither the dump
  nor the load says so — the same silence §17.5 notes for a table holding rows
  its constraints reject.

---

## 20. Phase 5d — done

Landed 2026-08-18. §4.2's and §4.6's users, roles and grants: `users: true` on a
MariaDB source used to be refused outright with 52037.

**Are accounts supported now? Yes** — on the vendor → vendor path, the whole ACL
state round-trips: every `SHOW CREATE USER` attribute (password hashes, `IDENTIFIED
VIA … OR …` multi-auth, `ed25519`, `unix_socket`, `REQUIRE SSL`, resource limits,
`ACCOUNT LOCK`, `PASSWORD EXPIRE`), roles with their own DDL, the role graph,
`WITH ADMIN OPTION`, default roles, `GRANT EXECUTE ON PACKAGE [BODY]` and the
implicit `PUBLIC` role. `dumpInstance` → `loadDump` and `copyInstance` both
reproduce a source instance's accounts so that `SHOW CREATE USER` + `SHOW GRANTS`
for every account is byte-identical on the target.

**52037 stays.** It was upstream's (BUG#34049624), not the port's, and it still
answers a real question: a **MySQL-shaped** dump cannot express a MariaDB
account. Its condition moved from "the source is MariaDB" to "the dump is not in
MariaDB's dialect" — `is_maria_db && !is_maria_db_dialect(v)`, which on a MySQL
build is the same thing (the 5.6 remap of §13.1 is unconditional there) and on a
MariaDB build is never true. The message and the test asserting it are untouched.

### 20.1 What a MariaDB role is, measured

On MariaDB 12.3.2. Everything predicted about `SHOW CREATE USER` and the auth
plugins turned out either wrong or harmless; the role model is where the work is.

| | Measured |
|---|---|
| Where a role lives | `mysql.user`, `is_role='Y'`, with an **empty host** — so `SELECT DISTINCT user, host FROM mysql.user` already returns roles alongside accounts |
| Namespace | its **own**: `` `r` `` the role and `` `r`@`%` `` the user coexist, and `DROP USER 'r'@'%'` leaves the role standing |
| `SHOW CREATE USER` for a role | **error 1133**, "Can't find any matching row in the user table" — in *every* spelling: `` `r` ``, `'r'@'%'`, `` `r`@`` `` |
| `SHOW GRANTS FOR` a role | works only **hostless**: `` SHOW GRANTS FOR `r` `` yes, `SHOW GRANTS FOR 'r'@''` → error 1141 |
| `DROP USER` on a role | reports **success** and does nothing. Only `DROP ROLE` removes one |
| `CREATE ROLE IF NOT EXISTS` / `DROP ROLE IF EXISTS` | both exist |
| `WITH ADMIN` | not in any `CREATE ROLE` output — it comes back as `` GRANT `r` TO <admin> WITH ADMIN OPTION `` in the *administrator's* `SHOW GRANTS` |
| `PUBLIC` | a real `mysql.user` row with `is_role='Y'`, appearing once it holds a grant — but `CREATE ROLE PUBLIC` is **error 1959** |
| `information_schema.USER_PRIVILEGES` | lists every user and **not one role**: a role holds only `USAGE`, and I_S has no row for that |
| `DENY` (§12.2) | **not in 12.3.2** — `DENY DELETE ON db.* TO u` is a syntax error, so the `parse_grant` handling phase 1 added is still future-proofing |
| `activate_all_roles_on_login`, `mandatory_roles`, `partial_revokes` | none exist |
| `mysql.user.account_locked` | **does not exist** — the lock state lives in the `global_priv` JSON and surfaces only through `SHOW CREATE USER` |

Two more, about the statements rather than the objects:

- **`SHOW GRANTS FOR` a role is transitive.** It walks the role graph down and
  emits the grants of every role granted to it, *under their own grantee*. With
  a `baserole → midrole → toprole` chain, `` SHOW GRANTS FOR `toprole` `` returns
  eleven statements, eight of which grant to `midrole` or `baserole`. Verified
  **not** to happen for a plain user: a user's `SHOW GRANTS` lists the roles it
  was granted, not what those roles carry.
- **The default role arrives as a statement, not a clause.** MySQL 8.0 puts
  `DEFAULT ROLE` inside `SHOW CREATE USER`; MariaDB emits a whole
  `` SET DEFAULT ROLE `r` FOR `u`@`h` `` as the last line of `SHOW GRANTS`, and
  **rejects the `TO` spelling** MySQL uses.

And the good news, all measured by replaying the dumped text into a second
server: **every statement MariaDB's `SHOW CREATE USER` and `SHOW GRANTS` produce
re-executes verbatim**, including `IDENTIFIED BY PASSWORD '*hash'`,
`IDENTIFIED VIA mysql_native_password USING '…' OR unix_socket`,
`REQUIRE SSL WITH MAX_QUERIES_PER_HOUR 10`, `ACCOUNT LOCK PASSWORD EXPIRE` and
`GRANT SELECT ON db.* TO PUBLIC`. There is no DDL to synthesize for an account —
only for a role, which has none to read.

### 20.2 The failure was the §16 shape again

As with sequences and packages, the gap aborted the dump rather than losing
anything quietly, and for the same reason: an object was addressed as something
it is not.

```
Writing users DDL
ERROR: MySQL Error (1133): Can't find any matching row in the user table
```

`fetch_users()` reads `mysql.user`, which on MariaDB includes the roles, so
`dump_grants` reached `SHOW CREATE USER 'baserole'@''` for the first role on the
instance and died. Any MariaDB instance with a role — which includes every
instance with MRS installed — could not be dumped with `users: true` the moment
52037 stopped guarding the path.

Visible alongside it: **"23 out of 12 users will be dumped"**, because the
*filtered* count came from `mysql.user` and the *total* from
`I_S.USER_PRIVILEGES`, which sees no role.

### 20.3 Design

**A role is an account entry which knows it is a role.** `Instance_cache::users`
keeps holding every row of `mysql.user`, roles included — that is what MySQL does
too, and it means the user filters, the counts and the per-account loop need no
special case. Only `fetch_roles()` changes: on MariaDB it asks `is_role='Y'`
rather than applying MySQL's `authentication_string='' AND account_locked='Y' AND
password_expired='Y'` heuristic, which cannot describe a MariaDB role and would
fail on the missing `account_locked` column anyway.

**Three names per account, not one.** `dump_grants` used to carry one string per
account, `shcore::make_account()`'s `'user'@'host'`. A MariaDB role needs three
different forms, so `Dumped_account` carries them:

| | User | MariaDB role |
|---|---|---|
| `label` — the `-- begin`/`-- end` marker, and hence what the loader filters and drops by | `'u'@'h'` | `` `r` `` |
| `show_target` — after `SHOW CREATE USER` / `SHOW GRANTS FOR` | `'u'@'h'` | `` `r` `` |
| `grantee` — the `I_S.*_PRIVILEGES` grantee, for `expand_all_privileges()` | `'u'@'h'` | `'r'@''` |

The label being hostless is what makes `DROP ROLE IF EXISTS` on the load side a
valid statement — `DROP ROLE IF EXISTS 'r'@''` is a syntax error — and
`shcore::split_account()` still parses it, so `excludeUsers` keeps working
against a role by name.

**A role's DDL is synthesized, because there is none to read.** `CREATE ROLE IF
NOT EXISTS <label>`, and that is the whole statement: a role has no attribute a
`CREATE ROLE` could carry, and even its administrator comes back through
`SHOW GRANTS`. `PUBLIC` gets no create block at all — it cannot be created — but
its grants are still dumped, and they load into a target which has never heard of
it.

**A role's own marker, and a statement type to match.** The block is
`-- begin role` / `-- end role`, and `preprocess_users_script()` maps it to a new
`User_statements::Type::CREATE_ROLE`. The loader treats that type like
`CREATE_USER` when creating and applying grants, and reaches for `DROP ROLE IF
EXISTS` when `dropExistingObjects` drops it. Distinguishing it in the file rather
than sniffing the statement text is what lets `execute_grant_and_drop_account_on_error`
drop the right kind of object too.

**Transitive grants are dropped by grantee, not by position.** `keep_own_grants()`
parses each statement `SHOW GRANTS FOR` a role returned and keeps only the ones
whose grantee is that role. Doing it any other way would be wrong twice over: the
same privilege would be granted several times over a deep role graph, and a
statement granting to a role the user *excluded* would run under a block the
filters had let through — which fails on a target that does not have that role.

**The default role moves to its own block.** MySQL's arrives as a clause of
`SHOW CREATE USER` and `strip_default_role()` lifts it out; MariaDB's arrives as
a finished statement in `SHOW GRANTS` and is moved out of the grants block by
name. `default_roles` therefore holds the **whole statement** now instead of the
clause, which is what lets the two vendors' spellings (`TO` and `FOR`) coexist
without a second code path at the point of writing. It has to leave the grants
block for two reasons: the block must not contain a non-`GRANT` statement, which
`parse_grant_statement()` throws on, and the statement has to run after every
role exists.

**Two privilege checks were on the wrong gate.**
`validate_preflight_privileges()` asked for `SELECT` on `mysql` only when
`is_8_0`, with a comment saying it stays that way until §4.6 lands. It is now
`requires_select_on_mysql_to_dump_users()`, true for MySQL 8.0 and every MariaDB —
which is what `SHOW CREATE USER` and `SHOW GRANTS FOR` another account actually
need there. `requires_super_to_dump_users()` stays MySQL-5.6-only; MariaDB split
`SUPER` up in 10.5 and never wanted it for this.

**`GRANT EXECUTE ON PACKAGE [BODY]` needed the §19 parser fix again.**
`parse_grant_statement()` reads the object type as one token and knew
`TABLE`/`FUNCTION`/`PROCEDURE`/`LIBRARY`. `PACKAGE` fell through to
`split_priv_level("PACKAGE")`, which produced a table-level grant on a schema
called `PACKAGE` — so the grant was reported as invalid, and
`strip_invalid_grants` would have commented it out. It now consumes `PACKAGE`, and
the second `BODY` token when it follows, and reports both as `Level::ROUTINE`:
both halves are routines to `I_S.ROUTINES` and to the routine filters, exactly as
§19.2 arranged.

**`mariadb.sys` joins the always-excluded accounts.** It is the counterpart of
`mysql.infoschema` / `mysql.session` / `mysql.sys` — the internal account owning
the `mysql.user` view over `mysql.global_priv`, locked, and holding grants on
`mysql.global_priv`. The dump side can only exclude it once the vendor is known,
so `Dump_instance_options` gained an `on_set_session()` override; options are
unpacked before the session is set (§8), so this does not disturb the
`excludeUsers` conflict check that counts the constructor's exclusions.

**The pre-existing-object check learned about roles.** `check_existing_users()`
matches the dump's accounts against `I_S.USER_PRIVILEGES` grantees, which on
MariaDB sees every user and no role at all. On a MariaDB target it now also asks
`mysql.user` for `is_role='Y'` and reports `` Role `r` already exists `` — the
same shape §16 and §19 needed, and for the same reason: a role and an account of
one name are two objects, so the query has to say which it means.

### 20.4 Verified

Live, MariaDB 12.3.2 (sandboxes on 3313 and 3315) and MySQL 26.7.0 (3310).

The fixture: users authenticating by password hash, `ed25519`, `unix_socket` and
`mysql_native_password OR unix_socket`; a user with `REQUIRE SSL` and two resource
limits; a user with `ACCOUNT LOCK PASSWORD EXPIRE` and no password; a
`baserole → midrole → toprole` chain with a fourth role whose administrator is a
user; a role and a user of the same name; grants at global, schema, table,
column, procedure, `PACKAGE` and `PACKAGE BODY` level; `WITH GRANT OPTION`,
`WITH ADMIN OPTION`, a default role, and a grant to `PUBLIC`.

- **The failing case now passes.** `dumpInstance` with `users: true` completes
  where it aborted on 1133, and reports `16 out of 24 users` — the two counts on
  the same footing at last.
- **The ACL state round-trips exactly.** `SHOW CREATE USER` plus `SHOW GRANTS`
  for every account, dumped from source and target and diffed, is **identical**
  after `dumpInstance` + `loadDump`, after a second load with
  `dropExistingObjects`, and after `copyInstance`.
- **Live, not just textually.** Connecting as the restored user with its original
  password works, `SELECT current_role()` returns the default role, and a
  `SELECT` reachable only through `toprole → midrole → baserole` succeeds — so
  the whole chain came back, not just its DDL.
- **`dropExistingObjects` really drops a role.** Proved by planting an extra
  `GRANT DELETE` on a target role first: after the load it is gone, which a
  `DROP USER` could not have achieved (it is a no-op on a role, §20.1).
- **`excludeUsers` works on a role from both ends.** On the load it prints
  `Skipping CREATE ROLE statements for user` and `Skipping GRANT/REVOKE`, and a
  role planted on the target keeps its unrelated grant — nothing was dropped or
  recreated. On the dump the role is omitted and the dangling role grants are
  reported: ``User `midrole` has a grant statement on a role `baserole` which is
  not included in the dump``.
- **The duplicate-object check reports a pre-existing role**, found for real: an
  earlier run had aborted partway and left two roles behind, and the next load
  named both.
- Unit tests: `Instance_cache_test.maria_db_roles` (role enumeration, the
  role/user name collision, the counts, filtering a role on its own),
  `Schema_dumper_test.dump_maria_db_roles` (expected output for the create
  blocks, the transitive trimming at two depths, the `FOR` spelling of the
  default role, and the statement types the loader reads back), and three
  `Compatibility_test.parse_grant_statement` cases for
  `GRANT EXECUTE ON PACKAGE [BODY]`.
- MariaDB build, all dump/load suites: **94 passed, 4 failed** — the same four
  §13.7 lists as pre-existing (`Instance_cache_test.table_columns`,
  `Schema_dumper_test.opt_mysqlaas` / `compat_ddl` / `unknown_collations`).

### 20.5 MySQL build unaffected

- MySQL build against MySQL 26.7.0, same filter: **103 passed, 0 failed**, with
  the four MariaDB-only tests skipped.
- A MySQL-server dump is unchanged in shape: same `-- begin user` blocks, same
  `SHOW CREATE USER` text, and the default role still written as
  ``SET DEFAULT ROLE `r`@`%` TO 'u'@'h'`` — verified against a MySQL role and a
  MySQL account with a default role. Every MariaDB branch in `dump_grants` is
  behind a predicate that is false for a MySQL server, and for a MySQL build even
  a MariaDB server is remapped to 5.6 (§13.1), so none of them can be reached.
- **52037 still fires on the MySQL build** for a simulated MariaDB source, so
  BUG#34049624's test is untouched: `!is_maria_db_dialect(v)` is always true
  there.

### 20.6 Not done here

- ~~**An auth plugin missing on the target aborts the load.** MariaDB's `ed25519`,
  `gssapi` and `pam` are loadable plugins, and `CREATE USER … IDENTIFIED VIA
  ed25519` fails with 1524 on a server that has not installed one — measured, and
  the fix is `INSTALL SONAME 'auth_ed25519'` on the target. The loader has
  exactly this handling already (warn, skip the account, continue) but only for
  MHS, where a plugin *cannot* be installed. Aborting is the safer default —
  silently skipping an account is a privilege change nobody asked for — but the
  error should say what to install, and the load has by then created some of the
  accounts.~~ **Addressed in §25** — it still aborts, for the reason given here,
  but it now says what to install and what it already did.
- **`PUBLIC`'s grants are not dropped by `dropExistingObjects`,** because there
  is no account to drop. A target's pre-existing `GRANT … TO PUBLIC` therefore
  survives a load that was asked to replace everything. Correct in the narrow
  sense — `DROP ROLE PUBLIC` is not a statement — but it is the one account whose
  grants are additive.
- **The skip notes say "user" for a role**: `Skipping CREATE ROLE statements for
  user \`r\``. The wording comes from the shared path in
  `preprocess_users_script()`, which is also what makes it worth leaving alone.
- **`I_S.USER_PRIVILEGES` is still the fallback** when `mysql.user` is
  unreadable, in both `fetch_users()` and `count_users()`. An account without
  `SELECT` on `mysql` cannot dump users at all now (§20.3), so the fallback is
  reached only for the count — where it undercounts by the number of roles, as
  before. Not worth a second query path.
- **A DENY is not carried**, because 12.3.2 cannot produce one (§20.1). When it
  can, `parse_grant`'s handling from §12.2 is on the privilege-*reading* side
  only; `dump_grants` has never seen one.
- **Column statistics (`mysql.column_stats`) are still not dumped** — §4.5's last
  open item, and the only remaining piece of §4.5/§4.6. It is data, not ACL, so
  it did not belong here. Now a **documented limitation** rather than pending
  work, with what it costs measured — §23.

---

## 21. Phase 6 — in progress

Started 2026-08-19. Goal is §9's last row: the end-to-end suites, deferred on
MariaDB since §12.6. The component-level tests are **done** — the four failures
every phase since §13.7 has carried are gone (§21.5) — and the scripted suites are
being brought up one at a time (§21.8).

Two things had to be fixed before a single scripted suite could run at all, and
neither was a test:

- **A stale sandbox boilerplate outranked a freshly built one.** The `sandbox`
  plugin bootstraps one data directory per server version and copies it per
  sandbox. The reuse check required a version stamp, but the *cleanup* of an
  unstamped directory sat inside the same branch, so a boilerplate left behind by
  an interrupted `mariadb-install-db` was never removed — and the publish step
  then discarded the newly built one as if a concurrent deploy had won a race.
  Every sandbox came up with an empty `mysql` schema and the deploy timed out with
  `Can't open and lock privilege tables: Table 'mysql.db' doesn't exist`.
  `_boilerplate_is_complete()` now decides both, and six unit tests in
  `test_unit_runtime.py` pin it.
- **`testutil.import_data()` needs a working client binary.** It shells out to
  `mysql`/`mariadb` from `PATH` to load the SQL fixtures, and the client in the
  MariaDB tarballs links `libgnutls`, which was missing locally — every suite died
  in its Setup chunk. An environment fix (`brew install gnutls` here), but worth
  knowing: the *server* binaries do not need it, so sandboxes deployed fine while
  every suite failed.

### 21.1 `__server_is_maria_db` — the gate the scripts were missing

The scripted suites decide what to run from `__version_num`, reading it as "has
what MySQL 8 has". MariaDB's numbering is its own, so on a MariaDB server every
one of those checks answers yes, and the suites then use MySQL-only variables,
plugins, statements and privileges. That is the same trap §7 fixed in the product
code, and it needs the same fix in the tests: `unittest/shell_script_tester.cc`
now defines **`__server_is_maria_db`** from `target_server_is_maria_db()`, beside
`__version_num`.

It is keyed on the **server**, like every product gate since §13 — a Shell built
against either vendor can be pointed at either server. `__mariadb_build` is
reserved for the two differences that really do come from the linked client
library (§21.4).

Chunk conditions compose, so a MySQL-only case becomes
`#@<> title {VER(>=8.0.24) and not __server_is_maria_db}`.

### 21.2 MySQL's version-gated comments are inert on MariaDB, not fatal

The suites make MySQL-only statements conditional with executable comments —
`/*!80021 alter instance disable innodb redo_log */`,
`CREATE SCHEMA … /*!80016 DEFAULT ENCRYPTION='Y' */`,
`REVOKE RELOAD /*!80023 , FLUSH_TABLES */`, `/*!80013 DEFAULT (RAND() * RAND())*/`,
`/*!90200 CREATE LIBRARY …*/`. The obvious reading is that MariaDB, reporting
120302, satisfies every one of those thresholds and runs them all.

**It does not.** Measured on 12.3.2: MariaDB executes `/*!VERSION … */` for
`VERSION` up to **50600** and again from **100000** — its own numbering — and
*skips* everything in between, which is exactly the MySQL 5.7 … 9.x range. So
every one of those statements is silently a no-op there, and none of them needed a
vendor check. (`/*M!VERSION … */` is the mirror image: only MariaDB executes it,
which is what the port's own DDL uses.)

What that does change is the **fixtures**: on MariaDB the "encrypted" schema is not
encrypted, the expression defaults are absent, no histogram is created and no
library exists — so assertions about those *effects* still have to be gated, even
though nothing raised an error. `/*!32312 IF NOT EXISTS*/`, which the dumper itself
writes, is below the cutoff and runs on both vendors, so expected-output strings
carrying it need no change.

### 21.3 The shared helpers were the biggest lever

Every suite calls `wipeout_server()` between cases, and it alone accounted for
most of the early failures:

| | Was | Now |
|---|---|---|
| `is_dynamic_data_masking_enabled()` | `SELECT … FROM mysql.component` for any server ≥ 9.7 by number, i.e. every MariaDB | false for MariaDB - there are no components |
| `wipeout_users()` | dropped `mariadb.sys`, which owns the `mysql.user` view, so every later query failed with 1446 "definer does not exist" | reserved like `mysql.session` and friends, and MariaDB's roles are dropped with `DROP ROLE` (a `DROP USER` on a role reports success and does nothing, §20.1) |
| `reset BINARY LOGS AND GTIDS` | a syntax error on MariaDB, and issued even with no binary log | `RESET MASTER` on MariaDB (`get_reset_binary_logs_keyword()` and its three siblings - the binary-log-status, replication-source and replication-option keywords - are vendor-aware now), skipped when `@@log_bin` is off |
| the GTID state | `RESET MASTER` does not clear `gtid_slave_pos`, so a target kept the position an earlier case restored and `updateGtidSet: 'append'` was then correctly refused | `SET GLOBAL gtid_slave_pos = ''` alongside the reset |
| `snapshot_accounts()` | `SHOW CREATE USER` for every `mysql.user` row - error 1133 on a role | a role is snapshotted by name and grants alone |
| `snapshot_libraries()` | `information_schema.libraries` for any server ≥ 9.2 by number | `instance_supports_libraries` is false for MariaDB |
| `get_ssl_config()` | client certificates from the data directory, where MySQL auto-generates them | next to the server certificate the instance is configured with, which holds for both layouts |

### 21.4 Two differences that are the client library's, not the server's

Both are gated on `__mariadb_build`, and they are the only cases where that is the
right question to ask:

- **`net-buffer-length` does not influence sub-chunking.** libmysqlclient sizes
  the `LOAD DATA LOCAL` read callback from the connection's net buffer
  (`MY_ALIGN(net.max_packet - 16, IO_SIZE)`, `libmysql.cc`); libmariadb hands it a
  fixed 4096-byte buffer (`ma_loaddata.c`). `util_copy_trx`'s rows are crafted to
  sit exactly on the transaction limit, so the smaller reads put one of them into
  a sub-chunk of its own: 10 sub-chunks where MySQL gets 9. The data is
  checksummed either way, and `maxBytesPerTransaction` still bounds every
  transaction — only the split points move.
- **A refused connection is worded differently.** libmariadb reports 2002
  `Can't connect to server on '<host>'` where libmysqlclient reports 2003
  `Can't connect to MySQL server on '<host>:<port>'`. `cannot_connect_error()`
  returns the pair.

And the same 4096-byte buffer cost the loader a **diagnostic**, which is a product
fix rather than a test one. `Transaction_buffer::consume()` decided that a row was
longer than `maxBytesPerTransaction` by comparing *one read* against the limit —
true only where the read size comes from the net buffer. With 4096-byte reads no
read is ever that big, so `util.loadDump` on a MariaDB-linked build stayed silent
about the one thing the user needs to know when a load fails on transaction size:
which row is too long. The detection moved to `read()`, where the answer is already
known — the buffer holds a whole transaction's worth of data with no row boundary
in it — and `mark_oversized_row()` reports it once per row per transaction. The
`Transaction_buffer.oversized_row_detection_does_not_depend_on_read_size` unit test
pins it across read sizes from a quarter of the limit to four times it.

### 21.5 The component-level tests are green

The four failures §13.7 recorded and every later phase repeated are resolved:

- **`Instance_cache_test.table_columns` was a real gap, and the fix is in the
  product.** The test asserts that the type the cache reads out of
  `information_schema` matches the type the same column arrives with on the wire.
  MariaDB's JSON is `LONGTEXT` plus an automatic `json_valid()` CHECK constraint,
  so I_S reports `longtext` — but the wire protocol *does* know: the server sends
  `format=json` in the extended metadata, and the Shell has reported such a column
  as `Type::Json` since the extended-metadata commit. So the cache was the half
  that was wrong. `fetch_json_check_constraints()` now reads
  `I_S.CHECK_CONSTRAINTS` on a MariaDB source and types those columns as JSON,
  behind `json_columns_use_check_constraints()` (§7.3) — one extra query, MariaDB
  only, and only when the dump has tables.

  The rule it applies is the server's own, measured on 12.3.2: a text column is
  JSON when its **column-level** constraint has a `json_valid()` call at the top
  level of its expression, alone or as a conjunct of an `AND`
  (`Field_longstr::make_send_field()` and
  `Item_cond_and::set_format_by_check_constraint()`). An `OR` does not count, a
  table-level constraint does not count, and `json_valid()`'s argument is not
  looked at — a constraint naming a *different* column still makes this one JSON.
  `Instance_cache_test.maria_db_json_columns` pins all six shapes and re-checks
  them against the protocol metadata.
- **`Schema_dumper_test.opt_mysqlaas` and `compat_ddl` are skipped when the server
  is MariaDB.** They exercise the MySQL HeatWave Service compatibility pass, which
  `Dump_options::on_validate()` refuses outright for a MariaDB source (§4.11): the
  DDL rewriting, the restricted privilege names and the collation mapping are all
  MySQL's, so there is no reachable behaviour to port expected output for.
- **`Schema_dumper_test.unknown_collations` is skipped on a MariaDB *build*.** It
  runs off a mock session, so the server has no say: `is_supported_collation()`
  asks the linked client library's charset table, where MariaDB's own
  `utf8mb4_uca1400_*` collations are known — and therefore not replaced — while the
  MySQL names they would be replaced with do not exist at all.

One product message was wrong for MariaDB and is fixed here: the loader warned
"Histogram creation enabled but **MySQL** Server 12.3.2 does not support it".

### 21.6 The binlog suites are not registered where the utility is not built

`util.dumpBinlogs()` / `util.loadBinlogs()` stay MySQL-only (§11.2), so
`find_py_tests()` skips any script whose name contains `binlogs` unless
`HAVE_BINLOG_UTILS` is defined. That covers `util_dump_binlogs`,
`util_dump_binlogs_replication`, `util_load_binlogs` and the cloud variants.

### 21.7 A MariaDB test sandbox now has a binary log

MySQL 8.0+ logs by default and MariaDB does not, and the suites were written
against the MySQL default: they reset the binary log, read its position and size
the binlog cache against `max_binlog_cache_size`. `deploy_sandbox_with_plugin()`
therefore passes `log_bin=binlog` for a non-raw sandbox, where a test's own
`log_bin` still wins. Raw sandboxes are left exactly as the server starts them.

### 21.8 Where the suites stand

Component-level tests, MariaDB 12.3.2 (a throwaway empty-password sandbox):
`Schema_dumper_test` + `Instance_cache_test` are **40 passed, 0 failed**, five
skipped (§21.5). The MySQL build against MySQL 26.7.0 is **42 passed, 0 failed**
with the MariaDB-only tests skipped, and `util_copy_trx` — the suite whose
expectations §21.4 touched — still passes there.

Scripted suites on MariaDB, one gtest each:

| Suite | State |
|---|---|
| `util_dump_chunking` | **passes** |
| `util_dump_instance_corners` | **passes** (already did) |
| `util_copy_trx` | **passes** — 26 failures at the start of the phase |
| `util_copy_tables` | **passes** — 96 failures at the start |
| `util_copy_schemas` | **passes** — 11 after the shared-helper fixes |
| `util_copy_instance` | in progress |
| `util_load_dump_trx`, `util_dump_and_load_ddm`, `util_dump_and_load_extra` | in progress |
| `util_dump_and_load`, `util_dump_instance`, `util_dump_schemas`, `util_dump_tables` | **not yet** — the four big ones (16k lines between them) |
| `util_dump_binlogs`, `util_dump_binlogs_replication`, `util_load_binlogs` | not registered (§21.6) |

The four remaining suites got their first honest run once the environment was
fixed - `util_dump_and_load` 173 failure blocks, `util_dump_instance` 218,
`util_dump_schemas` 139, `util_dump_tables` 415 - and the causes are catalogued
rather than unknown. Counting the distinct first causes across all four:

- **33 × `ocimds` refused for a MariaDB source.** Whole sections exist only to
  exercise the MySQL HeatWave Service compatibility pass, the same thing §21.5
  skips two unit tests for. They need chunk-level gating, not new expectations.
- **28 × "Target MariaDB version 'x' is newer than the MariaDB version this MySQL
  Shell was built against (13.1.0)".** The suites synthesize `targetVersion`
  values from the *Shell's* version (26.x), which is the right yardstick only for
  MySQL: §7.4 made the reference version vendor-dependent, so the helpers that
  build those values now follow it (§21.9).
- **22 × `Unknown system variable`** - `partial_revokes`,
  `sql_generate_invisible_primary_key`, `show_gipk_in_create_table_and_information_schema`
  and friends, the same shape §21.3 fixed centrally for the shared helpers.
- **44 × "An open session is required"**, which are cascades of the above rather
  than causes of their own.

The rest is expected-output work: MySQL's display widths and collation names in
DDL, where the `schema_dumper_t.cc` recipe from the `mariadb-schema-dumper-tests`
note applies, and fixtures whose `/*!8xxxx*/` clauses are inert on MariaDB so the
*effects* asserted afterwards do not happen (§21.2).

### 21.9 Not done here

- **The four big dump suites** (§21.8). They are the bulk of what is left of this
  phase. Their `targetVersion` sections are done: `__build_server_version` /
  `__build_server_version_num` carry `mysqlshdk::utils::k_build_server_version`
  into the scripts, and `dump_utils.inc` picks the yardstick off the server's
  vendor per §7.4 (`newest_target_version`, `target_version_rejected_msg()`),
  which is what the ~28 "newer than the MariaDB version this MySQL Shell was
  built against" failures were.
- **`targetVersion` policy errors carry no `Argument #N:` prefix, on either
  vendor, and the suites now expect none.** The prefix comes from
  `Arg_handler::get()` (`scripting/type_info.h`), which wraps the conversion of
  one argument, so an option setter which throws during unpacking gets it -
  `set_target_version_str()` still does, and the three "Invalid value of the
  'targetVersion' option" assertions still assert it. The *version policy* check
  cannot live there: it is vendor-dependent since §7.4, and the vendor arrives
  with the session, which `on_validate()` is the first hook to have. Restoring the
  prefix would mean letting `validate_and_configure()` take the argument position
  (10 call sites in `mod_util.cc` and `copy_operation.h`, each entry point knowing
  its own number, and `copy_operation.h` having none to give - it sets
  `targetVersion` from the target's `@@version`, not from an option). Decided
  against: the expectations are pinned to the current output instead, in the three
  `util_dump_{instance,schemas,tables}_norecord.py` suites, five assertions each.
- **The MySQL build has only been spot-checked on the scripted side** —
  `util_copy_trx` passes there, and both builds compile. The full MySQL scripted
  run is still the gate this phase has to clear before it can be called done. The
  12 assertions which made `util_dump_instance` known-red there are the prefix
  ones above and should go green with them, but that run has not happened yet.
- **The JavaScript suites are untouched.** `util_load_dump_norecord.js` and the
  `cli_dump_*` scripts only run where the Shell has JS, which a MariaDB build does
  not (`HAVE_JS`), so nothing there can be exercised from this side.
- **`util.importTable`'s own suites were not part of this pass**, only the
  dump/load and copy ones.
- **`transaction_registry` data is now skipped rather than translated.** Its rows
  are MariaDB's system-versioning bookkeeping, and the load could not write them
  anyway (error 1556), so a dump of the `mysql` schema carries the table's DDL and
  none of its rows — the same treatment `general_log` and `slow_log` have always
  had. Nothing reads it back.
- **`snapshot_routines()` in the test helper still asks only for `PROCEDURE` and
  `FUNCTION`**, so a MariaDB package is not part of an instance snapshot. The
  package round-trip has its own coverage (§19), and widening the helper would
  change every snapshot comparison at once.

---

## 22. A quoted identifier is not a string literal — fixed

The §16.5 view bug, diagnosed. It is not the `supports_view_table_usage`
fallback being wrong; the fallback is the only reason anyone noticed. The lexer
cannot read a quoted identifier the server itself printed.

`MySQLLexer.g4`'s rule was written from the string-literal rules next to it:

```
BACK_TICK_QUOTED_ID:
    BACK_TICK (({!this.isSqlModeActive(SqlMode.NoBackslashEscapes)}? '\\')? .)*? BACK_TICK;
```

An identifier is not a string. It escapes the quote character by **doubling**
it, which this rule does not know, and it gives a **backslash no special
meaning**, which this rule gets backwards. Both were measured on 12.3.2 — the
name is what the server reports, not what the shell guesses:

| Written | Name | Old lexer |
|---|---|---|
| `` `a``b` `` | `` a`b `` | two adjacent identifiers → `mismatched input` |
| `` `a\` `` | `a\` | consumes the closing quote, runs on into the next statement |
| ```` ```` ```` | `` ` `` | as above |
| `` `a\\` `` | `a\\` | correct |

Only the second and third shapes are new information; the first is what §16.5
recorded. Both reach a dump the same way: a view's column alias keeps the text
the query was written with, and `SHOW CREATE VIEW` / `I_S.VIEWS` print it back
properly escaped.

### 22.1 The fix

`MySQLBaseLexer::nextToken()` scans quoted identifiers itself, which is
**already how ANSI_QUOTES double quotes were handled** — `scan_ansi_quotes_identifier()`
existed for exactly this reason, doubling included. It is now
`scan_quoted_identifier(quote)` and both quote characters go through it: `"` when
`ANSI_QUOTES` is active, `` ` `` always. An unterminated identifier still falls
through to the generated lexer, so the syntax error is unchanged.

The grammar was **not** regenerated. Fixing the rule means an ANTLR run, and the
checked-in output is from 4.10.1 while the linked runtime is 4.13.2, so
regenerating would rewrite the whole lexer *and* parser for a one-line change and
conflict with every upstream merge. The rule carries a comment saying it is
overridden and that the two must be fixed together.

Nothing else moved: `span_quotable_sql_identifier()`, which unquotes what the
parser returns, already handled doubling and treated a backslash as an ordinary
character.

### 22.2 Verified

Only two things outside `libs/parser/` use this lexer: `instance_cache.cc`'s view
parsing and `provider_sql.cc`'s autocompletion. On the MariaDB build, against
12.3.2:

- `MysqlParserUtils` — 14 passed, including a new `quoted_identifier_escapes`
  covering each shape above, both quote characters, and the alias from §16.5.
- `Completer_frontend*`, `Completion_cache_refresh`, `Instance_cache_test`,
  `Schema_dumper_test` — 104 passed, 0 failed.
- End to end: a schema holding all three alias shapes now dumps, loads into a new
  schema, and every column name comes back byte-identical. It failed before the
  fix on the same schema, with `mismatched input 'b'` — the `` `a\` `` shape, not
  the one §16.5 reported.

### 22.3 Not done here

- **The MySQL build is unverified.** For any identifier without a doubled quote
  or a backslash the scanner consumes the same bytes and emits the same token, so
  the only inputs that change are ones which used to fail — but that is an
  argument, not a test run.
- **MySQL 8.0.13+ never reached this code**, since `I_S.VIEW_TABLE_USAGE` gives
  it the references without parsing. 5.7 and 8.0.0-8.0.12 dumps do, and so does
  autocompletion on every version.
- **`fetch_view_metadata()` still parses MariaDB view definitions with the MySQL
  grammar** (§13.8). This fixes a rule that was wrong for *both* vendors;
  MariaDB-only view syntax is still not understood.

---

## 23. Engine-independent statistics are not dumped — known limitation

§4.5's last open item, and a **deliberate limitation** rather than a gap left by
accident. Recorded here with what it costs, because the cost is not obvious and
the obvious workaround does not work.

MariaDB keeps engine-independent table statistics (EITS) in `mysql.table_stats`,
`mysql.index_stats` and `mysql.column_stats`. None of them is carried. The dump
excludes the `mysql` schema, so nothing about statistics survives a round trip,
and a restored instance runs on engine estimates alone.

### 23.1 What it costs, measured on 12.3.2 defaults

| | |
|---|---|
| `use_stat_tables` | `PREFERABLY_FOR_QUERIES` — EITS are *preferred* wherever they exist |
| `optimizer_use_condition_selectivity` | `4` — the level which reads histograms |
| `ANALYZE TABLE t` | collects **nothing** into the stat tables |
| `ANALYZE TABLE t PERSISTENT FOR ALL` | collects them, histograms included (`JSON_HB`) |

So the failure mode is a silent plan regression on the restored instance: no
error, no warning, and nothing in the dump or the load output mentions
statistics. Its magnitude is whatever a plan flip costs on the data.

Three things make it worse than it first sounds:

- **`analyzeTables` does not fix it.** `"histogram"` already says so - the loader
  warns `Histogram creation enabled but MariaDB Server x does not support it`.
  `"on"` is the silent one: it issues a plain `ANALYZE TABLE`, which refreshes the
  engine's own statistics but under the default `..._FOR_QUERIES` setting collects
  no EITS at all - measured, 0 rows - while its progress label still says
  "Updating table histograms and key distribution statistics".
- **It never self-heals.** Unlike InnoDB's own persistent statistics, EITS are
  refreshed only by an explicit `ANALYZE ... PERSISTENT`.
- **It hits exactly the users who care.** Nobody collects EITS by accident.

What bounds it: no data is lost and nothing is corrupt. Recovery is
`ANALYZE TABLE ... PERSISTENT FOR ALL` per table, scriptable from the dump's own
table list, at the price of one scan per table.

`util.copy*` is affected identically - same engines.

### 23.2 What an implementation would look like

Not planned work; notes so it does not have to be re-derived.

MySQL's histogram support is the model, and it carries **no statistics data**:
`fetch_table_histograms()` records only the column name and the requested bucket
count, and the load re-derives the histogram from the restored rows with
`ANALYZE TABLE ... UPDATE HISTOGRAM ON c WITH n BUCKETS`. The MariaDB analogue is
to record which columns and indexes had statistics and let the target recompute
with `ANALYZE TABLE ... PERSISTENT FOR COLUMNS (...) INDEXES (...)`.

The pieces: a `supports_persistent_statistics()` predicate of its own (today
`supports_column_statistics()` answers two questions at once); a MariaDB branch
in `fetch_table_histograms()` reading `mysql.column_stats`, degrading with a
warning where the account cannot read `mysql`; a sibling field in the per-table
metadata, which `Dumper::write_table_metadata()` already writes for every entry
point, so `dumpInstance`, `dumpSchemas`, `dumpTables` and `copy*` are covered by
one change; and a MariaDB branch in `Analyze_table_task::execute()`.

Two traps found while measuring:

- **The requested histogram size cannot be recovered from the stored one.**
  `histogram_size` was 254 and `mysql.column_stats.hist_size` came back as 2:
  `JSON_HB` sizes itself to the data. So the metadata can carry "statistics
  existed, of this type" but not MySQL's `WITH n BUCKETS` symmetry - the target's
  own `histogram_size` has to govern.
- **The stat tables are keyed by `db_name`/`table_name` as strings.** Carrying
  their rows would need explicit rewriting for a load into a renamed schema;
  hanging the metadata off the table avoids the question entirely, which is
  another reason to prefer the recompute design over dumping the rows.

---

## 24. A sequence DEFAULT carried the source schema's name — fixed

§16.5 recorded this as a `dumpTables` filter gap: dump a table whose column
`DEFAULT` names a sequence, and the sequence is not pulled in. Measuring it found
a second, worse defect underneath, which also reaches the paths §16 called done.

`SHOW CREATE TABLE` prints a sequence `DEFAULT` **fully qualified** even where the
sequence was named bare - ``DEFAULT nextval(`seqdep`.`s1`)`` - while the table
name itself is printed unqualified. The dumper wrote that verbatim, so the source
schema's name was baked into the DDL:

| Scenario | Before |
|---|---|
| `dumpSchemas`, load into a **renamed** schema | the new schema gets its own sequence (§16), and a table which ignores it and draws from the **source's** |
| `dumpTables` of the table alone, source schema present on the target | table draws from the **source's** sequence |
| `dumpTables`, source schema absent | `CREATE TABLE` fails, error 1146 |
| `dumpSchemas`, load into a schema of the same name | correct |

Rows one and two were silent. Measured, not inferred: inserting into the restored
`seqload2.t1` advanced `seqdep.s1`, so a restored copy consumed the original
database's sequence values.

### 24.1 The fix

`Schema_dumper::resolve_sequence_defaults()` drops the qualifier **only** where it
names the table's own schema. An unqualified `NEXTVAL()` in `CREATE TABLE`
resolves against the session's schema, which the loader has already selected, so
the reference binds to wherever the table lands - exactly how the table name
already behaves. A sequence in another schema reads identically in the DDL but
means something different, so its qualifier is the whole point and stays.

References are collected before anything is rewritten, and the identifiers are
read with `span_quotable_sql_identifier()` rather than by splitting on a back
tick - §22 is the reason that matters.

### 24.5 `nextval` is not the only spelling

The first version of the fix looked for `nextval(` only, and that was a hole: a
`DEFAULT` can name a sequence three ways, and the server stores each one
qualified.

| Written | Stored |
|---|---|
| `NEXTVAL(s1)`, `NEXT VALUE FOR s1` | ``nextval(`db`.`s1`)`` |
| `LASTVAL(s1)`, `PREVIOUS VALUE FOR s1` | ``lastval(`db`.`s1`)`` |
| `SETVAL(s1, 10)` | ``setval(`db`.`s1`,10,1,0)`` |

All three are rewritten. The set is exhaustive rather than a guess: a sequence
cannot appear anywhere else in table DDL, because a generated column referencing
one is refused with **error 1901** (`Function or expression 'nextval()' cannot be
used in the GENERATED ALWAYS AS clause`) and a `CHECK` with **error 1970**
(`CHECK does not support subqueries or stored functions`) - both measured.

### 24.2 The filter gap itself

Left as it was, deliberately: `dumpTables` selects exactly what was asked for, as
it does for a foreign key's target or a view's base tables. What changed is that
it no longer does so silently - each reference the dump does not carry gets a
warning, worded for which of the two cases it is:

```
WARNING: Table `seqdep`.`t1` has a column DEFAULT which references sequence `s1`.
The sequence is not included in this dump, so the table cannot be created unless
a sequence of that name already exists in the schema it is loaded into.
```

A cross-schema reference names the schema instead, since that one is not
relocatable. Where the sequence *is* in the dump - any ordinary `dumpSchemas` or
`dumpInstance` - nothing is printed.

Auto-including the sequence was considered and rejected: it would make the
dependency rule sequence-specific, and it would restore the sequence's position
as a side effect of asking for a table.

### 24.3 Verified

On the MariaDB build against 12.3.2:

- Renamed-schema load: the restored table now references its own schema's
  sequence, inserts draw from it, and the source's position is untouched.
- Cross-schema reference: qualifier preserved, table still points where it did.
- `dumpTables` alone: warning at dump time, then error 1146 naming
  `seqload4.s1` at load time rather than a silent bind to the source.
- No warning where the sequence is part of the dump.
- `Schema_dumper_test` + `Instance_cache_test` - 40 passed, 0 failed.

### 24.4 Not done here

- **No scripted-test coverage.** The rename case needs a dump and a load, so it
  belongs with the end-to-end suites; the `util_dump_tables` sections are where
  the warning would be asserted, and those are §21.8's four remaining suites.
- **The MySQL build is untouched but unverified.** `supports_sequences()` gates
  the whole function off there, so it cannot execute; the gate is the argument,
  not a test run.
- **Other schema-qualified references in table DDL were not audited.** Sequences
  are covered exhaustively (§24.5), but a `DEFAULT` is not the only thing the
  server prints with a schema on it, and nothing here looked for the rest.

---

## 25. A missing authentication plugin now says what to install

§20.6's last item. `ed25519`, `gssapi` and `pam` are loadable plugins, so a target
which never installed one refuses the account:

```
ERROR: While creating user accounts: MySQL Error 1524 (HY000):
Plugin 'ed25519' is not loaded: CREATE USER IF NOT EXISTS `u_ed25519`@`%` ...
```

The server names the missing plugin and stops there. The load then aborted with
nothing said about how to proceed or about the accounts it had already created.

**It still aborts** - that part was right. The loader's other path for this error
(warn, skip the account, continue) exists only for MHS, where a plugin *cannot*
be installed; anywhere else, silently skipping an account is a privilege change
nobody asked for. What was missing was the explanation, which
`explain_missing_auth_plugin()` now prints right after the error:

```
NOTE: The target server does not have the 'ed25519' authentication plugin
installed, which the account 'z_ed25519'@'%' requires. A MariaDB loadable plugin
is installed with INSTALL SONAME; plugin_library in information_schema.PLUGINS on
the source server names the library it comes from. Install the plugin on the
target and run the load again - accounts are created with IF NOT EXISTS, so the
ones which already exist are skipped - or exclude the affected accounts with the
'excludeUsers' option. 1 account was created before this failure and is left on
the target.
```

Three things it does not do: guess the library name (`ed25519` comes from
`auth_ed25519`, but `unix_socket` comes from `auth_socket`, so the source's
`plugin_library` is the honest answer rather than a pattern), roll back the
accounts already created, or fall back to skipping.

The retry advice is load-bearing and was verified rather than assumed: the users
script writes `CREATE USER IF NOT EXISTS` / `CREATE ROLE IF NOT EXISTS`, so
running the load again after installing the plugin completes and the existing
accounts are skipped.

### 25.1 Verified

Staged on 12.3.2 by installing `auth_ed25519`, creating an account with it,
dumping, then uninstalling the plugin before the load:

- The note appears after the error, names the plugin and the account.
- Ordering the accounts so the failing one is not first produces
  `1 account was created before this failure and is left on the target`;
  where it is first, the sentence is correctly absent.
- After `INSTALL SONAME 'auth_ed25519'`, re-running the same load reports
  `2 accounts were loaded` and both accounts exist with the right plugins.
- `Load_dump*` unit suites - 9 passed, 0 failed.

### 25.2 Not done here

- **The dump says nothing.** The source has the plugin installed and its
  `plugin_library` in `information_schema.PLUGINS`, so `dumpInstance` could warn
  at dump time - when there is still time to prepare the target - and even name
  the exact `INSTALL SONAME`. That is the better fix for the same problem and it
  is not done; the load-side note has to reconstruct the advice generically.
- **The counter counts completed statement groups**, so an account which the MHS
  path skipped mid-way is still counted as created. That path does not reach this
  note, so it cannot show a wrong number today.
- **No scripted coverage**, for the same reason as §24.4: staging it needs a
  plugin installed, a dump, and then the plugin removed.
