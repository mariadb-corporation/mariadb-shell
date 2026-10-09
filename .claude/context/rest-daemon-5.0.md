# MariaDB REST Daemon: changes for metadata schema 5.0.0

Back to the index: [../PROJECT_CONTEXT.md](../PROJECT_CONTEXT.md). Related: [mrs-module.md](mrs-module.md).

The MariaDB REST Daemon (the MySQL Router fork with the MRS plugin; its code is not in this repository or in mariadb-shell-plugins) still expects the MySQL REST Service metadata schema 4.x. This file collects everything the daemon has to change to serve the metadata schema 5.0.0 that MariaDB Shell deploys (`modules/mrs/db_schema/mariadb_rest_service.msm.project`, release `releases/deployment/mariadb_rest_service_deployment_5.0.0.sql`). Nothing of 5.0.0 has been released or installed yet, so the daemon needs no compatibility with 4.x names on a MariaDB server.

Collected 2026-10-09 from the work on branch `wip/mrs_module`. Keep it up to date when the schema changes in a way the daemon sees.

## 1. Schema name, prefix and postfix

- The schema is `mariadb_rest_service` (was `mysql_rest_service_metadata`).
- A server can hold several metadata schemas, one per customer of a cloud provider: `<prefix>mariadb_rest_service<postfix>`, e.g. `acme_mariadb_rest_service_eu`. Prefix: empty, or a letter or `_` followed by `[A-Za-z0-9_]`; postfix: empty, or `_` followed by `[A-Za-z0-9_]`; the base name occurs exactly once; at most 64 characters.
- **The daemon needs a configuration option for the metadata schema name**, default `mariadb_rest_service`, and must use it in every statement on the metadata (tables, the `rest_daemon_services` view, the `rest_daemon_status_*` procedures, `msm_schema_version`). One daemon instance serves one metadata schema.
- Optional, for parity with MariaDB Shell: when no name is configured, MariaDB Shell picks `mariadb_rest_service` if visible, else the only visible schema whose name contains `mariadb_rest_service` once (valid parts) and that has an `msm_schema_version` view (`metadata::resolve_metadata_schema()` in `modules/mrs/core/mrs_metadata.cc`). An explicit setting is safer for a long-running service.
- Inside the schema, routines, triggers, views and events use unqualified names; nothing in the schema names the schema itself. The daemon does not need to rewrite anything in the schema.

## 1a. The daemon's tables are renamed, `rest_daemon.id` is a UUID

Everything named after MySQL Router is renamed after the MariaDB REST Daemon:

| 4.x name | 5.0.0 name |
|---|---|
| table `router` | `rest_daemon` |
| `router.router_name` | `rest_daemon.name` (unique index `address_name` on `address`, `name`) |
| table `router_status` | `rest_daemon_status` |
| table `router_session` | `rest_daemon_session` |
| table `router_general_log` | `rest_daemon_general_log` (indexes `log_type`, `log_thread_id`) |
| columns `router_id` (status, general log) | `rest_daemon_id` |
| column `router_general_log.router_session_id` | `rest_daemon_general_log.rest_daemon_session_id` |
| view `router_services` (columns `router_id`, `router_name`, `router_developer`) | `rest_daemon_services` (columns `rest_daemon_id`, `rest_daemon_name`, `rest_daemon_developer`) |
| procedures `router_status_downsample(time, router_version, ...)`, `router_status_do_cleanup(time)` | `rest_daemon_status_downsample(time, daemon_version, ...)`, `rest_daemon_status_do_cleanup(time)` |
| events `router_status_cleanup`, `router_log_cleanup` | `rest_daemon_status_cleanup`, `rest_daemon_log_cleanup` |
| triggers `router_*`, `router_session_BEFORE_DELETE` | `rest_daemon_*`, `rest_daemon_session_BEFORE_DELETE` |

The other columns are unchanged. Id types:

- **`rest_daemon.id` is a `UUID` with `DEFAULT UUID_v7()`**, like every other id (was `INT UNSIGNED AUTO_INCREMENT`). When the daemon registers itself (bootstrap or first start), it inserts without an id and reads the generated one back (`SELECT id FROM rest_daemon WHERE address = ? AND name = ?`; `LAST_INSERT_ID()` does not work for a UUID default), or generates a UUID itself; it stores the id in its configuration as text, as with every other id (section 4). `rest_daemon_status.rest_daemon_id` and `rest_daemon_general_log.rest_daemon_id` are `UUID` too.
- **`rest_daemon_status.id`, `rest_daemon_session.id` and `rest_daemon_general_log.id` are `BIGINT UNSIGNED AUTO_INCREMENT`** (were `INT UNSIGNED`), and so is `rest_daemon_general_log.rest_daemon_session_id`. These are high-volume rows written by the daemon; read the ids as 64-bit unsigned values. A session id the daemon keeps in memory or in tokens must hold 64 bits.
- The audit log names a daemon by its UUID (`new_row_id` / `old_row_id` = `rest_daemon.id`, `table_name` = `"rest_daemon"`); the `CAST(LPAD(HEX(id), 32, '0') AS UUID)` conversion of the integer ids is gone.
- In REST SQL, `DROP REST DAEMON` and `SHOW REST SERVICES FOR DAEMON` take the UUID as a string: `DROP REST DAEMON '0199a1b2-6c3e-7d41-9a0f-2b8c4d5e6f70'`.

Fixed on the way (the daemon only notices that they work now): `rest_daemon_status_do_cleanup()` used MySQL's `ANY_VALUE()`, which MariaDB does not have, so the hourly `rest_daemon_status_cleanup` event always failed (now `MIN()`, which prefers a published `statusVariables` list among the daemons of one version); and `rest_daemon_status_downsample()` deleted the not yet aggregated status rows of the daemons of every version, not only of the one it aggregated.

## 2. Roles

- Renamed, and they carry the schema's prefix and postfix: `<prefix>mariadb_rest_service_<role><postfix>` for `admin`, `schema_admin`, `dev`, `user`, `meta_provider`, `data_provider` (e.g. `acme_mariadb_rest_service_meta_provider_eu`). Derive them from the configured schema name: split it around `mariadb_rest_service` into prefix and postfix (`metadata::role_name()`).
- The daemon's bootstrap (MySQL Router's `--mrs` bootstrap creates the daemon accounts and grants `mysql_rest_service_meta_provider` / `mysql_rest_service_data_provider`) must grant the new names, and make them the accounts' **default roles**: MariaDB activates only the default role at login and does not support `activate_all_roles_on_login`. With one account for metadata and data, it needs both roles but only one can be the default: use two accounts, or `SET ROLE` after connecting (open point from the docs conversion).
- `mysql_task_user` (MySQL's mysql_tasks placeholder role) no longer exists; nothing grants it.
- MariaDB Shell grants the privileges on exposed database objects to the data provider role of the schema (`<prefix>mariadb_rest_service_data_provider<postfix>`), as before.

## 3. Schema version

- `msm_schema_version` (view, columns `major`, `minor`, `patch`) reports `5.0.0`. The daemon must accept 5.0.x and refuse 4.x.
- The old `schema_version` view of MySQL's versions before 4.0 does not exist.
- MariaDB Shell's `SHOW REST METADATA STATUS` reports `required_rest_daemon_version` **26.10.0** (was `required_router_version` 8.1.0, MySQL Router's number; `k_required_rest_daemon_version` in `modules/mrs/core/mrs_metadata_schema.h`): the first MariaDB REST Daemon release must support 5.0.0 and carry version 26.10.0 or later, and should refuse to start on a metadata schema that is newer than it supports.

## 4. Ids are UUIDs

- Every id column and every foreign key to one is MariaDB's `UUID` type (was `BINARY(16)`); primary keys default to `UUID_v7()`, and `get_sequence_id()` returns a UUID. The values read as canonical lower-case text, e.g. `31000000-0000-0000-0000-000000000000` (was `0x31000000000000000000000000000000`).
- The daemon must stop treating ids as 16 binary bytes (hex conversion, `UUID_TO_BIN`/`BIN_TO_UUID`, base64 of the bytes, byte-wise comparisons) and bind / compare them as UUID text.
- The MySQL-only functions `UUID_TO_BIN_SWAP` and `BIN_TO_UUID_SWAP` were removed.
- Ids inside JSON documents are plain UUID strings (was hex or base64): the audit log rows, `object_fields_with_references` and `sdk_service_data`.
- `audit_log.id` stays an AUTO_INCREMENT integer but is now `BIGINT UNSIGNED` (was `INT`): read it as a 64-bit unsigned value (see 9). The ids of the daemon's own tables changed too, see 1a.
- The `MRS` and `MariaDB Internal` vendors have fixed ids `30000000-0000-0000-0000-000000000000` and `31000000-0000-0000-0000-000000000000`; the default auth app (`MariaDB`) and the `Full Access` role have id `31000000-0000-0000-0000-000000000000`.

## 5. Auth vendor and default auth app names

- Vendor `MySQL Internal` is now `MariaDB Internal` (comment: "Provides basic authentication via MariaDB Server accounts"); the default auth app `MySQL` is now `MariaDB`. The ids did not change. Identify the vendors by id, never by name; any name-based check or message in the daemon must use the new names.
- REST SQL writes the vendor as `VENDOR MARIADB` (was `VENDOR MYSQL`); this only matters if the daemon parses REST SQL.

## 6. Async tasks are gone

- MySQL's async-task feature (the `mysql_tasks` schema, MySQL HeatWave) is not supported on MariaDB. The `mysqlTask` option of a REST routine is no longer handled by MariaDB Shell, and a REST routine's `crud_operations` are always `CREATE` (called with POST), never all four.
- The daemon's task code paths (starting a routine as a task, task status polling, kill, the `mysql_tasks` schema) are not needed; remove them or keep them unreachable. The client SDK no longer has `start()`, `watch()` or `kill()`.

## 7. Status counters (`rest_daemon_status` and the status variables)

- Renamed columns of `rest_daemon_status`: `mysql_connections` → `mariadb_connections`, `mysql_queries` → `mariadb_queries`, `active_mysql_connections` → `active_mariadb_connections`. The daemon writes these rows and must use the new names.
- The daemon publishes its counters in `rest_daemon.attributes` → `statusVariables`, a JSON list of `{"name": ..., "column": ..., "nonResettable": ...}`; `rest_daemon_status_do_cleanup()` downsamples `rest_daemon_status` with that list per daemon version. The entries that name a column must use the new columns.
- The fallback list for daemons that publish none (`old_status_variables` in `rest_daemon_status_do_cleanup()`, `development/sections/150-20_procedures_functions.sql`) now names the counters `mariadbConnectionsReused`, `mariadbConnectionsCreated`, `mariadbConnectionsClosed`, `mariadbConnectionsActive`, `mariadbQueries`, `mariadbChangeUser`, `mariadbPrepareStmt`, `mariadbExecuteStmt`, `mariadbRemoveStmt` (were `mysql...`). The daemon should report its counters under these names, or always publish `statusVariables`.

## 8. Smaller changes

- The JSON schema of `service.in_development` (CHECK constraint) has the id `https://mariadb.com/mrs/service/in_development` (was `https://dev.mysql.com/mrs/service/in_development`); only relevant if the daemon validates against it by id.
- `rest_daemon.product_name` is documented as e.g. `'MariaDB REST Daemon'` (was `'MySQL Router'`); the daemon should report its own product name there.
- The default landing page (`config.data` → `defaultStaticContent`, served from the root URI) now holds `index.html`, `favicon.ico`, `favicon.svg`, `standalone-preact.js` and `mariadb-seal.svg` (was `sakila.svg`); its texts say MariaDB REST Service / MariaDB REST Daemon and link to the MariaDB REST Service docs.
- `audit_log` and its triggers are unchanged in shape apart from the wider id (see 9).
- MariaDB Shell's `SHOW REST DAEMONS` treats a daemon as active when `rest_daemon.last_check_in` is less than 10 seconds old.

## 9. Audit log ids are not in commit order (Galera)

- `audit_log.id` is `BIGINT UNSIGNED AUTO_INCREMENT`. On a Galera cluster, `wsrep_auto_increment_control` (on by default) gives every node its own offset and an increment of the cluster size, so the ids are unique across the nodes, with gaps. The schema deliberately uses no sequence: on Galera a sequence must be `INCREMENT BY 0` (the same offsets) or `NOCACHE` (a replicated round trip per value, with certification conflicts), and its values are not in commit order either.
- An id is assigned when the trigger inserts the row, not when the transaction commits. With concurrent writers, and much more so on Galera with several write nodes (where the window is the replication and certification delay), a row with a lower id can become visible after a higher one: node A inserts id 10, node B inserts id 11 and commits first, a reader sees 11, then A's 10 arrives.
- **If the daemon polls the audit log with `WHERE id > <last seen id>`** (MySQL Router's MRS plugin reads the changes after the last id it saw; check the daemon's code), it misses such rows and keeps serving the old state of the changed object until something else reloads it. Change the polling to:
  - re-read an overlap window on every poll, e.g. `WHERE id > <last seen id> - <margin>` or the entries of the last few seconds by `changed_at`, and skip the ids already processed (keep the ids of the window in memory);
  - and set `wsrep_sync_wait` (e.g. `1`, for reads) on the polling connection, so a poll sees everything the cluster has committed before it. This does not cover transactions that have not committed yet, so it does not replace the overlap window.
- `changed_at` is `CURRENT_TIMESTAMP` of the node that made the change, so it is not a cluster-wide order either; a window by time needs a margin for clock skew.
- Do not use a single-row change counter that every trigger increments: concurrent metadata changes on different nodes would conflict on that row and be rolled back.
- MariaDB Shell does the same for its own cache: its fingerprint is `MAX(id)` and `COUNT(*)` of the audit log (`metadata::Audit_log_mark`), so a late row with a lower id still changes it. Its `metadata_version` (last column of `SHOW REST METADATA STATUS`) is only `MAX(id)` and documented as possibly missing such a change.

## 10. Open points

- Unverified on MariaDB: `readOwnWrites` / GTID handling (MariaDB GTIDs differ from MySQL's) and the `$asof` / `$scn` filters.
