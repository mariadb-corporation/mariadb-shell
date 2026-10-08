#@<> Initialization
# The REST SQL statements of the mrs module: metadata, services and schemas.
# They run through the SQL handler the module registers, so session.run_sql()
# returns their results like those of any SQL statement.
import json
import os

testutil.deploy_sandbox(__mysql_sandbox_port1, "root")
shell.connect(__sandbox_uri1)

def rest(sql):
    return session.run_sql(sql)

def rest_rows(sql):
    return [list(row) for row in rest(sql).fetch_all()]

def rest_info(sql):
    return rest(sql).get_info()

#@<> The built-in SQL handler
EXPECT_EQ([{"name": "MRS", "description": "MariaDB REST Service SQL Extension"}], shell.list_sql_handlers())

#@<> Statements need the metadata schema
EXPECT_THROWS(lambda: rest("SHOW REST SERVICES"), "The MRS metadata schema `mysql_rest_service_metadata` is not installed. Run CONFIGURE REST METADATA first.")

#@<> SHOW REST METADATA STATUS before the schema exists
res = rest("SHOW REST METADATA STATUS")
EXPECT_EQ(["service_configured", "service_enabled", "service_upgradeable", "service_upgrade_ignored", "service_count", "service_being_upgraded", "major_upgrade_required", "current_metadata_version", "available_metadata_version", "required_router_version"], res.get_column_names())
row = res.fetch_one()
EXPECT_EQ("false", row[0])
EXPECT_EQ(None, row[7])

#@<> CONFIGURE REST METADATA creates the schema
EXPECT_EQ("REST metadata configured successfully.", rest_info("CONFIGURE REST METADATA ENABLED"))
EXPECT_EQ([[4, 1, 6]], [list(r) for r in session.run_sql("SELECT major, minor, patch FROM mysql_rest_service_metadata.msm_schema_version").fetch_all()])
EXPECT_EQ(1, session.run_sql("SELECT service_enabled FROM mysql_rest_service_metadata.config").fetch_one()[0])

#@<> SHOW REST METADATA STATUS with the schema
row = rest("SHOW REST METADATA STATUS").fetch_one()
EXPECT_EQ("true", row[0])
EXPECT_EQ("true", row[1])
EXPECT_EQ(0, row[4])
EXPECT_EQ("4.1.6", row[7])
EXPECT_EQ("4.1.6", row[8])

#@<> CONFIGURE REST METADATA again: no changes, options are applied
EXPECT_EQ("REST Metadata updated successfully.", rest_info("CONFIGURE REST METADATA DISABLED OPTIONS {\"a\": 1}"))
EXPECT_EQ(0, session.run_sql("SELECT service_enabled FROM mysql_rest_service_metadata.config").fetch_one()[0])
EXPECT_EQ({"a": 1}, json.loads(session.run_sql("SELECT data FROM mysql_rest_service_metadata.config").fetch_one()[0]))
rest("CONFIGURE REST METADATA ENABLED MERGE OPTIONS {\"b\": 2}")
EXPECT_EQ({"a": 1, "b": 2}, json.loads(session.run_sql("SELECT data FROM mysql_rest_service_metadata.config").fetch_one()[0]))
#@<> CONFIGURE REST METADATA UPDATE IF AVAILABLE with the current version
EXPECT_NO_THROWS(lambda: rest("CONFIGURE REST METADATA UPDATE IF AVAILABLE ENABLED"))

#@<> Syntax errors
EXPECT_THROWS(lambda: rest("CREATE REST SERVICE"), "Syntax Error: Syntax error, unexpected end of input")
EXPECT_THROWS(lambda: rest("CREATE REST SERVICE `noslash`"), "Syntax Error: Invalid REST request path or wildcard [Ln 1: Col 20]")
EXPECT_THROWS(lambda: rest("SHOW REST FOO"), "Syntax Error: Syntax error, unexpected identifier")

#@<> CREATE REST SERVICE
EXPECT_EQ("REST SERVICE `/myService` created successfully.", rest_info("CREATE REST SERVICE /myService"))
EXPECT_THROWS(lambda: rest("CREATE REST SERVICE /myService"), "Failed to create the REST SERVICE `/myService`. The request_path is already used by another entity.")
EXPECT_THROWS(lambda: rest("CREATE REST SERVICE /mrs"), "Failed to create the REST SERVICE `/mrs`. The REST service path `/mrs` is reserved and cannot be used.")
EXPECT_EQ("REST SERVICE `/myService` created successfully.", rest_info("CREATE REST SERVICE IF NOT EXISTS /myService"))
EXPECT_EQ("REST SERVICE `/myService` created successfully.", rest_info("CREATE OR REPLACE REST SERVICE /myService"))
EXPECT_EQ([["/myService", "ENABLED", "NO", ""]], rest_rows("SHOW REST SERVICES"))

#@<> CREATE REST SERVICE with all options
rest("""CREATE REST SERVICE /full
    DISABLED
    PUBLISHED
    PROTOCOL HTTP
    COMMENT "A full service"
    AUTHENTICATION
        PATH "/auth"
        REDIRECTION "/done"
        VALIDATION "^/done.*$"
        PAGE CONTENT "<html></html>"
    OPTIONS {"headers": {"X": "y"}, "list": [1, 2.5, true, null]}
    METADATA {"position": 1}""")
EXPECT_EQ([["/full", "DISABLED", "NO", ""], ["/myService", "ENABLED", "NO", ""]], rest_rows("SHOW REST SERVICES"))
EXPECT_EQ("""CREATE OR REPLACE REST SERVICE /full
    DISABLED
    COMMENT 'A full service'
    PUBLISHED
    AUTHENTICATION
        PATH '/auth'
        REDIRECTION '/done'
        VALIDATION '^/done.*$'
        PAGE CONTENT '<html></html>'
    OPTIONS {
        "headers": {
            "X": "y"
        },
        "list": [
            1,
            2.5,
            true,
            null
        ]
    }
    METADATA {
        "position": 1
    };""", rest("SHOW CREATE REST SERVICE /full").fetch_one()[0])
EXPECT_EQ("HTTP", session.run_sql("SELECT url_protocol FROM mysql_rest_service_metadata.service WHERE url_context_root = '/full'").fetch_one()[0])

#@<> A service gets the default options
options = session.run_sql("SELECT options FROM mysql_rest_service_metadata.service WHERE url_context_root = '/myService'").fetch_one()[0]
EXPECT_CONTAINS('"Access-Control-Allow-Credentials": "true"', options)
EXPECT_CONTAINS('"returnInternalErrorDetails": true', options)

#@<> Services in development
EXPECT_EQ("REST SERVICE `mike@/myService` created successfully.", rest_info("CREATE REST SERVICE mike@/myService"))
EXPECT_EQ("REST SERVICE `'alfredo@oracle.com',miguel@/myService` created successfully.", rest_info("CREATE REST SERVICE miguel,'alfredo@oracle.com'@/myService"))
EXPECT_EQ([["/full", "DISABLED", "NO", ""], ["/myService", "ENABLED", "NO", ""], ["'alfredo@oracle.com',miguel@/myService", "ENABLED", "NO", ""], ["mike@/myService", "ENABLED", "NO", ""]], rest_rows("SHOW REST SERVICES"))
EXPECT_THROWS(lambda: rest("CREATE REST SERVICE mike@/myService"), "Failed to create the REST SERVICE `mike@/myService`. The request_path is already used by another entity.")
EXPECT_EQ([["alfredo@oracle.com", "miguel"], ["mike"]], sorted(sorted(json.loads(r[0])["developers"]) for r in session.run_sql("SELECT in_development FROM mysql_rest_service_metadata.service WHERE url_context_root = '/myService' AND in_development IS NOT NULL").fetch_all()))

#@<> ALTER REST SERVICE
res = rest("ALTER REST SERVICE /full ENABLED UNPUBLISHED PROTOCOL HTTPS COMMENT 'changed' AUTHENTICATION PATH DEFAULT REDIRECTION DEFAULT")
EXPECT_EQ(1, res.get_affected_items_count())
EXPECT_EQ("""CREATE OR REPLACE REST SERVICE /full
    COMMENT 'changed'
    AUTHENTICATION
        VALIDATION '^/done.*$'
        PAGE CONTENT '<html></html>'
    OPTIONS {
        "headers": {
            "X": "y"
        },
        "list": [
            1,
            2.5,
            true,
            null
        ]
    }
    METADATA {
        "position": 1
    };""", rest("SHOW CREATE REST SERVICE /full").fetch_one()[0])

#@<> ALTER REST SERVICE: options and MERGE OPTIONS
rest("ALTER REST SERVICE /full OPTIONS {\"test\": 1}")
rest("ALTER REST SERVICE /full MERGE OPTIONS {\"test2\": 2}")
EXPECT_CONTAINS('OPTIONS {\n        "test": 1,\n        "test2": 2\n    }', rest("SHOW CREATE REST SERVICE /full").fetch_one()[0])
rest("ALTER REST SERVICE /full MERGE OPTIONS {\"test\": null}")
EXPECT_CONTAINS('OPTIONS {\n        "test2": 2\n    }', rest("SHOW CREATE REST SERVICE /full").fetch_one()[0])
rest("ALTER REST SERVICE /full OPTIONS {\"test3\": 3}")
EXPECT_CONTAINS('OPTIONS {\n        "test3": 3\n    }', rest("SHOW CREATE REST SERVICE /full").fetch_one()[0])

#@<> ALTER REST SERVICE: new request path and developers
rest("ALTER REST SERVICE mike@/myService NEW REQUEST PATH mike,alfredo@/myService")
rest("ALTER REST SERVICE miguel,'alfredo@oracle.com'@/myService NEW REQUEST PATH /myService2")
EXPECT_EQ([["/full", "ENABLED", "NO", ""], ["/myService", "ENABLED", "NO", ""], ["alfredo,mike@/myService", "ENABLED", "NO", ""], ["/myService2", "ENABLED", "NO", ""]], rest_rows("SHOW REST SERVICES"))
EXPECT_THROWS(lambda: rest("ALTER REST SERVICE /nope DISABLED"), "Failed to update the REST SERVICE `/nope`. Could not find the REST SERVICE /nope.")
EXPECT_THROWS(lambda: rest("ALTER REST SERVICE joe@/myService DISABLED"), "Could not find the REST SERVICE joe@/myService.")

#@<> USE REST SERVICE
EXPECT_THROWS(lambda: rest("SHOW REST SCHEMAS"), "Cannot SHOW the REST schemas. No REST SERVICE specified.")
EXPECT_EQ("Now using REST SERVICE `/myService`.", rest_info("USE REST SERVICE /myService"))
EXPECT_EQ([["/full", "ENABLED", "NO", ""], ["/myService", "ENABLED", "YES", ""], ["alfredo,mike@/myService", "ENABLED", "NO", ""], ["/myService2", "ENABLED", "NO", ""]], rest_rows("SHOW REST SERVICES"))
EXPECT_EQ([], rest_rows("SHOW REST SCHEMAS"))
EXPECT_THROWS(lambda: rest("USE REST SERVICE /nope"), "Cannot USE the specified REST object. A REST SERVICE with the request path /nope could not be found.")
EXPECT_EQ("Now using REST SERVICE `alfredo,mike@/myService`.", rest_info("USE REST SERVICE mike,alfredo@/myService"))
rest("USE REST SERVICE /myService")

#@<> SHOW CREATE REST SERVICE of the current service
EXPECT_EQ("CREATE REST SERVICE", rest("SHOW CREATE REST SERVICE").get_column_names()[0])
EXPECT_CONTAINS("CREATE OR REPLACE REST SERVICE /myService\n", rest("SHOW CREATE REST SERVICE").fetch_one()[0])

#@<> CREATE REST SCHEMA
testutil.import_data(__sandbox_uri1, os.path.join(__data_path, "sql", "sakila-schema.sql"))
EXPECT_THROWS(lambda: rest("CREATE REST SCHEMA /nope FROM nope"), "Failed to create the REST SCHEMA `/myService/nope`. The given database schema name 'nope' does not exists.")
EXPECT_EQ("REST SCHEMA `/myService/sakila` created successfully.", rest_info("CREATE REST SCHEMA /sakila FROM sakila"))
EXPECT_EQ("REST SCHEMA `/full/sakila` created successfully.", rest_info("CREATE REST SCHEMA ON SERVICE /full FROM `sakila` DISABLED ITEMS PER PAGE 10 COMMENT 'The sakila schema' AUTHENTICATION REQUIRED OPTIONS {\"o\": 1} METADATA {\"m\": 2}"))
EXPECT_EQ([["/sakila", "ENABLED"]], rest_rows("SHOW REST SCHEMAS"))
EXPECT_EQ([["/sakila", "DISABLED"]], rest_rows("SHOW REST SCHEMAS FROM SERVICE /full"))
EXPECT_EQ("""CREATE OR REPLACE REST SCHEMA /sakila ON SERVICE /full
    FROM `sakila`
    DISABLED
    AUTHENTICATION REQUIRED
    ITEMS PER PAGE 10
    COMMENT 'The sakila schema'
    OPTIONS {
        "o": 1
    }
    METADATA {
        "m": 2
    };""", rest("SHOW CREATE REST SCHEMA /sakila ON SERVICE /full").fetch_one()[0])
EXPECT_EQ("""CREATE OR REPLACE REST SCHEMA /sakila ON SERVICE /myService
    FROM `sakila`
    AUTHENTICATION NOT REQUIRED;""", rest("SHOW CREATE REST SCHEMA /sakila").fetch_one()[0])

#@<> CREATE REST SCHEMA: IF NOT EXISTS and OR REPLACE
EXPECT_EQ("REST SCHEMA `/myService/sakila` created successfully.", rest_info("CREATE REST SCHEMA IF NOT EXISTS /sakila FROM sakila PRIVATE"))
EXPECT_EQ([["/sakila", "ENABLED"]], rest_rows("SHOW REST SCHEMAS"))
EXPECT_EQ("REST SCHEMA `/myService/sakila` created successfully.", rest_info("CREATE OR REPLACE REST SCHEMA /sakila FROM sakila PRIVATE"))
EXPECT_EQ([["/sakila", "PRIVATE"]], rest_rows("SHOW REST SCHEMAS"))

#@<> The first schema of the current service becomes the current schema
EXPECT_EQ("Now using REST SCHEMA `/sakila` on REST SERVICE `/myService`.", rest_info("USE REST SCHEMA /sakila"))
EXPECT_THROWS(lambda: rest("USE REST SCHEMA /nope"), "A REST SCHEMA with the request path /nope could not be found.")
EXPECT_EQ("Now using REST SCHEMA `/sakila` on REST SERVICE `/full`.", rest_info("USE REST SERVICE /full SCHEMA /sakila"))
rest("USE REST SERVICE /myService")

#@<> ALTER REST SCHEMA
res = rest("ALTER REST SCHEMA /sakila NEW REQUEST PATH /sakila2 ENABLED AUTHENTICATION REQUIRED ITEMS PER PAGE 5 COMMENT 'c' MERGE OPTIONS {\"x\": 1}")
EXPECT_EQ(1, res.get_affected_items_count())
rest("ALTER REST SCHEMA /sakila2 MERGE OPTIONS {\"y\": 2}")
EXPECT_EQ("""CREATE OR REPLACE REST SCHEMA /sakila2 ON SERVICE /myService
    FROM `sakila`
    AUTHENTICATION REQUIRED
    ITEMS PER PAGE 5
    COMMENT 'c'
    OPTIONS {
        "x": 1,
        "y": 2
    };""", rest("SHOW CREATE REST SCHEMA /sakila2").fetch_one()[0])
EXPECT_THROWS(lambda: rest("ALTER REST SCHEMA /nope DISABLED"), "Failed to update the REST SCHEMA `/myService/nope`. Could not find the REST SCHEMA /myService/nope.")
rest("ALTER REST SCHEMA /sakila2 ON SERVICE /myService NEW REQUEST PATH /sakila")

#@<> DROP REST SCHEMA
EXPECT_THROWS(lambda: rest("DROP REST SCHEMA /nope"), "Failed to drop the REST SCHEMA `/myService/nope`. The given REST SCHEMA `/myService/nope` could not be found.")
EXPECT_EQ("REST SCHEMA `/myService/nope` dropped successfully.", rest_info("DROP REST SCHEMA IF EXISTS /nope"))
EXPECT_EQ("REST SCHEMA `/full/sakila` dropped successfully.", rest_info("DROP REST SCHEMA /sakila FROM SERVICE /full"))
EXPECT_EQ([], rest_rows("SHOW REST SCHEMAS FROM /full"))
EXPECT_EQ([["/sakila", "ENABLED"]], rest_rows("SHOW REST SCHEMAS"))

#@<> CLONE REST SERVICE
res = rest("CLONE REST SERVICE /myService NEW REQUEST PATH /myClone")
EXPECT_EQ(1, res.get_affected_items_count())
EXPECT_EQ([["/sakila", "ENABLED"]], rest_rows("SHOW REST SCHEMAS FROM SERVICE /myClone"))
EXPECT_EQ("REST SERVICE `/myClone` dropped successfully.", rest_info("DROP REST SERVICE /myClone"))

#@<> DUMP and LOAD REST SERVICE
dump_file = os.path.join(__tmp_dir, "myService.mrs.sql")
if os.path.exists(dump_file):
    os.remove(dump_file)
EXPECT_EQ([["Result stored in '%s'" % dump_file]], rest_rows("DUMP REST SERVICE /myService AS SQL SCRIPT INCLUDING DATABASE ENDPOINTS TO '%s'" % dump_file))
with open(dump_file) as f:
    dump = f.read()
EXPECT_CONTAINS("CREATE OR REPLACE REST SERVICE /myService\n", dump)
EXPECT_CONTAINS("CREATE OR REPLACE REST SCHEMA /sakila ON SERVICE /myService\n", dump)
EXPECT_THROWS(lambda: rest("DUMP REST SERVICE /myService AS SQL SCRIPT INCLUDING ALL ENDPOINTS TO ZIP '%s.zip'" % dump_file), "Dumping to a ZIP file is not supported")
# The dump uses CREATE OR REPLACE, so loading it again replaces the service
EXPECT_EQ([["Service '/myService' loaded from '%s'" % dump_file]], rest_rows("LOAD REST SERVICE FROM '%s'" % dump_file))
EXPECT_EQ([["/sakila", "ENABLED"]], rest_rows("SHOW REST SCHEMAS FROM SERVICE /myService"))
EXPECT_EQ([["Service '/loaded' loaded from '%s'" % dump_file]], rest_rows("LOAD REST SERVICE AS /loaded FROM '%s'" % dump_file))
EXPECT_EQ([["/sakila", "ENABLED"]], rest_rows("SHOW REST SCHEMAS FROM SERVICE /loaded"))
EXPECT_THROWS(lambda: rest("LOAD REST SERVICE FROM '/nope/nope.sql'"), "The specified file was not found.")
rest("DROP REST SERVICE /loaded")
os.remove(dump_file)

#@<> DROP REST SERVICE
EXPECT_THROWS(lambda: rest("DROP REST SERVICE /nope"), "Failed to drop the REST SERVICE `/nope`. The given REST SERVICE `/nope` could not be found.")
EXPECT_EQ("REST SERVICE `/nope` dropped successfully.", rest_info("DROP REST SERVICE IF EXISTS /nope"))
EXPECT_EQ("REST SERVICE `alfredo,mike@/myService` dropped successfully.", rest_info("DROP REST SERVICE mike,alfredo@/myService"))
EXPECT_EQ("REST SERVICE `/myService` dropped successfully.", rest_info("DROP REST SERVICE /myService"))
EXPECT_THROWS(lambda: rest("SHOW REST SCHEMAS"), "No REST SERVICE specified.")
rest("DROP REST SERVICE /myService2")
rest("DROP REST SERVICE /full")
EXPECT_EQ([], rest_rows("SHOW REST SERVICES"))

#@<> Statements through the command line in SQL mode
testutil.call_mysqlsh([__sandbox_uri1, "--sql", "-e", "CREATE REST SERVICE /cli; SHOW REST SERVICES; DROP REST SERVICE /cli;"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("REST SERVICE `/cli` created successfully.")
EXPECT_STDOUT_CONTAINS("/cli\tENABLED\tNO\t")
EXPECT_STDOUT_CONTAINS("REST SERVICE `/cli` dropped successfully.")
WIPE_OUTPUT()

#@<> The module can be disabled, so the Python mrs_plugin can be loaded instead
testutil.call_mysqlsh(["--disable-modules=mrs", "--py", "-e", "print('mrs' in globals(), len(shell.list_sql_handlers()))"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("False 0")
WIPE_OUTPUT()
testutil.call_mysqlsh(["--disable-modules= MRS ,mrs", "--py", "-e", "print(shell.options.disabledModules)"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("mrs")
WIPE_OUTPUT()
testutil.call_mysqlsh(["--disable-modules=nope", "--py", "-e", "print(1)"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("The acceptable values for the option --disable-modules are a comma separated list of: mrs")
WIPE_OUTPUT()

#@<> The mrs global object
EXPECT_EQ(["help", "run_script"], dir(mrs))
script_file = os.path.join(__tmp_dir, "mrs_script.sql")
testutil.create_file(script_file, "CREATE REST SERVICE /scripted COMMENT 'from a script';\nSHOW REST SERVICES;\n")
mrs.run_script(script_file)
EXPECT_STDOUT_CONTAINS("REST SERVICE `/scripted` created successfully.")
EXPECT_STDOUT_CONTAINS("/scripted")
EXPECT_THROWS(lambda: mrs.run_script(os.path.join(__tmp_dir, "nope.sql")), "does not exist")
rest("DROP REST SERVICE /scripted")
os.remove(script_file)

#@<> Cleanup
session.close()
testutil.destroy_sandbox(__mysql_sandbox_port1)
