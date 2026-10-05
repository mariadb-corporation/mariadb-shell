---
description: >-
  Start the MariaDB MCP server with mcp start-server, choose between the stdio
  and streamable HTTP transports, select the tool groups it provides, and read
  its log output.
---

# Starting the MCP Server

`mcp start-server` starts the MCP server. It runs in the foreground until it is stopped, and serves the tools to one MCP client over standard input and output, or to any number of clients over HTTP:

```bash
mariadb-shell -- mcp start-server --transport=stdio
```

In most setups, you don't run this command yourself. You register it with your MCP client, which starts the server when it needs it and stops it when it exits. See [Connecting MCP Clients](connecting-mcp-clients.md). Run the command yourself to serve over HTTP, or to test the server.

Configure the connections and directories the server may access before you start it, as described in [Configuring Access](configuring-access.md). A server without a configuration starts, but refuses every connection and every path.

## Options

| Option | Default | Description |
| --- | --- | --- |
| `--transport=<name>` | `streamable-http` | The MCP transport: `stdio` or `streamable-http`. See [Transports](#transports). |
| `--host=<address>` | `127.0.0.1` | The address that the HTTP server binds to. Only used by `streamable-http`. See [Security and Session Handling](security-and-session-handling.md#network-exposure) before you change it. |
| `--port=<number>` | `8080` | The TCP port of the HTTP server. Only used by `streamable-http`. |
| `--functionGroups=<list>` | All groups | The tool groups to provide, as a comma-separated list of `db`, `msm`, `sandbox`, and `migrator`. See [Function Groups](#function-groups). |
| `--allowedHosts=<list>` | None | Additional values of the HTTP `Host` header to accept, for a server that clients reach under another name, for example through a reverse proxy. Only used by `streamable-http`. See [Host and Origin Validation](security-and-session-handling.md#host-and-origin-validation). |
| `--gui` | Off | Reserved for the MariaDB extension for Visual Studio Code, which starts the server with it. Don't use it for other clients: in this mode, the server allows access to every local path. |

To show the built-in help for the options, run:

```bash
mariadb-shell -- mcp start-server --help
```

While the server runs, it disables the interactive wizards of MariaDB Shell, so that no tool waits for input that nobody can give.

## Transports

### stdio

With `--transport=stdio`, the server exchanges MCP messages over its standard input and output. The MCP client starts the server as a child process, and the server serves only that client. It exits when the client closes its standard input.

```bash
mariadb-shell -- mcp start-server --transport=stdio
```

Use `stdio` whenever the MCP client runs on the same machine and can start a command, which applies to most coding agents and desktop applications. It needs no network port, and no other process can talk to the server.

Standard output carries only MCP messages. The server redirects all other output, including messages of MariaDB Shell and of the tools, to standard error, which MCP clients typically write to their MCP log.

### Streamable HTTP

With `--transport=streamable-http`, which is the default, the server listens on an HTTP port and serves every client that connects to it. The MCP endpoint is `/mcp`:

```bash
mariadb-shell -- mcp start-server --port=8080
```

```text
INFO:     Started server process [84513]
INFO:     Waiting for application startup.
StreamableHTTP session manager started
INFO:     Application startup complete.
INFO:     Uvicorn running on http://127.0.0.1:8080 (Press CTRL+C to quit)
```

Clients connect to `http://127.0.0.1:8080/mcp`. Press Ctrl+C to stop the server.

Use HTTP when the client can't start a command, when several clients share one server, or when you want to watch the server's output while you work. The HTTP server writes its startup messages to standard error and one access log line for each request to standard output.

{% hint style="danger" %}
The MCP server has no authentication. Every client that can reach the HTTP port can open the configured connections with the stored passwords. Keep the default `--host=127.0.0.1`, which accepts connections from the local machine only. See [Security and Session Handling](security-and-session-handling.md#network-exposure).
{% endhint %}

### Which Transport to Use

| | stdio | Streamable HTTP |
| --- | --- | --- |
| Started by | The MCP client | You |
| Clients | One, the process that started it | Any number |
| Network port | None | `--port`, on `127.0.0.1` by default |
| Ends | When the client exits | When you stop it |
| Idle sessions closed after 30 minutes | No | Yes |

## Function Groups

By default, the server provides all tool groups. To restrict what an agent can do, provide only the groups it needs:

```bash
mariadb-shell -- mcp start-server --transport=stdio --functionGroups=db
```

| Group | Provides |
| --- | --- |
| `db` | Tools for the configured database connections. |
| `msm` | Tools for MariaDB Schema Management projects. The `msm.deploy_schema` tool deploys onto a connection opened with the `db` tools, so the server provides it only when `db` is enabled as well. |
| `sandbox` | Tools for local sandbox instances. |
| `migrator` | Tools for the migration from MySQL to MariaDB. The server registers them only if the migration tooling is installed; see [Migration Tooling](configuring-access.md#migration-tooling). |

For example, a server for an agent that should only read schemas and run queries on configured servers provides `db`. A server for schema development on local sandboxes provides `db,msm,sandbox`.

When the migration tooling isn't installed, the server writes this message to standard error at startup and serves the other groups:

```text
2026-10-05T20:25:50+0200 [mcp] migration tools not registered: the MySQL-to-MariaDB migration tooling (v1.5.0) is not installed in '/home/dev/.local/share/mariadb-migrator'
```

## Log Output

The server writes one line to standard error for each event that concerns a database connection, with either transport:

* a connection opened, with the configured URI and the client it is bound to
* a use of a connection refused, because the request came from another client
* a connection refused, because the client couldn't be identified
* an idle session closed, or a session that failed to close

```text
2026-08-07T14:03:11+0200 [mcp] db.connect: opened connection 6f2a91c4... on 'mariadb://mcp@db.example.com:3306' for address=127.0.0.1 session=0123abcd...
2026-08-07T14:37:44+0200 [mcp] db: closed the idle session of connection 6f2a91c4... (address=127.0.0.1 session=0123abcd...) after 1800s unused; the connection stays valid and opens a new session when it is used again
```

The log doesn't contain the SQL statements that clients run. Connection IDs and MCP session IDs are cut to their first eight characters, because a client that knows them can use the connection. For the meaning of the client binding, see [Security and Session Handling](security-and-session-handling.md#connections-belong-to-the-client-that-opened-them).

To keep the log of an HTTP server, redirect standard error to a file:

```bash
mariadb-shell -- mcp start-server --port=8080 2>> ~/mcp-server.log
```

For a `stdio` server, the MCP client captures standard error. Check the MCP log of the client.

## Troubleshooting

| Message or symptom | Cause | Solution |
| --- | --- | --- |
| *Unsupported transport '…'. Supported transports are: streamable-http, stdio.* | The value of `--transport` is misspelled, or names a transport that the server doesn't support, such as `sse`. | Use `stdio` or `streamable-http`. |
| *Unknown function group(s): …* | `--functionGroups` names a group that doesn't exist. | Use `db`, `msm`, `sandbox`, or `migrator`. |
| *There is no object registered under name 'mcp'* | The `mcp` plugin isn't installed, for example in a MariaDB Shell built from source, or plugins are disabled with `--disable-builtin-plugins`. | Install a MariaDB Shell release package. |
| *error while attempting to bind on address ('127.0.0.1', 8080): … address already in use* | Another process uses the port. | Choose another port with `--port`. |
| A client receives HTTP status `421 Misdirected Request`. | The client uses a host name that the server doesn't accept. | Add the name with `--allowedHosts`. See [Host and Origin Validation](security-and-session-handling.md#host-and-origin-validation). |
| No `migrator` tools | The migration tooling isn't installed, or the server was started before it was installed. | Install it with `mcp setup --installMigrator`, then restart the server. |
