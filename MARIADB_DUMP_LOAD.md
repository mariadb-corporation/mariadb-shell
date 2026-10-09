# Dump & Load on MariaDB

How the dump, load and copy utilities behave in the MariaDB build, and why they
differ from upstream MySQL Shell. Companion to [MARIADB_PORT.md](MARIADB_PORT.md),
which covers the rest of the port. Server behaviour quoted here was measured on
MariaDB 12.3 to 13.1 unless a section names another version.

---

## 1. Scope

### 1.1 Utilities

`util.dumpInstance()`, `dumpSchemas()`, `dumpTables()`, `loadDump()`,
`copyInstance()`, `copySchemas()`, `copyTables()`, `importTable()` and
`exportTable()` are built for both vendors. `util.dumpBinlogs()` /
`loadBinlogs()` are MySQL-only (`HAVE_BINLOG_UTILS`, `MARIADB_PORT.md` §4).

### 1.2 Vendor to vendor

MySQL → MySQL behaves as upstream. MariaDB → MariaDB carries MariaDB's own
objects and semantics (§5, §6). Loading across vendors is refused before any DDL
runs (`SHERR_LOAD_VENDOR_MISMATCH`, 53039), and `util.copy*` refuses a vendor
mismatch between its two sessions.

The refusal keys on the dump's **dialect**, not on the source vendor. A MySQL
build still remaps a MariaDB source to version 5.6 in `common::server_version()`
(upstream behaviour, kept under `#ifndef MARIADB_BUILD`), which suppresses
everything MariaDB-specific and produces a MySQL-shaped dump. That is upstream's
MariaDB → MySQL migration path, and it keeps working:

| Dump produced by | Source | Target | Result |
|---|---|---|---|
| MariaDB build | MariaDB | MariaDB | loads |
| MariaDB build | MariaDB | MySQL | refused |
| MySQL build | MariaDB | MySQL | loads (upstream's migration path) |
| either | MySQL | MariaDB | refused |

A MySQL build reading a MariaDB server therefore never dumps sequences, packages
or MariaDB accounts: its predicates see a 5.6 server (§9.6).

### 1.3 MySQL-only features

MySQL-only features keep their code and tests and are switched off for a MariaDB
server by the predicates in §2.2: invisible primary key generation, JavaScript
libraries, PKE-as-PK, dynamic data masking, vector store conversion, `BULK LOAD`,
`restrict_fk_on_non_standard_key`, `sql_require_primary_key`, histograms.

Two options are refused outright for a MariaDB source, in
`Dump_options::on_validate()`, because they only make sense for MySQL HeatWave
Service: `ocimds`, and the `compatibility` list (every value rewrites MySQL DDL or
accounts to fit HeatWave). Allowing the DDL-only subset is
[AIPL-25](https://jira.mariadb.org/browse/AIPL-25).

The remote storage backends (OCI, S3, Azure) work for both vendors: they are
client-side transport with no server dependency.

---

## 2. Vendor-aware gating

### 2.1 The vendor travels with the version

`common::Server_version` pairs a version with `is_maria_db`, on both sides of a
dump:

- **Dump.** `Dump_options` takes the source vendor from
  `ISession::get_server_vendor()` in `on_set_session()`, and `Schema_dumper`'s
  target version carries the same vendor.
- **Load.** `Load_dump_options` holds the target as a `Server_version`, built
  from `SELECT @@version` plus the handshake vendor. `Dump_reader::source_server()`
  gives the dump's source.
- **Copy.** `copy_operation.h` reads both sessions the same way.

`get_server_vendor()` reads the handshake string the client already holds
(`mysql_get_server_info()`), so it costs no round trip, and it throws
`Not connected` on a session that is not connected. Every vendor decision is made
from the server connected to, never from `MARIADB_BUILD`.

In a MariaDB build the version is the server's real one. `Server_version`'s
`is_5_6` / `is_5_7` / `is_8_0` mean **MySQL** 5.6 / 5.7 / 8.0 and are false for
MariaDB. `server_version(const Version &, bool is_maria_db)` builds one without
detection, for vendors that come from the handshake, the manifest or a
`targetVersion`.

### 2.2 Feature predicates

[common/dump/server_features.h](modules/util/common/dump/server_features.h) holds
every gate. Each answers "does this vendor, at this version, have feature X",
never "is the version at least 8.0", because MariaDB's version numbers are not
on MySQL's scale. MySQL thresholds are upstream's, so the MySQL build gets the
same answers.

Most predicates are MySQL-only (a MySQL threshold, false for MariaDB). The ones
that answer differently for MariaDB:

| Predicate | MariaDB |
|---|---|
| `supports_backup_stage` | 10.4+ (§3.1) |
| `requires_explicit_select_privilege` | true: only MySQL 8.0's data dictionary reports what the account cannot see |
| `requires_select_on_mysql_to_dump_users` | true (§6.2) |
| `supports_show_create_user` | 10.2+ |
| `supports_role_dumping`, `roles_are_hostless`, `show_grants_expands_roles`, `default_role_in_show_grants` | 10.0.5+ (§6.1) |
| `supports_check_constraint_checks` | 10.2+ (§5.2) |
| `supports_sequences` | 10.3+ (§5.1) |
| `supports_packages` | 10.3+ (§5.3) |
| `supports_key_period_usage` | 10.5+ (§5.6) |
| `json_columns_use_check_constraints` | 10.5+ (§5.5) |
| `supports_optimizer_hints`, `supports_wide_bit_xor` | false: `SQL_NO_CACHE`, and checksums in 64-bit slices |
| `supports_view_table_usage` | false: views are parsed instead (§8.2) |
| `supports_gtid_set_functions` | false (§4.4) |
| `is_rolled_back_deadlock` | 1213, or 4060 on MariaDB (§7.3) |
| `engine_manages_external_data` | engine list, no vendor check (§5.7) |

### 2.3 Dialect

`is_maria_db_dialect(v)` is `is_maria_db && !is_5_6`: whether a dump from that
server carries MariaDB SQL. It decides the cross-vendor refusal (§1.2), the GTID
handling (§4) and whether a MariaDB account can be dumped (§6.2).
`produces_maria_db_dialect()` answers the same question for `util.copy*`, which
has no manifest.

### 2.4 Reference version

Checks of the form "is this server newer than the Shell" compare against
`reference_version()`, chosen by the vendor of the **server**:

| Server | Reference |
|---|---|
| MySQL | the Shell's own version, which follows MySQL's calendar numbering |
| MariaDB | `k_build_server_version`, the MariaDB server version this Shell was built from |

The build exports that version as `MYSH_BUILD_SERVER_VERSION`, from
`GET_MYSQL_VERSION()` in `version.cmake`, which reads `VERSION` in a MariaDB tree.
A MariaDB build without it fails to configure, because the Shell's own version is
not a MariaDB version.

It governs:

- the "unsupported server, upgrade the Shell first" error, for a newer major
  version;
- the "server is newer than the Shell" warning, for a newer minor version;
- the default `targetVersion` of a dump, and the limit `targetVersion` is
  checked against.

For a MariaDB source, `targetVersion` takes a MariaDB version, and one newer than
the build's server version is refused. The check runs in
`Dump_options::on_validate()`, because the vendor is only known once the session
is set. So its errors carry no `Argument #N:` prefix on either vendor; the scripted
suites expect none.

### 2.5 Manifest

`@.json` records the source vendor in `source.vendor` (`"mariadb"` or `"mysql"`),
and `server_info()` prefers it over detecting the vendor from the version string.
Dumps without the field still load: the version string tells.

`source.sysvars` is the source's whole `SHOW GLOBAL VARIABLES`, unfiltered, as
upstream writes it. The load reads back only `version`, `hostname`, `port`,
`server_uuid`, `lower_case_table_names` and `partial_revokes`. The rest includes
paths, host names, `init_connect`, the `wsrep_*` addresses and options, and
`report_password` when the server was started with one; MariaDB masks
`wsrep_sst_auth`.

---

## 3. Consistency and privileges

### 3.1 The backup lock

| Step | MySQL | MariaDB |
|---|---|---|
| snapshot window | `FLUSH TABLES WITH READ LOCK` | the same |
| backup lock, held for the dump | `LOCK INSTANCE FOR BACKUP`, main session | `BACKUP STAGE START` + `BACKUP STAGE BLOCK_DDL`, **dedicated session** |
| release | session close | `BACKUP STAGE END`, then close |
| privilege | `BACKUP_ADMIN` | `RELOAD` |

`Dumper::start_backup_stage()` opens `m_backup_stage_session`.
`Dumper::unlock_instance()` ends the stage and runs from the same
`on_leave_scope` in `do_run()` that closes the main session, so an interrupt or
an exception releases the server-wide stage. A killed connection releases it too.
`Dumper::backup_lock_privilege()` names the privilege for the messages.

The stage needs its own session for two reasons in the server source
(`sql/backup.cc`, `sql/mdl.cc`, `sql/sql_parse.cc`). `SQLCOM_BACKUP` commits the
running transaction. And `backup_start()` refuses a connection that holds FTWRL.
The stage is also a server-wide singleton (`MDL_BACKUP_START` is incompatible with
itself), and stages only move forward: there is no way from `BLOCK_COMMIT` back to
`BLOCK_DDL`. So FTWRL stays the snapshot lock and `BLOCK_DDL` is the backup lock.

Dry runs check `RELOAD` but do not enter the stage, as MySQL dry runs do not take
`LOCK INSTANCE FOR BACKUP`. `consistent: false` takes no lock on either vendor.

### 3.2 What `BLOCK_DDL` blocks

| Another session doing | Under `BLOCK_DDL` |
|---|---|
| DDL | blocked |
| InnoDB DML, Aria DML (`TRANSACTIONAL=1`) | runs |
| MyISAM DML | **blocked** for the length of the dump |
| account management | **runs** |
| FTWRL, `LOCK TABLES ... READ` | succeed |
| a second `BACKUP STAGE START` | blocked |

`LOCK INSTANCE FOR BACKUP` blocks no DML, so the MyISAM row is a MariaDB-only cost.
`start_backup_stage()` logs it.

Because account management is not blocked, the `LOCK TABLES` fallback
(`lock_all_tables()`) always locks the `mysql` grant tables on MariaDB. MySQL skips
them when its backup lock is held. On MariaDB the list is `global_priv` (`user` is
a view over it), `roles_mapping`, `columns_priv`, `db`, `func`, `proc`,
`procs_priv`, `proxies_priv`, `tables_priv` and `event`. MariaDB has no
`default_roles`, `global_grants` or `role_edges`, and keeps events in `mysql.event`.

### 3.3 Lock waits

The backup stage runs with its own `lock_wait_timeout`,
`k_block_ddl_lock_wait_timeout` (five minutes), set before `BACKUP STAGE START`.
`START` waits as well when another connection holds a stage. MariaDB's default
wait is 86400 seconds, so without the bound a blocked dump sits at "Locking
instance for backup" for a day. When the bound expires, `report_ddl_in_flight()`
prints a note listing the statements that hold the lock, from
`I_S.PROCESSLIST` (best effort: another account's statements need `PROCESS`). The
dump then fails with the 1205 and releases what it holds.

`FLUSH TABLES WITH READ LOCK` waits for the server's `lock_wait_timeout`, by
design. It is upstream's shared code, used by both vendors, and every comparable
tool does the same: `mysqldump`, `mariadb-dump`, and `mariadb-backup`, whose
`--startup-wait-timeout` defaults to waiting forever. While it waits behind a long
statement it blocks other sessions' writes, so a shorter wait for one dump means
lowering `lock_wait_timeout` for the server or the account. Only dry runs bound it,
to one second (BUG#33173739).

### 3.4 Privileges

- **`BINLOG MONITOR`** replaces `REPLICATION CLIENT` (renamed in 10.5;
  `SHOW PRIVILEGES` does not report the old name). The privilege check and the
  messages that name it choose by vendor.
- **Role-granted privileges are resolved.** MariaDB enables exactly one role on
  connect, the account's default role, and enabling a role enables what is granted
  to it. `SHOW GRANTS` has no `USING` clause: its bare form reports the account's
  own grants, the closure of its active role and `PUBLIC`, and needs no privilege
  on `mysql`. `User_privileges` reads the roles from
  `information_schema.APPLICABLE_ROLES` (current account) or
  `mysql.user.default_role` (another account), and issues the bare `SHOW GRANTS` or
  `SHOW GRANTS FOR <role>`.
- **`--skip-grant-tables`.** MariaDB's `CURRENT_USER()` is then a bare `@`, where
  MySQL reports `'skip-grants user'@'skip-grants host'`. The dumper's
  `current_account()` reports MariaDB's in MySQL's spelling, so `User_privileges`
  and the dumper's skip-grant-tables handling treat both vendors alike.

---

## 4. GTID positions

### 4.1 The MariaDB model

A MariaDB GTID is `domain-server-sequence`. A **position** is a comma-separated
list with at most one entry per domain, the last sequence applied in that domain,
so it covers every earlier sequence of the domain. There is no `gtid_mode`,
`GTID_PURGED`, `GTID_SUBSET()` or `GTID_SUBTRACT()`.

| Variable | Meaning |
|---|---|
| `gtid_binlog_pos` | what this server wrote to its binary log |
| `gtid_slave_pos` | what it applied as a replica |
| `gtid_current_pos` | the union of the two, and what `mariadb-backup` records |

The dump stores `gtid_current_pos` in `gtidExecuted`, read explicitly rather than
from the fifth column of `SHOW MASTER STATUS`, which is `gtid_binlog_pos` and is
wrong for a replica without `log_slave_updates`. On MariaDB GTIDs come with the
binary log, so a dump counts GTIDs as enabled whenever `log_bin` is on.
`common::gtid_executed()` and `common::binlog()` choose by
`is_maria_db_dialect()`. A MySQL-shaped dump of a MariaDB server keeps upstream's
behaviour and records no position.

### 4.2 `Mariadb_gtid_position`

[mysqlshdk/libs/mysql/mariadb_gtid.h](mysqlshdk/libs/mysql/mariadb_gtid.h) is a
value type, a map from domain to `{server_id, sequence}` with `parse`, `str`
(domains ascending), `contains`, `intersects` and `merge`. It needs no session.
`contains()` ignores `server_id`: the domain and the sequence say which
transactions are covered. `intersects()` means "share a domain with a non-zero
sequence".

### 4.3 `updateGtidSet`

`Dump_loader::update_maria_db_gtid_position()` assigns to `gtid_slave_pos`. That is
`mariadb-backup`'s mechanism, and what `CHANGE MASTER TO ... master_use_gtid =
slave_pos` resumes from.

| | MySQL | MariaDB |
|---|---|---|
| `replace` | `SET GLOBAL GTID_PURGED = <dump>` | `SET GLOBAL gtid_slave_pos = <dump>` |
| `append` | `SET GLOBAL GTID_PURGED = '+<dump>'` | union computed client side, then assigned whole |
| refused while | Group Replication runs | any replica thread runs |

`Dump_loader::validate_update_gtid_set()` holds both vendors' checks:

- `replace` requires the dumped position to contain the target's current
  `gtid_slave_pos`, so nothing already replicated is forgotten (53041,
  `SHERR_LOAD_UPDATE_GTID_REPLACE_REQUIRES_SUPERSET_POSITION`);
- `append` requires the two positions to share no domain (53042,
  `..._APPEND_POSITIONS_INTERSECT`);
- a running replica refuses either (53040, `..._REPLICATION_IS_RUNNING`).
  Replication state comes from `SHOW ALL SLAVES STATUS`, which covers every
  connection, matching columns by name.

`gtid_binlog_state` is never written: setting it needs `RESET MASTER`, which would
discard the target's binary log.

Galera is not detected, by design. MySQL refuses `updateGtidSet` on a running Group
Replication member because the server itself rejects the change
(`ER_UPDATE_GTID_PURGED_WITH_GR`). MariaDB has no such rule for Galera:
`gtid_slave_pos` is node-local on purpose (`sql/rpl_gtid.cc` keeps
`mysql.gtid_slave_pos` out of wsrep replication), because asynchronous replication
into a cluster runs on one node. A load with `updateGtidSet` sets the position on
the node it is connected to, while Galera replicates the data to every node, and
the replica is started on that same node.

### 4.4 Verifying that no DDL ran during a dump

A dump that takes no lock checks afterwards that no DDL ran meanwhile. If the
GTID position moved, it replays the binary log range of the dump with
`SHOW BINLOG EVENTS` (`mysqlshdk/libs/mysql/binlog_utils.cc`, built for both
vendors) and tests each statement that ran during the dump for DDL. Only the
choice of those statements differs by vendor:

| Server | Transactions that ran during the dump |
|---|---|
| MySQL | `GTID_SUBTRACT()` of the two `gtid_executed` sets, server side |
| MariaDB | after the end position and not covered by the start one, compared client side with `Mariadb_gtid_position::contains()` |

The server decides (`supports_gtid_set_functions()`, `is_maria_db_dialect()`), so
a Shell built against either vendor checks a dump of either server. Without GTIDs
the check compares binary log file positions, and every transaction between them
counts. A server that fits neither branch gets a note and a dump treated as not
verified.

### 4.5 `showMetadata`

`Dump_reader::show_metadata()` labels the value `GTID_position` for a MariaDB dump
and `Executed_GTID_set` for a MySQL one.

---

## 5. MariaDB objects and types

### 5.1 Sequences

A sequence is reported by `I_S.TABLES` with `TABLE_TYPE='SEQUENCE'`, shares the
table namespace, and has three views of itself:

| Source | Gives | Lacks |
|---|---|---|
| `I_S.TABLES` | that it exists | anything about the sequence |
| `I_S.SEQUENCES` | start, min, max, increment, cycle | the current position, and `CACHE` |
| the sequence read as a table | `next_not_cached_value`, `cycle_count` | |

The design follows `mysqldump`'s (`get_sequence_structure`):

- **Enumerated and filtered as tables.** `filter_tables()` routes them to
  `Instance_cache::Schema::sequences`, and `includeTables` / `excludeTables`
  select them. There is no sequence option of its own, and no `sequences: false`
  toggle: a table defaulting to a sequence cannot be restored without it.
  `Dump_options::dump_sequences()` is true for the dump entry points and false
  for `exportTable`.
- **DDL in the schema script.** `Schema_dumper::dump_sequences_ddl()` writes
  `SHOW CREATE SEQUENCE` and `DO SETVAL(s, <next_not_cached_value>, 0)` into the
  schema's script, which the loader runs before any table DDL, so a
  `DEFAULT NEXT VALUE FOR s` resolves. Sequences take no data chunk and no lock.
- **The position is ahead by design.** `next_not_cached_value` counts the values
  the cache reserved (`CACHE 1000 INCREMENT BY 5` after one value reports
  `5100`). Restoring it skips values and never repeats one; it is where the server
  itself resumes after a restart. `SETVAL` is used rather than
  `ALTER SEQUENCE ... RESTART WITH` because it only moves a sequence forward.
- **`cycle_count`.** A cycling sequence that has wrapped is written as
  `DO SETVAL(s, v, 0, <cycle_count>)`, the fourth argument being the round. Any
  other sequence gets the three-argument form, because the server refuses a round
  on a sequence that does not cycle (`ER_SEQUENCE_RUN_OUT`). `mariadb-dump` does
  not carry the round.
- **Load side.** The per-schema metadata lists `sequences`, written only when
  there are any. From it the loader runs `DROP SEQUENCE IF EXISTS` for
  `dropExistingObjects` (after the tables, which may use one) and checks for
  existing sequences in `I_S.SEQUENCES`. `Sql_transform::add_execution_condition()`
  treats `CREATE|ALTER|DROP SEQUENCE` and `DO SETVAL(<name>, ...)` as statements
  about that sequence, so an excluded sequence loses both.

**`DEFAULT` references.** `SHOW CREATE TABLE` prints a sequence reference fully
qualified even when it was written bare, in three spellings: `nextval()` (also
`NEXT VALUE FOR`), `lastval()` (also `PREVIOUS VALUE FOR`) and `setval()`. A
sequence can appear nowhere else in table DDL: a generated column refuses one
(error 1901), and so does a `CHECK` (1970). `Schema_dumper::resolve_sequence_defaults()`
drops the qualifier where it names the table's own schema, so the reference
follows the table into a renamed schema, as the table name does. A reference into
another schema keeps its qualifier. Identifiers are read with
`span_quotable_sql_identifier()`.

`dumpTables` selects exactly the objects named, as for a foreign key's target. A
`DEFAULT` that names a sequence the dump does not carry gets a warning saying the
table cannot be created unless that sequence exists where it is loaded.
Including the sequence automatically was rejected: it would make the dependency
rule sequence-specific, and restore the sequence's position as a side effect.

### 5.2 Check constraints

The DDL needs nothing: `SHOW CREATE TABLE` carries every constraint, and the
compatibility rewriting leaves them alone. What differs is how enforcement is
switched off:

| | MySQL | MariaDB |
|---|---|---|
| per constraint | `[NOT] ENFORCED`, in the DDL | no such syntax |
| per session | none | `check_constraint_checks` |

MySQL's switch travels inside the dumped `CREATE TABLE`, and an enforced
constraint can never be violated. A MariaDB table can legitimately hold rows its
own constraints reject, and no DDL can say so. So the loading session has to turn
enforcement off, which `mariadb-import` also does for every import
(`client/mysqlimport.cc`). `Dump_loader::create_session()` and
`import_table::Load_data_worker::init_session()` set `check_constraint_checks = 0`
on a MariaDB target (`supports_check_constraint_checks()`), beside
`unique_checks` and `foreign_key_checks`.

### 5.3 Oracle-mode packages

| | MariaDB |
|---|---|
| `I_S.ROUTINES` | `ROUTINE_TYPE` `'PACKAGE'` and `'PACKAGE BODY'` |
| `I_S.PARAMETERS` | no rows |
| `SHOW CREATE PACKAGE [BODY]`, `DROP PACKAGE [BODY] IF EXISTS` | work in any `sql_mode` |
| `CREATE PACKAGE [BODY]` | needs `sql_mode=ORACLE` |
| `DROP PACKAGE` | drops the body too |
| namespace | its own: a function and a package may share a name |

- **Dumped by the routine pass** (`dump_routines_for_db()`), in `mysqldump`'s order:
  PACKAGE, FUNCTION, PROCEDURE, PACKAGE BODY. A specification may declare types
  the routines use, and a body may call those routines.
- **Filtered as routines.** `includeRoutines` / `excludeRoutines` select them, so
  excluding `db.pkg1` excludes the package, its body and a function of that name.
  There is no package option, as in `mysqldump`.
- **Cached as two name sets**, `Instance_cache::Schema::packages` and
  `package_bodies`: a package has no parameters or library references, which is
  all `Routine` carries.
- **The `DROP` is written bare**, `DROP PACKAGE IF EXISTS x;`. A version comment
  would hide it from the loader's statement filters (§8.3).
- **Load side.** The per-schema metadata lists `packages` and `packageBodies`. The
  existing-object check asks `I_S.ROUTINES` for the type explicitly, and
  `dropExistingObjects` drops bodies before specifications.
  `add_execution_condition()` reads `PACKAGE BODY` as one type.

### 5.4 System-versioned tables

- **A table.** `I_S.TABLES` reports one as `TABLE_TYPE='SYSTEM VERSIONED'`, which
  `fetch_tables()` treats as a table, with `Instance_cache::Table::system_versioned`
  set. A `PARTITION BY SYSTEM_TIME` table has the same type.
- **Current rows only.** The dump reads a versioned table with an ordinary
  `SELECT`, which is `mariadb-dump`'s default, so its history is not carried, and
  the loaded rows start a new history. `Dumper::warn_about_system_versioned_tables()`
  says so for every such table whose data is dumped (§9.2).
- **Never by partition.** `fetch_table_partitions()` skips a versioned table, as it
  does NDB tables. MariaDB refuses partition selection on one (error 1726), and the
  HISTORY partition holds superseded rows without their period columns, which
  would load as live data. The table is dumped whole.
  `Dump_options::validate_partitions()` refuses a versioned table in `dumpTables`'
  `partitions` option, as it refuses a partition that does not exist.
- **Views** using `FOR SYSTEM_TIME` (`ALL`, `AS OF`, `BETWEEN`) dump and load
  normally. The MySQL grammar cannot read them, which costs only the reference
  check (§8.2).

### 5.5 Column types

`mysqlshdk::db::dbstring_to_type()` maps the data types MariaDB adds. `UUID`,
`INET4`, `INET6` and `XMLTYPE` are `Type::String`: the server sends each in its
printable form and accepts it back in that form. `Type::Bytes` would hex-escape
them into something the load cannot read. `XMLTYPE` (13.1.1+) is stored like a
`LONGBLOB` with a character set.

A type the mapping does not know stops the dump with 52044
(`SHERR_DUMP_UNSUPPORTED_COLUMN_TYPE`), naming the column and table and suggesting
`excludeTables`. It is raised in `Instance_cache_builder::fetch_columns()`. There
is no default, deliberately: the type decides whether a value travels as text or
as bytes, and guessing wrong corrupts data silently. MariaDB's other data type
plugins, `associative_array` and `sys_refcursor`, exist only in stored programs.
`mysql_json` (MySQL 5.7 binary JSON, only in upgraded tables) is unmapped and
stops the dump with this error.

**JSON.** MariaDB's `JSON` is `LONGTEXT` plus an automatic `json_valid()` `CHECK`
constraint, so `I_S.COLUMNS` reports `longtext`. The wire protocol reports such a
column as JSON (`format=json` in the extended metadata, MDEV-20016), and so does
the Shell. `Instance_cache_builder::fetch_json_check_constraints()` reads
`I_S.CHECK_CONSTRAINTS` on a MariaDB source and types those columns as JSON
(`json_columns_use_check_constraints()`), applying the server's own rule
(`Field_longstr::make_send_field()`). A text column is JSON when its
**column-level** constraint has a `json_valid()` call at the top level of its
expression, alone or as a conjunct of an `AND`. An `OR` or a table-level
constraint does not count, and the argument of `json_valid()` is not looked at.

### 5.6 `WITHOUT OVERLAPS` keys

MariaDB refuses `REPLACE` on a table with a `UNIQUE ... WITHOUT OVERLAPS` key
(error 1235) and accepts `INSERT`, `LOAD DATA` and `LOAD DATA ... IGNORE`. A
period without the key, system versioning and vector indexes all accept `REPLACE`.

`I_S.KEY_PERIOD_USAGE` lists exactly these keys. `fetch_period_unique_keys()`
reads it once per dump into `Instance_cache::Table::period_unique_key`, written to
the table's metadata as `periodUniqueKey`. The loader loads such a table's chunks
with `IGNORE` instead of `REPLACE`. The two differ only for a row already present,
which on a resumed load is the same row, and resuming already relies on duplicates
being ignored. The chunks also load one at a time (§7.2).

### 5.7 Tables with no data of their own

`common::engine_manages_external_data()` is `mariadb-dump`'s `MED_ENGINES` list
(`--no-data-med`, on by default): `MRG_MyISAM`, `MRG_ISAM`, `FEDERATED`,
`CONNECT`, `OQGRAPH`, `SPIDER`, `VP`, compared case-insensitively.
`Dumper::should_dump_data()` skips such a table's rows, so its metadata says
`includesData: false` and the load creates only its DDL. A note names each one.
`Schema_dumper::check_if_ignore_table()` uses the same list.

A MERGE table is a union of tables dumped on their own. Loading its rows back
through the engine duplicated them (`INSERT_METHOD=LAST`) or failed the load
(`INSERT_METHOD=NO`). FEDERATED, Spider and CONNECT tables proxy remote servers or
files. This applies to both vendors, as `mysqldump` does it. `util.exportTable()`
still exports the rows of a table it is explicitly asked for.

`mysql.transaction_registry`, MariaDB's system-versioning bookkeeping, is dumped
without its rows, like `general_log` and `slow_log`: the server treats it as a log
table, and loading into it fails with error 1556.

### 5.8 Other features

These dump and load with identical DDL and data, and need no special handling:
`VECTOR` columns and vector indexes, `INVISIBLE`, `COMPRESSED`, `VIRTUAL` and
`PERSISTENT` columns, `IGNORED` indexes, application-time periods, Aria table
options, subpartitions, `PAGE_COMPRESSED`, dynamic columns, and combinations of
these with system versioning, sequences and JSON checks.

**Encrypted tables** dump their `ENCRYPTED` / `ENCRYPTION_KEY_ID` options, never
encrypted data, since encryption is per server. A target without a key management
plugin refuses the table with `errno: 140 "Wrong create options"`, which does not
mention encryption. That is the server's own error, as in upstream; a load-side
note explaining it is [AIPL-29](https://jira.mariadb.org/browse/AIPL-29). The
compression algorithm of a `PAGE_COMPRESSED` table is the target's
`innodb_compression_algorithm`, not part of the DDL.

### 5.9 Schema case of routines and events

With `lower_case_table_names=2`, which is the macOS default, MariaDB keeps the case of
a schema's name in `I_S.SCHEMATA`, `TABLES`, `VIEWS` and `TRIGGERS`. It stores the
schema of a routine or an event lower-cased (`mysql.proc.db`, `mysql.event.db`), so
`I_S.ROUTINES.ROUTINE_SCHEMA`, `PARAMETERS.SPECIFIC_SCHEMA` and
`EVENTS.EVENT_SCHEMA` report `t1` for schema `T1`. The server lower-cases the
name whenever `lower_case_table_names` is non-zero (`sp.cc`, `events.cc`). With 1
the tables are lower-cased as well, so the two agree, and only 2 has the mismatch.
MySQL 8.0+ reports the data dictionary's name everywhere (verified on 9.7.1).

The instance cache matches schema names exactly, both in SQL
(`includeSchemas`/`excludeSchemas` and the schema part of
`includeRoutines`/`includeEvents`, which are binary compares) and when looking up
the schema of each row. So every function, procedure, package and event of a
schema with an upper-case name was left out of `dumpSchemas()`,
`dumpInstance()` and `copy*()`, with no warning. The first sign was
`loadDump()` failing on a view that called one of the missing functions.

`Instance_cache_builder::use_schemata_case()` handles this. On MariaDB with
`lower_case_table_names=2`, it joins those three views to `I_S.SCHEMATA` on
`CAST(LOWER(SCHEMA_NAME) AS BINARY)` and reads the schema name from there, so
the filters and the lookup see `T1`. The compare is binary because the
information_schema collation, `utf8mb3_general_ci`, would also join `café` to
`cafe`. Two schemas whose names differ only in case cannot exist under
`lower_case_table_names=2`. `SHOW CREATE` and the loader's existence checks
(`routine_schema = ?`) already compare case-insensitively and needed no change.
Triggers, sequences (which are reported by `I_S.TABLES`) and tables were never
affected. Libraries do not exist on MariaDB.

`Instance_cache_test.maria_db_lower_case_routine_schema` covers it. It is skipped
unless the server is MariaDB with `lower_case_table_names=2`.

---

## 6. Accounts and roles

### 6.1 The MariaDB role model

| | MariaDB |
|---|---|
| where a role lives | `mysql.user`, `is_role='Y'`, **empty host** |
| namespace | its own: role `r` and user `r`@`%` coexist |
| `SHOW CREATE USER` for a role | error 1133, in every spelling |
| `SHOW GRANTS FOR` a role | only hostless: `` `r` `` works, `'r'@''` is error 1141 |
| `SHOW GRANTS FOR` a role, content | **transitive**: includes the grants of every role granted to it, under their own grantee |
| `DROP USER` on a role | reports success and does nothing; only `DROP ROLE` removes one |
| `WITH ADMIN` | shown as `GRANT r TO <admin> WITH ADMIN OPTION` in the administrator's grants |
| default role | a `SET DEFAULT ROLE r FOR <account>` statement at the end of `SHOW GRANTS`; the `TO` spelling is rejected |
| `PUBLIC` | a role row once it holds a grant; `CREATE ROLE PUBLIC` is error 1959 |
| `I_S.USER_PRIVILEGES` | lists every user and no role |
| `activate_all_roles_on_login`, `mandatory_roles`, `partial_revokes`, `mysql.user.account_locked` | do not exist |

Every statement MariaDB's `SHOW CREATE USER` and `SHOW GRANTS` print re-executes
as it is, including `IDENTIFIED BY PASSWORD '*hash'`,
`IDENTIFIED VIA plugin USING '...' OR plugin`, `REQUIRE SSL`, resource limits,
`ACCOUNT LOCK` and `PASSWORD EXPIRE`. Only a role's DDL has to be written by the
dumper.

### 6.2 Dumping accounts

- **Roles are accounts that know they are roles.** `Instance_cache::users` holds
  every `mysql.user` row, roles included, so the user filters, counts and
  per-account loop need no special case. `fetch_roles()` asks for `is_role='Y'`.
- **Three names per account** (`Dumped_account`):

  | | User | Role |
  |---|---|---|
  | `label`: block marker, what the loader filters and drops by | `'u'@'h'` | `` `r` `` |
  | `show_target`: for `SHOW CREATE USER` / `SHOW GRANTS FOR` | `'u'@'h'` | `` `r` `` |
  | `grantee`: for the `I_S.*_PRIVILEGES` tables | `'u'@'h'` | `'r'@''` |

- **A role's DDL is written by the dumper:** `CREATE ROLE IF NOT EXISTS <label>`,
  in a `-- begin role` / `-- end role` block. `PUBLIC` gets no create block, but its
  grants are dumped.
- **`keep_own_grants()`** keeps only the statements of `SHOW GRANTS FOR` a role
  whose grantee is that role. Otherwise a deep role graph would grant the same
  privilege several times, and a grant to an excluded role would run under a block
  the filters let through.
- **The default role** is moved out of the grants block as a whole statement, for
  both vendors, and runs after every role exists.
- **Privileges:** dumping accounts needs `SELECT` on `mysql`
  (`requires_select_on_mysql_to_dump_users()`). `SUPER` is required only on MySQL
  5.6.
- **`mariadb.sys`** is always excluded, like `mysql.sys` and `mysql.session`. It
  owns the `mysql.user` view. `Dump_instance_options::on_set_session()` adds it
  once the vendor is known.
- **`GRANT EXECUTE ON PACKAGE [BODY]`** is parsed as a routine-level grant by
  `parse_grant_statement()`, matching how the routine filters see packages.
- **52037** (`SHERR_DUMP_USERS_MARIA_DB_NOT_SUPPORTED`, upstream's BUG#34049624)
  refuses `users: true` only for a MySQL-shaped dump of a MariaDB server
  (`!is_maria_db_dialect()`), which cannot express a MariaDB account. It never
  fires in a MariaDB build.

### 6.3 Roles granted to dumped accounts

Naming a user in `includeUsers` does not name the roles granted to it, but those
roles are part of its privileges. `Instance_cache_builder::add_granted_roles()`
walks `mysql.roles_mapping` from the selected accounts and adds the roles they
hold, transitively, with a note listing them. They join both `users` and `roles`
in the cache.

An explicit `excludeUsers` wins: an excluded role is not added, nor is anything
reachable only through it. The dangling grant is reported where the exclusion
happens, and again by `dump_grants()`. A role counts as dumped there when the
filters select it or it is a hostless role among the dumped accounts
(`is_dumped_role`), so roles the deduction added are not reported.

This applies only where roles are hostless. MySQL dumps are unchanged. If
`mysql.roles_mapping` cannot be read, the dump continues with a warning.

### 6.4 DENY

MariaDB 13.1.1 has `DENY <privileges> ON <level> TO <grantee>` (13.1.0 does not).
`SHOW GRANTS` reports DENYs next to GRANTs at every level (global, schema, table,
column, routine, `PACKAGE [BODY]`), for users and roles. Since a DENY has a GRANT's
token order, `compatibility::parse_grant_statement()` reads it as one and marks it
with `Privilege_level_info::deny`. Then the role trimming, the inclusion checks and
the loader's one-privilege-at-a-time fallback handle it unchanged.
`to_grant_statement()` rebuilds a `DENY` from it. The users script carries the
lines as `SHOW GRANTS` prints them, including the password hash the global DENY
line repeats. `User_privileges` reads DENY as a revoke.

### 6.5 Loading accounts

- `preprocess_users_script()` maps a role block to
  `User_statements::Type::CREATE_ROLE`. The loader creates and grants it like a
  user, and drops it with `DROP ROLE IF EXISTS` for `dropExistingObjects`.
- `check_existing_users()` also asks `mysql.user` for roles on a MariaDB target,
  since `I_S.USER_PRIVILEGES` does not see them, and reports
  ``Role `r` already exists``.
- **Missing authentication plugin.** `ed25519`, `gssapi`, `pam` and `parsec` are
  loadable, so a target without the plugin refuses the account (error 1524). The
  load still aborts, as upstream does: skipping an account is a privilege change
  nobody asked for. Upstream's skip path exists only for HeatWave, where a plugin
  cannot be installed. `explain_missing_auth_plugin()` adds a note naming the
  plugin and the account, and says that `INSTALL SONAME` installs it, that
  `plugin_library` in the source's `I_S.PLUGINS` names the library, and how many
  accounts were already created. Accounts are created `IF NOT EXISTS`, so loading
  again after installing the plugin completes. A dump-time warning is
  [AIPL-27](https://jira.mariadb.org/browse/AIPL-27).
- **`PUBLIC`'s grants are merged, not replaced**, by design. `dropExistingObjects`
  never revokes from `PUBLIC`: it is shared state on the target, and its grants on
  schemas outside the dump are how other applications there get access. A clean
  `PUBLIC` means revoking its grants on the target before loading.

---

## 7. Loading

### 7.1 Session setup

On a MariaDB target the loader's sessions set `check_constraint_checks = 0`
(§5.2), and none of the MySQL-only variables: `sql_generate_invisible_primary_key`,
`restrict_fk_on_non_standard_key`, `innodb_parallel_read_threads`, and the
`PS_CURRENT_THREAD_ID()` probe. `check_tables_without_primary_key()` tolerates the
missing `sql_require_primary_key` row. The progress file is named after
`@@server_id`, since MariaDB has no `server_uuid`.

`createInvisiblePKs` works on MariaDB 10.3+ (`supports_invisible_pks()`): the
loader adds the column itself, through `INVISIBLE` columns, and needs no
`sql_generate_invisible_primary_key`.

### 7.2 Scheduling

`Dump_reader::schedule_chunk_proportionally()` never has two chunks of one of these
tables loading at once:

- **A table on a non-transactional engine.** The dump writes each table's `engine`
  into its metadata. At the start of a load the loader reads
  `SELECT ENGINE FROM information_schema.ENGINES WHERE TRANSACTIONS = 'YES'` into
  `m_transactional_engines`, so the target decides. Writes to Aria, MyISAM and the
  other non-transactional engines take a table lock anyway, and a chunk that fails
  part-way keeps its rows, so one chunk in flight limits what a failed load leaves
  behind. An empty `engine`, from an older dump, counts as transactional. This
  applies to both vendors.
- **A table with a `WITHOUT OVERLAPS` key** (§5.6). Concurrent inserts into a
  period-keyed unique index deadlock almost every time: 99 of 120 concurrent
  `LOAD DATA ... IGNORE` on 12.3.2, while 120 `REPLACE` loads into an ordinary
  table produced none.

Other tables keep the remaining threads busy. The key is the table's data key, so
the partitions of a partitioned table, which are separate indexes, still load
concurrently.

**`dropExistingObjects` drops a trigger only with its table.** The drops are
separate tasks on separate workers, and `DROP TABLE` takes the table's triggers
with it. A `DROP TRIGGER IF EXISTS` for one of them running at the same time fails
on MariaDB with error 13, `Can't get stat of './schema/tt.TRN'`: the server finds
a trigger through its `.TRN` file, which the table drop removes underneath it. So
the loader drops a trigger on its own only when the table it is on stays. Trigger
names are per schema, so the existing trigger of a dumped name may be on another
table than the dump's; before scheduling a schema's drops, the loader asks
`I_S.TRIGGERS.EVENT_OBJECT_TABLE` which table each existing trigger is on.

### 7.3 Deadlock retries

`common::is_rolled_back_deadlock()` is "1213, or 4060 on MariaDB", for both retry
loops: the loader's `execute_statement()` and `import_table`'s `LOAD DATA`. A
deadlock victim inside a statement whose errors are suppressed, such as
`LOAD DATA ... IGNORE`, is rolled back and refused at commit with 4060
(`ER_ROLLBACK_ONLY`). The transaction was rolled back completely, as with 1213, so
retrying from the same starting point is safe. On MySQL 4060 is
`ER_INVALID_USER_FOR_REGISTRATION`, hence the vendor check.

`importTable` keeps each chunk in memory until it is loaded
(`Allocated_file::retain_data()`), so a retried `LOAD DATA` can resend it.
`util.copy*` cannot retry: its chunks are a live stream.

### 7.4 Progress monitoring

- **Row throughput.** MariaDB has no `Innodb_rows_inserted` status variable. When
  it cannot be read the loader counts rows on the client, as the `BULK LOAD` path
  already does, and logs it once. The source is chosen on the first sample, so a
  later transient failure skips a sample rather than switching scales.
- **A failing monitor is dropped, not the thread.** `Monitoring::monitor()` catches
  per monitor, so one unreadable value cannot stop progress reporting, or the
  `kill_queries()` that a hard interrupt relies on.
- **The index stage** finishes from `m_indexes_completed`, the counter the workers
  keep, not from a copy only the monitoring thread updated. With
  `deferTableIndexes` that copy could stay behind and deadlock the progress thread
  against the main thread.

All three are vendor-neutral: MySQL without `performance_schema` hit the first, and
a progress estimate reaching 100% early could hit the third.

### 7.5 Oversized rows

`Transaction_buffer` detects a row longer than `maxBytesPerTransaction` in
`read()`, when the buffer holds a whole transaction's worth of data with no row
boundary, and `mark_oversized_row()` reports it once per row. libmariadb hands the
`LOAD DATA LOCAL` callback a fixed 4096-byte buffer (`ma_loaddata.c`), where
libmysqlclient sizes it from the net buffer, so a check on a single read never
fired on the MariaDB build.

### 7.6 `importTable`

`importTable` sets `check_constraint_checks = 0` (§5.2), retries 4060 (§7.3), and
keeps `unique_checks` on for a MariaDB target unless duplicates are replaced, to
avoid InnoDB's bulk-insert path dropping rows under `IGNORE`
(`MARIADB_PORT.md` §13.5).

---

## 8. SQL parsing

### 8.1 Quoted identifiers

An identifier escapes its quote character by doubling it, and a backslash is an
ordinary character in it: `` `a``b` `` is the name ``a`b`` and `` `a\` `` is `a\`.
The grammar's `BACK_TICK_QUOTED_ID` rule was written from the string-literal rules
and gets both wrong. `MySQLBaseLexer::nextToken()` scans quoted identifiers itself
(`scan_quoted_identifier()`), back ticks always and double quotes under
`ANSI_QUOTES`. An unterminated one falls through to the generated lexer. The
grammar is not regenerated, because the checked-in output is ANTLR 4.10.1 while the
runtime is 4.13.2, so a regeneration would rewrite the whole parser. The rule
carries a comment saying the two must be fixed together. This affects both vendors.

### 8.2 View references

MariaDB has no `I_S.VIEW_TABLE_USAGE`, so `fetch_view_metadata()` parses
`VIEW_DEFINITION` with the MySQL grammar to find a view's tables. The references
feed only `check_view_for_table_references()`, which warns about views using
tables the dump does not carry. A view the parser cannot read (`FOR SYSTEM_TIME`,
other MariaDB-only syntax) gets an empty reference set and a warning saying its
tables cannot be checked; its DDL, from `SHOW CREATE VIEW`, is dumped unchanged.
A MariaDB grammar is [AIPL-28](https://jira.mariadb.org/browse/AIPL-28).

### 8.3 MariaDB version comments

MariaDB executes `/*!NNNNN ... */` for versions up to 50600 and from 100000 (its
own numbering), and skips the MySQL 5.7 to 9.x range in between. Only MariaDB
executes `/*M!NNNNNN ... */`. The Shell's `SQL_iterator` knows `/*!` and `/*+` but
treats `/*M!` as an ordinary comment, so a statement inside one is invisible to the
loader's statement filters. MariaDB-only DDL is therefore written bare. Cross-vendor
loads are refused anyway (§1.2).

---

## 9. Known limitations

### 9.1 Engine-independent statistics

`mysql.table_stats`, `index_stats` and `column_stats` are not carried, and there is
no warning, by decision. They are system state in the `mysql` schema, not data of
the dumped schemas, and are tied to schema and table names. MariaDB's defaults
(`use_stat_tables=PREFERABLY_FOR_QUERIES`, `optimizer_use_condition_selectivity=4`)
prefer them where they exist, so a restored instance can choose different plans.
`analyzeTables: "on"` issues a plain `ANALYZE TABLE`, which collects none.
Recovery is `ANALYZE TABLE ... PERSISTENT FOR ALL` per table after the load.
`mariadb-dump --system=stats` copies the rows; the Shell has no equivalent.

An implementation would follow MySQL's histogram support: record which columns and
indexes had statistics in the table metadata, and recompute on the target with
`ANALYZE TABLE ... PERSISTENT FOR COLUMNS (...) INDEXES (...)`. The stored size
cannot be requested back (`JSON_HB` sizes itself), so the target's
`histogram_size` governs.

### 9.2 System-versioning history

Only current rows are dumped (§5.4). `mariadb-dump --dump-history` (10.11+) reads
`FOR SYSTEM_TIME ALL` and restores under `system_versioning_insert_history=1`.
[AIPL-26](https://jira.mariadb.org/browse/AIPL-26).

### 9.3 Silent cases

- A restored table may hold rows its `CHECK` constraints reject, and a package
  specification may have no body. Both are faithful to the source, and nothing
  says so.
- `FOR SYSTEM_TIME` views lose the reference check (§8.2), so a view using a
  filtered-out table is not reported if it is also unparseable.
- `I_S.USER_PRIVILEGES` is the fallback for counting accounts when `mysql.user`
  cannot be read, and it undercounts by the number of roles.
- The load's skip notes say "user" for a role.

### 9.4 Untested or not carried

- Spider, CONNECT, OQGRAPH, VP and FEDERATEDX are handled (§5.7) but not tested:
  the server packages used here do not ship them.
- `CREATE SERVER` definitions in `mysql.servers`, which Spider and FEDERATED tables
  name, are not dumped (`mariadb-dump --system=servers` carries them).
- S3 tables are dumped like any other table. `mariadb-dump` skips them unless
  `--copy-s3-tables`.
- Subpartitions of a system-versioned table.

### 9.5 Behaviour kept from upstream

- `FLUSH TABLES WITH READ LOCK` waits for the server's `lock_wait_timeout` (§3.3),
  and the backup stage bound is a constant, not an option.
- `LOCK INSTANCE FOR BACKUP` on MySQL also waits for `lock_wait_timeout` (a year by
  default); MySQL behaviour is unchanged.
- `BACKUP LOCK <table>` is not used: it is one table per connection, which a dump
  cannot afford.
- `sessionInitSql` statements are not retried, and one that inserts into an empty
  table can deadlock the loader's sessions through `MARIADB_PORT.md` §13.5.
- `source.sysvars` is written unmasked (§2.5).

### 9.6 A MySQL build reading MariaDB

A MySQL build sees a MariaDB source as MySQL 5.6 (§1.2), so its dump omits
sequences and packages and refuses MariaDB accounts. That is correct for
MariaDB → MySQL, which cannot express them, but the omission is not reported:
there is no `Compatibility_issue` for it, and that machinery is MySQL-only.

---

## 10. Testing

### 10.1 Vendor variables

`unittest/shell_script_tester.cc` defines, for the scripted tests:

| Variable | Meaning |
|---|---|
| `__server_is_maria_db` | the server under test is MariaDB (`Shell_test_env::target_server_is_maria_db()`) |
| `__mariadb_build` | the Shell is linked against libmariadb |
| `__build_server_version`, `__build_server_version_num` | `k_build_server_version` (§2.4) |
| `__server_supports_optimizer_hints` | `supports_optimizer_hints()` for the server under test |
| `__have_binlog_utils` | `HAVE_BINLOG_UTILS` |

Gate on the server unless the difference comes from the client library. A
MySQL-only case is written `#@<> title {VER(>=8.0.24) and not __server_is_maria_db}`.
`dump_utils.inc` builds `targetVersion` values from the reference version
(`newest_target_version`, `target_version_rejected_msg()`).

Only two differences belong to the build, both from libmariadb:

- `LOAD DATA LOCAL` reads in 4096-byte pieces (§7.5), so `net-buffer-length` does
  not move sub-chunk boundaries, and `util_copy_trx` sees one sub-chunk more.
- A refused connection is 2002 `Can't connect to server on '<host>'`, against
  libmysqlclient's 2003; `cannot_connect_error()` returns the pair.

`is_supported_collation()` reads the linked library's charset table, so tests of
collation replacement are gated on the build (`Schema_dumper_test.unknown_collations`
and the BUG#38089433 chunks of `util_dump_and_load`).

### 10.2 Fixtures

`/*!8xxxx ... */` clauses in fixtures are skipped by MariaDB (§8.3). They raise no
error, but their effects do not happen: no encrypted schema, no expression
defaults, no histogram, no library. So assertions about those effects are gated.
`/*!32312 IF NOT EXISTS*/`, which the dumper writes, runs on both.

The shared helpers in `setup_py/setup.py` and `dump_utils.inc` handle MariaDB:
`RESET MASTER` and the other replication keywords, clearing `gtid_slave_pos`,
`mariadb.sys` reserved in `wipeout_users()`, roles dropped with `DROP ROLE` and
snapshotted by name and grants, no components, no libraries.
`snapshot_routines()` covers procedures and functions only, not packages.

### 10.3 Suites

- [util_dump_and_load_mariadb_norecord.py](unittest/scripts/auto/py_shell/scripts/util_dump_and_load_mariadb_norecord.py)
  covers the MariaDB-only behaviour end to end, one section per topic of this
  document, and skips itself on MySQL. Chunks that inject errors with
  `testutil.set_trap()` need a debug build (`__dbug`).
- The dump, load, copy, import and export suites run on both vendors, with
  MySQL-only sections gated, not deleted. HeatWave (`ocimds`) sections and the
  `Schema_dumper_test` cases `opt_mysqlaas` and `compat_ddl` are skipped for a
  MariaDB server.
- The binlog suites are not registered without `HAVE_BINLOG_UTILS`
  (`find_py_tests()` skips scripts whose names contain `binlogs`).
- The JavaScript suites (`util_load_dump_norecord.js`, `cli_dump_*`) need `HAVE_JS`,
  which neither build here configures.
- `Instance_cache_test.stats` recomputes the counts it compares against, so it has
  to follow any change in how the cache counts tables or users.
