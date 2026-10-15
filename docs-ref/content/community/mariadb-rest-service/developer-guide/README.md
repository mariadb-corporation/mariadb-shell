---
description: >-
  Set up the MariaDB REST Service, define REST services and endpoints, serve
  static content and MRS scripts, secure the endpoints, and deploy REST
  services from development to production.
icon: book
---

# Developer Guide

The developer guide of the MariaDB REST Service (MRS) covers the tasks of building REST services, from configuring a MariaDB server for MRS to deploying a finished REST service on a production server.

You define REST services with REST SQL statements, which MariaDB Shell runs in SQL mode, or with the dialogs of MariaDB Shell for VS Code. The statements are documented in the [REST SQL Reference](../rest-sql-reference/README.md). How the MariaDB REST Daemon serves the REST services is described in [Architecture](../architecture.md).

A typical project goes through these steps:

1. [Configure MRS](configuring-mrs.md) on a MariaDB server and grant the MRS roles to the developers.
2. [Add a REST service](adding-rest-services.md) and expose database schemas, tables, views, and routines as REST endpoints.
3. Design the JSON documents the endpoints return with [REST data mapping views](rest-data-mapping-views.md).
4. Serve the files of a web app and run server-side TypeScript with [static content and MRS scripts](static-content-and-mrs-scripts.md).
5. Protect the endpoints with [authentication and authorization](authentication-and-authorization.md).
6. Test the endpoints [interactively](working-interactively.md) in a DB Notebook.
7. [Deploy the REST service](deploying-rest-services.md) to a test or production server.

## In This Section

{% content-ref url="configuring-mrs.md" %}
[Configuring MRS](configuring-mrs.md)
{% endcontent-ref %}

{% content-ref url="adding-rest-services.md" %}
[Adding REST Services and Database Objects](adding-rest-services.md)
{% endcontent-ref %}

{% content-ref url="rest-data-mapping-views.md" %}
[REST Data Mapping Views](rest-data-mapping-views.md)
{% endcontent-ref %}

{% content-ref url="static-content-and-mrs-scripts.md" %}
[Static Content and MRS Scripts](static-content-and-mrs-scripts.md)
{% endcontent-ref %}

{% content-ref url="deploying-rest-services.md" %}
[Deploying REST Services](deploying-rest-services.md)
{% endcontent-ref %}

{% content-ref url="authentication-and-authorization.md" %}
[Authentication and Authorization](authentication-and-authorization.md)
{% endcontent-ref %}

{% content-ref url="working-interactively.md" %}
[Working Interactively with REST Services](working-interactively.md)
{% endcontent-ref %}

{% content-ref url="vs-code-dialog-reference.md" %}
[VS Code Dialog Reference](vs-code-dialog-reference.md)
{% endcontent-ref %}

{% content-ref url="examples.md" %}
[Examples](examples.md)
{% endcontent-ref %}
