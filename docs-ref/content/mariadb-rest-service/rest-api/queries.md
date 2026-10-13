---
description: >-
  Read the metadata and data of REST-enabled schemas, tables, and views of the
  MariaDB REST Service over HTTP, and insert, update, and delete rows, with
  request and response examples.
---

# REST Queries

A REST service of the MariaDB REST Service (MRS) provides access to one or more schemas and their metadata, and to the database objects they contain, such as tables, views, and procedures, and their metadata. The MariaDB REST Daemon serves the requests shown on this page.

## Get Schema Metadata

This example retrieves a list of resources available through the specified schema alias. It shows the RESTful services that were created by REST-enabling a table, view, or procedure.

Pattern:

```text
GET http://<HOST>:<PORT>/<ServiceAlias>/<SchemaAlias>/metadata-catalog/
```

Example:

```text
GET http://localhost:8000/mrs/sakila/metadata-catalog/
```

Result:

```json
{
    "items": [
        {
            "name": "/actor",
            "links": [
                {
                    "rel": "describes",
                    "href": "/mrs/sakila/actor"
                },
                {
                    "rel": "canonical",
                    "href": "/mrs/sakila/metadata-catalog/actor"
                }
            ]
        },
        {
            "name": "/address",
            "links": [
                {
                    "rel": "describes",
                    "href": "/mrs/sakila/address"
                },
                {
                    "rel": "canonical",
                    "href": "/mrs/sakila/metadata-catalog/address"
                }
            ]
        }
    ],
    "limit": 25,
    "offset": 0,
    "hasMore": false,
    "count": 2,
    "links": [
        {
            "rel": "self",
            "href": "/mrs/sakila/metadata-catalog/"
        }
    ]
}
```

Each available resource has two hyperlinks:

* The link with the `describes` relation points to the resource itself.
* The link with the `canonical` relation points to the resource metadata.

## Get Object Metadata

This example retrieves the metadata of an individual object, which describes the object. The `canonical` link relation gives the location of the metadata.

Pattern:

```text
GET http://<HOST>:<PORT>/<ServiceAlias>/<SchemaAlias>/metadata-catalog/<ObjectAlias>/
```

Example:

```text
GET http://localhost:8000/mrs/sakila/metadata-catalog/actor/
```

Result:

```json
{
    "name": "/actor",
    "primaryKey": [
        "actor_id"
    ],
    "members": [
        {
            "name": "actor_id",
            "type": "null"
        },
        {
            "name": "first_name",
            "type": "null"
        },
        {
            "name": "last_name",
            "type": "null"
        },
        {
            "name": "last_update",
            "type": "string"
        }
    ],
    "links": [
        {
            "rel": "collection",
            "href": "/mrs/sakila/metadata-catalog",
            "mediaType": "application/json"
        },
        {
            "rel": "canonical",
            "href": "/mrs/sakila/metadata-catalog/actor"
        },
        {
            "rel": "describes",
            "href": "/mrs/sakila/actor"
        }
    ]
}
```

## Get Object Data

This example retrieves the data in the object. Each row in the object corresponds to a JSON object in the `items` JSON array.

Pattern:

```text
GET http://<HOST>:<PORT>/<ServiceAlias>/<SchemaAlias>/<ObjectAlias>/
```

Example:

```text
GET http://localhost:8000/mrs/sakila/actor/
```

Result:

```json
{
    "items": [
        {
            "links": [
                {
                    "rel": "self",
                    "href": "/mrs/sakila/actor/1"
                }
            ],
            "actor_id": 1,
            "last_name": "GUINESSS",
            "first_name": "PENELOPE",
            "last_update": "2021-09-28 20:18:53.000000"
        },
        {
            "links": [
                {
                    "rel": "self",
                    "href": "/mrs/sakila/actor/2"
                }
            ],
            "actor_id": 2,
            "last_name": "WAHLBERG",
            "first_name": "NICK",
            "last_update": "2006-02-15 03:34:33.000000"
        },
        {
            "links": [
                {
                    "rel": "self",
                    "href": "/mrs/sakila/actor/3"
                }
            ],
            "actor_id": 3,
            "last_name": "CHASE",
            "first_name": "ED",
            "last_update": "2006-02-15 03:34:33.000000"
        },
        ...
    ]
}
```

### Get Table Data Using Pagination

Use the `offset` and `limit` parameters to page through the result data.

Pattern:

```text
GET http://<HOST>:<PORT>/<ServiceAlias>/<SchemaAlias>/<ObjectAlias>/?offset=<Offset>&limit=<Limit>
```

Example:

```text
GET http://localhost:8080/mrs/sakila/actor/?offset=10&limit=2
```

Result:

```json
{
    "items": [
        {
            "links": [
                {
                    "rel": "self",
                    "href": "/mrs/sakila/actor/11"
                }
            ],
            "actor_id": 11,
            "last_name": "CAGE",
            "first_name": "ZERO",
            "last_update": "2006-02-15 03:34:33.000000"
        },
        {
            "links": [
                {
                    "rel": "self",
                    "href": "/mrs/sakila/actor/12"
                }
            ],
            "actor_id": 12,
            "last_name": "BERRY",
            "first_name": "KARL",
            "last_update": "2006-02-15 03:34:33.000000"
        }
    ],
    "limit": 2,
    "offset": 10,
    "hasMore": true,
    "count": 2,
    "links": [
        {
            "rel": "self",
            "href": "/mrs/sakila/actor/"
        },
        {
            "rel": "next",
            "href": "/mrs/sakila/actor/?offset=12&limit=2"
        },
        {
            "rel": "prev",
            "href": "/mrs/sakila/actor/?offset=8&limit=2"
        },
        {
            "rel": "first",
            "href": "/mrs/sakila/actor/?limit=2"
        }
    ]
}
```

### Get Table Data Using Query

Use a filter clause in the `q` parameter to restrict the set of objects that are returned. See [Filtering in REST Queries](filtering.md) for the syntax.

Pattern:

```text
GET http://<HOST>:<PORT>/<ServiceAlias>/<SchemaAlias>/<ObjectAlias>/?q=<FilterClause>
```

Example:

```text
GET http://localhost:8080/mrs/sakila/actor/?q={"last_name":{"$like":"WAW%"}}
```

Result:

```json
{
    "items": [
        {
            "links": [
                {
                    "rel": "self",
                    "href": "/mrs/sakila/actor/97"
                }
            ],
            "actor_id": 97,
            "last_name": "HAWKE",
            "first_name": "MEG",
            "last_update": "2006-02-15 03:34:33.000000"
        }
    ],
    "limit": 25,
    "offset": 0,
    "hasMore": false,
    "count": 1,
    "links": [
        {
            "rel": "self",
            "href": "/mrs/sakila/actor/"
        }
    ]
}
```

### Get Table Row Using Primary Key

This example retrieves an object by specifying its identifying key values.

{% hint style="info" %}
A table requires a primary key to be part of a REST service.
{% endhint %}

Pattern:

```text
GET http://<HOST>:<PORT>/<ServiceAlias>/<SchemaAlias>/<ObjectAlias>/<KeyValues>
```

`<KeyValues>` is a comma-separated list of key values (in key order).

Example:

```text
GET http://localhost:8000/mrs/sakila/actor/53
```

Result:

```json
{
    "links": [
        {
            "rel": "self",
            "href": "/mrs/sakila/actor/53"
        }
    ],
    "actor_id": 53,
    "last_name": "TEMPLE",
    "first_name": "MENA",
    "last_update": "2006-02-15 03:34:33.000000"
}
```

## Insert Table Row

To insert data into a table, send a POST request whose body is a JSON object that contains the data to insert.

If the object has a primary key, the POST request can include the primary key value in the body. If the table has an `AUTO_INCREMENT` column, you can omit the primary key column.

Pattern:

```text
POST http://<HOST>:<PORT>/<ServiceAlias>/<SchemaAlias>/<ObjectAlias>/
```

Example:

```sh
curl -i -H "Content-Type: application/json" -X POST -d "{ \"last_name\" : \"FOLEY\", \"first_name\": \"MIKE\" }" "http://localhost:8000/mrs/sakila/actor/" Content-Type: application/json
```

Result:

```json
{
    "links": [
        {
            "rel": "self",
            "href": "/mrs/sakila/actor/201"
        }
    ],
    "actor_id": 201,
    "last_name": "FOLEY",
    "first_name": "MIKE",
    "last_update": "2022-11-29 15:35:17.000000"
}
```

## Update or Insert Table Row

To insert, update, or "upsert" (update if the row exists, insert if not) data in a table, send a PUT request whose body is a JSON object with the data to insert or update.

Pattern:

```text
PUT http://<HOST>:<PORT>/<ServiceAlias>/<SchemaAlias>/<ObjectAlias>/<KeyValues>
```

Example:

```sh
curl -i -H "Content-Type: application/json" -X PUT -d "{ \"last_name\" : \"FOLEY\", \"first_name\": \"JACK\" }" "https://localhost:8000/mrs/sakila/actor/201" Content-Type: application/json
```

Result:

```json
{
    "links": [
        {
            "rel": "self",
            "href": "/mrs/sakila/actor/201"
        }
    ],
    "actor_id": 201,
    "last_name": "FOLEY",
    "first_name": "JACK",
    "last_update": "2022-11-29 15:45:10.000000"
}
```

## Delete Using Filter

To delete rows of a table or other database object, specify a filter clause that identifies the rows to delete.

Pattern:

```text
DELETE http://<HOST>:<PORT>/<ServiceAlias>/<SchemaAlias>/<ObjectAlias>/?q=<FilterClause>
```

Example:

```sh
curl -i -X DELETE "https://localhost:8000/mrs/sakila/actor/?q=\{\"actor_id\":201\}"
```

Result:

```json
{
    "itemsDeleted": 1
}
```
