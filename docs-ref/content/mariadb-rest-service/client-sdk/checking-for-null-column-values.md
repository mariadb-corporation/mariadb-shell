---
description: >-
  Filter REST documents by columns that contain or do not contain NULL values
  with the MRS TypeScript client SDK.
---

# Checking for NULL Column Values

MariaDB Server supports `NOT NULL` constraints, which ensure that a column cannot hold a `NULL` value. By default, a column can hold `NULL` values. With the MariaDB REST Service (MRS), you include records with `NULL` columns in, or exclude them from, the result set with the `$null` and `$notnull` operators.

The MRS TypeScript SDK has a special syntax to filter records by whether a field contains a `NULL` value. With the sakila sample database available under a REST service called `myService`, filter records by `NULL` column values as follows:

```typescript
myService.sakila.address.find({ select: ["address", "address2"], where: { address2: null } })
[
    {
      "address": "47 MySakila Drive",
      "address2": null,
    },
    {
      "address": "28 MariaDB Boulevard",
      "address2": null,
    },
    {
      "address": "23 Workhaven Lane",
      "address2": null,
    },
    {
      "address": "1411 Lillydale Drive",
      "address2": null,
    }
]
```

In the same way, filter records where a column does not contain `NULL` as follows:

```typescript
myService.sakila.address.findFirst({ select: ["address", "address2"], where: { address2: { not: null } } })
{
  "address": "1913 Hanoi Way",
  "address2": "",
}
```

Applying such a filter to a field that maps to a column with a `NOT NULL` constraint is a TypeScript compilation error:

```typescript
myService.sakila.address.findFirst({ where: { address: null } })
```

```text
Type 'null' is not assignable to type 'string | DataFilterField<IMyServiceSakilaAddressParams, string | undefined> | ComparisonOpExpr<string | undefined>[] | undefined'.
```
