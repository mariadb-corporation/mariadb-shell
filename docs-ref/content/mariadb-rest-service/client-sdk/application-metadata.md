---
description: >-
  Attach application-specific metadata to REST services, schemas, and objects,
  and read it from a client application with the getMetadata command of the
  MRS client SDK.
---

# Application Metadata

You can attach application-specific metadata to any resource of the MariaDB REST Service (MRS): a REST service, a REST schema, or a REST object. A client application reads it with the [`getMetadata`](typescript-client-api.md#getmetadata) command of the client SDK ([`get_metadata`](python-client-api.md#get_metadata) in Python).

## REST Services

Consider a REST service with custom metadata, created as follows:

```sql
CREATE OR REPLACE REST SERVICE /myService
    METADATA {
      "type": "service"
    };
```

After you generate the TypeScript SDK for this service, get the custom metadata as follows:

```typescript
import { MyService } from "/path/to/sdk/myService"

const myService = new MyService()

const metadata = await myService.getMetadata()
console.log(metadata) // { type: "service" }
```

If the REST service has no custom metadata:

```sql
CREATE OR REPLACE REST SERVICE /myService
```

the command returns an empty object:

```typescript
import { MyService } from "/path/to/sdk/myService"

const myService = new MyService()

const metadata = await myService.getMetadata()
console.log(metadata) // {}
```

## REST Schemas

Consider a database schema created as follows:

```sql
CREATE DATABASE IF NOT EXISTS my_db;
```

and a corresponding REST schema with custom metadata, created as follows:

```sql
CREATE OR REPLACE REST SCHEMA /myDb ON SERVICE /myService
    FROM `my_db`
    METADATA {
      "type": "schema"
    };
```

Get the custom metadata as follows:

```typescript
import { MyService } from "/path/to/sdk/myService"

const myService = new MyService()

const metadata = await myService.myDb.getMetadata()
console.log(metadata) // { type: "schema" }
```

As for a REST service, if a REST schema has no custom metadata:

```sql
CREATE OR REPLACE REST SCHEMA /myDb ON SERVICE /myService
    FROM `my_db`
```

the command returns an empty object:

```typescript
import { MyService } from "/path/to/sdk/myService"

const myService = new MyService()

const metadata = await myService.myDb.getMetadata()
console.log(metadata) // {}
```

If the REST schema requires authentication, here with a MariaDB account that logs in through the default authentication app `MySQL` for MariaDB internal authentication:

```sql
CREATE OR REPLACE REST SCHEMA /myDb ON SERVICE /myService
    FROM `my_db`
    AUTHENTICATION REQUIRED
    METADATA {
      "type": "schema"
    };

CREATE USER foo IDENTIFIED BY 'bar';

ALTER REST SERVICE /myService ADD AUTH APP "MySQL";
```

the command only succeeds if the client authenticates first:

```typescript
import { MyService } from "/path/to/sdk/myService"

const myService = new MyService()

await myService.authenticate({ username: "foo", password: "bar", app: "MySQL" })
const metadata = await myService.myDb.getMetadata()
```

Otherwise, the command fails with an authentication error:

```typescript
import { MyService } from "/path/to/sdk/myService"

const myService = new MyService()

try {
    const metadata = await myService.myDb.getMetadata()
} catch (err) {
    console.log(err) // Not authenticated. Please authenticate first before accessing the path /myService/myDb/_metadata.
}
```

## REST Objects

You can specify custom metadata for any kind of REST object: a `VIEW`, `FUNCTION`, `PROCEDURE`, or `SCRIPT`.

As an example, consider a table created as follows:

```sql
CREATE TABLE IF NOT EXISTS my_table (id INT AUTO_INCREMENT NOT NULL, name VARCHAR(3), PRIMARY KEY (id));
```

and a corresponding REST view with custom metadata, created as follows:

```sql
CREATE OR REPLACE REST VIEW /myTable ON SERVICE /myService SCHEMA /myDb
    AS `my_db`.`my_table` CLASS MyServiceMyDbMyTable {
        id: id @SORTABLE @KEY,
        name: name
    }
    METADATA {
        "type": "table"
    };
```

Get the custom metadata as follows:

```typescript
import { MyService } from "/path/to/sdk/myService"

const myService = new MyService()

const metadata = await myService.myDb.myTable.getMetadata()
console.log(metadata) // { type: "table" }
```

As for a REST service and schema, if the REST view has no custom metadata:

```sql
CREATE OR REPLACE REST VIEW /myTable ON SERVICE /myService SCHEMA /myDb
    AS `my_db`.`my_table` CLASS MyServiceMyDbMyTable {
        id: id @SORTABLE @KEY,
        name: name
    };
```

the command returns an empty object:

```typescript
import { MyService } from "/path/to/sdk/myService"

const myService = new MyService()

const metadata = await myService.myDb.myTable.getMetadata()
console.log(metadata) // {}
```

If the REST view requires authentication:

```sql
CREATE OR REPLACE REST VIEW /myTable ON SERVICE /myService SCHEMA /myDb
    AS `my_db`.`my_table` CLASS MyServiceMyDbMyTable {
        id: id @SORTABLE @KEY,
        name: name
    }
    AUTHENTICATION REQUIRED
    METADATA {
        "type": "table"
    };
```

then, as for a REST schema, the command only succeeds if the client authenticates first:

```typescript
import { MyService } from "/path/to/sdk/myService"

const myService = new MyService()

await myService.authenticate({ username: "foo", password: "bar", app: "MySQL" })
const metadata = await myService.myDb.myTable.getMetadata()
console.log(metadata) // { type: "table" }
```

Otherwise, the command fails with an authentication error:

```typescript
import { MyService } from "/path/to/sdk/myService"

const myService = new MyService()

try {
    const metadata = await myService.myDb.myTable.getMetadata()
} catch (err) {
    console.log(err) // Not authenticated. Please authenticate first before accessing the path /myService/myDb/myTable/_metadata.
}
```
