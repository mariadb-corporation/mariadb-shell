---
description: >-
  Move a REST service from a development server to a test or production
  server by dumping and loading it with the mrs plugin functions, or bundle it
  with its database schemas in a REST project.
---

# Deploying REST Services

You move a REST service of the MariaDB REST Service (MRS) from a development server to another server, for example for testing or production, by dumping it on the first server and loading it on the second. The mrs plugin of MariaDB Shell provides functions for both steps.

## Dumping and Loading a REST Service

`mrs.dump.service()` writes a REST SQL script that recreates a REST service, and `mrs.load.service()` runs such a script:

```python
mrs.dump.service(service_path="/myService", file_path="~/myService.mrs.sql",
                 endpoints="ALL")
```

```python
mrs.load.service(file_path="~/myService.mrs.sql")
```

`mrs.dump.service()` takes the following options:

| Option | Description |
| --- | --- |
| `service_path` | The request path of the REST service. |
| `file_path` | The file to write. |
| `endpoints` | The endpoints to include. See the list below. |
| `overwrite` | Overwrite the file if it exists. |

Each choice of `endpoints` includes the former:

- `DATABASE` (the default): the REST schemas and their REST objects, like tables, views, procedures, and functions.
- `DATABASE AND STATIC`: also the content sets without MRS scripts, with their files.
- `DATABASE AND STATIC AND DYNAMIC` or `ALL`: also the content sets with MRS scripts.
- An empty string: only the REST service itself.

`mrs.load.service()` takes the `file_path` of the script and, optionally, an `as_path` that creates the REST service under another request path:

```python
mrs.load.service(file_path="~/myService.mrs.sql", as_path="/myServiceTest")
```

The script doesn't include the database schemas that the REST service is based on. They must already exist on the target server. To deploy them together with the REST service, use a REST project.

## REST Projects

A REST project bundles one or more REST services with the database schemas they are based on. `mrs.dump.service_project()` writes a project to a directory or a ZIP file, and `mrs.load.service_project()` loads it from a directory, a ZIP file, a URL, or a GitHub repository:

```python
mrs.dump.service_project(
    services=[{"name": "/myService",
               "include_database_endpoints": True,
               "include_static_endpoints": True,
               "include_dynamic_endpoints": True}],
    schemas=[{"name": "sakila"}],
    settings={"name": "myServiceProject", "version": "1.0.0"},
    destination="~/myServiceProject.zip",
    zip=True)
```

```python
mrs.load.service_project(source="~/myServiceProject.zip")
```

See [REST Projects](../rest-sql-reference/rest-services.md#rest-projects) in the REST SQL reference for all options.

## Using REST SQL

The script that `mrs.dump.service()` writes is the result of the `SHOW CREATE REST SERVICE` statement with an `INCLUDING ... ENDPOINTS` clause, which any client can send to MariaDB Shell:

```sql
SHOW CREATE REST SERVICE /myService INCLUDING ALL ENDPOINTS;
```

Saved to a file, the script runs like any SQL script, for example with `mariadb-shell dba@localhost --sql -f myService.mrs.sql`, or with `\source myService.mrs.sql` in SQL mode. See [`SHOW CREATE REST SERVICE`](../rest-sql-reference/rest-services.md#show-create-rest-service).
