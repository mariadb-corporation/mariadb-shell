---
description: >-
  Examples of FilterObject specifications for each operator of the filter
  grammar of the MariaDB REST Service, from sorting and equality to ranges,
  null checks, and combined conditions.
---

# FilterObject Examples

The following examples show a FilterObject for each property and operator of the [FilterObject grammar](filtering.md#filterobject-grammar) of the MariaDB REST Service (MRS). Pass the FilterObject in the `q` parameter of a query, as described in [Filtering in REST Queries](filtering.md).

## Order By Property ($orderby)

### Order By With Literals

```json
{
  "$orderby": {"SALARY":  "ASC","ENAME":"DESC"}
}
```

### Order By With Numbers

```json
{
  "$orderby": {"SALARY":  -1,"ENAME":  1}
}
```

## As Of Property ($asof)

### With SCN (Implicit)

```json
{
  "$asof": 1273919
}
```

### With SCN (Explicit)

```json
{
  "$asof": {"$scn": "1273919"}
}
```

### With Date (Implicit)

```json
{
  "$asof": "2014-06-30T00:00:00Z"
}
```

### With Date (Explicit)

```json
{
  "$asof": {"$date": "2014-06-30T00:00:00Z"}
}
```

## Equals Operator ($eq)

### Implicit

The implicit form supports strings and dates, too.

```json
{
  "SALARY": 1000
}
```

### Explicit

```json
{
  "SALARY": {"$eq": 1000}
}
```

### Strings

```json
{
  "ENAME": {"$eq":"SMITH"}
}
```

### Dates

```json
{
  "HIREDATE": {"$date": "1981-11-17T08:00:00Z"}
}
```

## Not Equals Operator ($ne)

### Numbers

```json
{
  "SALARY": {"$ne": 1000}
}
```

### Strings

```json
{
  "ENAME": {"$ne":"SMITH"}
}
```

### Dates

```json
{
  "HIREDATE": {"$ne": {"$date":"1981-11-17T08:00:00Z"}}
}
```

## Less Than Operator ($lt)

The operator supports dates and numbers only.

### Numbers

```json
{
  "SALARY": {"$lt": 10000}
}
```

### Dates

```json
{
  "HIREDATE": {"$lt": {"$date":"1999-12-17T08:00:00Z"}}
}
```

## Less Than or Equals Operator ($lte)

The operator supports dates and numbers only.

### Numbers

```json
{
  "SALARY": {"$lte": 10000}
}
```

### Dates

```json
{
  "HIREDATE": {"$lte": {"$date":"1999-12-17T08:00:00Z"}}
}
```

## Greater Than Operator ($gt)

The operator supports dates and numbers only.

### Numbers

```json
{
  "SALARY": {"$gt": 10000}
}
```

### Dates

```json
{
  "HIREDATE": {"$gt": {"$date":"1999-12-17T08:00:00Z"}}
}
```

## Greater Than or Equals Operator ($gte)

The operator supports dates and numbers only.

### Numbers

```json
{
  "SALARY": {"$gte": 10000}
}
```

### Dates

```json
{
  "HIREDATE": {"$gte": {"$date":"1999-12-17T08:00:00Z"}}
}
```

## In String Operator ($instr)

The operator supports strings only.

```json
{
  "ENAME": {"$instr":"MC"}
}
```

## Not In String Operator ($ninstr)

The operator supports strings only.

```json
{
  "ENAME": {"$ninstr":"MC"}
}
```

## Like Operator ($like)

The operator supports strings. It does not support an escape character, so you cannot match a literal `_` or `%` character.

```json
{
  "ENAME": {"$like":"AX%"}
}
```

## Between Operator ($between)

The operator supports strings, dates, and numbers.

### Numbers

```json
{
  "SALARY": {"$between": [1000,2000]}
}
```

### Dates

```json
{
  "HIREDATE": {"$between": [{"$date":"1989-12-17T08:00:00Z"},{"$date":"1999-12-17T08:00:00Z"}]}
}
```

### Strings

```json
{
  "ENAME": {"$between": ["A","C"]}
}
```

### Null Ranges

A `null` lower bound makes the operator equivalent to `$lte`, a `null` upper bound to `$gte`. Null ranges are supported by numbers and dates only.

```json
{
  "SALARY": {"$between": [null,2000]}
}
```

```json
{
  "SALARY": {"$between": [1000,null]}
}
```

## Null Operator ($null)

```json
{
  "ENAME": {"$null": null}
}
```

## Not Null Operator ($notnull)

```json
{
  "ENAME": {"$notnull": null}
}
```

## And Operator ($and)

The operator supports all operators, including `$and` and `$or`.

The following example matches a salary greater than 1000 and a name that starts with S or T:

```json
{
  "SALARY": {"$gt": 1000},
  "ENAME": {"$or": [{"$like":"S%"}, {"$like":"T%"}]}
}
```

The following expression is invalid, because the operators `$lt` and `$gt` lack a column context:

```json
{
  "$and": [{"$lt": 5000},{"$gt": 1000}]
}
```

The following expression is a valid alternative to the invalid one:

```json
{
  "$and": [{"SALARY": {"$lt": 5000}}, {"SALARY": {"$gt": 1000}}]
}
```

## Or Operator ($or)

The operator supports all operators, including `$and` and `$or`, like the `$and` operator.

The following example matches a name that starts with S or a salary greater than 1000:

```json
{
  "$or": [{"SALARY":{"$gt": 1000}},{"ENAME": {"$like":"S%"}}]
}
```
