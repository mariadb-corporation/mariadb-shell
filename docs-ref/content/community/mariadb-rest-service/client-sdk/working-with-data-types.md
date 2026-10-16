---
description: >-
  How the MRS client SDK represents spatial, date and time, vector, and large
  numeric values of MariaDB Server in TypeScript and Python.
---

# Working with Data Types

MariaDB Server supports an extensive list of data types, many of which do not map directly to the native data types of a programming language. This page describes how the MariaDB REST Service (MRS) client SDK handles them. The conversion rules apply consistently across REST view fields, REST procedure input and output parameters, and REST function input parameters and results.

## Spatial Data Types

MariaDB Server supports spatial column data types that hold geometry values, based on the conventions of the OpenGIS Geometry Model. Some of them hold single values:

* `POINT`
* `LINESTRING`
* `POLYGON`

Others hold collections of geometry values:

* `MULTIPOINT`
* `MULTILINESTRING`
* `MULTIPOLYGON`
* `GEOMETRYCOLLECTION`

A `GEOMETRYCOLLECTION` can store a collection of objects of any type. The other collection types (`MULTIPOINT`, `MULTILINESTRING`, and `MULTIPOLYGON`) restrict the members of the collection to one geometry type.

The `GEOMETRY` data type holds a value of any of the types above.

The SDK represents spatial values as [GeoJSON](https://datatracker.ietf.org/doc/html/rfc7946) objects, both when inserting or updating records (upstream commands) and when finding records (downstream commands).

The examples use the sakila sample database, available under a REST service called `myService`. They work with the `location` column of the `address` table, which has the generic `GEOMETRY` data type.

### Create

{% tabs %}
{% tab title="TypeScript" %}
```typescript
// GeoJSON
myService.sakila.address.create({ data: {
  location: {
    type: "Point",
    coordinates: [11.11, 12.22]
  }
}})
```
{% endtab %}

{% tab title="Python" %}
```python
from sdk.python import MyService, IMyServiceSakilaAddress as Address

my_service = MyService()

address: Address = await my_service.sakila.address.create(
    {
        "location": {
            "type": "Point",
            "coordinates": [11.11, 12.22],
        }
    }
)
```
{% endtab %}
{% endtabs %}

### Update

The same convention applies when updating records of the same table.

{% tabs %}
{% tab title="TypeScript" %}
```typescript
// GeoJSON
myService.sakila.address.update({
  where: {
    address_id: 1
  },
  data: {
    location: {
      type: "Point",
      coordinates: [11.11, 12.22]
    }
  }
})

myService.sakila.address.updateMany({
  where: [{
    address_id: 1
  }, {
    address_id: 2
  }],
  data: {
    location: {
      type: "Point",
      coordinates: [11.11, 12.22]
    }
  }
})
```
{% endtab %}

{% tab title="Python" %}
```python
from sdk.python import MyService, IMyServiceSakilaAddress as Address

my_service = MyService()

address: Address = await my_service.sakila.address.update(
    data={
        "address_id": 1,
        "location": {
            "type": "Point",
            "coordinates": [11.11, 12.22],
        }
    }
)
```
{% endtab %}
{% endtabs %}

### Find

Spatial fields of the documents you find are GeoJSON objects as well.

```python
from sdk.python import MyService, IMyServiceSakilaAddress as Address, MrsDocumentNotFoundError

my_service = MyService()

doc_id = 1
try:
    address: Address = await my_service.sakila.address.find_first_or_throw(
        where={"address_id": doc_id}
    )
except MrsDocumentNotFoundError:
    raise MrsDocumentNotFoundError(msg=f"No address document exists matching actor_id={doc_id}")

print(address.location)
# {"type": "Point", "coordinates": [11.11, 12.22]}
```

### Types Mismatch

If the column has a narrow data type such as `POINT` instead of the generic `GEOMETRY`, an incompatible type on the client side is a type error: a compilation error in TypeScript, a mypy error in Python. For example, take the table `mrs_tests.spatial_tests` created as follows:

```sql
CREATE DATABASE IF NOT EXISTS mrs_tests;
CREATE TABLE IF NOT EXISTS mrs_tests.spatial_tests (id INT AUTO_INCREMENT NOT NULL, ls LINESTRING, PRIMARY KEY (id));
```

With the table and its schema available from the same `myService` REST service, you cannot insert a `Point`, because the column only accepts a `LineString`:

{% tabs %}
{% tab title="TypeScript" %}
```typescript
myService.mrsTests.spatialTests.create({
  data: {
    ls: {
      type: "Point",
      coordinates: [0, 0]
    }
  }
})
```

```text
Type 'Point' is not assignable to type 'LineString'.
```
{% endtab %}

{% tab title="Python" %}
```python
from sdk.python.my_service import IMyServiceMrsTestsSpatialTests as SpatialTests


my_doc: SpatialTests = await my_service.mrs_tests.spatial_tests.update(
    data={
        "id": 1,
        "ls": {
            "type": "Point",
            "coordinates": [0, 0],
        }
    }
)
```

```text
Type `Point` is not assignable to type `LineString`.
```
{% endtab %}
{% endtabs %}

In the same way, inserting or updating multiple values for a single field when the column data type only allows a single value, or the other way around, is a type error. For example, if the table `mrs_tests.spatial_tests` was created as follows:

```sql
CREATE TABLE IF NOT EXISTS mrs_tests.spatial_tests (id INT AUTO_INCREMENT NOT NULL, ls GEOMETRYCOLLECTION, PRIMARY KEY (id));
```

you cannot insert a `Point`, because the column only accepts a `MultiPoint`, a `MultiLineString`, or a `MultiPolygon`:

{% tabs %}
{% tab title="TypeScript" %}
```typescript
myService.mrsTests.spatialTests.create({
  data: {
    ls: {
      type: "Point",
      coordinates: [0, 0]
    }
  }
})
```

```text
Type 'Point' is not assignable to type 'MultiPoint | MultiLineString | MultiPolygon'.
```
{% endtab %}

{% tab title="Python" %}
```python
from sdk.python.my_service import IMyServiceMrsTestsSpatialTests as SpatialTests


my_doc: SpatialTests = await my_service.mrs_tests.spatial_tests.create(
    {
        "ls": {
            "type": "Point",
            "coordinates": [0, 0],
        }
    }
)
```

```text
Type `Point` is not assignable to type `MultiPoint`, `MultiLineString` or `MultiPolygon`.
```
{% endtab %}
{% endtabs %}

## Date and Time Data Types

MariaDB Server supports the following data types for temporal values:

* `TIMESTAMP`: date and time parts.
* `DATETIME`: date and time parts.
* `DATE`: a date part, but no time part.
* `TIME`: a time part, but no date part.
* `YEAR`: year values.

### Python SDK

The MRS Python SDK uses the following client-side data types for date and time values:

| MariaDB Data Type | MRS Python SDK Data Type |
| :---: | :---: |
| `TIMESTAMP` | [`datetime.datetime`](https://docs.python.org/3/library/datetime.html#datetime.datetime) |
| `DATETIME` | [`datetime.datetime`](https://docs.python.org/3/library/datetime.html#datetime.datetime) |
| `DATE` | [`datetime.date`](https://docs.python.org/3/library/datetime.html#datetime.date) |
| `TIME` | [`datetime.timedelta`](https://docs.python.org/3/library/datetime.html#datetime.timedelta) |
| `YEAR` | `int` |

Use these data types when inserting or updating (upstream commands) a record with a field of a date and time column. Symmetrically, for downstream commands such as finding records, date and time fields have these data types.

The following examples show the date and time data types with the MRS Python SDK.

{% hint style="info" %}
The examples assume that a sample database named `mrs_tests` exists.
{% endhint %}

#### Example: REST View

Consider the following sample table:

```sql
/*
Sample table including a column for each date and time data type.
*/
DROP TABLE IF EXISTS mrs_tests.table_date_and_time;

CREATE TABLE mrs_tests.table_date_and_time (
    idx SMALLINT UNSIGNED NOT NULL AUTO_INCREMENT,
    ts TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    dt DATETIME(6),
    d DATE,
    t TIME(6),
    y YEAR,
    PRIMARY KEY  (idx)
);

INSERT INTO  mrs_tests.table_date_and_time (dt, d, t, y) VALUES
    ("2023-07-30 14:59:01", "1987-12-09", "119:10:0.100023", 1999),
    ("2025-02-27 09:41:25.000678", "2010-01-01", "099:35:0.60003", 2005);
```

After you add the schema (`mrs_tests`) and the view (`table_date_and_time`) to the REST service (for example `my_service`), you can use the MRS Python SDK.

Insert a record into the `table_date_and_time` table:

```python
import datetime
from sdk.python import MyService

my_service = MyService()

doc = await my_service.mrs_tests.table_date_and_time.create(
    {
        "dt": datetime.datetime.now(),
        "d": datetime.date(2020, 10, 20),
        "t": datetime.timedelta(days=31, microseconds=202023),
        "y": 1976,
    }
)
```

{% hint style="info" %}
To set the field `d` to `NULL`, use `None` instead of `datetime.date(...)`.
{% endhint %}

Update a record of the `table_date_and_time` table:

```python
import datetime
from sdk.python import MyService

my_service = MyService()

doc = await my_service.mrs_tests.table_date_and_time.update(
    data={
        "idx": 1,
        "t": datetime.timedelta(days=4, hours=4, minutes=1, seconds=1),
    }
)
```

Find a record of the `table_date_and_time` table:

```python
import datetime
from sdk.python import MyService

my_service = MyService()

doc = await my_service.mrs_tests.table_date_and_time.find_first(
    where={
        "AND": [
            {
                "dt": {
                    "lt": datetime.datetime.fromisoformat(
                        "2023-07-30 15:59:01"
                    )
                }
            },
            {"d": {"gte": datetime.date.fromisoformat("1987-12-09")}},
        ]
    }
)
```

#### Example: REST Function

Consider the following sample function:

```sql
/*
Sample functions using date and time data types.
*/
DROP FUNCTION IF EXISTS mrs_tests.func_date_and_time_ts;
CREATE FUNCTION mrs_tests.func_date_and_time_ts (ts TIMESTAMP(4))
    RETURNS TIMESTAMP(4) DETERMINISTIC
    RETURN TIMESTAMPADD(MONTH, 1, ts);
```

After you add the schema (`mrs_tests`) and the function (`func_date_and_time_ts`) to the REST service (for example `my_service`), call the function:

```python
import datetime
from sdk.python import MyService

my_service = MyService()

# `ts` stands for timestamp
value = await my_service.mrs_tests.func_date_and_time_ts.call(ts=datetime.datetime.now())
```

#### Types Mismatch

If the client side specifies an unexpected data type for a field (column), mypy reports a typing error.

## Vector Data Types

MariaDB Server stores vectors in the `VECTOR(N)` data type, where `N` is the number of entries and each entry is a 4-byte (single-precision) floating-point value.

### Python SDK

#### Client-Side Representation

The MRS Python SDK uses the following client-side data type for vector values:

| MariaDB Data Type | MRS Python SDK Data Type |
| :---: | :---: |
| `VECTOR` | [`list`](https://docs.python.org/3/tutorial/datastructures.html#more-on-lists) of [`float`](https://docs.python.org/3/library/functions.html#float) |

Use this data type when inserting or updating (upstream commands) a record with a field of a vector column. Symmetrically, for downstream commands such as finding records, vector fields have this data type.

When a vector column has a `NULL` value, the Python SDK represents it as `None`.

#### Out of Bounds

Each entry of a vector is a 4-byte (single-precision) floating-point value. A Python list can hold a wider range of floating-point values, so nothing stops the application from specifying entries that are out of bounds, such as double-precision values.

The client does not verify the entries. It sends them to the server as they are and lets the server handle them, so expect an error for values the server cannot store.

#### Examples

The following examples show the vector data type with the MRS Python SDK.

{% hint style="info" %}
The examples assume that a sample database named `mrs_tests` exists.
{% endhint %}

Consider the following sample table:

```sql
/*
Sample table including a column for vector type.
*/
DROP TABLE IF EXISTS mrs_tests.table_vector;
CREATE TABLE mrs_tests.table_vector (
    idx SMALLINT UNSIGNED NOT NULL AUTO_INCREMENT,
    embedding VECTOR(3),
    PRIMARY KEY  (idx)
);
```

After you add the schema (`mrs_tests`) and the view (`table_vector`) to the REST service (for example `my_service`), you can use the MRS Python SDK.

Insert records into the `table_vector` table:

```python
import asyncio
from sdk.python import MyService


async def main():
    my_service = MyService()

    data = [
        {
            "embedding": [
                3.1415159702301025,
                2.719064950942993,
                -87.53939819335938,
            ]
        },
        {"embedding": [9.147116, -76.769115, -5.354053]},
        {"embedding": None},
    ]
    async for doc in my_service.mrs_tests.table_vector.create_many(data):
        print(doc.embedding)

    # ------STDOUT-------
    # [3.1415159702301025, 2.719064950942993, -87.53939819335938]
    # [9.147116, -76.769115, -5.354053]
    # None


if __name__ == "__main__":
    asyncio.run(main())
```

Update a record of the `table_vector` table:

```python
import asyncio
from sdk.python import MyService


async def main():
    my_service = MyService()

    doc = await my_service.mrs_tests.table_vector.update(
        data={
            "idx": 1,
            "embedding": [-3.141516, 5.769005, -0.334013],
        }
    )


if __name__ == "__main__":
    asyncio.run(main())
```

Find a record of the `table_vector` table:

```python
import asyncio
from sdk.python import MyService, MrsDocumentNotFoundError


async def main():
    my_service = MyService()

    doc_id = 2
    try:
        doc = await my_service.mrs_tests.table_vector.find_first_or_throw(
            where={"idx": doc_id}
        )
    except MrsDocumentNotFoundError:
        raise MrsDocumentNotFoundError(msg=f"No document exists matching idx={doc_id}")

    print(doc.embedding)

    # ------STDOUT-------
    # [9.147116, -76.769115, -5.354053]


if __name__ == "__main__":
    asyncio.run(main())
```

## Lossy Numbers

A TypeScript `number` uses the double-precision 64-bit binary format defined by the IEEE 754 standard. It cannot represent integers above 2^53-1 (which are valid in the 64-bit integer range) or fixed-point arbitrary-precision decimals without losing precision. This matters because the `BIGINT UNSIGNED` data type of MariaDB Server represents numbers up to 2^64-1, and the `DECIMAL`/`NUMERIC` data type represents fixed-point numbers.

A 64-bit integer can be represented without losing precision by a `BigInt`. If a raw JSON number would lose precision, the TypeScript SDK converts it into a `BigInt` instance. Otherwise, it converts it into a regular `number` instance.

For example, consider the following table:

```sql
CREATE TABLE IF NOT EXISTS my_db.my_table (small BIGINT UNSIGNED, large BIGINT UNSIGNED);
INSERT INTO my_db.my_table (small, large) VALUES (1234, 18446744073709551615);
```

with a REST view created as follows:

```sql
CREATE REST VIEW /myTable
    ON SERVICE /myService SCHEMA /myDb
    AS `my_db`.`my_table` {
        small: small,
        large: large,
    };
```

Retrieve the document with the TypeScript SDK as follows:

```typescript
const doc = await myService.myDb.myTable.findFirst({ where: { large: 18446744073709551615n } })
console.log(doc.small) // 1234
console.log(typeof doc.small) // number
console.log(doc.large) // 18446744073709551615n
console.log(typeof doc.large) // bigint
```

There is no similar construct for fixed-point decimals. The MRS TypeScript SDK handles such a value as a `string` if it would lose precision. For example, consider the following table:

```sql
CREATE TABLE IF NOT EXISTS my_db.my_table (wide DECIMAL(18, 17), narrow DECIMAL(18, 17));
INSERT INTO my_db.my_table (wide, narrow) VALUES (1.234, 1.23456789012345678);
```

with a REST view created as follows:

```sql
CREATE REST VIEW /myTable
    ON SERVICE /myService SCHEMA /myDb
    AS `my_db`.`my_table` {
        wide: wide,
        narrow: narrow,
    };
```

Retrieve the document with the TypeScript SDK as follows:

```typescript
const doc = await myService.myDb.myTable.findFirst({ where: { narrow: "1.23456789012345678" } })
console.log(doc.wide) // 1.234
console.log(typeof doc.wide) // number
console.log(doc.narrow) // 1.23456789012345678
console.log(typeof doc.narrow) // string
```
