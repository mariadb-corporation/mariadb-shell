---
description: >-
  A hands-on introduction to the MariaDB REST Service: set up a development
  environment in VS Code, define REST endpoints for the sakila sample
  database, and access them.
icon: rocket
---

# Quickstart

The MariaDB REST Service (MRS) provides a fast and powerful way to serve JSON documents to client applications through an HTTPS REST interface. This quickstart shows how to set up MRS and how to define and access REST endpoints for MariaDB database objects.

The quickstart uses VS Code with the MariaDB Shell for VS Code extension, which gives you graphical editors for REST services and starts a local MariaDB REST Daemon for development. For each step done in the GUI, it also shows the [REST SQL](../rest-sql-reference/README.md) statements that do the same, which you can run in MariaDB Shell.

You go through the following steps:

1. [Setting Up the MariaDB REST Service](setting-up.md): prepare a MariaDB Server, install VS Code and the extension, configure the server for MRS, grant the privileges, and start a MariaDB REST Daemon for development.
2. [Defining REST Endpoints](defining-rest-endpoints.md): load the sakila sample database, create a REST service, and add a table as a REST endpoint.
3. [Accessing REST Endpoints](accessing-rest-endpoints.md): access the REST endpoint from a web browser, the TypeScript prompt of the DB Notebook, and `curl`, and deploy a web app.

To learn more about MRS, see the [Developer Guide](../developer-guide/README.md).

## In This Section

{% content-ref url="setting-up.md" %}
[setting-up.md](setting-up.md)
{% endcontent-ref %}

{% content-ref url="defining-rest-endpoints.md" %}
[defining-rest-endpoints.md](defining-rest-endpoints.md)
{% endcontent-ref %}

{% content-ref url="accessing-rest-endpoints.md" %}
[accessing-rest-endpoints.md](accessing-rest-endpoints.md)
{% endcontent-ref %}
