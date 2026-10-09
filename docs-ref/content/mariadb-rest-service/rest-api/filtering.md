---
description: >-
  Filter and sort the result of a query against a REST-enabled table or view
  of the MariaDB REST Service with a FilterObject in the q parameter, and the
  grammar of the FilterObject.
---

# Filtering in REST Queries

This page describes filtering in queries against REST-enabled tables and views of the MariaDB REST Service (MRS). [FilterObject Examples](filter-examples.md) shows each operator in practice.

Filtering limits a collection resource with a dynamic filter definition per request, across multiple page resources. Each page contains a subset of the items of the complete collection. Filtering enables efficient traversal of large collections.

To filter a query, add the parameter `q=FilterObject`, where `FilterObject` is a JSON object that specifies the selection and sorting to apply to the resource. For example, take the following resource:

```text
https://example.com/myService/sakila/actor/
```

The following query contains a filter that restricts the `first_name` column to `"BRUCE"`. The REST object uses the default JSON field mapping, which translates the database column `first_name` (snake_case) to the JSON field `firstName` (camelCase).

```text
https://example.com/myService/sakila/actor/?q={"firstName":"BRUCE"}
```

## FilterObject Grammar

The FilterObject is a JSON object that complies with the following syntax:

```text
    FilterObject { orderby , asof, wmembers }
```

The `orderby`, `asof`, and `wmembers` attributes are optional. They are defined as follows:

```text
orderby
    "$orderby": {orderByMembers}

orderByMembers
    orderByProperty
    orderByProperty , orderByMembers

orderByProperty
    columnName : sortingValue

sortingValue
    "ASC"
    "DESC"
    "-1"
    "1"
    -1
    1

asof
    "$asof": gtid

wmembers
    wpair
    wpair , wmembers

wpair
    columnProperty
    complexOperatorProperty

columnProperty
    columnName : string
    columnName : number
    columnName : date
    columnName : geo
    columnName : vector
    columnName : boolean
    columnName : simpleOperatorObject
    columnName : complexOperatorObject
    columnName : [complexValues]

columnName
"\p{Alpha}[[\p{Alpha}]]([[\p{Alnum}]#$_])*$"

complexOperatorProperty
    complexKey : [complexValues]
    complexKey : simpleOperatorObject

complexKey
    "$and"
    "$or"

complexValues
    complexValue , complexValues

complexValue
    simpleOperatorObject
    complexOperatorObject
    columnObject

columnObject
    {columnProperty}

simpleOperatorObject
    {simpleOperatorProperty}

complexOperatorObject
    {complexOperatorProperty}

simpleOperatorProperty
    "$eq" : string | number | date | geo | vector | boolean
    "$ne" : string | number | date | geo | vector | boolean
    "$lt" :  number | date
    "$lte" : number | date
    "$gt" : number | date
    "$gte" : number | date
    "$instr" : string
    "$ninstr" : string
    "$like" : string
    "$null" : null
    "$notnull" : null
    "$between" : betweenValue
    "$match": fullTextSearch

betweenValue
    [null , betweenNotNull]
    [betweenNotNull , null]
    [betweenRegular , betweenRegular]

betweenNotNull
    number
    date

betweenRegular
    string
    number
    date

fullTextSearch
    {"$params":[fieldList], "$against":{"$expr":fullTextExpr}}
    {"$params":[fieldList], "$against":{"$expr":fullTextExpr, "$modifier":fullTextMod}}
```

The data types are defined as follows:

```text
string
    JSONString

number
    JSONNumber

date
      {"$date":"datechars"}

gtid
    JSONString

geo
    https://en.wikipedia.org/wiki/GeoJSON

vector
    [numberList]

numberList
    number, numberList

fieldList
    fieldName, fieldList

fieldName: JSONString

fullTextExpr: JSONString

fullTextMod:
    "IN NATURAL LANGUAGE MODE"
    "IN NATURAL LANGUAGE MODE WITH QUERY EXPANSION"
    "IN BOOLEAN MODE"
    "WITH QUERY EXPANSION"
```

The `datechars`, `JSONString`, and `JSONNumber` terms are defined as follows:

```text
datechars is an RFC3339 date format in UTC (Z)


JSONString
        ""
        " chars "
chars
        char
        char chars
char
        any-Unicode-character except-"-or-\-or-control-character
        \"
        \\
        \/
        \b
        \f
        \n
        \r
        \t
        \u four-hex-digits


JSONNumber
    int
    int frac
    int exp
    int frac exp
int
    digit
    digit1-9 digits
    - digit
    - digit1-9 digits
frac
    . digits
exp
    e digits
digits
    digit
    digit digits
e
    e
    e+
    e-
    E
    E+
    E-
```

Encode the FilterObject in the URL according to [Section 2.1 of RFC 3986](https://datatracker.ietf.org/doc/html/rfc3986#section-2.1).
