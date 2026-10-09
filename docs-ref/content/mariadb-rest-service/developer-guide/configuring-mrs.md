---
description: >-
  Configure a MariaDB server for the MariaDB REST Service: deploy the MRS
  metadata schema with CONFIGURE REST METADATA, grant the MRS roles, and
  connect MariaDB REST Daemon instances to serve the REST endpoints.
---

# Configuring MRS

To configure the MariaDB REST Service (MRS), follow these steps:

1. Deploy a MariaDB Server.
2. [Configure the MRS metadata schema](#configuring-the-mrs-metadata-schema) on the server.
3. [Grant the MRS roles](#granting-users-access-to-mrs) to the MariaDB accounts that work with MRS.
4. [Run one or more MariaDB REST Daemon instances](#running-the-mariadb-rest-daemon) that serve the REST endpoints.

After these steps, MRS is fully configured, and you can [add REST services](adding-rest-services.md).

## Configuring the MRS Metadata Schema

MRS stores its configuration in the `mysql_rest_service_metadata` database schema. To deploy the metadata schema, use MariaDB Shell for VS Code or MariaDB Shell, as described in this section.

{% hint style="info" %}
The MariaDB account that configures the metadata schema needs the privileges to create database schemas and roles. It is common practice to use the `root` account or a dedicated `dba` account with `ALL PRIVILEGES` and `WITH GRANT OPTION`.
{% endhint %}

### Using MariaDB Shell for VS Code

1. Start VS Code, install the MariaDB Shell for VS Code extension, and add a DB connection to the MariaDB Server that you want to configure for MRS.
2. Right-click the connection in the DATABASE CONNECTIONS view and select **Configure Instance for MariaDB REST Service Support**.

![The context menu of a DB connection with the command that configures the instance for the REST service](../../.gitbook/assets/mariadb-rest-service/vsc-mrs-configure.png)

The MRS metadata schema is now configured.

### Using MariaDB Shell

In MariaDB Shell, you configure the metadata schema with the REST SQL statement [`CONFIGURE REST METADATA`](../rest-sql-reference/rest-metadata.md#configure-rest-metadata). MariaDB Shell runs REST SQL statements in SQL mode, like any other SQL statement.

The following example connects to a local MariaDB Server with the `dba` account:

```sh
mariadb-shell dba@localhost --sql
```

Then it configures the metadata schema:

```sql
CONFIGURE REST METADATA;
```

```text
Query OK, 0 rows affected
REST metadata configured successfully.
```

The statement also takes options that enable or disable MRS and set global configuration options, for example for authentication throttling. See [`CONFIGURE REST METADATA`](../rest-sql-reference/rest-metadata.md#configure-rest-metadata).

To check the configuration, run [`SHOW REST STATUS`](../rest-sql-reference/rest-metadata.md#show-rest-status). It reports whether the metadata schema is configured and enabled, how many REST services are enabled, and whether the metadata schema can be updated:

```sql
SHOW REST STATUS;
```

### Updating the MRS Metadata Schema

When the metadata schema on a server is older than the version that MariaDB Shell deploys, `CONFIGURE REST METADATA` leaves it as it is and reports that it needs an update. The other REST SQL statements refuse to work on an outdated metadata schema. To update it, add `UPDATE IF AVAILABLE`:

```sql
CONFIGURE REST METADATA UPDATE IF AVAILABLE;
```

Before the update, MariaDB Shell dumps the metadata schema to the `plugin_data/msm_plugin/backups` folder of its user configuration and loads the dump again if the update fails. The steps are logged to `plugin_data/msm_plugin/msm_schema_update_log.txt`.

### Removing the MRS Metadata Schema

To remove MRS from a server, drop the metadata schema with a MariaDB account that has the privilege to drop it:

```sql
DROP SCHEMA mysql_rest_service_metadata;
```

{% hint style="warning" %}
Dropping the metadata schema deletes the definitions of all REST services, REST users, and REST roles on the server. The application data in your own schemas stays.
{% endhint %}

## Granting Users Access to MRS

After the metadata schema is configured, grant access to it to every MariaDB account that works with MRS. In addition, the MRS data provider role needs access to the application data that the REST endpoints expose, so that MRS can serve it.

### MRS User Roles

MRS has a multi-tiered access model. Configuring the metadata schema creates the following roles, which you grant to MariaDB accounts:

| Access Level | Role Name | Description |
| --- | --- | --- |
| Root | - | Accounts with `ALL PRIVILEGES`, like the default `root` account, have full access to all features. |
| REST Service Admin | `mysql_rest_service_admin` | Full access to all features of MRS. |
| REST Schema Admin | `mysql_rest_service_schema_admin` | Adds REST schemas and endpoints to existing REST services. |
| REST Service Developer | `mysql_rest_service_dev` | Defines REST endpoints for existing REST schemas. |
| REST Service User | `mysql_rest_service_user` | Accesses REST endpoints as a MariaDB account, through MariaDB internal authentication. |

Grant a role with the `GRANT` statement. A role takes effect in a session only after it is activated with `SET ROLE`. To have it activated automatically when the account connects, which MariaDB Shell for VS Code requires, make it the account's default role with `SET DEFAULT ROLE`.

The following example grants the `mysql_rest_service_admin` role to the `dba` account and makes it the account's default role:

```sql
GRANT 'mysql_rest_service_admin' TO 'dba'@'%';
SET DEFAULT ROLE mysql_rest_service_admin FOR 'dba'@'%';
```

{% hint style="info" %}
MariaDB activates one role per session, and an account has one default role. To give an account the privileges of several roles at once, grant them to a role of your own and make that role the default role.
{% endhint %}

### MRS Provider Roles

Two further roles are used by the MariaDB REST Daemon to operate MRS:

| Access Level | Role Name | Description |
| --- | --- | --- |
| Metadata Schema Read-Only | `mysql_rest_service_meta_provider` | Used by the MariaDB REST Daemon to read the REST services it serves from the metadata schema. |
| Application Data Access | `mysql_rest_service_data_provider` | Used by the MariaDB REST Daemon to read and write the application data that the REST services expose. It applies to all REST users authenticated through the `MRS` vendor and through OAuth2 vendors. REST users authenticated through MariaDB internal authentication, vendor `MySQL Internal`, use their own privileges. |

When you define a REST endpoint, make sure that the `mysql_rest_service_data_provider` role has the privileges on the database objects behind it:

- For REST views on a table or view, the privileges are granted automatically.
- For REST procedures and REST functions, the `EXECUTE` privilege is granted automatically. If the routine calls other routines or accesses other database objects, grant the privileges on them to `mysql_rest_service_data_provider` yourself.

The following example exposes the procedure `test.my_procedure`, which calls the procedure `test.my_sub_procedure`. The script creates both procedures and defines the REST endpoint `/myService/test/myProcedure`. The `EXECUTE` privilege on `test.my_procedure` is granted automatically, but the endpoint would still fail, because the privilege on `test.my_sub_procedure` is missing. The final `GRANT` statement adds it, and the endpoint works.

```sql
CREATE SCHEMA IF NOT EXISTS `test`;

DELIMITER %%
DROP PROCEDURE IF EXISTS `test`.`my_procedure`%%
CREATE PROCEDURE `test`.`my_procedure`(IN arg1 INTEGER, OUT arg2 INTEGER)
SQL SECURITY DEFINER
NOT DETERMINISTIC
BEGIN
    CALL `test`.`my_sub_procedure`(arg1, arg2);
END%%

DROP PROCEDURE IF EXISTS `test`.`my_sub_procedure`%%
CREATE PROCEDURE `test`.`my_sub_procedure`(IN arg1 INTEGER, OUT arg2 INTEGER)
SQL SECURITY DEFINER
NOT DETERMINISTIC
BEGIN
    SET arg2 = arg1 * 2;
END%%
DELIMITER ;

CREATE OR REPLACE REST SERVICE /myService;
CREATE REST SCHEMA /test ON SERVICE /myService FROM test;
CREATE REST PROCEDURE /myProcedure
    ON SERVICE /myService SCHEMA /test
    AS `test`.`my_procedure`;

GRANT EXECUTE ON PROCEDURE `test`.`my_sub_procedure` TO 'mysql_rest_service_data_provider';
```

## Running the MariaDB REST Daemon

The MariaDB REST Daemon serves the REST endpoints. It reads the REST services from the metadata schema and answers the HTTPS requests for them, running the SQL on the MariaDB Server. You can run several instances against the same metadata schema. See [Architecture](../architecture.md).

Each instance registers itself in the metadata schema when it starts and checks in regularly. To list the instances, run [`SHOW REST DAEMONS`](../rest-sql-reference/rest-daemons.md#show-rest-daemons):

```sql
SHOW REST DAEMONS;
```

An instance that runs in developer mode also serves the REST services that are still in development for a given developer. See [Development Setup](../architecture.md#development-setup). To remove an instance that no longer runs from the metadata, use [`DROP REST DAEMON`](../rest-sql-reference/rest-daemons.md#drop-rest-daemon).

{% hint style="info" %}
The installation and bootstrap of the MariaDB REST Daemon are documented with its release.
{% endhint %}

### MariaDB Accounts for the MariaDB REST Daemon

The MariaDB REST Daemon connects to the MariaDB Server with an account that has the [MRS provider roles](#mrs-provider-roles):

- With one account, the MariaDB REST Daemon uses it for both the metadata schema and the application data. The account needs both the `mysql_rest_service_meta_provider` and the `mysql_rest_service_data_provider` role.
- With two accounts, the MariaDB REST Daemon uses one for the metadata schema and the other for the application data. Grant `mysql_rest_service_meta_provider` to the first account and `mysql_rest_service_data_provider` to the second.

To create the two accounts by hand, connect to the server with MariaDB Shell or MariaDB Shell for VS Code and run the following statements, with your own account names, host, and passwords. Each account gets its role as default role, so that the role is active when the MariaDB REST Daemon connects:

```sql
CREATE USER 'mrs_metadata'@'<daemon_host>' IDENTIFIED BY '<password>';
GRANT 'mysql_rest_service_meta_provider' TO 'mrs_metadata'@'<daemon_host>';
SET DEFAULT ROLE mysql_rest_service_meta_provider FOR 'mrs_metadata'@'<daemon_host>';

CREATE USER 'mrs_data'@'<daemon_host>' IDENTIFIED BY '<password>';
GRANT 'mysql_rest_service_data_provider' TO 'mrs_data'@'<daemon_host>';
SET DEFAULT ROLE mysql_rest_service_data_provider FOR 'mrs_data'@'<daemon_host>';
```
