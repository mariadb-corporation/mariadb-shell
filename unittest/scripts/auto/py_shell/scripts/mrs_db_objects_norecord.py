#@<> Initialization
# The REST SQL statements of the mrs module for database objects: REST VIEW
# (data mapping views), REST PROCEDURE and REST FUNCTION.
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
    return metadata_value("SELECT Table_priv FROM mysql.tables_priv WHERE User = 'mysql_rest_service_data_provider' AND Db = 'sakila' AND Table_name = '%s'" % table)

def routine_privileges(routine, routine_type):
    return metadata_value("SELECT Proc_priv FROM mysql.procs_priv WHERE User = 'mysql_rest_service_data_provider' AND Db = 'sakila' AND Routine_name = '%s' AND Routine_type = '%s'" % (routine, routine_type))

def db_object_column(request_path, column):
    return metadata_value("SELECT %s FROM mysql_rest_service_metadata.db_object WHERE request_path = '%s'" % (column, request_path))

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
EXPECT_EQ("TABLE", db_object_column("/city", "object_type"))
EXPECT_EQ("READ", db_object_column("/city", "crud_operations"))
EXPECT_EQ(1, db_object_column("/city", "requires_auth"))
EXPECT_EQ("FEED", db_object_column("/city", "format"))
EXPECT_EQ("Select", table_privileges("city"))
# The references of the table are stored as disabled fields
EXPECT_EQ(6, metadata_value("SELECT COUNT(*) FROM mysql_rest_service_metadata.object_field"))
EXPECT_EQ(4, metadata_value("SELECT COUNT(*) FROM mysql_rest_service_metadata.object_field WHERE enabled = 1"))
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
EXPECT_EQ("CREATE,READ,UPDATE,DELETE", db_object_column("/city", "crud_operations"))
EXPECT_EQ("Select,Insert,Update,Delete", table_privileges("city"))
EXPECT_EQ("Select,Insert,Update,Delete", table_privileges("country"))
EXPECT_EQ("Select,Insert,Update,Delete", table_privileges("address"))
EXPECT_EQ([["/city", "DISABLED"]], rest_rows("SHOW REST VIEWS"))

#@<> The stored data mapping
EXPECT_EQ("true", metadata_value("SELECT JSON_EXTRACT(options, '$.dataMappingViewInsert') FROM mysql_rest_service_metadata.object WHERE name = 'MyCity'"))
EXPECT_EQ("true", metadata_value("SELECT JSON_EXTRACT(options, '$.duality_view_insert') FROM mysql_rest_service_metadata.object WHERE name = 'MyCity'"))
EXPECT_EQ("true", metadata_value("SELECT JSON_EXTRACT(options, '$.duality_view_no_check') FROM mysql_rest_service_metadata.object WHERE name = 'MyCity'"))
EXPECT_EQ("true", metadata_value("SELECT JSON_EXTRACT(db_column, '$.is_primary') FROM mysql_rest_service_metadata.object_field WHERE name = 'cityId' AND parent_reference_id IS NULL"))
EXPECT_EQ("varchar(50)", metadata_value("SELECT JSON_VALUE(db_column, '$.datatype') FROM mysql_rest_service_metadata.object_field WHERE name = 'address' AND db_column IS NOT NULL"))
EXPECT_EQ("string", metadata_value("SELECT JSON_VALUE(json_schema, '$.type') FROM mysql_rest_service_metadata.object_field WHERE name = 'address' AND db_column IS NOT NULL"))
EXPECT_EQ(0, metadata_value("SELECT allow_filtering FROM mysql_rest_service_metadata.object_field WHERE name = 'city' AND parent_reference_id IS NULL"))
EXPECT_EQ(1, metadata_value("SELECT no_update FROM mysql_rest_service_metadata.object_field WHERE name = 'lastUpdate' AND parent_reference_id IS NULL"))
EXPECT_EQ("n:1", metadata_value("SELECT JSON_VALUE(r.reference_mapping, '$.kind') FROM mysql_rest_service_metadata.object_reference r JOIN mysql_rest_service_metadata.object_field f ON f.represents_reference_id = r.id WHERE f.name = 'country'"))
EXPECT_EQ("1:n", metadata_value("SELECT JSON_VALUE(r.reference_mapping, '$.kind') FROM mysql_rest_service_metadata.object_reference r JOIN mysql_rest_service_metadata.object_field f ON f.represents_reference_id = r.id WHERE f.name = 'addresses'"))
EXPECT_EQ("city_id", metadata_value("SELECT JSON_VALUE(r.reference_mapping, '$.column_mapping[0].base') FROM mysql_rest_service_metadata.object_reference r JOIN mysql_rest_service_metadata.object_field f ON f.represents_reference_id = r.id WHERE f.name = 'addresses'"))
EXPECT_EQ("true", metadata_value("SELECT JSON_EXTRACT(r.options, '$.dataMappingViewInsert') FROM mysql_rest_service_metadata.object_reference r JOIN mysql_rest_service_metadata.object_field f ON f.represents_reference_id = r.id WHERE f.name = 'addresses'"))
EXPECT_EQ(1, metadata_value("SELECT r.unnest FROM mysql_rest_service_metadata.object_reference r JOIN mysql_rest_service_metadata.object_field f ON f.represents_reference_id = r.id WHERE f.name = 'country'"))
# The fields of a referenced table hang below the reference
EXPECT_EQ("country", metadata_value("SELECT p.name FROM mysql_rest_service_metadata.object_field f JOIN mysql_rest_service_metadata.object_field p ON p.represents_reference_id = f.parent_reference_id WHERE f.name = 'country' AND f.db_column IS NOT NULL"))

#@<> SHOW CREATE REST VIEW round trip
EXPECT_EQ("REST VIEW `/svc/sakila/city` created successfully.", rest_info(full_city))
EXPECT_EQ(full_city, show_create("SHOW CREATE REST VIEW /city"))
EXPECT_EQ(full_city, show_create("SHOW CREATE REST DATA MAPPING VIEW /city ON SERVICE /svc SCHEMA /sakila"))

#@<> CREATE REST VIEW: IF NOT EXISTS and OR REPLACE
EXPECT_EQ("REST VIEW `/svc/sakila/city` created successfully.", rest_info("CREATE REST VIEW IF NOT EXISTS /city AS sakila.city"))
EXPECT_EQ(full_city, show_create("SHOW CREATE REST VIEW /city"))
EXPECT_EQ("REST VIEW `/svc/sakila/city` created successfully.", rest_info("CREATE OR REPLACE REST VIEW /city AS sakila.city"))
EXPECT_CONTAINS("CLASS SvcSakilaCity {", show_create("SHOW CREATE REST VIEW /city"))
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
EXPECT_EQ("city", metadata_value("SELECT f2.name FROM mysql_rest_service_metadata.object_reference r JOIN mysql_rest_service_metadata.object_field f ON f.represents_reference_id = r.id JOIN mysql_rest_service_metadata.object_field f2 ON f2.id = r.reduce_to_value_of_field_id WHERE f.name = 'cities'"))
EXPECT_EQ("countryId", metadata_value("SELECT f.name FROM mysql_rest_service_metadata.object o JOIN mysql_rest_service_metadata.object_field f ON f.id = o.row_ownership_field_id WHERE o.name = 'SvcSakilaCountry'"))
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
EXPECT_EQ("VIEW", db_object_column("/filmList", "object_type"))
film_list = show_create("SHOW CREATE REST VIEW /filmList")
EXPECT_CONTAINS("    AS `sakila`.`film_list` CLASS SvcSakilaFilmList {\n        fid: FID,\n        title: title,\n", film_list)
EXPECT_CONTAINS("        rating: rating,\n        actors: actors\n    }\n", film_list)
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
EXPECT_CONTAINS('OPTIONS {\n        "x": 1,\n        "y": 2\n    }', show_create("SHOW CREATE REST VIEW /cities"))
rest("ALTER REST VIEW /cities ON SERVICE /svc SCHEMA /sakila MERGE OPTIONS {\"x\": null}")
EXPECT_CONTAINS('OPTIONS {\n        "y": 2\n    }', show_create("SHOW CREATE REST VIEW /cities"))
EXPECT_EQ("Select,Insert,Update,Delete", table_privileges("city"))
EXPECT_THROWS(lambda: rest("ALTER REST VIEW /city DISABLED"), "Failed to update the REST VIEW `/svc/sakila/city`. The given REST VIEW `/svc/sakila/city` could not be found.")
EXPECT_THROWS(lambda: rest("ALTER REST VIEW /cities ON SERVICE /svc SCHEMA /nope DISABLED"), "Failed to update the REST VIEW `/svc/nope/cities`. The REST schema `/svc/nope` was not found.")

#@<> ALTER REST VIEW: CLASS without a mapping renames the object and keeps the fields
res = rest("ALTER REST VIEW /cities CLASS Cities2 @INSERT")
EXPECT_EQ(1, res.get_affected_items_count())
altered = show_create("SHOW CREATE REST VIEW /cities")
EXPECT_CONTAINS("    AS `sakila`.`city` CLASS Cities2 @INSERT {\n        cityId: city_id @KEY @NOCHECK @SORTABLE,\n", altered)
EXPECT_CONTAINS("        addresses: sakila.address @INSERT {\n            addressId: address_id @KEY,\n            address: address\n        }\n    }\n", altered)
# CREATE + READ from the class, UPDATE from the reference that allows inserts
EXPECT_EQ("CREATE,READ,UPDATE", db_object_column("/cities", "crud_operations"))
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
EXPECT_EQ("READ", db_object_column("/cities", "crud_operations"))
EXPECT_EQ("Select", table_privileges("city"))
EXPECT_EQ(1, metadata_value("SELECT COUNT(*) FROM mysql_rest_service_metadata.object WHERE db_object_id = (SELECT id FROM mysql_rest_service_metadata.db_object WHERE request_path = '/cities')"))
rest("ALTER REST VIEW /cities NEW REQUEST PATH /city")
EXPECT_EQ([["/city", "DISABLED"], ["/country", "ENABLED"], ["/filmList", "ENABLED"]], rest_rows("SHOW REST VIEWS"))

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
EXPECT_EQ("PROCEDURE", db_object_column("/filmInStock", "object_type"))
EXPECT_EQ("CREATE", db_object_column("/filmInStock", "crud_operations"))
EXPECT_EQ("Execute", routine_privileges("film_in_stock", "PROCEDURE"))
EXPECT_EQ("true", metadata_value("SELECT JSON_EXTRACT(db_column, '$.out') FROM mysql_rest_service_metadata.object_field WHERE name = 'pFilmCount'"))
EXPECT_EQ("false", metadata_value("SELECT JSON_EXTRACT(db_column, '$.in') FROM mysql_rest_service_metadata.object_field WHERE name = 'pFilmCount'"))
EXPECT_CONTAINS("int", metadata_value("SELECT JSON_VALUE(db_column, '$.datatype') FROM mysql_rest_service_metadata.object_field WHERE name = 'pFilmCount'"))
EXPECT_EQ([["PARAMETERS", 0], ["RESULT", 1], ["RESULT", 2]], [list(r) for r in session.run_sql("SELECT kind, position FROM mysql_rest_service_metadata.object WHERE db_object_id = (SELECT id FROM mysql_rest_service_metadata.db_object WHERE request_path = '/filmInStock') ORDER BY position").fetch_all()])
EXPECT_EQ([["/filmInStock", "ENABLED"]], rest_rows("SHOW REST PROCEDURES"))
EXPECT_EQ([], rest_rows("SHOW REST FUNCTIONS"))

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
EXPECT_EQ("int", metadata_value("SELECT JSON_VALUE(db_column, '$.datatype') FROM mysql_rest_service_metadata.object_field WHERE name = 'b'"))
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
EXPECT_CONTAINS("    PARAMETERS P2 {\n        pFilmId: p_film_id @IN\n    }\n    RESULT R2 {\n", altered)
EXPECT_CONTAINS('    OPTIONS {\n        "test": 1,\n        "test2": 2\n    };', altered)
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
EXPECT_EQ("FUNCTION", db_object_column("/inventoryInStock", "object_type"))
EXPECT_EQ("CREATE", db_object_column("/inventoryInStock", "crud_operations"))
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
EXPECT_CONTAINS("    RESULT Stock {\n        inStock: result @DATATYPE(\"bool\")\n    }\n", show_create("SHOW CREATE REST FUNCTION /inStock"))
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

#@<> CLONE REST SERVICE copies the objects with their data mapping
rest("CLONE REST SERVICE /svc NEW REQUEST PATH /svc2")
EXPECT_EQ([["/city", "DISABLED"], ["/country", "ENABLED"]], rest_rows("SHOW REST VIEWS FROM SERVICE /svc2 SCHEMA /sakila"))
EXPECT_EQ(show_create("SHOW CREATE REST VIEW /city").replace("ON SERVICE /svc ", "ON SERVICE /svc2 "), show_create("SHOW CREATE REST VIEW /city ON SERVICE /svc2 SCHEMA /sakila"))
EXPECT_EQ(show_create("SHOW CREATE REST VIEW /country").replace("ON SERVICE /svc ", "ON SERVICE /svc2 "), show_create("SHOW CREATE REST VIEW /country ON SERVICE /svc2 SCHEMA /sakila"))
EXPECT_EQ(show_create("SHOW CREATE REST PROCEDURE /filmInStock").replace("ON SERVICE /svc ", "ON SERVICE /svc2 "), show_create("SHOW CREATE REST PROCEDURE /filmInStock ON SERVICE /svc2 SCHEMA /sakila"))
EXPECT_EQ(customer_balance.replace("ON SERVICE /svc ", "ON SERVICE /svc2 "), show_create("SHOW CREATE REST FUNCTION /customerBalance ON SERVICE /svc2 SCHEMA /sakila"))
rest("DROP REST SERVICE /svc2")

#@<> DUMP and LOAD REST SERVICE with the database endpoints
dump_file = os.path.join(__tmp_dir, "svc.mrs.sql")
if os.path.exists(dump_file):
    os.remove(dump_file)
rest("DUMP REST SERVICE /svc AS SQL SCRIPT INCLUDING DATABASE ENDPOINTS TO '%s'" % dump_file)
with open(dump_file) as f:
    dump = f.read()
EXPECT_CONTAINS("CREATE OR REPLACE REST VIEW /city\n    ON SERVICE /svc SCHEMA /sakila\n    AS `sakila`.`city` CLASS Cities3 {", dump)
EXPECT_CONTAINS("CREATE OR REPLACE REST PROCEDURE /filmInStock\n", dump)
EXPECT_CONTAINS("CREATE OR REPLACE REST FUNCTION /customerBalance\n", dump)
rest("LOAD REST SERVICE AS /loaded FROM '%s'" % dump_file)
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
EXPECT_EQ(0, metadata_value("SELECT COUNT(*) FROM mysql_rest_service_metadata.object"))
EXPECT_EQ(0, metadata_value("SELECT COUNT(*) FROM mysql_rest_service_metadata.object_field"))
EXPECT_EQ(0, metadata_value("SELECT COUNT(*) FROM mysql_rest_service_metadata.object_reference"))
EXPECT_EQ(None, table_privileges("city"))
EXPECT_EQ(None, routine_privileges("film_in_stock", "PROCEDURE"))
EXPECT_EQ(None, routine_privileges("get_customer_balance", "FUNCTION"))
rest("DROP REST SERVICE /svc")

#@<> Cleanup
session.close()
testutil.destroy_sandbox(__mysql_sandbox_port1)
