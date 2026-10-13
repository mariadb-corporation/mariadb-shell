---
description: >-
  Create, read, update, and delete documents on REST views with the MRS client
  SDK, page through results, select nested fields, and request read-your-writes
  consistency.
---

# Working with REST Views

The MariaDB REST Service (MRS) client SDK exposes each REST view as an object with commands to create, read, update, and delete REST documents.

## Create a New Document

To insert a new document on a REST view, use the `create` API method.

### Example of Inserting a New Document

Given the REST view `/actor` defined as follows, this example shows how to insert a new document.

```sql
CREATE REST SERVICE IF NOT EXISTS /myService;

CREATE REST SCHEMA IF NOT EXISTS /sakila ON SERVICE /myService FROM sakila;

CREATE OR REPLACE REST VIEW /actor
    ON SERVICE /myService SCHEMA /sakila
    AS sakila.actor CLASS MyServiceSakilaActor @INSERT @UPDATE @DELETE {
        actorId: actor_id @SORTABLE @KEY,
        firstName: first_name,
        lastName: last_name,
        lastUpdate: last_update
    }
    AUTHENTICATION REQUIRED;
```

{% hint style="info" %}
Inserting a new document in the `actor` table requires neither the `actorId` field nor the `lastUpdate` field: the former is an auto-generated primary key (`AUTO_INCREMENT`), and the latter maps to a column with the default value `CURRENT_TIMESTAMP()`.
{% endhint %}

{% tabs %}
{% tab title="TypeScript" %}
```typescript
myService.sakila.actor.create({ data: { firstName: "FOO", lastName: "BAR" } })
```
{% endtab %}

{% tab title="Python" %}
```python
my_service.sakila.actor.create(data={"first_name": "FOO", "last_name": "BAR"})
```
{% endtab %}
{% endtabs %}

## Read Documents

To fetch documents from a REST view, use the family of `find` API commands. Each command covers a specific use case.

| API Command | Description |
| --- | --- |
| `find()` | Fetches a page of the list of documents that were found. |
| `findFirst()` | Fetches the first document that was found. |
| `findFirstOrThrow()` | Same as `findFirst()`, but throws when no document was found. |
| `findUnique()` | Fetches the first document that matches a unique key lookup. |
| `findUniqueOrThrow()` | Same as `findUnique()`, but throws when no document was found. |

{% hint style="info" %}
The exact spelling of the API commands depends on the SDK language, as each SDK follows the naming conventions of its language, for example snake_case for Python.
{% endhint %}

### Querying Data in Multiple Pages

When a query on a REST view produces multiple documents, they are sent to the client in pages that the client requests on demand. By default, each page contains at most 25 documents. You can customize the page size at the REST object level, or with the `take` option of the `find()` command. The command fetches the first page of documents and lets you keep consuming more matching documents while they exist.

For example, to retrieve the first 50 documents with the default page size:

```typescript
let countries = await myService.sakila.country.find();
print(countries)
if (countries.hasMore) {
    countries = await countries.next();
    print(countries)
}
[
  {
    "country": "Afghanistan",
    "countryId": 1,
    "lastUpdate": "2006-02-15 04:44:00.000000",
  },
  // ...
  {
    "country": "Congo, The Democratic Republic of the",
    "countryId": 25,
    "lastUpdate": "2006-02-15 04:44:00.000000",
  },
]
[
  {
    "country": "Czech Republic",
    "countryId": 26,
    "lastUpdate": "2006-02-15 04:44:00.000000",
  },
  // ...
  {
    "country": "Japan",
    "countryId": 50,
    "lastUpdate": "2006-02-15 04:44:00.000000",
  }
]
```

To retrieve all documents that match a filter, skipping the first ones and lowering the page size:

```typescript
let countries = await myService.sakila.country.find({ where: { country: { $like: "C%" } }, take: 3, skip: 2 });
print(countries)
while (countries.hasMore) {
    countries = await countries.next();
    print(countries)
}
[
  {
    "country": "Canada",
    "countryId": 20,
    "lastUpdate": "2006-02-15 04:44:00.000000",
  },
  {
    "country": "Chad",
    "countryId": 21,
    "lastUpdate": "2006-02-15 04:44:00.000000",
  },
  {
    "country": "Chile",
    "countryId": 22,
    "lastUpdate": "2006-02-15 04:44:00.000000",
  },
]
// ...
[
  {
    "country": "Czech Republic",
    "countryId": 26,
    "lastUpdate": "2006-02-15 04:44:00.000000",
  }
]
```

### Querying Data Across Relational Tables

MariaDB Server supports foreign keys, which cross-reference related data across tables, and foreign key constraints, which help keep the related data consistent.

A foreign key relationship involves a parent table that holds the initial column values, and a child table with column values that reference the parent column values. The foreign key constraint is defined on the child table. Foreign keys establish one-to-one, one-to-many, or many-to-many relationships between rows in those tables.

With MRS, you can expand these relationships to embed related data from different tables in the same result set, with the REST data mapping view feature available for each REST object. The client then selects which columns to expand, using a query syntax that navigates along the nesting path of columns on other tables that a root column in the main (parent) table references.

A key feature of the MRS SDK is the ability to query these relations between two database objects and include or exclude specific columns from the query response.

The feature is available through the `select` option of the following API commands:

* `findFirst()`
* `find()`
* `findUnique()`

By default, the query response contains all object fields, expanded or not, and their values. To exclude specific fields from the query response, use a plain object whose properties are the names of the fields to exclude, each with the value `false`.

With the sakila sample database available under a REST service called `myService`, and the one-to-one relationship between the `city` and `country` tables expanded through the REST data mapping view feature, exclude the `lastUpdate` and `country.lastUpdate` fields as follows:

```typescript
myService.sakila.city.findFirst({ select: { lastUpdate: false, country: { lastUpdate: false } } })
{
  "city": "A Coruña (La Coruña)",
  "cityId": 1,
  "country": {
    "country": "Spain",
    "countryId": 87
  },
  "countryId": 87
}
```

In the same way, if the many-to-many relationship between the `actor` and `film` tables is expanded, the following command excludes the identifiers of each nested object:

```typescript
myService.sakila.actor.findFirst({ select: { filmActor: { actorId: false, film: { filmId: false, languageId: false, originalLanguageId: false } } } })
{
  "actorId": 58,
  "lastName": "AKROYD",
  "filmActor": [
    {
      "film": {
        "title": "BACKLASH UNDEFEATED",
        "length": 118,
        "rating": "PG-13",
        "lastUpdate": "2006-02-15 05:03:42.000000",
        "rentalRate": 4.99,
        "description": "A Stunning Character Study of a Mad Scientist And a Mad Cow who must Kill a Car in A Monastery",
        "releaseYear": 2006,
        "rentalDuration": 3,
        "replacementCost": 24.99,
        "specialFeatures": "Trailers,Behind the Scenes"
      },
      "filmId": 48,
      "lastUpdate": "2006-02-15 05:05:03.000000"
    },
    // ...
  ],
  "firstName": "CHRISTIAN",
  "lastUpdate": "2006-02-15 04:34:33.000000"
}
```

To cherry-pick the fields to include in the query response, use the same object format with the value `true`, or a list of the field names to include.

This works for one-to-one relationships:

```typescript
myService.sakila.city.findFirst({ select: { city: true, country: { country: true } } })
{
  "city": "A Coruña (La Coruña)",
  "country": {
    "country": "Spain",
  }
}
```

And for many-to-many relationships:

```typescript
myService.sakila.actor.findFirst({ select: ['filmActor.film.title'] })
{
  "filmActor": [
    {
      "film": {
        "title": "BACKLASH UNDEFEATED"
      }
    },
    {
      "film": {
        "title": "BETRAYED REAR"
      }
    }
    // ...
  ]
}
```

## Updating a Document

The SDK offers two ways to update an existing document on a REST view:

1. **The REST view `update` method.** The REST view class exposes an `update` API method that you call with the new document data. You specify all fields explicitly, including the primary key fields.
2. **The Document API.** When you have fetched an MRS document with one of the `find` API methods, you apply the changes directly to the fields of that document, and then call the `update` method of the document object. See [Updating a Document Using the Document API](#updating-a-document-using-the-document-api).

### Updating a Document Using the REST View update Method

To update a document on the REST view, you specify all fields that are not nullable.

In the following example, neither `firstName` nor `lastName` is nullable, so both have to be specified. The `description` column of the `film_text` table, on the other hand, is nullable.

{% tabs %}
{% tab title="TypeScript" %}
```typescript
myService.sakila.actor.update({ data: { id: 1, firstName: "PENELOPE", lastName: "CRUZ" } }) // Property 'lastUpdate' is missing in type '{ actorId: number; lastName: string; firstName: string; }' but required in type 'IUpdateMyServiceSakilaActor'.
myService.sakila.filmText.update({ data: { film_id: 1, title: "FOO" } })
```
{% endtab %}

{% tab title="Python" %}
```python
my_service.sakila.actor.update(data={"id": 1, "first_name": "PENELOPE", "last_name": "CRUZ"}) # Missing key "last_update" for TypedDict "IUpdateMyServiceSakilaActor"
my_service.sakila.film_text.update(data={"film_id": 1, "title": "FOO"})
```
{% endtab %}
{% endtabs %}

### Updating a Document Using the Document API

Documents fetched from REST view endpoints come with a convenient object-oriented API: you call the `update` and `delete` methods directly on the document.

The `update` and `delete` methods are only available if the REST view enables the `UPDATE` and `DELETE` CRUD operations, respectively, and specifies the identifier fields (which map to the primary key of the underlying table).

{% hint style="info" %}
In the TypeScript SDK, the identifier fields of a REST document are read-only. The Python SDK does not enforce this.
{% endhint %}

{% tabs %}
{% tab title="TypeScript" %}
```typescript
let actor = await myService.sakila.actor.findFirst()
if (actor) {
    console.log(actor.actorId) // 1
    console.log(actor.lastName) // "GUINESS"
    actor.lastName = "NOGUINESS"
    await actor.update()
}

actor = await myService.sakila.actor.findFirst()
if (actor) {
    console.log(actor.lastName) // "NOGUINESS"
    await actor.delete()
}

actor = await myService.sakila.actor.findFirst()
if (actor) {
    console.log(actor.actorId) // 2
}
```
{% endtab %}

{% tab title="Python" %}
```python
actor = await my_service.sakila.actor.find_first()
if actor:
    print(actor.actor_id) # 1
    print(actor.last_name) # "GUINESS"
    actor.last_name = "NOGUINESS"
    await actor.update()

actor = await my_service.sakila.actor.find_first()
if actor:
    print(actor.last_name) # "NOGUINESS"
    await actor.delete()

actor = await my_service.sakila.actor.find_first()
if actor:
    print(actor.actor_id) # 2
```
{% endtab %}
{% endtabs %}

### Language-Specific Implementation Details

All MRS SDK commands that return one or more REST documents to the application simplify the client-side data structure. SDK-specific details, such as protocol resource metadata (ETags and GTIDs) or HATEOAS properties (links and pagination control fields), are not exposed, but the SDK still tracks them at runtime. Although an application does not handle these details, they can determine the behavior of an SDK command.

For instance, when you update a REST document, the SDK sends its [ETag](https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/ETag) to the MariaDB REST Daemon to detect [mid-air collisions](https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/ETag#avoiding_mid-air_collisions), so that changes made to the document after the application retrieved it are not overwritten. In the same way, a command that writes data (`INSERT` or `UPDATE`) runs a server-side transaction that can generate a global transaction ID (GTID), which the SDK sends to the MariaDB REST Daemon if the application requires [read consistency](#read-your-writes-consistency) in a setup of multiple server instances.

To hide and lock these details, the SDK wraps the data responses of the MariaDB REST Daemon, or applies access control on top of the details in those responses. In TypeScript, the client-side instance is wrapped in a [`Proxy`](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Proxy) object. In Python, it is wrapped in a [`dataclass`](https://docs.python.org/3/library/dataclasses.html).

The result looks as follows:

```typescript
const actor = await myService.sakila.actor.findFirst()

try {
    delete actor._metadata
} catch (err) {
    console.log(err.message) // The "_metadata" property cannot be deleted.
}

try {
    actor._metadata = { foo: "bar" }
} catch (err) {
    console.log(err.message) // The "_metadata" property cannot be changed.
}
```

These wrappers also add a small contextual API to the object representation of a REST document, with the utility commands `update()` and `delete()`, which operate on that particular document.

### Contextual Fields and Parameters

Inserting a new document and updating an existing document in a table or view through MRS have different requirements. MRS does not support partial updates, so every time an application updates a row, it provides a complete representation of the row as it will become. That representation can still leave columns unset, as long as the columns have no constraint that prevents it. Inserting new rows has no such limitation, but an application should still be aware of the underlying column constraints, to require the minimum set of fields, or to give better feedback (for example, through the type checker) about missing fields that the operation requires.

For the MRS SDK, this means that the type definitions used to insert and update rows distinguish between required and optional fields. A field is required unless it maps to an auto-generated primary key column, a foreign key column, a nullable column, or a column with a default value. When inserting, all of these make a field optional. When updating, due to the limitation described above, a field is only optional when it maps to a nullable column or to a column with row ownership.

## Deleting a Document

Like updating, deleting a document works either with the REST view `delete` method or with the `delete` method of the Document API, called directly on the object. See [Updating a Document Using the Document API](#updating-a-document-using-the-document-api) for an example of the Document API.

## Read Your Writes Consistency

With multiple MariaDB Server instances in a replication setup, data read from one instance can depend on data written on a different instance that has not been replicated yet to the instance being read from. This is a classic concern of distributed systems, formalized as [Read Your Writes](https://jepsen.io/consistency/models/read-your-writes) consistency.

To make sure an application always reads its own writes, the server identifies each committed transaction with a global transaction identifier (GTID). The client receives a GTID for each write operation and can send it back, so that a subsequent read accounts for all data written up to the operation that generated that GTID. This usually has a cost, so the application enables the behavior explicitly, depending on the topology it runs on.

MRS ensures that an application reads its own writes consistently across a cluster of server instances only when retrieving or deleting resources. In the TypeScript SDK, use the `readOwnWrites` option, available for the following commands:

* `find()`
* `findFirst()`
* `findFirstOrThrow()`
* `findUnique()`
* `findUniqueOrThrow()`
* `delete()`
* `deleteMany()`

```typescript
myService.sakila.actor.findFirst({ readOwnWrites: true })
```

The option only has an effect when the application runs on a cluster of server instances where the GTID infrastructure is configured and enabled. Otherwise, it is ignored.
