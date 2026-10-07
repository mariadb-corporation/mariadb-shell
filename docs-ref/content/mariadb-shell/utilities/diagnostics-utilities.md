---
description: >-
  The util.debug collectors of MariaDB Shell, which package server, shell,
  and host information into a ZIP file, and their current status with
  MariaDB Server.
---

# Diagnostics Utilities

The `util.debug` object provides three collectors that gather diagnostic information into one ZIP file, for troubleshooting or for attaching to a support request:

| Function | Purpose |
| --- | --- |
| `util.debug.collect_diagnostics()` | A one-time snapshot of the server configuration, status, and schema statistics. |
| `util.debug.collect_high_load_diagnostics()` | Repeated snapshots of performance metrics over a period, for a server that is under heavy load. |
| `util.debug.collect_slow_query_diagnostics()` | Runs one query and collects its execution plan, optimizer trace, and the performance metrics during its execution. |

{% hint style="danger" %}
**Not usable with MariaDB Server in MariaDB Shell 26.9.5.** All three collectors stop with the following error when the global session is connected to MariaDB Server, and delete the partial ZIP file:

```text
An error occurred during data collection. Partial output deleted.
mysqlsh.DBError: MySQL Error (1193): Unknown system variable 'server_uuid'
```

The collectors were written for MySQL Server and query system variables, tables, and `sys` schema objects that MariaDB Server doesn't have, starting with `@@server_uuid`. The `ignoreErrors` option doesn't avoid the error. Until the collectors support MariaDB Server, collect the information you need with SQL, for example `SHOW GLOBAL VARIABLES`, `SHOW GLOBAL STATUS`, `SHOW ENGINE INNODB STATUS`, and `SHOW FULL PROCESSLIST`, and include the server error log and the [MariaDB Shell log](../logging-and-debugging.md).
{% endhint %}

The rest of this page describes the collectors as they are implemented, for use with MySQL Server and for when MariaDB Server support is added. The details come from the built-in help (`\? util.debug`) and the source of the `debug` plugin.

## Common Behavior

* The collectors need an open global session. Each one opens its own session with the same connection options.
* `path` is the ZIP file to create. If it doesn't end in `.zip`, the extension is added. If it ends in `/` (or `\` on Windows), a file named `mysql-diagnostics-<date>-<time>.zip` is created in that directory. An existing file is never overwritten.
* The ZIP file is created with mode `rw-------` on systems with POSIX permissions, because it can contain configuration details and query text. All entries are stored under one top-level directory named after the file.
* If anything fails during collection, the ZIP file is deleted.
* When the session connects to `localhost`, the collectors can also run operating system commands on the database host (the `hostInfo` and `customShell` options). Over TCP to any other host name, including `127.0.0.1`, host information is not collected and `customShell` is refused.

The connected account needs broad read access. The minimal account in the test suite of the collectors has `SELECT`, `PROCESS`, `REPLICATION CLIENT`, and `REPLICATION SLAVE` on `*.*`, and `EXECUTE`, `SELECT`, and `CREATE TEMPORARY TABLES` on `sys.*`. A `pfsInstrumentation` value other than `current` also needs the right to update the Performance Schema setup tables.

## collect\_diagnostics()

```python
util.debug.collect_diagnostics(path[, options])
```

Collects one snapshot of:

* The shell version, the shell options, the host name of the client, and the MariaDB Shell log file.
* Server identity and time settings, global variables, and the error log. On `localhost`, the error log file is copied; otherwise it is read through SQL where possible.
* InnoDB status and metrics, lock information, and the Performance Schema configuration.
* Replication status.
* Schema statistics: tables without a primary key, unused indexes, and stored routine sizes. With `schemaStats`, also the 20 largest tables with their indexes, the tables per storage engine, and all of `information_schema.TABLES`.
* Optionally, the slow query log, and host information.

| Option | Type | Default | Description |
| --- | --- | --- | --- |
| `schemaStats` | Boolean | `false` | Also collect the full schema statistics. |
| `slowQueries` | Boolean | `false` | Also collect slow query information. Requires `slow_query_log=ON` and `log_output=TABLE`. |
| `innodbMutex` | Boolean | `false` | Also collect `SHOW ENGINE INNODB MUTEX`. This statement can affect the performance of a busy server. |
| `ignoreErrors` | Boolean | `false` | Continue when a diagnostic query fails. |
| `hostInfo` | Boolean | `true` | On `localhost`, run operating system diagnostic commands. See [Host Information](#host-information). |
| `customSql` | list of strings | `[]` | Additional SQL statements to run. Each result is stored in the ZIP file. |
| `customShell` | list of strings | `[]` | Additional operating system commands to run. Only on `localhost`. |

## collect\_high\_load\_diagnostics()

```python
util.debug.collect_high_load_diagnostics(path[, options])
```

Collects general information once, then collects performance metrics several times with a pause in between, so that you can compare the snapshots. The general information includes the shell options, schema statistics, stored routine statistics, the process list, open tables, the error log, slow queries, and the time that a fixed `SELECT BENCHMARK()` loop takes on the server. Each iteration collects InnoDB status, lock, buffer pool, and transaction information, and Performance Schema metrics.

With the defaults, the collection takes about five minutes: two iterations with a pause of 300 seconds.

| Option | Type | Default | Description |
| --- | --- | --- | --- |
| `iterations` | integer | `2` | Number of metric snapshots. |
| `delay` | integer | `300` | Seconds to wait between snapshots. |
| `pfsInstrumentation` | string | `current` | `current` leaves the Performance Schema configuration as it is. `medium` enables all consumers except the history consumers, and all instruments except `wait/synch/%`. `full` enables all consumers and instruments. The original configuration is restored at the end. More instrumentation gives more detail, at a cost in server performance. |
| `innodbMutex` | Boolean | `false` | Also collect `SHOW ENGINE INNODB MUTEX`. |
| `hostInfo` | Boolean | `true` | On `localhost`, run operating system diagnostic commands. |
| `customSql` | list of strings | `[]` | Additional SQL statements. Prefix a statement with `before:` (the default), `during:`, or `after:` to run it once before the first snapshot, once in each snapshot, or once after the last snapshot. |
| `customShell` | list of strings | `[]` | Additional operating system commands, with the same prefixes as `customSql`. Only on `localhost`. |

## collect\_slow\_query\_diagnostics()

```python
util.debug.collect_slow_query_diagnostics(path, query[, options])
```

Runs `query` once in a separate session, collects metrics in the background while it runs, and adds the following to what `collect_high_load_diagnostics()` collects:

* The `EXPLAIN` output and the optimizer trace of the query.
* The DDL and statistics of every table that the query references.
* The execution time, the fetch time, the number of rows, and the warnings of the query.

The query really runs. Don't pass a statement that changes data unless you intend to change it.

| Option | Type | Default | Description |
| --- | --- | --- | --- |
| `delay` | integer | `15` | Seconds between the background metric snapshots while the query runs. The built-in help states 5 seconds; the code uses 15. |
| `pfsInstrumentation` | string | `current` | As for `collect_high_load_diagnostics()`. |
| `innodbMutex` | Boolean | `false` | Also collect `SHOW ENGINE INNODB MUTEX`. |
| `hostInfo` | Boolean | `true` | On `localhost`, run operating system diagnostic commands. |
| `customSql` | list of strings | `[]` | Additional SQL statements, with the `before:`, `during:`, and `after:` prefixes. |
| `customShell` | list of strings | `[]` | Additional operating system commands, with the same prefixes. Only on `localhost`. |

## Host Information

With `hostInfo` enabled and a session to `localhost`, the collectors run a fixed set of commands and store their output:

* **Linux:** system and kernel information (`uname`, `lsb_release`, `/proc/cpuinfo`, `/proc/meminfo`), memory, swap, and NUMA statistics, mounts and disks (`df`, `lsblk`, LVM commands), CPU, I/O, and memory sampling over a few seconds (`mpstat`, `iostat`, `vmstat`, `top`), the process list, `sysctl -a`, kernel messages, and `sudo sosreport`. The `sudo` command can prompt for your password.
* **macOS:** `uname`, mounts and disks, `iostat`, `top`, the process list, resource limits, kernel messages, and listening sockets.
* **Windows:** `systeminfo` and an `msinfo32` report.

Commands that aren't installed or aren't permitted record their error and don't stop the collection.

## Command Line Use

All three collectors support [command line integration](../using-mariadb-shell/command-line-integration.md):

```sh
mariadb-shell admin@db1.example.com -- util debug collect-diagnostics /tmp/diag.zip --schema-stats=true
```
