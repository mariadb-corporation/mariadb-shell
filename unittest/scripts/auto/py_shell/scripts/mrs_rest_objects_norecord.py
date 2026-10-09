#@ {__server_is_maria_db}
#@<> Initialization
# The REST SQL statements of the mrs module for database objects: REST VIEW
# (data mapping views), REST PROCEDURE and REST FUNCTION.
import json
import os

testutil.deploy_sandbox(__mysql_sandbox_port1, "root")
shell.connect(__sandbox_uri1)
testutil.import_data(__sandbox_uri1, os.path.join(__data_path, "sql", "sakila-schema.sql"))

def rest(sql):
    return session.run_sql(sql)

def rest_rows(sql):
    return [list(row) for row in rest(sql).fetch_all()]

def rest_info(sql):
    return rest(sql).get_info()

def show_create(sql):
    return rest(sql).fetch_one()[0]

def metadata_value(sql):
    row = session.run_sql(sql).fetch_one()
    return row[0] if row else None

def table_privileges(table):
    return metadata_value("SELECT Table_priv FROM mysql.tables_priv WHERE User = 'mariadb_rest_service_data_provider' AND Db = 'sakila' AND Table_name = '%s'" % table)

def routine_privileges(routine, routine_type):
    return metadata_value("SELECT Proc_priv FROM mysql.procs_priv WHERE User = 'mariadb_rest_service_data_provider' AND Db = 'sakila' AND Routine_name = '%s' AND Routine_type = '%s'" % (routine, routine_type))

def rest_object_column(request_path, column):
    return metadata_value("SELECT %s FROM mariadb_rest_service.rest_object WHERE request_path = '%s'" % (column, request_path))

rest("CONFIGURE REST METADATA ENABLED")
rest("CREATE REST SERVICE /svc")
rest("CREATE REST SCHEMA /sakila ON SERVICE /svc FROM sakila")

#@<> Statements need a REST schema
EXPECT_THROWS(lambda: rest("CREATE REST VIEW /city AS sakila.city"), "Failed to create the REST VIEW `/city`. No REST schema given.")
EXPECT_THROWS(lambda: rest("SHOW REST VIEWS"), "Cannot SHOW the REST db objects. No REST schema given.")
EXPECT_THROWS(lambda: rest("CREATE REST VIEW /city ON SERVICE /svc SCHEMA /nope AS sakila.city"), "Failed to create the REST VIEW `/svc/nope/city`. The REST schema `/svc/nope` was not found.")
EXPECT_THROWS(lambda: rest("CREATE REST VIEW /city ON SERVICE /nope SCHEMA /sakila AS sakila.city"), "Failed to create the REST VIEW `/nope/sakila/city`. Could not find the REST SERVICE /nope.")

rest("USE REST SERVICE /svc")
rest("USE REST SCHEMA /sakila")
EXPECT_EQ([], rest_rows("SHOW REST VIEWS"))

#@<> CREATE REST VIEW without a data mapping exposes all columns
EXPECT_EQ("REST VIEW `/svc/sakila/city` created successfully.", rest_info("CREATE REST VIEW /city AS sakila.city"))
EXPECT_EQ("""CREATE OR REPLACE REST VIEW /city
    ON SERVICE /svc SCHEMA /sakila
    AS `sakila`.`city` CLASS SvcSakilaCity {
        cityId: city_id @KEY,
        city: city,
        countryId: country_id,
        lastUpdate: last_update
    }
    AUTHENTICATION REQUIRED;""", show_create("SHOW CREATE REST VIEW /city"))
EXPECT_EQ("TABLE", rest_object_column("/city", "object_type"))
EXPECT_EQ("READ", rest_object_column("/city", "crud_operations"))
EXPECT_EQ(1, rest_object_column("/city", "requires_auth"))
EXPECT_EQ("FEED", rest_object_column("/city", "format"))
EXPECT_EQ("Select", table_privileges("city"))
# The references of the table are stored as disabled fields
EXPECT_EQ(6, metadata_value("SELECT COUNT(*) FROM mariadb_rest_service.data_mapping_field"))
EXPECT_EQ(4, metadata_value("SELECT COUNT(*) FROM mariadb_rest_service.data_mapping_field WHERE enabled = 1"))
EXPECT_THROWS(lambda: rest("CREATE REST VIEW /city AS sakila.city"), "Failed to create the REST VIEW `/svc/sakila/city`. The request_path is already used by another entity.")

#@<> CREATE REST VIEW with nested references, annotations and all options
rest("""CREATE OR REPLACE REST VIEW /city AS sakila.city CLASS MyCity @INSERT @UPDATE @DELETE @NOCHECK {
    cityId: city_id @KEY @SORTABLE @NOCHECK,
    city: city @NOFILTERING,
    lastUpdate: last_update @NOUPDATE,
    country: sakila.country @UNNEST {
        country: country
    },
    addresses: sakila.address @INSERT {
        addressId: address_id,
        address: address @DATATYPE("varchar(50)") JSON SCHEMA {"type": "string"}
    }
}
DISABLED AUTHENTICATION NOT REQUIRED ITEMS PER PAGE 10 COMMENT 'The cities'
MEDIA TYPE 'application/json' FORMAT ITEM OPTIONS {"a": 1} METADATA {"m": [1, 2]}""")
full_city = """CREATE OR REPLACE REST VIEW /city
    ON SERVICE /svc SCHEMA /sakila
    AS `sakila`.`city` CLASS MyCity @INSERT @UPDATE @DELETE @NOCHECK {
        cityId: city_id @KEY @NOCHECK @SORTABLE,
        city: city @NOFILTERING,
        lastUpdate: last_update @NOUPDATE,
        country: sakila.country @UNNEST {
            country: country
        },
        addresses: sakila.address @INSERT {
            addressId: address_id @KEY,
            address: address
        }
    }
    DISABLED
    AUTHENTICATION NOT REQUIRED
    ITEMS PER PAGE 10
    COMMENT 'The cities'
    MEDIA TYPE 'application/json'
    FORMAT ITEM
    OPTIONS {
        "a": 1
    }
    METADATA {
        "m": [
            1,
            2
        ]
    };"""
EXPECT_EQ(full_city, show_create("SHOW CREATE REST VIEW /city"))
EXPECT_EQ("CREATE,READ,UPDATE,DELETE", rest_object_column("/city", "crud_operations"))
EXPECT_EQ("Select,Insert,Update,Delete", table_privileges("city"))
EXPECT_EQ("Select,Insert,Update,Delete", table_privileges("country"))
EXPECT_EQ("Select,Insert,Update,Delete", table_privileges("address"))
EXPECT_EQ([["/city", "DISABLED"]], rest_rows("SHOW REST VIEWS"))

#@<> The stored data mapping
EXPECT_EQ("true", metadata_value("SELECT JSON_EXTRACT(options, '$.dataMappingViewInsert') FROM mariadb_rest_service.data_mapping WHERE name = 'MyCity'"))
EXPECT_EQ("true", metadata_value("SELECT JSON_EXTRACT(options, '$.duality_view_insert') FROM mariadb_rest_service.data_mapping WHERE name = 'MyCity'"))
EXPECT_EQ("true", metadata_value("SELECT JSON_EXTRACT(options, '$.duality_view_no_check') FROM mariadb_rest_service.data_mapping WHERE name = 'MyCity'"))
EXPECT_EQ("true", metadata_value("SELECT JSON_EXTRACT(db_column, '$.is_primary') FROM mariadb_rest_service.data_mapping_field WHERE name = 'cityId' AND parent_reference_id IS NULL"))
EXPECT_EQ("varchar(50)", metadata_value("SELECT JSON_VALUE(db_column, '$.datatype') FROM mariadb_rest_service.data_mapping_field WHERE name = 'address' AND db_column IS NOT NULL"))
EXPECT_EQ("string", metadata_value("SELECT JSON_VALUE(json_schema, '$.type') FROM mariadb_rest_service.data_mapping_field WHERE name = 'address' AND db_column IS NOT NULL"))
EXPECT_EQ(0, metadata_value("SELECT allow_filtering FROM mariadb_rest_service.data_mapping_field WHERE name = 'city' AND parent_reference_id IS NULL"))
EXPECT_EQ(1, metadata_value("SELECT no_update FROM mariadb_rest_service.data_mapping_field WHERE name = 'lastUpdate' AND parent_reference_id IS NULL"))
EXPECT_EQ("n:1", metadata_value("SELECT JSON_VALUE(r.reference_mapping, '$.kind') FROM mariadb_rest_service.data_mapping_reference r JOIN mariadb_rest_service.data_mapping_field f ON f.represents_reference_id = r.id WHERE f.name = 'country'"))
EXPECT_EQ("1:n", metadata_value("SELECT JSON_VALUE(r.reference_mapping, '$.kind') FROM mariadb_rest_service.data_mapping_reference r JOIN mariadb_rest_service.data_mapping_field f ON f.represents_reference_id = r.id WHERE f.name = 'addresses'"))
EXPECT_EQ("city_id", metadata_value("SELECT JSON_VALUE(r.reference_mapping, '$.column_mapping[0].base') FROM mariadb_rest_service.data_mapping_reference r JOIN mariadb_rest_service.data_mapping_field f ON f.represents_reference_id = r.id WHERE f.name = 'addresses'"))
EXPECT_EQ("true", metadata_value("SELECT JSON_EXTRACT(r.options, '$.dataMappingViewInsert') FROM mariadb_rest_service.data_mapping_reference r JOIN mariadb_rest_service.data_mapping_field f ON f.represents_reference_id = r.id WHERE f.name = 'addresses'"))
EXPECT_EQ(1, metadata_value("SELECT r.unnest FROM mariadb_rest_service.data_mapping_reference r JOIN mariadb_rest_service.data_mapping_field f ON f.represents_reference_id = r.id WHERE f.name = 'country'"))
# The fields of a referenced table hang below the reference
EXPECT_EQ("country", metadata_value("SELECT p.name FROM mariadb_rest_service.data_mapping_field f JOIN mariadb_rest_service.data_mapping_field p ON p.represents_reference_id = f.parent_reference_id WHERE f.name = 'country' AND f.db_column IS NOT NULL"))

#@<> SHOW CREATE REST VIEW round trip
EXPECT_EQ("REST VIEW `/svc/sakila/city` created successfully.", rest_info(full_city))
EXPECT_EQ(full_city, show_create("SHOW CREATE REST VIEW /city"))
EXPECT_EQ(full_city, show_create("SHOW CREATE REST DATA MAPPING VIEW /city ON SERVICE /svc SCHEMA /sakila"))

#@<> SHOW CREATE REST VIEW FORMAT=JSON
res = rest("SHOW CREATE REST VIEW /city FORMAT=JSON")
EXPECT_EQ(["CREATE REST VIEW"], res.get_column_names())
doc = json.loads(res.fetch_one()[0])
EXPECT_EQ({"name": "city", "schema_name": "sakila", "request_path": "/city", "object_type": "TABLE", "crud_operations": ["CREATE", "READ", "UPDATE", "DELETE"], "format": "ITEM", "enabled": 0, "requires_auth": False, "items_per_page": 10, "media_type": "application/json", "comments": "The cities", "options": {"a": 1}, "metadata": {"m": [1, 2]}}, {k: doc[k] for k in ["name", "schema_name", "request_path", "object_type", "crud_operations", "format", "enabled", "requires_auth", "items_per_page", "media_type", "comments", "options", "metadata"]})
EXPECT_EQ(rest_object_column("/city", "id"), doc["id"])
EXPECT_EQ(1, len(doc["data_mappings"]))
obj = doc["data_mappings"][0]
EXPECT_EQ(["MyCity", "RESULT"], [obj["name"], obj["kind"]])
EXPECT_TRUE(obj["options"]["dataMappingViewInsert"])
# Columns left out of the mapping are stored as disabled fields
fields = [f for f in obj["fields"] if f["enabled"]]
EXPECT_EQ(["cityId", "city", "lastUpdate", "country", "addresses"], [f["name"] for f in fields if f["parent_reference_id"] is None])
EXPECT_EQ(["countryId"], [f["name"] for f in obj["fields"] if not f["enabled"] and f["parent_reference_id"] is None])
city_id = [f for f in fields if f["name"] == "cityId"][0]
EXPECT_EQ(["city_id", True, None], [city_id["db_column"]["name"], city_id["db_column"]["is_primary"], city_id["data_mapping_reference"]])
EXPECT_TRUE(city_id["allow_sorting"])
EXPECT_TRUE(city_id["no_check"])
EXPECT_FALSE([f for f in fields if f["name"] == "city"][0]["allow_filtering"])
# A field representing a reference carries it; the fields below point to it
country = [f for f in fields if f["name"] == "country" and f["parent_reference_id"] is None][0]
reference = country["data_mapping_reference"]
EXPECT_EQ(country["represents_reference_id"], reference["id"])
EXPECT_EQ(["n:1", "country"], [reference["reference_mapping"]["kind"], reference["reference_mapping"]["referenced_table"]])
EXPECT_TRUE(reference["unnest"])
EXPECT_EQ(["country"], [f["name"] for f in fields if f["parent_reference_id"] == reference["id"]])
EXPECT_EQ(doc, json.loads(show_create("SHOW CREATE REST DATA MAPPING VIEW /city ON SERVICE /svc SCHEMA /sakila FORMAT=JSON")))

#@<> CREATE REST VIEW: IF NOT EXISTS and OR REPLACE
EXPECT_EQ("REST VIEW `/svc/sakila/city` created successfully.", rest_info("CREATE REST VIEW IF NOT EXISTS /city AS sakila.city"))
EXPECT_EQ(full_city, show_create("SHOW CREATE REST VIEW /city"))
EXPECT_EQ("REST VIEW `/svc/sakila/city` created successfully.", rest_info("CREATE OR REPLACE REST VIEW /city AS sakila.city"))
EXPECT_IN("CLASS SvcSakilaCity {", show_create("SHOW CREATE REST VIEW /city"))
# Only the privileges on the database object itself are revoked, those of
# referenced tables may be shared with other REST views
EXPECT_EQ("Select", table_privileges("city"))
EXPECT_EQ("Select,Insert,Update,Delete", table_privileges("address"))
EXPECT_EQ("REST VIEW `/svc/sakila/city` created successfully.", rest_info(full_city))

#@<> CREATE REST VIEW: @UNNEST of a 1:n reference reduces to a single column
rest("""CREATE REST VIEW /country AS sakila.country {
    countryId: country_id @ROWOWNERSHIP,
    country: country,
    cities: sakila.city @UNNEST {
        city: city
    }
}""")
EXPECT_EQ("""CREATE OR REPLACE REST VIEW /country
    ON SERVICE /svc SCHEMA /sakila
    AS `sakila`.`country` CLASS SvcSakilaCountry {
        countryId: country_id @KEY @ROWOWNERSHIP,
        country: country,
        cities: sakila.city @UNNEST {
            city: city
        }
    }
    AUTHENTICATION REQUIRED;""", show_create("SHOW CREATE REST VIEW /country"))
EXPECT_EQ("city", metadata_value("SELECT f2.name FROM mariadb_rest_service.data_mapping_reference r JOIN mariadb_rest_service.data_mapping_field f ON f.represents_reference_id = r.id JOIN mariadb_rest_service.data_mapping_field f2 ON f2.id = r.reduce_to_value_of_field_id WHERE f.name = 'cities'"))
EXPECT_EQ("countryId", metadata_value("SELECT f.name FROM mariadb_rest_service.data_mapping o JOIN mariadb_rest_service.data_mapping_field f ON f.id = o.row_ownership_field_id WHERE o.name = 'SvcSakilaCountry'"))
EXPECT_EQ([["/city", "DISABLED"], ["/country", "ENABLED"]], rest_rows("SHOW REST VIEWS"))
EXPECT_EQ([["/city", "DISABLED"], ["/country", "ENABLED"]], rest_rows("SHOW REST DATA MAPPING VIEWS ON SERVICE /svc SCHEMA /sakila"))

#@<> CREATE REST VIEW: error cases
EXPECT_THROWS(lambda: rest("CREATE REST VIEW /bad AS sakila.country { countryId: country_id, cities: sakila.city @UNNEST { cityId: city_id, city: city } }"), "Failed to create the REST VIEW `/svc/sakila/bad`. Only one column `cityId` must be defined for a N:1 unnest operation. The column `city` needs to be removed.")
EXPECT_THROWS(lambda: rest("CREATE REST VIEW /bad AS sakila.country { countryId: country_id, cities: sakila.city @UNNEST { } }"), "At least one column must be defined for a N:1 unnest operation.")
EXPECT_THROWS(lambda: rest("CREATE REST VIEW /bad AS sakila.country { films: sakila.film { title: title } }"), "The table `sakila`.`film` has no reference to `sakila`.`country`.")
EXPECT_THROWS(lambda: rest("CREATE REST VIEW /bad AS sakila.country { nope: nope }"), "The column `nope` does not exist on `sakila`.`country`.")
EXPECT_THROWS(lambda: rest("CREATE REST VIEW /bad AS sakila.nope"), "The table or view `sakila`.`nope` does not exist.")
EXPECT_THROWS(lambda: rest("CREATE REST VIEW /bad AS sakila.city CLASS MyCity"), "The object name MyCity is already in use on this REST schema.")
EXPECT_EQ([["/city", "DISABLED"], ["/country", "ENABLED"]], rest_rows("SHOW REST VIEWS"))

#@<> CREATE REST VIEW on a database view, in the schema of the REST schema
rest("CREATE REST VIEW /filmList AS film_list")
EXPECT_EQ("VIEW", rest_object_column("/filmList", "object_type"))
film_list = show_create("SHOW CREATE REST VIEW /filmList")
EXPECT_IN("    AS `sakila`.`film_list` CLASS SvcSakilaFilmList {\n        fid: FID,\n        title: title,\n", film_list)
EXPECT_IN("        rating: rating,\n        actors: actors\n    }\n", film_list)
EXPECT_EQ("Select", table_privileges("film_list"))

#@<> ALTER REST VIEW: path, options and MERGE OPTIONS
res = rest("ALTER REST VIEW /city NEW REQUEST PATH /cities ENABLED AUTHENTICATION REQUIRED ITEMS PER PAGE 20 COMMENT 'altered' FORMAT FEED OPTIONS {\"x\": 1}")
EXPECT_EQ(1, res.get_affected_items_count())
EXPECT_EQ("""CREATE OR REPLACE REST VIEW /cities
    ON SERVICE /svc SCHEMA /sakila
    AS `sakila`.`city` CLASS MyCity @INSERT @UPDATE @DELETE @NOCHECK {
        cityId: city_id @KEY @NOCHECK @SORTABLE,
        city: city @NOFILTERING,
        lastUpdate: last_update @NOUPDATE,
        country: sakila.country @UNNEST {
            country: country
        },
        addresses: sakila.address @INSERT {
            addressId: address_id @KEY,
            address: address
        }
    }
    AUTHENTICATION REQUIRED
    ITEMS PER PAGE 20
    COMMENT 'altered'
    MEDIA TYPE 'application/json'
    OPTIONS {
        "x": 1
    }
    METADATA {
        "m": [
            1,
            2
        ]
    };""", show_create("SHOW CREATE REST VIEW /cities"))
rest("ALTER REST VIEW /cities MERGE OPTIONS {\"y\": 2}")
EXPECT_IN('OPTIONS {\n        "x": 1,\n        "y": 2\n    }', show_create("SHOW CREATE REST VIEW /cities"))
rest("ALTER REST VIEW /cities ON SERVICE /svc SCHEMA /sakila MERGE OPTIONS {\"x\": null}")
EXPECT_IN('OPTIONS {\n        "y": 2\n    }', show_create("SHOW CREATE REST VIEW /cities"))
EXPECT_EQ("Select,Insert,Update,Delete", table_privileges("city"))
EXPECT_THROWS(lambda: rest("ALTER REST VIEW /city DISABLED"), "Failed to update the REST VIEW `/svc/sakila/city`. The given REST VIEW `/svc/sakila/city` could not be found.")
EXPECT_THROWS(lambda: rest("ALTER REST VIEW /cities ON SERVICE /svc SCHEMA /nope DISABLED"), "Failed to update the REST VIEW `/svc/nope/cities`. The REST schema `/svc/nope` was not found.")

#@<> ALTER REST VIEW: CLASS without a mapping renames the object and keeps the fields
res = rest("ALTER REST VIEW /cities CLASS Cities2 @INSERT")
EXPECT_EQ(1, res.get_affected_items_count())
altered = show_create("SHOW CREATE REST VIEW /cities")
EXPECT_IN("    AS `sakila`.`city` CLASS Cities2 @INSERT {\n        cityId: city_id @KEY @NOCHECK @SORTABLE,\n", altered)
EXPECT_IN("        addresses: sakila.address @INSERT {\n            addressId: address_id @KEY,\n            address: address\n        }\n    }\n", altered)
# CREATE + READ from the class, UPDATE from the reference that allows inserts
EXPECT_EQ("CREATE,READ,UPDATE", rest_object_column("/cities", "crud_operations"))
EXPECT_EQ("Select,Insert,Update", table_privileges("city"))
EXPECT_EQ("Select,Insert,Update,Delete", table_privileges("address"))
EXPECT_THROWS(lambda: rest("ALTER REST VIEW /cities CLASS SvcSakilaCountry"), "The object name SvcSakilaCountry is already in use on this REST schema.")

#@<> ALTER REST VIEW: CLASS with a mapping replaces the data mapping
rest("ALTER REST VIEW /cities CLASS Cities3 { cityId: city_id, city: city @SORTABLE } DISABLED")
EXPECT_EQ("""CREATE OR REPLACE REST VIEW /cities
    ON SERVICE /svc SCHEMA /sakila
    AS `sakila`.`city` CLASS Cities3 {
        cityId: city_id @KEY,
        city: city @SORTABLE
    }
    DISABLED
    AUTHENTICATION REQUIRED
    ITEMS PER PAGE 20
    COMMENT 'altered'
    MEDIA TYPE 'application/json'
    OPTIONS {
        "y": 2
    }
    METADATA {
        "m": [
            1,
            2
        ]
    };""", show_create("SHOW CREATE REST VIEW /cities"))
EXPECT_EQ("READ", rest_object_column("/cities", "crud_operations"))
EXPECT_EQ("Select", table_privileges("city"))
EXPECT_EQ(1, metadata_value("SELECT COUNT(*) FROM mariadb_rest_service.data_mapping WHERE rest_object_id = (SELECT id FROM mariadb_rest_service.rest_object WHERE request_path = '/cities')"))
rest("ALTER REST VIEW /cities NEW REQUEST PATH /city")
EXPECT_EQ([["/city", "DISABLED"], ["/country", "ENABLED"], ["/filmList", "ENABLED"]], rest_rows("SHOW REST VIEWS"))

#@<> ALTER REST VIEW: a failed change keeps the old privileges
# REVOKE and GRANT commit implicitly, so the privileges change before the
# metadata transaction; when the metadata update fails (here: ITEMS PER PAGE
# out of range for its INT UNSIGNED column), the old privileges come back.
EXPECT_EQ("Select", table_privileges("city"))
EXPECT_THROWS(lambda: rest("ALTER REST VIEW /city CLASS Cities4 @INSERT @UPDATE @DELETE { cityId: city_id, city: city } ITEMS PER PAGE 5000000000"), "Out of range value for column 'items_per_page'")
EXPECT_EQ("Select", table_privileges("city"))
EXPECT_EQ("READ", rest_object_column("/city", "crud_operations"))
EXPECT_IN("CLASS Cities3 {", show_create("SHOW CREATE REST VIEW /city"))
# Without a change of the privileges nothing is revoked or granted
rest("ALTER REST VIEW /city COMMENT 'same privileges'")
EXPECT_EQ("Select", table_privileges("city"))

#@<> DROP REST VIEW
EXPECT_EQ("REST VIEW `/svc/sakila/filmList` dropped successfully.", rest_info("DROP REST VIEW /filmList"))
EXPECT_EQ(None, table_privileges("film_list"))
EXPECT_THROWS(lambda: rest("DROP REST VIEW /filmList"), "Failed to drop the REST VIEW `/svc/sakila/filmList`. The given REST VIEW `/svc/sakila/filmList` could not be found.")
EXPECT_EQ("REST VIEW `/svc/sakila/filmList` dropped successfully.", rest_info("DROP REST VIEW IF EXISTS /filmList"))
EXPECT_EQ("REST VIEW `/svc/sakila/filmList` dropped successfully.", rest_info("DROP REST DATA MAPPING VIEW IF EXISTS /filmList FROM SERVICE /svc SCHEMA /sakila"))
EXPECT_EQ([["/city", "DISABLED"], ["/country", "ENABLED"]], rest_rows("SHOW REST VIEWS"))
EXPECT_THROWS(lambda: rest("SHOW CREATE REST VIEW /filmList"), "Failed to get the REST VIEW `/svc/sakila/filmList`. The given REST VIEW `/svc/sakila/filmList` could not be found.")

#@<> CREATE REST PROCEDURE with PARAMETERS and RESULTs
EXPECT_EQ("REST PROCEDURE `/svc/sakila/filmInStock` created successfully.", rest_info("""CREATE REST PROCEDURE /filmInStock AS sakila.film_in_stock
PARAMETERS FilmInStockParams {
    pFilmId: p_film_id @IN,
    pStoreId: p_store_id @IN,
    pFilmCount: p_film_count @OUT
}
RESULT FilmInStock {
    inventoryId: inventory_id @DATATYPE("int")
}
RESULT {
    total: total
}"""))
film_in_stock = """CREATE OR REPLACE REST PROCEDURE /filmInStock
    ON SERVICE /svc SCHEMA /sakila
    AS `sakila`.`film_in_stock`
    PARAMETERS FilmInStockParams {
        pFilmId: p_film_id @IN,
        pStoreId: p_store_id @IN,
        pFilmCount: p_film_count @OUT
    }
    RESULT FilmInStock {
        inventoryId: inventory_id @DATATYPE("int")
    }
    RESULT SvcSakilaFilmInStock2 {
        total: total @DATATYPE("varchar(255)")
    }
    AUTHENTICATION REQUIRED;"""
EXPECT_EQ(film_in_stock, show_create("SHOW CREATE REST PROCEDURE /filmInStock"))
EXPECT_EQ("PROCEDURE", rest_object_column("/filmInStock", "object_type"))
EXPECT_EQ("CREATE", rest_object_column("/filmInStock", "crud_operations"))
EXPECT_EQ("Execute", routine_privileges("film_in_stock", "PROCEDURE"))
EXPECT_EQ("true", metadata_value("SELECT JSON_EXTRACT(db_column, '$.out') FROM mariadb_rest_service.data_mapping_field WHERE name = 'pFilmCount'"))
EXPECT_EQ("false", metadata_value("SELECT JSON_EXTRACT(db_column, '$.in') FROM mariadb_rest_service.data_mapping_field WHERE name = 'pFilmCount'"))
EXPECT_IN("int", metadata_value("SELECT JSON_VALUE(db_column, '$.datatype') FROM mariadb_rest_service.data_mapping_field WHERE name = 'pFilmCount'"))
EXPECT_EQ([["PARAMETERS", 0], ["RESULT", 1], ["RESULT", 2]], [list(r) for r in session.run_sql("SELECT kind, position FROM mariadb_rest_service.data_mapping WHERE rest_object_id = (SELECT id FROM mariadb_rest_service.rest_object WHERE request_path = '/filmInStock') ORDER BY position").fetch_all()])
EXPECT_EQ([["/filmInStock", "ENABLED"]], rest_rows("SHOW REST PROCEDURES"))
EXPECT_EQ([], rest_rows("SHOW REST FUNCTIONS"))

#@<> SHOW CREATE REST PROCEDURE FORMAT=JSON
doc = json.loads(show_create("SHOW CREATE REST PROCEDURE /filmInStock FORMAT=JSON"))
EXPECT_EQ(["PROCEDURE", "film_in_stock"], [doc["object_type"], doc["name"]])
EXPECT_EQ([["FilmInStockParams", "PARAMETERS"], ["FilmInStock", "RESULT"], ["SvcSakilaFilmInStock2", "RESULT"]], [[o["name"], o["kind"]] for o in doc["data_mappings"]])
params = doc["data_mappings"][0]["fields"]
EXPECT_EQ([["pFilmId", True, False], ["pStoreId", True, False], ["pFilmCount", False, True]], [[f["name"], f["db_column"]["in"], f["db_column"]["out"]] for f in params])

#@<> SHOW CREATE REST PROCEDURE round trip
EXPECT_EQ("REST PROCEDURE `/svc/sakila/filmInStock` created successfully.", rest_info(film_in_stock))
EXPECT_EQ(film_in_stock, show_create("SHOW CREATE REST PROCEDURE /filmInStock"))
EXPECT_EQ("REST PROCEDURE `/svc/sakila/filmInStock` created successfully.", rest_info("CREATE REST PROCEDURE IF NOT EXISTS /filmInStock AS sakila.film_in_stock"))
EXPECT_EQ(film_in_stock, show_create("SHOW CREATE REST PROCEDURE /filmInStock"))

#@<> CREATE REST PROCEDURE without PARAMETERS exposes all parameters
rest("CREATE REST PROCEDURE /filmNotInStock AS film_not_in_stock PRIVATE COMMENT 'not in stock'")
EXPECT_EQ("""CREATE OR REPLACE REST PROCEDURE /filmNotInStock
    ON SERVICE /svc SCHEMA /sakila
    AS `sakila`.`film_not_in_stock`
    PARAMETERS SvcSakilaFilmNotInStockParams {
        pFilmId: p_film_id @IN,
        pStoreId: p_store_id @IN,
        pFilmCount: p_film_count @OUT
    }
    PRIVATE
    AUTHENTICATION REQUIRED
    COMMENT 'not in stock';""", show_create("SHOW CREATE REST PROCEDURE /filmNotInStock"))
EXPECT_EQ([["/filmInStock", "ENABLED"], ["/filmNotInStock", "PRIVATE"]], rest_rows("SHOW REST PROCEDURES"))

#@<> CREATE REST PROCEDURE: error cases and FORCE
EXPECT_THROWS(lambda: rest("CREATE REST PROCEDURE /bad AS sakila.film_in_stock PARAMETERS { nope: nope }"), "Failed to create the REST PROCEDURE `/svc/sakila/bad`. The column `nope` does not exist on `sakila`.`film_in_stock`.")
EXPECT_THROWS(lambda: rest("CREATE REST PROCEDURE /bad AS sakila.nope"), "Failed to create the REST PROCEDURE `/svc/sakila/bad`.")
EXPECT_THROWS(lambda: rest("SHOW CREATE REST FUNCTION /filmInStock"), "Failed to get the REST FUNCTION `/svc/sakila/filmInStock`. The given REST object `/svc/sakila/filmInStock` is not a REST FUNCTION.")
EXPECT_THROWS(lambda: rest("SHOW CREATE REST VIEW /filmInStock"), "The given REST object `/svc/sakila/filmInStock` is not a REST VIEW.")
EXPECT_THROWS(lambda: rest("DROP REST VIEW /filmInStock"), "The given REST VIEW `/svc/sakila/filmInStock` could not be found.")
res = rest("""CREATE REST PROCEDURE /forced AS sakila.nope FORCE
PARAMETERS {
    a: a @IN,
    b: b @OUT @DATATYPE("INT")
}
RESULT {
    x: x
}""")
EXPECT_EQ("REST PROCEDURE `/svc/sakila/forced` created successfully.", res.get_info())
EXPECT_EQ(1, res.get_warnings_count())
EXPECT_EQ("""CREATE OR REPLACE REST PROCEDURE /forced
    ON SERVICE /svc SCHEMA /sakila
    AS `sakila`.`nope`
    PARAMETERS SvcSakilaForcedParams {
        a: a @IN,
        b: b @OUT
    }
    RESULT SvcSakilaForced {
        x: x @DATATYPE("varchar(255)")
    }
    AUTHENTICATION REQUIRED;""", show_create("SHOW CREATE REST PROCEDURE /forced"))
EXPECT_EQ("int", metadata_value("SELECT JSON_VALUE(db_column, '$.datatype') FROM mariadb_rest_service.data_mapping_field WHERE name = 'b'"))
EXPECT_EQ("REST PROCEDURE `/svc/sakila/forced` dropped successfully.", rest_info("DROP REST PROCEDURE /forced"))

#@<> ALTER REST PROCEDURE
res = rest("""ALTER REST PROCEDURE /filmInStock ON SERVICE /svc SCHEMA /sakila NEW REQUEST PATH /filmInStock2
PARAMETERS P2 {
    pFilmId: p_film_id @IN
}
RESULT R2 {
    inventoryId: inventory_id @DATATYPE("int")
}
COMMENT 'altered'""")
EXPECT_EQ(1, res.get_affected_items_count())
EXPECT_EQ("""CREATE OR REPLACE REST PROCEDURE /filmInStock2
    ON SERVICE /svc SCHEMA /sakila
    AS `sakila`.`film_in_stock`
    PARAMETERS P2 {
        pFilmId: p_film_id @IN
    }
    RESULT R2 {
        inventoryId: inventory_id @DATATYPE("int")
    }
    AUTHENTICATION REQUIRED
    COMMENT 'altered';""", show_create("SHOW CREATE REST PROCEDURE /filmInStock2"))
EXPECT_EQ("Execute", routine_privileges("film_in_stock", "PROCEDURE"))
# Options alone leave the data mapping as it is
rest("ALTER REST PROCEDURE /filmInStock2 MERGE OPTIONS {\"test\": 1}")
rest("ALTER REST PROCEDURE /filmInStock2 MERGE OPTIONS {\"test2\": 2}")
altered = show_create("SHOW CREATE REST PROCEDURE /filmInStock2")
EXPECT_IN("    PARAMETERS P2 {\n        pFilmId: p_film_id @IN\n    }\n    RESULT R2 {\n", altered)
EXPECT_IN('    OPTIONS {\n        "test": 1,\n        "test2": 2\n    };', altered)
rest("ALTER REST PROCEDURE /filmInStock2 NEW REQUEST PATH /filmInStock")
EXPECT_THROWS(lambda: rest("ALTER REST PROCEDURE /nope DISABLED"), "Failed to update the REST PROCEDURE `/svc/sakila/nope`. The given REST PROCEDURE `/svc/sakila/nope` could not be found.")
EXPECT_THROWS(lambda: rest("ALTER REST PROCEDURE /city DISABLED"), "The given REST PROCEDURE `/svc/sakila/city` could not be found.")

#@<> CREATE REST FUNCTION
EXPECT_EQ("REST FUNCTION `/svc/sakila/inventoryInStock` created successfully.", rest_info("CREATE REST FUNCTION /inventoryInStock AS sakila.inventory_in_stock"))
EXPECT_EQ("""CREATE OR REPLACE REST FUNCTION /inventoryInStock
    ON SERVICE /svc SCHEMA /sakila
    AS `sakila`.`inventory_in_stock`
    PARAMETERS SvcSakilaInventoryInStockParams {
        pInventoryId: p_inventory_id @IN
    }
    RESULT SvcSakilaInventoryInStock {
        result: result @DATATYPE("tinyint")
    }
    AUTHENTICATION REQUIRED;""", show_create("SHOW CREATE REST FUNCTION /inventoryInStock"))
EXPECT_EQ("FUNCTION", rest_object_column("/inventoryInStock", "object_type"))
EXPECT_EQ("CREATE", rest_object_column("/inventoryInStock", "crud_operations"))
EXPECT_EQ("Execute", routine_privileges("inventory_in_stock", "FUNCTION"))

rest("""CREATE OR REPLACE REST FUNCTION /customerBalance AS sakila.get_customer_balance
PARAMETERS BalanceParams {
    pCustomerId: p_customer_id @IN,
    pEffectiveDate: p_effective_date @IN
}
RESULT Balance {
    balance: result @DATATYPE("decimal")
}
AUTHENTICATION NOT REQUIRED""")
customer_balance = """CREATE OR REPLACE REST FUNCTION /customerBalance
    ON SERVICE /svc SCHEMA /sakila
    AS `sakila`.`get_customer_balance`
    PARAMETERS BalanceParams {
        pCustomerId: p_customer_id @IN,
        pEffectiveDate: p_effective_date @IN
    }
    RESULT Balance {
        balance: result @DATATYPE("decimal")
    }
    AUTHENTICATION NOT REQUIRED;"""
EXPECT_EQ(customer_balance, show_create("SHOW CREATE REST FUNCTION /customerBalance"))
EXPECT_EQ("REST FUNCTION `/svc/sakila/customerBalance` created successfully.", rest_info(customer_balance))
EXPECT_EQ(customer_balance, show_create("SHOW CREATE REST FUNCTION /customerBalance ON SERVICE /svc SCHEMA /sakila"))
EXPECT_EQ([["/customerBalance", "ENABLED"], ["/inventoryInStock", "ENABLED"]], rest_rows("SHOW REST FUNCTIONS"))
EXPECT_THROWS(lambda: rest("CREATE REST FUNCTION /bad AS sakila.get_customer_balance RESULT { nope: nope }"), "Failed to create the REST FUNCTION `/svc/sakila/bad`. The column `nope` does not exist on `sakila`.`get_customer_balance`.")
EXPECT_THROWS(lambda: rest("CREATE REST FUNCTION /bad AS sakila.nope"), "Failed to create the REST FUNCTION `/svc/sakila/bad`.")

#@<> SHOW CREATE REST FUNCTION FORMAT=JSON
doc = json.loads(show_create("SHOW CREATE REST FUNCTION /customerBalance FORMAT=JSON"))
EXPECT_EQ(["FUNCTION", "get_customer_balance"], [doc["object_type"], doc["name"]])
EXPECT_EQ([["BalanceParams", "PARAMETERS"], ["Balance", "RESULT"]], [[o["name"], o["kind"]] for o in doc["data_mappings"]])

#@<> SHOW REST COLUMNS of a table
res = rest("SHOW REST COLUMNS FROM sakila.city")
EXPECT_EQ(["position", "name", "kind", "datatype", "not_null", "is_primary", "id_generation", "reference"], res.get_column_names())
rows = [list(r) for r in res.fetch_all()]
EXPECT_EQ([1, "city_id", "COLUMN", "smallint(5) unsigned", "YES", "YES", "auto_inc", None], rows[0])
EXPECT_EQ([2, "city", "COLUMN", "varchar(50)", "YES", "NO", None, None], rows[1])
EXPECT_EQ([["country", "REFERENCE", "n:1 sakila.country (country_id = country_id)"], ["address", "REFERENCE", "1:n sakila.address (city_id = city_id)"]], [[r[1], r[2], r[7]] for r in rows if r[2] == "REFERENCE"])
# The schema defaults to the one of the current REST schema; TABLE, IN and
# FROM are optional
EXPECT_EQ(rows, rest_rows("SHOW REST COLUMNS IN TABLE city"))
EXPECT_EQ(rows, rest_rows("SHOW REST COLUMNS FROM TABLE `sakila`.`city`"))
EXPECT_THROWS(lambda: rest("SHOW REST COLUMNS FROM VIEW sakila.city"), "Cannot SHOW the REST COLUMNS of `sakila`.`city`. `sakila`.`city` is a table, not a view.")
EXPECT_THROWS(lambda: rest("SHOW REST COLUMNS FROM sakila.nope"), "Cannot SHOW the REST COLUMNS of `sakila`.`nope`. The database object `sakila`.`nope` was not found.")
EXPECT_THROWS(lambda: rest("SHOW REST COLUMNS FROM TABLE sakila.nope"), "The table `sakila`.`nope` was not found.")
EXPECT_EQ("FID", rest_rows("SHOW REST COLUMNS FROM VIEW sakila.film_list")[0][1])

#@<> SHOW REST COLUMNS FORMAT=JSON of a table
res = rest("SHOW REST COLUMNS FROM sakila.city FORMAT=JSON")
EXPECT_EQ(["REST COLUMNS"], res.get_column_names())
doc = json.loads(res.fetch_one()[0])
EXPECT_EQ(["sakila", "city", "TABLE"], [doc["schema"], doc["name"], doc["type"]])
EXPECT_EQ(["city_id", "city", "country_id", "last_update", "country", "address"], [c["name"] for c in doc["columns"]])
EXPECT_EQ({"name": "city_id", "datatype": "smallint(5) unsigned", "not_null": True, "is_primary": True, "id_generation": "auto_inc"}, {k: doc["columns"][0]["db_column"][k] for k in ["name", "datatype", "not_null", "is_primary", "id_generation"]})
EXPECT_EQ(None, doc["columns"][0]["reference_mapping"])
EXPECT_EQ({"kind": "1:n", "to_many": True, "referenced_schema": "sakila", "referenced_table": "address", "column_mapping": [{"base": "city_id", "ref": "city_id"}]}, {k: doc["columns"][5]["reference_mapping"][k] for k in ["kind", "to_many", "referenced_schema", "referenced_table", "column_mapping"]})

#@<> SHOW REST COLUMNS of a procedure and a function
rows = rest_rows("SHOW REST COLUMNS FROM PROCEDURE sakila.film_in_stock")
EXPECT_EQ([[1, "p_film_id", "IN"], [2, "p_store_id", "IN"], [3, "p_film_count", "OUT"]], [r[:3] for r in rows])
EXPECT_IN("int", rows[0][3])
EXPECT_EQ(rows, rest_rows("SHOW REST COLUMNS FROM sakila.film_in_stock"))
rows = rest_rows("SHOW REST COLUMNS FROM sakila.inventory_in_stock")
EXPECT_EQ([[1, "p_inventory_id", "IN"], [0, None, "RETURN"]], [r[:3] for r in rows])
EXPECT_EQ("tinyint", rows[1][3])
EXPECT_THROWS(lambda: rest("SHOW REST COLUMNS FROM FUNCTION sakila.film_in_stock"), "The function `sakila`.`film_in_stock` was not found.")
doc = json.loads(show_create("SHOW REST COLUMNS FROM FUNCTION sakila.inventory_in_stock FORMAT=JSON"))
EXPECT_EQ(["FUNCTION", "tinyint"], [doc["type"], doc["return_type"]])
EXPECT_EQ([{"position": 1, "name": "p_inventory_id", "mode": "IN"}], [{k: p[k] for k in ["position", "name", "mode"]} for p in doc["parameters"]])
doc = json.loads(show_create("SHOW REST COLUMNS FROM PROCEDURE sakila.film_in_stock FORMAT=JSON"))
EXPECT_EQ(["p_film_id", "p_store_id", "p_film_count"], [p["name"] for p in doc["parameters"]])
EXPECT_FALSE("return_type" in doc)

#@<> ALTER REST FUNCTION
res = rest("ALTER REST FUNCTION /inventoryInStock RESULT Stock { inStock: result @DATATYPE(\"bool\") } ITEMS PER PAGE 5")
EXPECT_EQ(1, res.get_affected_items_count())
EXPECT_EQ("""CREATE OR REPLACE REST FUNCTION /inventoryInStock
    ON SERVICE /svc SCHEMA /sakila
    AS `sakila`.`inventory_in_stock`
    PARAMETERS SvcSakilaInventoryInStockParams {
        pInventoryId: p_inventory_id @IN
    }
    RESULT Stock {
        inStock: result @DATATYPE("bool")
    }
    AUTHENTICATION REQUIRED
    ITEMS PER PAGE 5;""", show_create("SHOW CREATE REST FUNCTION /inventoryInStock"))
rest("ALTER REST FUNCTION /inventoryInStock NEW REQUEST PATH /inStock MERGE OPTIONS {\"f\": 1}")
EXPECT_IN("    RESULT Stock {\n        inStock: result @DATATYPE(\"bool\")\n    }\n", show_create("SHOW CREATE REST FUNCTION /inStock"))
EXPECT_EQ("Execute", routine_privileges("inventory_in_stock", "FUNCTION"))
EXPECT_THROWS(lambda: rest("ALTER REST FUNCTION /filmInStock DISABLED"), "The given REST FUNCTION `/svc/sakila/filmInStock` could not be found.")

#@<> DROP REST PROCEDURE and DROP REST FUNCTION
EXPECT_EQ("REST PROCEDURE `/svc/sakila/filmNotInStock` dropped successfully.", rest_info("DROP REST PROCEDURE /filmNotInStock"))
EXPECT_THROWS(lambda: rest("DROP REST PROCEDURE /filmNotInStock"), "Failed to drop the REST PROCEDURE `/svc/sakila/filmNotInStock`. The given REST PROCEDURE `/svc/sakila/filmNotInStock` could not be found.")
EXPECT_EQ("REST PROCEDURE `/svc/sakila/filmNotInStock` dropped successfully.", rest_info("DROP REST PROCEDURE IF EXISTS /filmNotInStock"))
EXPECT_EQ(None, routine_privileges("film_not_in_stock", "PROCEDURE"))
EXPECT_EQ("REST FUNCTION `/svc/sakila/inStock` dropped successfully.", rest_info("DROP REST FUNCTION /inStock FROM SERVICE /svc SCHEMA /sakila"))
EXPECT_THROWS(lambda: rest("DROP REST FUNCTION /inStock"), "The given REST FUNCTION `/svc/sakila/inStock` could not be found.")
EXPECT_EQ("REST FUNCTION `/svc/sakila/inStock` dropped successfully.", rest_info("DROP REST FUNCTION IF EXISTS /inStock"))
EXPECT_EQ(None, routine_privileges("inventory_in_stock", "FUNCTION"))
EXPECT_EQ([["/filmInStock", "ENABLED"]], rest_rows("SHOW REST PROCEDURES"))
EXPECT_EQ([["/customerBalance", "ENABLED"]], rest_rows("SHOW REST FUNCTIONS"))

#@<> SHOW REST VIEWS / PROCEDURES / FUNCTIONS FORMAT=JSON
def json_list(sql, column):
    res = rest(sql)
    EXPECT_EQ([column], res.get_column_names())
    rows = res.fetch_all()
    EXPECT_EQ(1, len(rows))
    return json.loads(rows[0][0])

for kind in ["VIEW", "PROCEDURE", "FUNCTION"]:
    docs = json_list("SHOW REST %sS FORMAT=JSON" % kind, "REST %sS" % kind)
    # The same rows as the traditional form, ordered by request path
    EXPECT_EQ([r[0] for r in rest_rows("SHOW REST %sS" % kind)], [d["request_path"] for d in docs])
    EXPECT_EQ(sorted(d["request_path"] for d in docs), [d["request_path"] for d in docs])
    for doc in docs:
        # The SHOW CREATE document without its data mappings
        EXPECT_FALSE("data_mappings" in doc)
        full = json.loads(show_create("SHOW CREATE REST %s %s FORMAT=JSON" % (kind, doc["request_path"])))
        del full["data_mappings"]
        EXPECT_EQ(full, doc)
EXPECT_EQ(["/city", "/country"], [d["request_path"] for d in json_list("SHOW REST DATA MAPPING VIEWS FROM SERVICE /svc SCHEMA /sakila FORMAT=JSON", "REST VIEWS")])
EXPECT_EQ(["TABLE", "TABLE"], [d["object_type"] for d in json_list("SHOW REST VIEWS ON SCHEMA /sakila FORMAT=JSON", "REST VIEWS")])
EXPECT_EQ(["/filmInStock"], [d["request_path"] for d in json_list("SHOW REST PROCEDURES FORMAT=JSON", "REST PROCEDURES")])
EXPECT_EQ(["/customerBalance"], [d["request_path"] for d in json_list("SHOW REST FUNCTIONS FORMAT=JSON", "REST FUNCTIONS")])
# A schema without SCRIPT objects
EXPECT_EQ([], json_list("SHOW REST SCRIPTS FORMAT=JSON", "REST SCRIPTS"))
EXPECT_EQ([], rest_rows("SHOW REST SCRIPTS"))
EXPECT_EQ([["/city", "DISABLED"], ["/country", "ENABLED"]], rest_rows("SHOW REST VIEWS FORMAT=TRADITIONAL"))

#@<> sdk_service_data returns one nested JSON document
service_id = metadata_value("SELECT id FROM mariadb_rest_service.service WHERE url_context_root = '/svc'")
doc = json.loads(session.run_sql("CALL mariadb_rest_service.sdk_service_data(?)", [service_id]).fetch_one()[0])
EXPECT_EQ(dict, type(doc))
EXPECT_EQ(True, doc["enabled"])
EXPECT_EQ(False, doc["published"])
EXPECT_EQ(dict, type(doc["options"]))
EXPECT_EQ(["/sakila"], [s["request_path"] for s in doc["rest_schemas"]])
schema_doc = doc["rest_schemas"][0]
EXPECT_EQ(dict, type(schema_doc))
EXPECT_EQ(False, schema_doc["requires_auth"])
# Only the enabled objects (/city is disabled)
EXPECT_EQ(["/country", "/customerBalance", "/filmInStock"], sorted(o["request_path"] for o in schema_doc["rest_objects"]))
country = [o for o in schema_doc["rest_objects"] if o["request_path"] == "/country"][0]
EXPECT_EQ(dict, type(country))
EXPECT_EQ(bool, type(country["requires_auth"]))
EXPECT_EQ(bool, type(country["internal"]))
EXPECT_EQ(dict, type(country["data_mappings"][0]))
fields = country["data_mappings"][0]["fields"]
EXPECT_EQ(dict, type(fields[0]))
for field in fields:
    for flag in ["enabled", "allow_filtering", "allow_sorting", "no_check", "no_update"]:
        EXPECT_EQ(bool, type(field[flag]), flag)
# A field representing a reference carries it as an object with a boolean unnest
references = [f["data_mapping_reference"] for f in fields if f["data_mapping_reference"] is not None]
EXPECT_LT(0, len(references))
EXPECT_EQ(dict, type(references[0]))
EXPECT_EQ(bool, type(references[0]["unnest"]))
EXPECT_EQ(dict, type(references[0]["reference_mapping"]))
# The same flags as SHOW CREATE REST VIEW ... FORMAT=JSON
shown = json.loads(show_create("SHOW CREATE REST VIEW /country FORMAT=JSON"))["data_mappings"][0]["fields"]
EXPECT_EQ(sorted((f["id"], f["enabled"], f["allow_filtering"]) for f in shown), sorted((f["id"], f["enabled"], f["allow_filtering"]) for f in fields))

#@<> CLONE REST SERVICE copies the objects with their data mapping
rest("CLONE REST SERVICE /svc NEW REQUEST PATH /svc2")
EXPECT_EQ([["/city", "DISABLED"], ["/country", "ENABLED"]], rest_rows("SHOW REST VIEWS FROM SERVICE /svc2 SCHEMA /sakila"))
EXPECT_EQ(show_create("SHOW CREATE REST VIEW /city").replace("ON SERVICE /svc ", "ON SERVICE /svc2 "), show_create("SHOW CREATE REST VIEW /city ON SERVICE /svc2 SCHEMA /sakila"))
EXPECT_EQ(show_create("SHOW CREATE REST VIEW /country").replace("ON SERVICE /svc ", "ON SERVICE /svc2 "), show_create("SHOW CREATE REST VIEW /country ON SERVICE /svc2 SCHEMA /sakila"))
EXPECT_EQ(show_create("SHOW CREATE REST PROCEDURE /filmInStock").replace("ON SERVICE /svc ", "ON SERVICE /svc2 "), show_create("SHOW CREATE REST PROCEDURE /filmInStock ON SERVICE /svc2 SCHEMA /sakila"))
EXPECT_EQ(customer_balance.replace("ON SERVICE /svc ", "ON SERVICE /svc2 "), show_create("SHOW CREATE REST FUNCTION /customerBalance ON SERVICE /svc2 SCHEMA /sakila"))
rest("DROP REST SERVICE /svc2")

#@<> The dump of a service with its database endpoints loads the objects again
dump = show_create("SHOW CREATE REST SERVICE /svc INCLUDING DATABASE ENDPOINTS")
EXPECT_IN("CREATE OR REPLACE REST VIEW /city\n    ON SCHEMA /sakila\n    AS `sakila`.`city` CLASS Cities3 {", dump)
EXPECT_IN("CREATE OR REPLACE REST PROCEDURE /filmInStock\n", dump)
EXPECT_IN("CREATE OR REPLACE REST FUNCTION /customerBalance\n", dump)
dump_file = os.path.join(__tmp_dir, "svc.mrs.sql")
testutil.create_file(dump_file, dump.replace("SERVICE /svc", "SERVICE /loaded"))
testutil.call_mysqlsh([__sandbox_uri1, "--sql", "-f", dump_file], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
WIPE_OUTPUT()
EXPECT_EQ([["/city", "DISABLED"], ["/country", "ENABLED"]], rest_rows("SHOW REST VIEWS FROM SERVICE /loaded SCHEMA /sakila"))
EXPECT_EQ([["/filmInStock", "ENABLED"]], rest_rows("SHOW REST PROCEDURES FROM SERVICE /loaded SCHEMA /sakila"))
EXPECT_EQ(show_create("SHOW CREATE REST VIEW /city").replace("ON SERVICE /svc ", "ON SERVICE /loaded "), show_create("SHOW CREATE REST VIEW /city ON SERVICE /loaded SCHEMA /sakila"))
EXPECT_EQ(customer_balance.replace("ON SERVICE /svc ", "ON SERVICE /loaded "), show_create("SHOW CREATE REST FUNCTION /customerBalance ON SERVICE /loaded SCHEMA /sakila"))
rest("DROP REST SERVICE /loaded")
os.remove(dump_file)

#@<> Dropping the REST schema removes the objects and their privileges
rest("DROP REST VIEW /city")
rest("DROP REST VIEW /country")
rest("DROP REST PROCEDURE /filmInStock")
rest("DROP REST FUNCTION /customerBalance")
EXPECT_EQ([], rest_rows("SHOW REST VIEWS"))
EXPECT_EQ([], rest_rows("SHOW REST PROCEDURES"))
EXPECT_EQ([], rest_rows("SHOW REST FUNCTIONS"))
EXPECT_EQ(0, metadata_value("SELECT COUNT(*) FROM mariadb_rest_service.data_mapping"))
EXPECT_EQ(0, metadata_value("SELECT COUNT(*) FROM mariadb_rest_service.data_mapping_field"))
EXPECT_EQ(0, metadata_value("SELECT COUNT(*) FROM mariadb_rest_service.data_mapping_reference"))
EXPECT_EQ(None, table_privileges("city"))
EXPECT_EQ(None, routine_privileges("film_in_stock", "PROCEDURE"))
EXPECT_EQ(None, routine_privileges("get_customer_balance", "FUNCTION"))
rest("DROP REST SERVICE /svc")

#@<> Cleanup
session.close()
testutil.destroy_sandbox(__mysql_sandbox_port1)
