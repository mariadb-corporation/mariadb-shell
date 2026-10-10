---
description: >-
  Dump, load, copy, export and import data through the MariaDB MCP server,
  with background tasks a client follows and can cancel.
---

# Dump and Load Tools

The `util` tools of the MCP server run the [dump](../utilities/dump-and-load/dump-utilities.md), [load](../utilities/dump-and-load/load-dump-utility.md), [copy](../utilities/dump-and-load/copy-utilities.md), [export and import](../utilities/table-export-and-import.md) utilities of MariaDB Shell on a connection opened with `db.connect`. These utilities run for minutes or hours, longer than an MCP client waits for a tool call to return, so each tool starts the work as a background **task** and returns its ID at once. The client then follows the task, and can cancel it.

The server provides these tools only when it provides the `db` group as well, and never in [multi-tenant mode](multi-tenant-mode.md).

## Tools

| Tool | Starts | Arguments |
| --- | --- | --- |
| `util.dump_instance` | [`util.dump_instance()`](../utilities/dump-and-load/dump-utilities.md) | `connection_id`, `output_url`, `options` |
| `util.dump_schemas` | `util.dump_schemas()` | `connection_id`, `schemas`, `output_url`, `options` |
| `util.dump_tables` | `util.dump_tables()` | `connection_id`, `schema`, `tables`, `output_url`, `options` |
| `util.export_table` | [`util.export_table()`](../utilities/table-export-and-import.md) | `connection_id`, `table`, `output_url`, `options` |
| `util.load_dump` | [`util.load_dump()`](../utilities/dump-and-load/load-dump-utility.md) | `connection_id`, `url`, `options` |
| `util.import_table` | `util.import_table()` | `connection_id`, `urls` (a file or a list; names may contain `*` and `?`), `options` |
| `util.copy_instance` | [`util.copy_instance()`](../utilities/dump-and-load/copy-utilities.md) | `connection_id`, `target_connection_id`, `options` |
| `util.copy_schemas` | `util.copy_schemas()` | `connection_id`, `schemas`, `target_connection_id`, `options` |
| `util.copy_tables` | `util.copy_tables()` | `connection_id`, `schema`, `tables`, `target_connection_id`, `options` |
| `util.get_task` | Returns the state of a task. | `task_id`, `since`, `wait_ms` |
| `util.list_tasks` | Lists the tasks of the client. | None |
| `util.cancel_task` | Stops a task. | `task_id` |

`options` holds the utility's own options, with their camelCase names, for example `{"threads": 8, "users": false}`. The tools set `session`, `progressCallback`, and `showProgress` themselves, and refuse them in `options`. The target of a copy is a second connection opened with `db.connect`.

Each start tool returns the task's ID and status:

```json
{"task_id": "4c0d0f0e-5a51-4a34-9c6e-0b8d2f3c1e77", "status": "running"}
```

## Following a Task

`util.get_task` returns the task's state:

| Field | Content |
| --- | --- |
| `status` | `pending`, `running`, `completed`, `failed`, or `cancelled`. |
| `title`, `kind` | What the task does, such as `Dump shop to /backups/shop` and `dump_schemas`. |
| `stages` | The stages of the utility so far, such as `Gathering information`, `Writing DDL`, and `Dumping data`, each with its status and, once finished, its duration in seconds. |
| `stage`, `progress` | The current stage and its progress: `current`, `total`, and `percent`. For a stage that moves data, also `throughput` (items per second), `eta_seconds`, and `items` (`bytes` or `rows`). |
| `messages` | What the utility printed after `since`, each with `seq`, `time`, `level` (such as `status`, `warning`, or `error`), and `text`. |
| `next_since` | The `since` to pass on the next call, to get only the messages that are new. |
| `result`, `error` | Once the task ended: what it produced, such as the dump's folder, or why it failed. |

With `wait_ms`, up to 30000, the call waits until something changes before it answers, so a client can follow a task without calling in a tight loop:

```json
{"task_id": "4c0d0f0e-5a51-4a34-9c6e-0b8d2f3c1e77", "since": 12, "wait_ms": 2000}
```

## Cancelling a Task

`util.cancel_task` stops the utility at its next progress update, as Ctrl+C does when you run it in MariaDB Shell, and the task ends as `cancelled`. A load that was cancelled resumes where it stopped when you load the same dump again.

## How Tasks Run

* **A task has a session of its own.** It's opened on the connection that `connection_id` names, with the same checks as a session that is reopened after an idle period. The connection's own session stays free, so other tools can use it while the task runs.
* **Tasks belong to the client that started them,** as connections do. Another client gets the same error as for a task that doesn't exist.
* **Limits:** a client can run four tasks at the same time. A finished task is kept for one hour, and the server keeps 100 finished tasks at most.
* **Paths** are checked against the [allowed directories](configuring-access.md). A URL, and an object storage prefix given with `s3BucketName`, `osBucketName`, or `azureContainerName`, are passed to the utility unchecked.

The tools need a MariaDB Shell whose utilities accept the `session` and `progressCallback` options; see [Running on Your Own Session](../utilities/dump-and-load/dump-utilities.md#running-on-your-own-session). With an older shell, every task fails with a message saying so.

## Related Pages

{% content-ref url="starting-the-mcp-server.md" %}
[starting-the-mcp-server.md](starting-the-mcp-server.md)
{% endcontent-ref %}

{% content-ref url="../utilities/dump-and-load/dump-utilities.md" %}
[dump-utilities.md](../utilities/dump-and-load/dump-utilities.md)
{% endcontent-ref %}
