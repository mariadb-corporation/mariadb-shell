---
description: >-
  Set up, start, stop, and delete MariaDB REST Daemons through the MariaDB MCP
  server, for sandbox instances and other servers.
---

<!--
  SYNCED: this page is maintained in mariadb-corporation/mariadb-shell, under docs-ref/content/,
  and copied one way into mariadb-docs (tools/community/mariadb-shell/).
  Make every change in mariadb-corporation/mariadb-shell. A change made in mariadb-docs or in the
  GitBook editor is overwritten by the next sync.
-->

# REST Daemon Tools

The `restd` tools of the MCP server set up a MariaDB REST Daemon, `mariadb-restd`, for a server, and start, stop, list, and delete such deployments. The daemon serves the REST services defined in the server's [REST metadata](../../mariadb-rest-service/README.md). Once started, it keeps running when the MCP server ends, as a sandbox's server does.

`restd.configure_restd` sets a daemon up for a connection opened with `db.connect`, so the server provides it only when it provides the `db` group as well. The other tools work on the deployment folders and need no connection. The server never provides the `restd` group in [multi-tenant mode](multi-tenant-mode.md).

## Tools

| Tool | Does | Arguments |
| --- | --- | --- |
| `restd.configure_restd` | Sets up a daemon for a server and starts it. See [Setting Up a Daemon](#setting-up-a-daemon). | `connection_id`, `password`, `sandbox_port`, `sandbox_dir`, `port`, `metadata_schema`, `restd_path` |
| `restd.start_restd` | Starts the daemon of a deployment. Does nothing when it is running already. | `directory`, `restd_path` |
| `restd.stop_restd` | Stops the daemon of a deployment. A daemon that doesn't end within 30 seconds is killed. | `directory` |
| `restd.delete_restd_config` | Stops the daemon and deletes its deployment. See [Deleting a Deployment](#deleting-a-deployment). | `directory`, `connection_id`, `drop_accounts` |
| `restd.get_restd_status` | Lists the deployments, running and stopped. | None |

`directory` is a deployment's folder, as `restd.get_restd_status` lists it. `restd_path` is the `mariadb-restd` executable to run, or the folder it is in; when you leave it out, the tools look for it on the `PATH`, in `~/.local/bin`, and among the releases that its install script unpacked into `~/.local/share/mariadb-restd/<version>/bin` (`%LOCALAPPDATA%\Programs\mariadb-restd\<version>\bin` on Windows), taking the newest. The environment variable `MARIADB_RESTD_PREFIX` names another folder for these releases.

`restd.get_restd_status`, `restd.start_restd`, `restd.stop_restd`, and `restd.configure_restd` return the state of a deployment, one object for each:

| Field | Content |
| --- | --- |
| `directory` | The deployment's folder. |
| `host`, `server_port` | The server that the daemon serves. |
| `port`, `url` | The port of the daemon, and its address, such as `https://localhost:8443`. |
| `status`, `pid` | `running` or `stopped`, and the daemon's process ID, `null` when it is stopped. |
| `log_path` | The daemon's log file. |
| `metadata_schema` | The REST metadata schema that the daemon serves. |
| `connection_uri` | The connection that the deployment was set up with. |
| `sandbox_port` | The port of the sandbox instance whose daemon it is, `null` for another server. |

`restd.get_restd_status` lists the deployments of the sandbox instances in the default sandbox directory and those of other servers.

## Setting Up a Daemon

`restd.configure_restd` takes the connection ID of a connection to the server, opened with `db.connect`. For a sandbox instance, also pass its port as `sandbox_port`, and `sandbox_dir` if it isn't in the default sandbox directory. The tool works in this order:

1. **Checks the password.** It opens a session of its own with the password you pass, or, without one, the password stored for the connection. The setup gets the same password.
2. **Chooses the REST metadata schema.** It takes the one that `metadata_schema` names, or else the one the server has. If the server has several, you must name one. If it has none, the tool creates `mariadb_rest_service` (or the schema you named) with [CONFIGURE REST METADATA](../../mariadb-rest-service/rest-sql-reference/rest-metadata.md#configure-rest-metadata).
3. **Finds `mariadb-restd`.** Where it isn't found, the tool installs its latest release with the install script of the MariaDB REST Daemon, after it asks the MCP client to confirm, using the MCP elicitation feature. The MariaDB extension for Visual Studio Code, which starts the server with `--gui`, installs it without asking. If the client can't ask, or you decline, the tool stops; install `mariadb-restd` yourself, or pass `restd_path`. The environment variables `MARIADB_RESTD_TAG`, `MARIADB_RESTD_PREFIX`, and `MARIADB_RESTD_BINDIR` are passed on to the install script.
4. **Checks the account's privileges.** The setup creates a server account for the daemon and grants it the metadata schema's roles, so the connection's account needs:
   * `CREATE USER` on `*.*`, and the `GRANT OPTION` on `*.*`
   * `SELECT`, `INSERT`, and `UPDATE` on the metadata schema
   * the schema's two roles `WITH ADMIN OPTION`, which are `mariadb_rest_service_meta_provider` and `mariadb_rest_service_data_provider` for the default schema

   `ALL PRIVILEGES ON *.* WITH GRANT OPTION`, as `root` has it, covers all of them. The tool names whatever is missing.
5. **Runs `mariadb-restd --setup`.** The setup registers the daemon in the REST metadata, creates a server account `mariadb_restd_<random>` for it, and writes the deployment: a keyring with the account's password and a generated JWT secret, a self-signed TLS certificate, and the configuration. The password goes to the setup's standard input, never on its command line.
6. **Starts the daemon** and waits until it accepts connections.

Besides the deployment's state, the tool returns `installed`, whether it installed `mariadb-restd`, and `setup_output`, what the setup printed. If there is a deployment in the folder already, the tool refuses to set up another one; delete it first with `restd.delete_restd_config`.

### Deployment Folders and Ports

| Server | Deployment folder | Port of the daemon |
| --- | --- | --- |
| A sandbox instance | `mariadb-restd` in the sandbox's folder, `<sandboxDir>/<port>/mariadb-restd` | The first free port from 8443 up |
| Another server | `~/.mariadb-restd/<host>_<port>`, on Windows in `%APPDATA%\MariaDB\mariadb-restd` | The first free port from 8443 up |

To choose the port yourself, pass `port`. The ports of stopped deployments count as taken, so that a deployment can start again on its own port. The daemon serves HTTPS with the self-signed certificate of the setup, so a client must accept that certificate. It logs to `log/mariadb-restd.log` in its deployment folder; `deployment.json` there records the connection that the deployment was set up with.

## Daemons of Sandbox Instances

The daemon of a sandbox instance follows the sandbox:

* `sandbox.start` starts the daemon after the server. If the daemon doesn't start, the sandbox stays started and the tool's message says why; start the daemon again with `restd.start_restd`.
* `sandbox.stop` and `sandbox.kill` stop the daemon before the server.
* `sandbox.delete` stops the daemon before it deletes the sandbox's folder, which holds the deployment.

This happens whether or not the server provides the `restd` group. These are the `sandbox` tools of the MCP server; the [`sandbox` object](../sandbox-instances.md) of MariaDB Shell leaves a daemon alone.

## Deleting a Deployment

`restd.delete_restd_config` stops the daemon, drops the server account that the setup created for it, removes the daemon's registration from the REST metadata, and deletes the deployment folder. It drops only accounts named `mariadb_restd_<random>`, as the setup generates them, never an account that you named. For the server objects, pass the `connection_id` of a connection to the deployment's server, opened with `db.connect`. If the registration can't be removed, the tool keeps it and says why.

When the server is gone, or about to be, pass `drop_accounts` as `false` to delete only the folder.

## Allowed Paths

`sandbox_dir` and `restd_path` are checked against the [allowed directories](configuring-access.md#allowed-paths). A `directory` that `restd.get_restd_status` lists, a sandbox instance's deployment or one in `~/.mariadb-restd`, is accepted without the check, because the MCP server created it. Any other `directory` is checked too.

<sub>_This page is licensed: GPLv2_</sub>
