---
description: >-
  Generate a TypeScript or Python client SDK for a REST service of the MariaDB
  REST Service (MRS) and use its typed Client API to work with REST views,
  routines, and authentication.
icon: code
---

# Client SDK

The MariaDB REST Service (MRS) offers a Software Development Kit (SDK) that makes it easier to write client applications that interact with a REST service.

The SDK features a Client API that is generated for each REST service, so it fits the REST schemas, views, functions, and procedures of that service exactly.

The SDK is generated for a specific programming language. TypeScript and Python are supported. Each language-specific SDK has its own, independent version number, which follows the rules of [Semantic Versioning 2.0.0](https://semver.org/).

Most examples in this guide are written in TypeScript. For the details of each language, see the [TypeScript Client API Reference](typescript-client-api.md) and the [Python Client API Reference](python-client-api.md).

## SDK Cheat Sheet

| Scope | TypeScript | Python | Description |
| ---: | :--- | :--- | --- |
| All | [getMetadata](typescript-client-api.md#getmetadata) | [get_metadata](python-client-api.md#get_metadata) | Returns the metadata of a REST service, schema, view, function, or procedure. |
| Service | [getAuthApps](typescript-client-api.md#servicegetauthapps) | [get_auth_apps](python-client-api.md#serviceget_auth_apps) | Returns the REST authentication apps of the REST service. |
| | [authenticate](typescript-client-api.md#serviceauthenticate) | [authenticate](python-client-api.md#serviceauthenticate) | Authenticates with the REST service. |
| | [deauthenticate](typescript-client-api.md#servicedeauthenticate) | [deauthenticate](python-client-api.md#servicedeauthenticate) | Closes an authenticated session to the REST service. |
| View | [create](typescript-client-api.md#viewcreate) | [create](python-client-api.md#viewcreate) | Creates a new document on a REST view endpoint. |
| | [createMany](typescript-client-api.md#viewcreatemany) | [create_many](python-client-api.md#viewcreate_many) | Creates a list of documents on a REST view endpoint. |
| | [find](typescript-client-api.md#viewfind) | [find](python-client-api.md#viewfind) | Reads the first page of documents of a search request and returns an iterator. |
| | [findFirst](typescript-client-api.md#viewfindfirst) | [find_first](python-client-api.md#viewfind_first) | Reads the first matching document of a search request. |
| | [findFirstOrThrow](typescript-client-api.md#viewfindfirst) | [find_first_or_throw](python-client-api.md#viewfind_first_or_throw) | Reads the first matching document of a search request and throws an error if none is found. |
| | [findUnique](typescript-client-api.md#viewfindunique) | [find_unique](python-client-api.md#viewfind_unique) | Reads the matching document of a primary key lookup. |
| | [findUniqueOrThrow](typescript-client-api.md#viewfinduniqueorthrow) | [find_unique_or_throw](python-client-api.md#viewfind_unique_or_throw) | Reads the matching document of a primary key lookup and throws an error if none is found. |
| | [delete](typescript-client-api.md#viewdelete) | [delete](python-client-api.md#viewdelete) | Deletes a document from a REST view endpoint. |
| | [deleteMany](typescript-client-api.md#viewdeletemany) | [delete_many](python-client-api.md#viewdelete_many) | Deletes several documents from a REST view endpoint. |
| | [update](typescript-client-api.md#viewupdate) | [update](python-client-api.md#viewupdate) | Updates a document on a REST view endpoint. |
| | [updateMany](typescript-client-api.md#viewupdatemany) | [update_many](python-client-api.md#viewupdate_many) | Updates several documents on a REST view endpoint. |
| Document | [update](typescript-client-api.md#documentupdate) | [update](python-client-api.md#documentupdate) | Updates a REST document that was fetched before. |
| | [delete](typescript-client-api.md#documentdelete) | [delete](python-client-api.md#documentdelete) | Deletes a REST document that was fetched before. |
| Function | [call](typescript-client-api.md#functioncall) | [call](python-client-api.md#functioncall) | Calls a REST function. |
| Procedure | [call](typescript-client-api.md#procedurecall) | [call](python-client-api.md#procedurecall) | Calls a REST procedure. |

## Generating the SDK Files

After you define a REST service, you generate its SDK in the programming language you need. You can do this in two ways:

* **From MariaDB Shell for VS Code.** The extension generates the SDK for a REST service from the user interface. In a DB Notebook, it also generates the TypeScript SDK on the fly, so you can prototype SDK API calls instantly.
* **From the command line.** To integrate the SDK generation into an existing development process, run MariaDB Shell on the command line.

The generated SDK contains a constructor (or initializer) for the client-side REST service object, following the conventions of the programming language. It optionally takes the base URL of the REST service as deployed on the MariaDB REST Daemon that serves it. This overrides the base URL that was given when the SDK was generated.

### Generating the SDK Files from MariaDB Shell for VS Code

To generate the SDK files for a development project, right-click the REST service and select `Dump to Disk > Dump REST Client SDK Files ...`. Select the folder inside your development project to place the SDK files in.

### On the Fly Generation of the TypeScript SDK in VS Code

MariaDB Shell for VS Code runs TypeScript code interactively inside a DB Notebook. To make working with MRS easier, the TypeScript SDK for the current REST service is available directly within DB Notebooks.

Whenever you edit a REST object, the TypeScript SDK is updated, so you can prototype REST queries with the Client API right away. This lets you adjust and fine-tune the REST views and routines until they meet your requirements, and prototype the Client API calls for your development project.

### Generating the SDK Files from the Command Line

To generate the SDK files on the command line, you need MariaDB Shell, installed as described in [Installation](../../mariadb-shell/installation/README.md).

Call the `mrs.dump.sdk_service_files()` plugin function to generate the SDK, for example:

```sh
mariadb-shell dba@localhost --py -e 'mrs.dump.sdk_service_files(directory="/path/to/project/sdk", options={"sdk_language": "TypeScript", "service_url": "https://example.com/myService"})'
```

The function takes the following parameters:

```text
\? mrs.dump.sdk_service_files
NAME
      sdk_service_files - Dumps the SDK service files for a REST Service

SYNTAX
      mrs.dump.sdk_service_files([kwargs])

WHERE
      kwargs: Dictionary - Options to determine what should be generated.

DESCRIPTION
      Returns:

          True on success

      The kwargs parameter accepts the following options:

      - directory: String - The directory to store the .mrs.sdk folder with the
        files.
      - options: Dictionary - Several options how the SDK should be created.
      - session: Object - The database session to use.

      The options option accepts the following options:

      - service_id: String - The ID of the service the SDK should be generated
        for. If not specified, the service identified by the url_context_root
        parameter is used.
      - url_context_root: String - The request path of the service the SDK
        should be generated for. If not specified, the default service is used.
      - db_connection_uri: String - The dbConnectionUri that was used to export
        the SDK files.
      - sdk_language: String - The SDK language to generate.
      - add_app_base_class: String - The additional AppBaseClass file name.
      - service_url: String - The url of the service.
      - version: Integer - The version of the generated files.
      - generationDate: String - The generation date of the SDK files.
      - header: String - The header to use for the SDK files.
```

### Identifier Naming

The identifiers of the REST resources (services, schemas, and objects) in the SDK are derived from their request path segments.

They follow the most common convention of each language, TypeScript and Python. As a result, the identifier generated for a request path such as `/myRequestPath` is the same as the one generated for `/my_request_path`. To avoid a naming conflict, the code generator tracks potential conflicts and appends a number suffix to duplicate identifiers, counting up with each duplicate. Following the default sort order of the database, the snake_case version takes precedence over the camelCase version.

| Request Path | TypeScript Class | Python Class | TypeScript Property | Python Property |
| --- | --- | --- | --- | --- |
| `/my_request_path` | `MyRequestPath` | `MyRequestPath` | `myRequestPath` | `my_request_path` |
| `/myRequestPath` | `MyRequestPath1` | `MyRequestPath1` | `myRequestPath1` | `my_request_path1` |

Naming conflicts can also happen with the static identifiers of the SDK functions available at each REST resource level (service, schema, or object), for example:

* `authenticate`
* `deauthenticate`
* `getMetadata` / `get_metadata`
* `getAuthApps` / `get_auth_apps`

In this case, the identifier of the schema or object gets the suffix.

Request paths that start with a digit are valid, but the resulting identifiers would be syntax errors in both languages. Following the common convention, the generated identifier gets an extra leading `_`.

## In This Section

{% content-ref url="working-with-rest-services.md" %}
[working-with-rest-services.md](working-with-rest-services.md)
{% endcontent-ref %}

{% content-ref url="working-with-rest-views.md" %}
[working-with-rest-views.md](working-with-rest-views.md)
{% endcontent-ref %}

{% content-ref url="working-with-rest-routines.md" %}
[working-with-rest-routines.md](working-with-rest-routines.md)
{% endcontent-ref %}

{% content-ref url="working-with-data-types.md" %}
[working-with-data-types.md](working-with-data-types.md)
{% endcontent-ref %}

{% content-ref url="checking-for-null-column-values.md" %}
[checking-for-null-column-values.md](checking-for-null-column-values.md)
{% endcontent-ref %}

{% content-ref url="application-metadata.md" %}
[application-metadata.md](application-metadata.md)
{% endcontent-ref %}

{% content-ref url="typescript-client-api.md" %}
[typescript-client-api.md](typescript-client-api.md)
{% endcontent-ref %}

{% content-ref url="python-client-api.md" %}
[python-client-api.md](python-client-api.md)
{% endcontent-ref %}
