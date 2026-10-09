# Database Schema Change Log for `mariadb_rest_service`

## 5.0.0

- First release of the schema as `mariadb_rest_service` (was `mysql_rest_service_metadata`), with the roles `mariadb_rest_service_admin`, `_schema_admin`, `_dev`, `_user`, `_meta_provider` and `_data_provider`
- The schema and role names take an optional prefix and postfix (MSM substitutions `schema_prefix` and `schema_postfix`), so a server can hold one metadata schema per customer; no object name is qualified with the schema any more
- Removed the `mysql_task_user` role of MySQL's async tasks
- Renamed the auth vendor `MySQL Internal` to `MariaDB Internal` and the default auth app `MySQL` to `MariaDB` (their ids are unchanged)
- Renamed the tables of the REST daemon instances `router`, `router_status`, `router_session` and `router_general_log` to `rest_daemon`, `rest_daemon_status`, `rest_daemon_session` and `rest_daemon_general_log`, the view `router_services` to `rest_daemon_services`, the procedures `router_status_downsample` and `router_status_do_cleanup` to `rest_daemon_status_downsample` and `rest_daemon_status_do_cleanup`, and the events `router_status_cleanup` and `router_log_cleanup` to `rest_daemon_status_cleanup` and `rest_daemon_log_cleanup`; the columns `router_id`, `router_session_id` and `router.router_name` are now `rest_daemon_id`, `rest_daemon_session_id` and `rest_daemon.name`
- Renamed the tables of REST schemas, REST objects and their data mappings after what REST SQL calls them: `db_schema` -> `rest_schema`, `db_object` -> `rest_object`, `object` -> `data_mapping`, `object_field` -> `data_mapping_field`, `object_reference` -> `data_mapping_reference`, `mrs_db_object_row_group_security` -> `mrs_rest_object_row_group_security`, `content_set_has_obj_def` -> `content_set_has_rest_object`, the view `object_fields_with_references` -> `data_mapping_fields_with_references`, and the columns `db_schema_id`, `db_object_id` and `object_id` -> `rest_schema_id`, `rest_object_id` and `data_mapping_id`; the triggers, constraints and indexes follow, and `sdk_service_data` returns `rest_schemas` -> `rest_objects` -> `data_mappings` -> `fields`
- `rest_daemon.id` is a `UUID` defaulting to `UUID_v7()` like every other id; the ids of `rest_daemon_status`, `rest_daemon_session` and `rest_daemon_general_log` are `BIGINT UNSIGNED` (were `INT UNSIGNED`)
- Fixed `rest_daemon_status_downsample` deleting the not yet aggregated status rows of the daemons of other versions
- Renamed the `rest_daemon_status` columns `mysql_connections`, `mysql_queries` and `active_mysql_connections` to `mariadb_connections`, `mariadb_queries` and `active_mariadb_connections`, and the `mysql*` status counters of the fallback list to `mariadb*`
- `audit_log.id` is `BIGINT UNSIGNED` (was `INT`)
- `dump_audit_log` writes the MariaDB `@@server_uid` as `server_uid` instead of MySQL's `@@server_uuid`, which does not exist on MariaDB
- Changed the id of the JSON schema of `service.in_development` to `https://mariadb.com/mrs/service/in_development`
- The default landing page shows the MariaDB seal and links to the MariaDB REST Service documentation
- Changed every id column, and every foreign key to one, from `BINARY(16)` to MariaDB's `UUID` type; primary keys default to `UUID_v7()`, and `get_sequence_id()` returns one
- Ids in JSON (the audit log, `object_fields_with_references`, `sdk_service_data`) are plain UUID strings instead of hex or base64
- `sdk_service_data` returns one nested JSON document on MariaDB: the rest schemas, rest objects, data mappings and fields are JSON objects instead of strings holding JSON, and the flags (`enabled`, `published`, `requires_auth`, `internal`, `allow_filtering`, `allow_sorting`, `no_check`, `no_update`) are `true`/`false` instead of numbers or the raw `BIT(1)` byte
- Removed the MySQL-only `UUID_TO_BIN_SWAP` and `BIN_TO_UUID_SWAP` functions
- Set `NO_AUTO_CREATE_USER` in the SQL mode of the scripts, so a GRANT to a missing account fails instead of creating it
- Dropped all earlier releases, which only ran on MySQL; no earlier version can be updated to 5.0.0

## 4.1.4

- Update the `sdk_service_data` PROCEDURE to aggregate objects ordered by position

## 4.1.3

- Fixed wrong time unit when compressing router_status log entries

## 4.1.2

- Added missing privileges for `restore_roles` and `sdk_service_data` PROCEDURE

## 4.1.1

- Changed whole schema to use a USE statement and not fully qualified database object names in order to allow the schema to be ignore during replication

## 4.1.0

- Added `restore_roles` PROCEDURE to restore all ROLEs used by MRS
- Added `sdk_service_data` PROCEDURE to collect all data of a given REST service

## 4.0.4

- Added fractional seconds precision to TIMESTAMP values in log tables

## 4.0.3

- Changed the export format of the audit_log dump to valid JSON
- Enabled the `delete_old_audit_log_entries` EVENT by default
- Added `msm_instance_demoted` and `msm_instance_promoted` procedures

## 4.0.2

- Changes to the router_general_log table
- Added event to clean the router_general_log table after one day
- Fix GRANTs for msm_schema_version

Copyright (c) 2025, Oracle and/or its affiliates.
