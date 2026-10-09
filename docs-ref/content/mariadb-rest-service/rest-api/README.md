---
description: >-
  Call the REST endpoints of the MariaDB REST Service directly over HTTP:
  the URL layout of a REST service, the request path rules, and how to test
  the endpoints with cURL.
icon: globe
---

# Core REST API

The pages in this section show the low-level HTTP requests that you can send to the REST endpoints of the MariaDB REST Service (MRS): queries and other operations against tables and views after you have REST-enabled them, filtering, and the authentication of REST users. The MariaDB REST Daemon serves these endpoints.

Use the examples to gain a deep understanding of how MRS works. To develop an application with MRS, use a higher-level MRS Software Development Kit (SDK) instead. See [Client SDK](../client-sdk/README.md) for the SDKs that are available for your programming language and platform.

## About MRS RESTful Web Services

MRS supports the creation of any number of distinct RESTful web services, also called REST services or MRS services. Each REST service usually maps to one or more web applications.

After you create a REST service, you access it at a URL of the following pattern:

```text
https://<HOSTNAME:PORT>/<MRS_SERVICE_PATH>/<MRS_DATABASE_SCHEMA_PATH>/<MRS_DATABASE_OBJECT_PATH>/
```

* `HOSTNAME:PORT/MRS_SERVICE_PATH`: The address at which the REST service runs, also called the MRS service URI.
* `MRS_DATABASE_SCHEMA_PATH`: The path that you provided when you REST-enabled the database schema. By default, it is the name of the schema.
* `MRS_DATABASE_OBJECT_PATH`: The path that you provided when you REST-enabled the database object (table, view, or procedure).

Together, these values make up the MRS endpoint URL, for example:

```text
https://localhost:8000/mrs/sakila/actor
```

## Request Path Syntax Requirements

To prevent path-based attacks, MRS requires the path element of each request URL to conform to the following rules. The path:

* Is not empty or whitespace-only.
* Does not contain any of the characters `?`, `#`, `;`, `%`.
* Does not contain the null character (`\u0000`).
* Does not contain characters in the range `\u0001`-`1`.
* Does not end with white space or a period (`.`).
* Does not contain a double forward slash (`//`) or a double backslash (`\\`).
* Does not contain two or more periods in sequence (`..`, `...`, and so on).
* Does not exceed the maximum path length.
* Does not match any of the following names (case-insensitive), with or without file extensions: `CON`, `PRN`, `AUX`, `CLOCK$`, `NUL`, `COM0`, `COM1`, `COM2`, `COM3`, `COM4`, `COM5`, `COM6`, `COM7`, `COM8`, `COM9`, `LPT0`, `LPT1`, `LPT2`, `LPT3`, `LPT4`, `LPT5`, `LPT6`, `LPT7`, `LPT8`, `LPT9`.

If you intend to enable REST endpoints for database objects, avoid object names that do not comply with these requirements. For example, do not create a table named `#EMPS`. To REST-enable an object with a non-compliant name, give its REST endpoint a path that complies with the requirements.

MRS applies these requirements to the URL-decoded form of the URL, to prevent circumvention by percent encoding.

## Testing RESTful Services with cURL

You can open the URL of a RESTful service in a web browser. Another way to test it is a command-line tool like cURL, which lets you see and control the data sent to and received from the RESTful service.

```sh
curl -i https://localhost:8000/mrs/sakila/actor/2
```

This example produces a response like the following:

```json
{
    "links": [
        {
            "rel": "self",
            "href": "http://localhost:8000/mrs/sakila/actor/2"
        }
    ],
    "actor_id": 2,
    "last_name": "WAHLBERG",
    "first_name": "NICK",
    "last_update": "2006-02-15 03:34:33.000000"
}
```

The `-i` option tells cURL to display the HTTP headers returned by the server.

## In This Section

{% content-ref url="queries.md" %}
[REST Queries](queries.md)
{% endcontent-ref %}

{% content-ref url="filtering.md" %}
[Filtering in REST Queries](filtering.md)
{% endcontent-ref %}

{% content-ref url="filter-examples.md" %}
[FilterObject Examples](filter-examples.md)
{% endcontent-ref %}

{% content-ref url="authentication.md" %}
[Authenticating REST Users](authentication.md)
{% endcontent-ref %}
