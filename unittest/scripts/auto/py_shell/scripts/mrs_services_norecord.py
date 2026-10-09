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
EXPECT_THROWS(lambda: rest("SHOW REST SERVICES"), "The MRS metadata schema `mariadb_rest_service` is not installed. Run CONFIGURE REST METADATA first.")

#@<> SHOW REST METADATA STATUS before the schema exists
res = rest("SHOW REST METADATA STATUS")
EXPECT_EQ(["service_configured", "service_enabled", "service_upgradeable", "service_upgrade_ignored", "service_count", "service_being_upgraded", "major_upgrade_required", "current_metadata_version", "available_metadata_version", "required_rest_daemon_version", "metadata_version", "metadata_schema"], res.get_column_names())
row = res.fetch_one()
EXPECT_EQ("false", row[0])
EXPECT_EQ(None, row[7])
EXPECT_EQ(None, row[10])
# The oldest MariaDB REST Daemon that serves this metadata version
EXPECT_EQ("26.10.0", row[9])
# The JSON form adds the released versions the shell can deploy and the
# configuration options
doc = json.loads(rest("SHOW REST METADATA STATUS FORMAT=JSON").fetch_one()[0])
EXPECT_EQ([False, None, None], [doc["service_configured"], doc["current_metadata_version"], doc["metadata_version"]])
EXPECT_EQ("5.0.0", doc["available_metadata_version"])
EXPECT_EQ("26.10.0", doc["required_rest_daemon_version"])
EXPECT_EQ(["5.0.0"], doc["available_metadata_versions"])
EXPECT_EQ({}, doc["configuration_options"])

#@<> CONFIGURE REST METADATA creates the schema
EXPECT_EQ("REST metadata configured successfully.", rest_info("CONFIGURE REST METADATA ENABLED"))
EXPECT_EQ([[5, 0, 0]], [list(r) for r in session.run_sql("SELECT major, minor, patch FROM mariadb_rest_service.msm_schema_version").fetch_all()])
EXPECT_EQ(1, session.run_sql("SELECT service_enabled FROM mariadb_rest_service.config").fetch_one()[0])

#@<> SHOW REST METADATA STATUS with the schema
row = rest("SHOW REST METADATA STATUS").fetch_one()
EXPECT_EQ("true", row[0])
EXPECT_EQ("true", row[1])
EXPECT_EQ(0, row[4])
EXPECT_EQ("5.0.0", row[7])
EXPECT_EQ("5.0.0", row[8])
# The metadata version is the id of the last audit log entry
metadata_version = row[10]
EXPECT_EQ(session.run_sql("SELECT CAST(COALESCE(MAX(id), 0) AS SIGNED) FROM mariadb_rest_service.audit_log").fetch_one()[0], metadata_version)
res = rest("SHOW REST STATUS FORMAT=JSON")
EXPECT_EQ(["REST METADATA STATUS"], res.get_column_names())
doc = json.loads(res.fetch_one()[0])
EXPECT_EQ({"service_configured": True, "service_enabled": True, "service_count": 0, "current_metadata_version": "5.0.0", "metadata_version": metadata_version}, {k: doc[k] for k in ["service_configured", "service_enabled", "service_count", "current_metadata_version", "metadata_version"]})
EXPECT_EQ(json.loads(session.run_sql("SELECT data FROM mariadb_rest_service.config").fetch_one()[0]), doc["configuration_options"])
EXPECT_EQ(dict, type(doc["configuration_options"]))

#@<> CONFIGURE REST METADATA again: no changes, options are applied
EXPECT_EQ("REST Metadata updated successfully.", rest_info("CONFIGURE REST METADATA DISABLED OPTIONS {\"a\": 1}"))
EXPECT_EQ(0, session.run_sql("SELECT service_enabled FROM mariadb_rest_service.config").fetch_one()[0])
EXPECT_EQ({"a": 1}, json.loads(session.run_sql("SELECT data FROM mariadb_rest_service.config").fetch_one()[0]))
rest("CONFIGURE REST METADATA ENABLED MERGE OPTIONS {\"b\": 2}")
EXPECT_EQ({"a": 1, "b": 2}, json.loads(session.run_sql("SELECT data FROM mariadb_rest_service.config").fetch_one()[0]))
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
EXPECT_EQ("HTTP", session.run_sql("SELECT url_protocol FROM mariadb_rest_service.service WHERE url_context_root = '/full'").fetch_one()[0])

#@<> A service gets the default options
options = session.run_sql("SELECT options FROM mariadb_rest_service.service WHERE url_context_root = '/myService'").fetch_one()[0]
EXPECT_EQ("true", json.loads(options)["headers"]["Access-Control-Allow-Credentials"])
EXPECT_EQ(True, json.loads(options)["returnInternalErrorDetails"])

#@<> Services in development
EXPECT_EQ("REST SERVICE `mike@/myService` created successfully.", rest_info("CREATE REST SERVICE mike@/myService"))
EXPECT_EQ("REST SERVICE `'alfredo@oracle.com',miguel@/myService` created successfully.", rest_info("CREATE REST SERVICE miguel,'alfredo@oracle.com'@/myService"))
EXPECT_EQ([["/full", "DISABLED", "NO", ""], ["/myService", "ENABLED", "NO", ""], ["'alfredo@oracle.com',miguel@/myService", "ENABLED", "NO", ""], ["mike@/myService", "ENABLED", "NO", ""]], rest_rows("SHOW REST SERVICES"))
EXPECT_THROWS(lambda: rest("CREATE REST SERVICE mike@/myService"), "Failed to create the REST SERVICE `mike@/myService`. The request_path is already used by another entity.")
EXPECT_EQ([["alfredo@oracle.com", "miguel"], ["mike"]], sorted(sorted(json.loads(r[0])["developers"]) for r in session.run_sql("SELECT in_development FROM mariadb_rest_service.service WHERE url_context_root = '/myService' AND in_development IS NOT NULL").fetch_all()))

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
EXPECT_IN('OPTIONS {\n        "test": 1,\n        "test2": 2\n    }', rest("SHOW CREATE REST SERVICE /full").fetch_one()[0])
rest("ALTER REST SERVICE /full MERGE OPTIONS {\"test\": null}")
EXPECT_IN('OPTIONS {\n        "test2": 2\n    }', rest("SHOW CREATE REST SERVICE /full").fetch_one()[0])
rest("ALTER REST SERVICE /full OPTIONS {\"test3\": 3}")
EXPECT_IN('OPTIONS {\n        "test3": 3\n    }', rest("SHOW CREATE REST SERVICE /full").fetch_one()[0])

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

#@<> Each session has its own current service
other = shell.open_session(__sandbox_uri1)
EXPECT_THROWS(lambda: other.run_sql("SHOW REST SCHEMAS"), "No REST SERVICE specified.")
other.run_sql("USE REST SERVICE /full")
EXPECT_EQ([["/full", "ENABLED", "YES", ""], ["/myService", "ENABLED", "NO", ""], ["alfredo,mike@/myService", "ENABLED", "NO", ""], ["/myService2", "ENABLED", "NO", ""]], [list(row) for row in other.run_sql("SHOW REST SERVICES").fetch_all()])
EXPECT_EQ([["/full", "ENABLED", "NO", ""], ["/myService", "ENABLED", "YES", ""], ["alfredo,mike@/myService", "ENABLED", "NO", ""], ["/myService2", "ENABLED", "NO", ""]], rest_rows("SHOW REST SERVICES"))

#@<> A current service dropped by another session is no longer used
# The checks before a statement are skipped while the metadata fingerprint
# is unchanged; the DROP writes the audit log, which changes it.
other.run_sql("CREATE REST SERVICE /gone")
rest("USE REST SERVICE /gone")
EXPECT_EQ([], rest_rows("SHOW REST SCHEMAS"))
other.run_sql("DROP REST SERVICE /gone")
EXPECT_THROWS(lambda: rest("SHOW REST SCHEMAS"), "Cannot SHOW the REST schemas. No REST SERVICE specified.")

#@<> A change committed late with a lower audit log id is noticed
# Audit log ids are not in commit order: with concurrent writers, or on a
# Galera cluster with several write nodes, a row with a lower id can become
# visible after a higher one and leave MAX(id) unchanged. The fingerprint
# also counts the rows. Simulated with a high id from "another node" and the
# DROP's rows moved below it.
def max_audit_id():
    return session.run_sql("SELECT MAX(id) FROM mariadb_rest_service.audit_log").fetch_one()[0]

other.run_sql("CREATE REST SERVICE /late")
high = max_audit_id() + 1000000
other.run_sql("INSERT INTO mariadb_rest_service.audit_log (id, table_name, dml_type, changed_by, changed_at) VALUES (?, 'service', 'UPDATE', 'node2', NOW(6))", [high])
rest("USE REST SERVICE /late")
EXPECT_EQ([], rest_rows("SHOW REST SCHEMAS"))
other.run_sql("DROP REST SERVICE /late")
other.run_sql("UPDATE mariadb_rest_service.audit_log SET id = id - 500000 WHERE id > ?", [high])
EXPECT_EQ(high, max_audit_id())
EXPECT_THROWS(lambda: rest("SHOW REST SCHEMAS"), "Cannot SHOW the REST schemas. No REST SERVICE specified.")

#@<> A metadata version changed by another client is noticed
view = session.run_sql("SHOW CREATE VIEW mariadb_rest_service.msm_schema_version").fetch_one()[1]
other.run_sql("CREATE OR REPLACE VIEW mariadb_rest_service.msm_schema_version (major, minor, patch) AS SELECT 4, 1, 6")
EXPECT_THROWS(lambda: rest("SHOW REST SERVICES"), "The MRS metadata schema version 4.1.6 is too old to be managed by this version of MariaDB Shell.")
other.run_sql("DROP VIEW mariadb_rest_service.msm_schema_version")
other.run_sql(view)
EXPECT_EQ(4, len(rest_rows("SHOW REST SERVICES")))
other.close()
rest("USE REST SERVICE /myService")

#@<> Values are stored as written whatever the session's sql_mode
# The module binds the values of its metadata statements in the quoting the
# session's sql_mode needs.
def stored_comment(path):
    return session.run_sql("SELECT comments FROM mariadb_rest_service.service WHERE url_context_root = ?", [path]).fetch_one()[0]

old_mode = session.run_sql("SELECT @@SESSION.sql_mode").fetch_one()[0]
session.run_sql("SET SESSION sql_mode = CONCAT(@@SESSION.sql_mode, ',NO_BACKSLASH_ESCAPES')")
rest("CREATE REST SERVICE /nbe COMMENT 'a\\b''c'")
EXPECT_EQ("a\\b'c", stored_comment("/nbe"))
rest("ALTER REST SERVICE /nbe COMMENT 'x\\y'")
EXPECT_EQ("x\\y", stored_comment("/nbe"))
EXPECT_EQ("x\\y", json.loads(rest("SHOW CREATE REST SERVICE /nbe FORMAT=JSON").fetch_one()[0])["comments"])
session.run_sql("SET SESSION sql_mode = ?", [old_mode])
rest("CREATE REST SERVICE /bs COMMENT 'a\\\\b\\'c'")
EXPECT_EQ("a\\b'c", stored_comment("/bs"))
rest("DROP REST SERVICE /nbe")
rest("DROP REST SERVICE /bs")

#@<> SHOW CREATE REST SERVICE of the current service
EXPECT_EQ("CREATE REST SERVICE", rest("SHOW CREATE REST SERVICE").get_column_names()[0])
EXPECT_IN("CREATE OR REPLACE REST SERVICE /myService\n", rest("SHOW CREATE REST SERVICE").fetch_one()[0])

#@<> The metadata version changes with the metadata
EXPECT_LT(metadata_version, rest("SHOW REST METADATA STATUS").fetch_one()[10])

#@<> SHOW CREATE REST SERVICE FORMAT=JSON
res = rest("SHOW CREATE REST SERVICE /full FORMAT=JSON")
EXPECT_EQ(["CREATE REST SERVICE"], res.get_column_names())
doc = json.loads(res.fetch_one()[0])
EXPECT_EQ("/full", doc["url_context_root"])
EXPECT_EQ("/full", doc["full_service_path"])
EXPECT_EQ(session.run_sql("SELECT id FROM mariadb_rest_service.service WHERE url_context_root = '/full'").fetch_one()[0], doc["id"])
EXPECT_EQ([], doc["developers"])
EXPECT_EQ([], doc["auth_apps"])
EXPECT_FALSE("schemas" in doc)
# The option columns are embedded as JSON
EXPECT_EQ(dict, type(doc["options"]))
EXPECT_EQ(json.loads(session.run_sql("SELECT options FROM mariadb_rest_service.service WHERE url_context_root = '/full'").fetch_one()[0]), doc["options"])
# The current service, any case of the format name, and the default format
EXPECT_EQ("/myService", json.loads(rest("SHOW CREATE REST SERVICE FORMAT = json").fetch_one()[0])["url_context_root"])
EXPECT_EQ("/myService", json.loads(rest("SHOW CREATE REST SERVICE /myService FORMAT='Json'").fetch_one()[0])["url_context_root"])
EXPECT_EQ(rest("SHOW CREATE REST SERVICE").fetch_one()[0], rest("SHOW CREATE REST SERVICE FORMAT=TRADITIONAL").fetch_one()[0])
EXPECT_THROWS(lambda: rest("SHOW CREATE REST SERVICE FORMAT=XML"), "Unknown REST format name: 'XML'")

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

#@<> SHOW CREATE REST SCHEMA FORMAT=JSON
res = rest("SHOW CREATE REST SCHEMA /sakila ON SERVICE /full FORMAT=JSON")
EXPECT_EQ(["CREATE REST SCHEMA"], res.get_column_names())
doc = json.loads(res.fetch_one()[0])
EXPECT_EQ({"name": "sakila", "schema_type": "DATABASE_SCHEMA", "request_path": "/sakila", "requires_auth": True, "enabled": 0, "items_per_page": 10, "comments": "The sakila schema", "options": {"o": 1}, "metadata": {"m": 2}}, {k: doc[k] for k in ["name", "schema_type", "request_path", "requires_auth", "enabled", "items_per_page", "comments", "options", "metadata"]})
# A service with its database endpoints carries its schemas
doc = json.loads(rest("SHOW CREATE REST SERVICE /full INCLUDING DATABASE ENDPOINTS FORMAT=JSON").fetch_one()[0])
EXPECT_EQ(["/sakila"], [schema["request_path"] for schema in doc["schemas"]])
EXPECT_EQ([], doc["schemas"][0]["db_objects"])

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

#@<> SHOW CREATE REST SERVICE ... INCLUDING ... ENDPOINTS is the dump; running it loads the service
# The script any client saves and runs: there are no DUMP or LOAD
# statements, a server cannot reach the client's files
EXPECT_THROWS(lambda: rest("DUMP REST SERVICE /myService AS SCRIPT INCLUDING ALL ENDPOINTS TO '/tmp/x'"), "")
EXPECT_THROWS(lambda: rest("LOAD REST SERVICE FROM '/tmp/x'"), "")
dump = rest("SHOW CREATE REST SERVICE /myService INCLUDING ALL ENDPOINTS").fetch_one()[0]
EXPECT_IN("CREATE OR REPLACE REST SERVICE /myService\n", dump)
# The script names the service once: the endpoints act on the current one
EXPECT_IN("CREATE OR REPLACE REST SERVICE /myService\n", dump)
EXPECT_IN(";\n\nUSE REST SERVICE /myService;\n\n", dump)
EXPECT_IN("CREATE OR REPLACE REST SCHEMA /sakila\n    FROM `sakila`", dump)
EXPECT_FALSE("ON SERVICE" in dump, dump)
EXPECT_EQ(dump, rest("SHOW CREATE REST SERVICE /myService INCLUDING DATABASE AND STATIC AND DYNAMIC ENDPOINTS").fetch_one()[0])
dump_file = os.path.join(__tmp_dir, "myService.mrs.sql")
# Loaded under another path by rewriting the script
testutil.create_file(dump_file, dump.replace("/myService", "/loaded"))
testutil.call_mysqlsh([__sandbox_uri1, "--sql", "-f", dump_file], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("REST SERVICE `/loaded` created successfully.")
WIPE_OUTPUT()
EXPECT_EQ([["/sakila", "ENABLED"]], rest_rows("SHOW REST SCHEMAS FROM SERVICE /loaded"))
EXPECT_EQ(dump.replace("/myService", "/loaded"), rest("SHOW CREATE REST SERVICE /loaded INCLUDING ALL ENDPOINTS").fetch_one()[0])
rest("DROP REST SERVICE /loaded")
os.remove(dump_file)

#@<> SHOW REST DAEMONS
EXPECT_EQ([], rest_rows("SHOW REST DAEMONS"))
# Daemons register themselves in the rest_daemon table of the metadata; the
# ids are UUIDs (given here to fix the order)
daemon_ids = ["0199a1b2-0000-7000-8000-000000000001", "0199a1b2-0000-7000-8000-000000000002"]
session.run_sql("""INSERT INTO mariadb_rest_service.rest_daemon
    (id, name, address, product_name, version, last_check_in, attributes, options) VALUES
    (?, 'daemon1', '127.0.0.1', 'MariaDB REST Daemon', '1.0.0', NOW(), '{}', '{}'),
    (?, 'daemon2', '127.0.0.2', 'MariaDB REST Daemon', '1.0.0', NOW() - INTERVAL 1 HOUR, '{"a": 1}', '{"developer": "mike"}')""", daemon_ids)
res = rest("SHOW REST DAEMONS")
EXPECT_EQ(["id", "name", "address", "product_name", "version", "last_check_in", "active", "developer"], res.get_column_names())
rows = [list(r) for r in res.fetch_all()]
EXPECT_EQ([[daemon_ids[0], "daemon1", "127.0.0.1", "MariaDB REST Daemon", "1.0.0", "YES", None], [daemon_ids[1], "daemon2", "127.0.0.2", "MariaDB REST Daemon", "1.0.0", "NO", "mike"]], [r[:5] + r[6:] for r in rows])
doc = json.loads(rest("SHOW REST DAEMONS FORMAT=JSON").fetch_one()[0])
EXPECT_EQ(daemon_ids, [d["id"] for d in doc])
EXPECT_EQ(["daemon1", "daemon2"], [d["name"] for d in doc])
EXPECT_EQ([{}, {"a": 1}], [d["attributes"] for d in doc])
EXPECT_EQ({"developer": "mike"}, doc[1]["options"])
# The audit log names the daemon by its id
EXPECT_EQ(daemon_ids, [r[0] for r in session.run_sql("SELECT new_row_id FROM mariadb_rest_service.audit_log WHERE table_name = 'rest_daemon' ORDER BY new_row_id").fetch_all()])

#@<> SHOW REST SERVICES FOR DAEMON
missing = "0199a1b2-0000-7000-8000-000000000999"
for daemon_id in daemon_ids:
    served = session.run_sql("SELECT COUNT(DISTINCT service_id) FROM mariadb_rest_service.rest_daemon_services WHERE rest_daemon_id = ?", [daemon_id]).fetch_one()[0]
    EXPECT_EQ(served, len(rest_rows("SHOW REST SERVICES FOR DAEMON '%s'" % daemon_id.upper())))
EXPECT_THROWS(lambda: rest("SHOW REST SERVICES FOR DAEMON '%s'" % missing), "Cannot SHOW the REST services. The given REST DAEMON `%s` could not be found." % missing)
EXPECT_THROWS(lambda: rest("SHOW REST SERVICES FOR DAEMON 999"), "Syntax Error")

#@<> The status cleanup aggregates the reports of the daemons of each version
# Reports older than 4 hours are summed up per minute, per daemon; the
# daemons of one version must not lose the reports of the other version.
session.run_sql("UPDATE mariadb_rest_service.rest_daemon SET version = '2.0.0' WHERE id = ?", [daemon_ids[1]])
for daemon_id in daemon_ids:
    for requests in [3, 4]:
        session.run_sql("INSERT INTO mariadb_rest_service.rest_daemon_status (rest_daemon_id, status_time, timespan, http_requests_get) VALUES (?, DATE_FORMAT(NOW() - INTERVAL 5 HOUR, '%Y-%m-%d %H:%i:10'), 10, ?)", [daemon_id, requests])
session.run_sql("CALL mariadb_rest_service.rest_daemon_status_do_cleanup(NOW())")
EXPECT_EQ([[daemon_ids[0], 60000, 7], [daemon_ids[1], 60000, 7]], [list(r) for r in session.run_sql("SELECT rest_daemon_id, timespan, CAST(http_requests_get AS SIGNED) FROM mariadb_rest_service.rest_daemon_status ORDER BY rest_daemon_id").fetch_all()])
session.run_sql("DELETE FROM mariadb_rest_service.rest_daemon_status")

#@<> DROP REST DAEMON removes its status reports and log entries
session.run_sql("INSERT INTO mariadb_rest_service.rest_daemon_status (rest_daemon_id, timespan) VALUES (?, 10)", [daemon_ids[0]])
session.run_sql("INSERT INTO mariadb_rest_service.rest_daemon_general_log (rest_daemon_id, log_type, message) VALUES (?, 'INFO', 'started')", [daemon_ids[0]])
res = rest("DROP REST DAEMON '%s'" % daemon_ids[0])
EXPECT_EQ("REST DAEMON `%s` dropped successfully." % daemon_ids[0], res.get_info())
EXPECT_EQ(1, res.get_affected_items_count())
EXPECT_EQ(0, session.run_sql("SELECT COUNT(*) FROM mariadb_rest_service.rest_daemon_status").fetch_one()[0])
EXPECT_EQ(0, session.run_sql("SELECT COUNT(*) FROM mariadb_rest_service.rest_daemon_general_log").fetch_one()[0])
EXPECT_EQ(["daemon2"], [r[1] for r in rest_rows("SHOW REST DAEMONS")])
EXPECT_THROWS(lambda: rest("DROP REST DAEMON '%s'" % missing), "Failed to drop the REST DAEMON `%s`. The given REST DAEMON `%s` could not be found." % (missing, missing))
EXPECT_EQ("REST DAEMON `%s` dropped successfully." % missing, rest_info("DROP REST DAEMON IF EXISTS '%s'" % missing))
rest("DROP REST DAEMON '%s'" % daemon_ids[1])
EXPECT_EQ([], rest_rows("SHOW REST DAEMONS"))

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

#@<> The mrs global object has no functions of its own
EXPECT_EQ(["help"], dir(mrs))

#@<> A REST SQL script runs like any SQL script
script_file = os.path.join(__tmp_dir, "mrs_script.sql")
testutil.create_file(script_file, "CREATE REST SERVICE /scripted COMMENT 'from a script';\nSELECT 1 AS plain_sql;\nSHOW REST SERVICES;\n")
testutil.call_mysqlsh([__sandbox_uri1, "--sql", "-f", script_file], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("REST SERVICE `/scripted` created successfully.")
EXPECT_STDOUT_CONTAINS("plain_sql")
EXPECT_STDOUT_CONTAINS("/scripted")
WIPE_OUTPUT()
rest("DROP REST SERVICE /scripted")
os.remove(script_file)

#@<> Cleanup
session.close()
testutil.destroy_sandbox(__mysql_sandbox_port1)
