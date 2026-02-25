#@<OUT> Execute upgrade checker vs MHS instance
The MySQL server at 100.103.26.184:3306, version 9.6.0-cloud - MySQL Enterprise
- Cloud, will now be checked for compatibility issues for upgrade to MySQL
9.7.0. To check for a different target server version, use the targetVersion
option.

1) System variable check for deprecation, removal, changes in defaults values
or invalid values. (sysVars)
   Following system variables that were detected as being used ware changed,
   deprecated or removed. Please update  your system accordingly before the
   upgrade.

   Error: The following system variables that were detected as being used are
   using values that are  invalid in the target version.
   - optimizer_switch: variable value includes 'hypergraph_optimizer=off',
   allowed values include: batched_key_access=on, batched_key_access=off,
   block_nested_loop=on, block_nested_loop=off, condition_fanout_filter=on,
   condition_fanout_filter=off, derived_condition_pushdown=on,
   derived_condition_pushdown=off, derived_merge=on, derived_merge=off,
   duplicateweedout=on, duplicateweedout=off, engine_condition_pushdown=on,
   engine_condition_pushdown=off, firstmatch=on, firstmatch=off, hash_join=on,
   hash_join=off, hash_set_operations=on, hash_set_operations=off,
   hypergraph_optimizer=on, index_condition_pushdown=on,
   index_condition_pushdown=off, index_merge=on, index_merge=off,
   index_merge_intersection=on, index_merge_intersection=off,
   index_merge_sort_union=on, index_merge_sort_union=off, index_merge_union=on,
   index_merge_union=off, loosescan=on, loosescan=off, materialization=on,
   materialization=off, mrr=on, mrr=off, mrr_cost_based=on, mrr_cost_based=off,
   prefer_ordering_index=on, prefer_ordering_index=off, semijoin=on,
   semijoin=off, skip_scan=on, skip_scan=off,
   subquery_materialization_cost_based=on,
   subquery_materialization_cost_based=off, subquery_to_derived=on,
   subquery_to_derived=off, use_index_extensions=on, use_index_extensions=off,
   use_invisible_indexes=on, use_invisible_indexes=off.

   Warning: The following system variables that were detected as being used are
   deprecated.  Consider updating your system to not rely on them before the
   upgrade.
   - audit_log_buffer_size: variable deprecated at version 9.6.0 is set to
   10485760 (COMMAND_LINE)..
   - audit_log_compression: variable deprecated at version 9.6.0 is set to GZIP
   (COMMAND_LINE)..
   - audit_log_database: variable deprecated at version 9.6.0 is set to
   mysql_audit (COMMAND_LINE)..
   - audit_log_file: variable deprecated at version 9.6.0 is set to
   /db/audit/audit.log (COMMAND_LINE)..
   - audit_log_flush_interval_seconds: variable deprecated at version 9.6.0 is
   set to 60 (COMMAND_LINE)..
   - audit_log_format: variable deprecated at version 9.6.0 is set to JSON
   (COMMAND_LINE)..
   - audit_log_format_unix_timestamp: variable deprecated at version 9.6.0 is
   set to ON (COMMAND_LINE)..
   - audit_log_max_size: variable deprecated at version 9.6.0 is set to
   5368709120 (PERSISTED)..
   - audit_log_prune_seconds: variable deprecated at version 9.6.0 is set to
   604800 (COMMAND_LINE)..
   - audit_log_rotate_on_size: variable deprecated at version 9.6.0 is set to
   5242880 (PERSISTED)..
   - audit_log_strategy: variable deprecated at version 9.6.0 is set to
   ASYNCHRONOUS (PERSISTED)..
   - connection_control_max_connection_delay: variable deprecated at version
   9.2.0 is set to 10000 (PERSISTED)..
   - log_bin_trust_function_creators: variable deprecated at version 8.0.34 is
   set to ON (PERSISTED)..
   - log_statements_unsafe_for_binlog: variable deprecated at version 8.0.34 is
   set to OFF (PERSISTED)..
   - performance_schema_show_processlist: variable deprecated at version 8.0.35
   is set to ON (PERSISTED)..
   - sha256_password_private_key_path: variable deprecated at version 8.0.16 is
   set to /db/metadata/pki/customer_endpoint/private_key.pem (PERSISTED)..
   - sha256_password_public_key_path: variable deprecated at version 8.0.16 is
   set to /db/metadata/pki/customer_endpoint/public_key.pem (PERSISTED)..
   - slave_parallel_workers: variable deprecated at version 8.0.26 is set to 12
   (PERSISTED)., consider using 'replica_parallel_workers' instead.
   - slave_preserve_commit_order: variable deprecated at version 8.0.26 is set
   to ON (PERSISTED)., consider using 'replica_preserve_commit_order' instead.



2) Issues reported by 'check table x for upgrade' command (checkTableCommand)
   No issues found

3) MySQL syntax check for routine-like objects (syntax)
   No issues found

4) Checks for foreign keys not referencing a full unique index
(foreignKeyReferences)
   No issues found

5) Check for deprecated or invalid user authentication methods.
(authMethodUsage)
   No issues found
Errors:   1
Warnings: 19
Notices:  0

ERROR: 1 errors were found. Please correct these issues before upgrading to avoid compatibility issues.

