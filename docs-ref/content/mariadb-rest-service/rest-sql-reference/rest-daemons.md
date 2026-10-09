---
description: >-
  List the MariaDB REST Daemon instances that serve the REST services, list the
  REST services an instance serves, and remove instances from the REST metadata.
---

# MariaDB REST Daemons

The MariaDB REST Daemon serves the REST endpoints of the MariaDB REST Service (MRS) from the REST metadata. Each instance registers itself in the REST metadata when it starts, and checks in regularly. An instance that runs in developer mode also serves the REST services that are in development and not yet published.

{% hint style="info" %}
The installation and bootstrap of the MariaDB REST Daemon are documented with its release.
{% endhint %}

## DROP REST DAEMON

The `DROP REST DAEMON` statement removes a MariaDB REST Daemon instance from the REST metadata, together with its status reports and log entries, for example an instance that no longer runs. The id is the UUID [SHOW REST DAEMONS](#show-rest-daemons) lists, given as a string. An instance that is still running registers itself again when it restarts.

### Syntax

```antlr
dropRestDaemonStatement:
    DROP REST DAEMON (IF EXISTS)? daemonId
;

daemonId:
    textStringLiteral
;
```

`dropRestDaemonStatement ::=`

![Railroad diagram of dropRestDaemonStatement](../../.gitbook/assets/mariadb-rest-service/sql/dropRestDaemonStatement.svg)

`daemonId ::=`

![Railroad diagram of daemonId](../../.gitbook/assets/mariadb-rest-service/sql/daemonId.svg)

### Examples

The following example removes the MariaDB REST Daemon instance with the id `0199a1b2-6c3e-7d41-9a0f-2b8c4d5e6f70`.

```sql
DROP REST DAEMON '0199a1b2-6c3e-7d41-9a0f-2b8c4d5e6f70';
```

## SHOW REST DAEMONS

The `SHOW REST DAEMONS` statement lists the MariaDB REST Daemon instances that serve the REST services.

### Syntax

```antlr
showRestDaemonsStatement:
    SHOW REST DAEMONS formatClause?
;
```

`showRestDaemonsStatement ::=`

![Railroad diagram of showRestDaemonsStatement](../../.gitbook/assets/mariadb-rest-service/sql/showRestDaemonsStatement.svg)

The result has one row per instance, with the columns `id`, `name`, `address`, `product_name`, `version`, `last_check_in`, `active`, and `developer`. An instance is active when it has checked in within the last 10 seconds. `developer` is the developer for whom an instance serves REST services in development.

With `FORMAT=JSON`, the result is a single JSON array of the instances, which also holds their `attributes` and `options` documents. For `formatClause`, see [SHOW CREATE ... FORMAT=JSON](rest-metadata.md).

### Examples

The following example lists the MariaDB REST Daemon instances.

```sql
SHOW REST DAEMONS;
```

## SHOW REST SERVICES FOR DAEMON

The `FOR DAEMON` clause of the [`SHOW REST SERVICES`](rest-services.md#show-rest-services) statement lists only the REST services the given MariaDB REST Daemon instance serves. The id is the UUID [SHOW REST DAEMONS](#show-rest-daemons) lists, given as a string.

### Syntax

```sql
SHOW REST SERVICES FOR DAEMON daemonId
```

The full rule is `showRestServicesStatement`, described under [SHOW REST SERVICES](rest-services.md#show-rest-services).

### Examples

The following example lists the REST services the MariaDB REST Daemon instance with the id `0199a1b2-6c3e-7d41-9a0f-2b8c4d5e6f70` serves.

```sql
SHOW REST SERVICES FOR DAEMON '0199a1b2-6c3e-7d41-9a0f-2b8c4d5e6f70';
```
