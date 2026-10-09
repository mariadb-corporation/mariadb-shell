---
description: >-
  Serve the files of a web app and run MRS scripts, TypeScript functions
  executed by the MariaDB REST Daemon, from REST content sets uploaded with
  mrs.load.content_set() or REST SQL.
---

# Static Content and MRS Scripts

Besides REST endpoints for database objects, a REST service of the MariaDB REST Service (MRS) can serve files, like the HTML, JavaScript, and image files of a web app, and run MRS scripts, TypeScript functions that the MariaDB REST Daemon executes. Both are stored in REST content sets. A content set has a request path on its REST service, and each of its files is served below that path.

## Uploading a Directory

To put the files of a directory into a content set, use the `mrs.load.content_set()` function of the mrs plugin in MariaDB Shell's Python mode. It creates the content set, uploads every file of the directory, and, if the files hold MRS scripts, registers the scripts as REST endpoints.

```python
mrs.load.content_set(directory="~/myApp/dist", content_set_path="/app",
                     service_path="/myService")
```

From the command line:

```sh
mariadb-shell dba@localhost --py -e 'mrs.load.content_set(directory="~/myApp/dist", content_set_path="/app", service_path="/myService")'
```

The function takes the following options:

| Option | Description |
| --- | --- |
| `directory` | The directory that holds the files. Each file is served at its path relative to this directory, for example `~/myApp/dist/index.html` at `/myService/app/index.html`. |
| `content_set_path` | The request path of the new content set. |
| `service_path` | The request path of the REST service. Without it, the current REST service is used. |
| `ignore_list` | A comma-separated list of file patterns to skip, matched against the path relative to the directory. `*` matches any characters, `?` a single one. The default `*node_modules/*, */.*` skips `node_modules` folders and hidden files and folders like `.git`. |
| `load_scripts` | Whether to register the MRS scripts of the files. By default, they are registered if the directory holds any. |
| `replace` | Replace an existing content set with the same request path. Use this to upload a new version of the files. |

The function returns the list of uploaded files.

## Serving a Web App

Upload a web app, for example a PWA built with a bundler, from its build output folder:

```python
mrs.load.content_set(directory="~/myApp/dist", content_set_path="/app",
                     service_path="/myService", replace=True)
```

All files of a content set without MRS scripts are public, so the whole web app is served.

## MRS Scripts

An MRS scripts project is a TypeScript project whose classes and functions carry the `@Mrs.module`, `@Mrs.script`, and `@Mrs.trigger` decorators:

```typescript
@Mrs.module({ name: "hello", requestPath: "/hello" })
class Hello {
    @Mrs.script({ name: "greet", requiresAuth: false })
    public static async greet(name: string): Promise<string> {
        return "Hello " + name;
    }
}
```

Build the project first, so that its build output folder (`build`, `dist`, `out`, or `output`) holds the compiled modules that the MariaDB REST Daemon executes. Then upload the whole project directory:

```python
mrs.load.content_set(directory="~/myScripts", content_set_path="/scripts",
                     service_path="/myService")
```

The function detects the MRS scripts and registers each `@Mrs.module` as a REST schema and each `@Mrs.script` or `@Mrs.trigger` as a REST endpoint below it, here `/myService/hello/greet`.

Registering the scripts also decides which files of the content set are public:

- Files in a static folder of the project (`static`, `assets`, `media`, `web`, `js`, `css`, or `images`) stay public.
- All other files, the TypeScript sources and the build output, become private. The MariaDB REST Daemon still reads them to run the scripts, but no client can download them, so the server-side code is not exposed.

{% hint style="warning" %}
Don't let a web app build into the build output folder of an MRS scripts project, because its files would become private. Upload the web app as its own content set, as shown in [Serving a Web App](#serving-a-web-app), or build it into a static folder of the scripts project, for example `web`.
{% endhint %}

To upload a new version of the scripts, build the project and upload it again with `replace=True`. The scripts registered before are replaced.

## Using REST SQL

`mrs.load.content_set()` reads the files on the client machine and sends one REST SQL statement per step to MariaDB Shell. Clients other than MariaDB Shell can send the same statements:

```sql
CREATE REST CONTENT SET /scripts ON SERVICE /myService;

CREATE REST CONTENT FILE `/src/hello.mts` ON SERVICE /myService CONTENT SET /scripts
    CONTENT '@Mrs.module({ name: "hello", requestPath: "/hello" }) ...';

CREATE REST CONTENT FILE `/dist/hello.mjs` ON SERVICE /myService CONTENT SET /scripts
    CONTENT 'export class Hello { ... }';

ALTER REST CONTENT SET /scripts ON SERVICE /myService
    LOAD TYPESCRIPT SCRIPTS;
```

Binary files are sent base64 encoded with `BINARY CONTENT`. See [`CREATE REST CONTENT SET`](../rest-sql-reference/rest-content.md#create-rest-content-set), [`CREATE REST CONTENT FILE`](../rest-sql-reference/rest-content.md#create-rest-content-file), and [`ALTER REST CONTENT SET`](../rest-sql-reference/rest-content.md#alter-rest-content-set) in the REST SQL reference.
