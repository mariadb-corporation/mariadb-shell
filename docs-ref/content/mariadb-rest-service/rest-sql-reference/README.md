---
description: >-
  Manage the MariaDB REST Service with REST SQL, the SQL statements that
  configure the REST metadata and create, change, list, and drop REST services,
  schemas, objects, content, authentication apps, users, and roles.
icon: code
---

# REST SQL Reference

A goal of the MariaDB REST Service (MRS) is a management interface that feels familiar to MariaDB developers and DBAs and fits into their existing processes.

For this purpose, MariaDB Shell extends the SQL it accepts with REST SQL: DDL (Data Definition Language) statements that manage the MariaDB REST Service. The statements are handled by the built-in `mrs` module of MariaDB Shell. You run them in SQL mode, or with `session.run_sql()` in Python mode, and any client that sends its SQL through MariaDB Shell, such as MariaDB Shell for VS Code, can use them as well.

This makes creating a REST service for your application as easy as creating a database schema or table.

## Example

The following script configures the MariaDB REST Service, creates a REST service `/myService`, and adds a REST schema `/sakila` and a REST data mapping view `/actor` that lists all actors and their film titles.

```sql
CONFIGURE REST METADATA;

CREATE REST SERVICE /myService;
USE REST SERVICE /myService;

CREATE REST SCHEMA /sakila FROM `sakila`;
USE REST SCHEMA /sakila;

CREATE REST VIEW /actor
AS `sakila`.`actor` {
    actorId: actor_id @SORTABLE,
    firstName: first_name,
    lastName: last_name,
    lastUpdate: last_update,
    filmActor: sakila.film_actor @UNNEST {
        film: sakila.film @UNNEST {
            title: title
        }
    }
}
AUTHENTICATION REQUIRED;
```

{% hint style="info" %}
The script requires the sakila sample database. Install it on the server before you run the script.
{% endhint %}

## Syntax Conventions

The REST SQL statements follow the lexical rules of MariaDB SQL statements.

* **Separators.** Statements are separated by `;`. Leading, trailing, and repeated semicolons are ignored, so `;SHOW REST SERVICES;;` is a valid script.
* **Comments.** `-- ` (two dashes followed by a space), `#` to the end of the line, and `/* ... */`. A `/*! ... */` version comment is not supported.
* **Request paths.** An unquoted request path is a sequence of `/segment` parts, for example `/myService/v1`. Each segment is an identifier: it may contain letters, digits, `_`, and `$`, but it must not consist of digits only or look like a number (`/2024`, `/1e5`). Write such paths, and paths with other characters, in backticks, for example `` CREATE REST SERVICE `/2024`; ``. A quoted request path has to start with `/`, or with a wildcard (`*`, `?`) where wildcards are allowed.
* **Identifiers.** Names of database schemas, tables, views, routines, and columns, and class names, are written unquoted or in backticks. Inside backticks, a backslash is an ordinary character and a backtick is written twice (`` `a``b` ``). With the `ANSI_QUOTES` SQL mode, a double-quoted string is an identifier as well.
* **Text.** Comments, passwords, and similar values are written in single quotes, or in double quotes unless `ANSI_QUOTES` is set. Escape a quote character by doubling it (`'it''s'`) or, unless `NO_BACKSLASH_ESCAPES` is set, with a backslash (`'it\'s'`). Names of REST users, roles, and authentication apps accept double quotes in every SQL mode.
* **Keywords as names.** Keywords used as names have to be quoted, for example `` `role` ``. `FILES` and `VENDORS` are exceptions and can be used unquoted, for example `` AS `sakila`.files ``.
* **REST users.** A REST user is written as `name@app`, for example `admin@myApp` or `"admin"@"MRS"`. Quote each part as needed (`MRS` is a keyword).
* **JSON values.** `OPTIONS`, `METADATA`, `APP OPTIONS`, and `JSON SCHEMA` take a JSON value. Its keys and strings are written in double quotes in every SQL mode. Numbers may be negative and may have a decimal part, for example `{"maxItems": -1, "ratio": 0.5}`.

## Reading the Syntax

Each statement is described with its grammar rules, written in ANTLR notation, followed by a railroad diagram of each rule. In the rules, `?` marks an optional element, `*` an element that can repeat or be left out, `+` an element that occurs at least once, and `|` separates alternatives. Keywords are written in upper case. Rules that several statements share, such as [`jsonOptions`](rest-metadata.md#rest-configuration-json-options) or [`serviceSchemaSelector`](rest-views.md#create-rest-view), are described once and linked from the other statements.

## Statement Overview

| Object | Statements |
| --- | --- |
| [REST metadata](rest-metadata.md) | `CONFIGURE REST METADATA`, `USE REST`, `SHOW REST [METADATA] STATUS`, `SHOW REST COLUMNS`, the `FORMAT` clause of `SHOW CREATE` |
| [REST services](rest-services.md) | `CREATE`, `CLONE`, `ALTER`, `DROP REST SERVICE`, `SHOW REST SERVICES`, `SHOW CREATE REST SERVICE` |
| [REST schemas](rest-schemas.md) | `CREATE`, `ALTER`, `DROP REST SCHEMA`, `SHOW REST SCHEMAS`, `SHOW CREATE REST SCHEMA` |
| [REST views](rest-views.md) | `CREATE`, `ALTER`, `DROP REST VIEW`, `SHOW REST VIEWS`, `SHOW CREATE REST VIEW` |
| [REST procedures and functions](rest-routines.md) | `CREATE`, `ALTER`, `DROP REST PROCEDURE` and `FUNCTION`, `SHOW REST PROCEDURES` and `FUNCTIONS`, `SHOW CREATE REST PROCEDURE` and `FUNCTION` |
| [REST content](rest-content.md) | `CREATE`, `ALTER`, `DROP REST CONTENT SET`, `CREATE`, `DROP REST CONTENT FILE`, `SHOW REST CONTENT SETS` and `FILES`, `SHOW CREATE REST CONTENT SET` and `FILE` |
| [REST authentication apps](rest-authentication.md) | `CREATE`, `ALTER`, `DROP REST AUTH APP`, `SHOW REST AUTH APPS`, `SHOW REST AUTH VENDORS`, `SHOW CREATE REST AUTH APP` |
| [REST users and roles](rest-users-and-roles.md) | `CREATE`, `ALTER`, `DROP REST USER`, `CREATE`, `DROP REST ROLE`, `GRANT`, `REVOKE REST [ROLE]`, `SHOW REST USERS`, `ROLES` and `GRANTS`, `SHOW CREATE REST USER` and `ROLE` |
| [MariaDB REST Daemons](rest-daemons.md) | `DROP REST DAEMON`, `SHOW REST DAEMONS`, `SHOW REST SERVICES FOR DAEMON` |

## In This Section

{% content-ref url="rest-metadata.md" %}
[REST Metadata](rest-metadata.md)
{% endcontent-ref %}

{% content-ref url="rest-services.md" %}
[REST Services](rest-services.md)
{% endcontent-ref %}

{% content-ref url="rest-schemas.md" %}
[REST Schemas](rest-schemas.md)
{% endcontent-ref %}

{% content-ref url="rest-views.md" %}
[REST Views](rest-views.md)
{% endcontent-ref %}

{% content-ref url="rest-routines.md" %}
[REST Procedures and Functions](rest-routines.md)
{% endcontent-ref %}

{% content-ref url="rest-content.md" %}
[REST Content Sets and Files](rest-content.md)
{% endcontent-ref %}

{% content-ref url="rest-authentication.md" %}
[REST Authentication Apps](rest-authentication.md)
{% endcontent-ref %}

{% content-ref url="rest-users-and-roles.md" %}
[REST Users and Roles](rest-users-and-roles.md)
{% endcontent-ref %}

{% content-ref url="rest-daemons.md" %}
[MariaDB REST Daemons](rest-daemons.md)
{% endcontent-ref %}
