#@ {__server_is_maria_db}
#@<> Initialization
# The REST CONTENT SET and REST CONTENT FILE statements of the mrs module:
# content given inline as text or base64, and MRS scripts registered from
# the stored files with ALTER REST CONTENT SET ... LOAD SCRIPTS. They run
# through the SQL handler the module registers, so session.run_sql() returns
# their results like those of any SQL statement.
import base64
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

def rest_text(sql):
    # The single cell of a SHOW CREATE result
    return rest(sql).fetch_one()[0]

def rest_script(script):
    # The statements of a SHOW CREATE output, one by one
    for statement in script.split(";\n\n"):
        rest(statement)

def query_rows(sql):
    return [list(row) for row in session.run_sql(sql).fetch_all()]

def file_hex(content_set, request_path):
    return session.run_sql(f"""SELECT HEX(f.content) FROM mariadb_rest_service.content_file f
        JOIN mariadb_rest_service.content_set cs ON cs.id = f.content_set_id
        WHERE cs.request_path = '{content_set}' AND f.request_path = '{request_path}'""").fetch_one()[0]

def content_set_row(request_path):
    # The options are compared as a document, their text layout varies
    row = list(session.run_sql(f"""SELECT content_type, requires_auth, enabled, comments, options
        FROM mariadb_rest_service.content_set WHERE request_path = '{request_path}'""").fetch_one())
    if row[4] is not None:
        row[4] = json.loads(row[4])
    return row

readme_txt = "Line '1'\nLine \"2\""
binary_test_file = os.path.join(__data_path, "mrs", "binary_test_file")
with open(binary_test_file, "rb") as f:
    binary_test_data = f.read()
binary_test_base64 = base64.b64encode(binary_test_data).decode()

#@<> Setup
rest("CONFIGURE REST METADATA ENABLED")
rest("CREATE REST SERVICE /svc")
rest("CREATE REST SERVICE /other")
rest("USE REST SERVICE /svc")

#@<> CREATE REST CONTENT SET
EXPECT_EQ("REST content set `/svc/static` created successfully.", rest_info("CREATE REST CONTENT SET /static"))
EXPECT_EQ([["/static", "ENABLED"]], rest_rows("SHOW REST CONTENT SETS"))
EXPECT_EQ([], rest_rows("SHOW REST CONTENT FILES FROM CONTENT SET /static"))
# The REST SQL defaults: a content set requires authentication
EXPECT_EQ(["STATIC", 1, 1, "", None], content_set_row("/static"))
EXPECT_EQ("REST content set `/svc/assets` created successfully.", rest_info("""CREATE REST CONTENT SET /assets ON SERVICE /svc
    DISABLED
    AUTHENTICATION NOT REQUIRED
    COMMENT 'Static assets'
    OPTIONS {"a": 1}"""))
EXPECT_EQ(["STATIC", 0, 0, "Static assets", {"a": 1}], content_set_row("/assets"))
EXPECT_EQ([["/assets", "DISABLED"], ["/static", "ENABLED"]], rest_rows("SHOW REST CONTENT SETS ON SERVICE /svc"))

#@<> Files are not read from the client: no FROM, no IGNORE list, and LOAD SCRIPTS is an ALTER
EXPECT_THROWS(lambda: rest("CREATE REST CONTENT SET /dir FROM '/tmp'"), "Syntax error, unexpected FROM")
EXPECT_THROWS(lambda: rest("CREATE REST CONTENT SET /dir IGNORE '*.txt'"), "Syntax error, unexpected identifier")
EXPECT_THROWS(lambda: rest("CREATE REST CONTENT SET /dir LOAD SCRIPTS"), "Syntax error, unexpected LOAD")
EXPECT_THROWS(lambda: rest("CREATE REST CONTENT FILE /x ON CONTENT SET /static FROM '/tmp/x'"), "Syntax error, unexpected FROM")

#@<> CREATE REST CONTENT SET errors
EXPECT_THROWS(lambda: rest("CREATE REST CONTENT SET /static"), "Failed to create the REST CONTENT SET `/svc/static`. The request_path is already used by another entity.")
EXPECT_THROWS(lambda: rest("CREATE REST CONTENT SET /x ON SERVICE /nope"), "Failed to create the REST CONTENT SET `/nope/x`. Could not find the REST SERVICE /nope.")
EXPECT_EQ([["/assets", "DISABLED"], ["/static", "ENABLED"]], rest_rows("SHOW REST CONTENT SETS"))

#@<> CREATE REST CONTENT SET IF NOT EXISTS and OR REPLACE
rest("CREATE REST CONTENT FILE `/index.html` ON CONTENT SET /static CONTENT '<html></html>'")
EXPECT_EQ("REST content set `/svc/static` created successfully.", rest_info("CREATE REST CONTENT SET IF NOT EXISTS /static COMMENT 'ignored'"))
EXPECT_EQ(["STATIC", 1, 1, "", None], content_set_row("/static"))
EXPECT_EQ(1, len(rest_rows("SHOW REST CONTENT FILES FROM CONTENT SET /static")))
# OR REPLACE starts over with an empty set
EXPECT_EQ("REST content set `/svc/static` created successfully.", rest_info("CREATE OR REPLACE REST CONTENT SET /static COMMENT 'replaced'"))
EXPECT_EQ(["STATIC", 1, 1, "replaced", None], content_set_row("/static"))
EXPECT_EQ([], rest_rows("SHOW REST CONTENT FILES FROM CONTENT SET /static"))
EXPECT_EQ("REST content set `/svc/empty` created successfully.", rest_info("CREATE REST CONTENT SET IF NOT EXISTS /empty"))

#@<> CREATE REST CONTENT FILE from inline text and base64
rest("CREATE REST CONTENT SET /inline AUTHENTICATION NOT REQUIRED COMMENT 'inline files'")
EXPECT_EQ("REST CONTENT FILE `/svc/inline/readme.txt` created successfully.", rest_info("""CREATE REST CONTENT FILE `/readme.txt` ON CONTENT SET /inline
    CONTENT 'Line \\'1\\'\nLine "2"'
    AUTHENTICATION NOT REQUIRED
    OPTIONS {"a": 1}"""))
EXPECT_EQ(readme_txt.encode().hex().upper(), file_hex("/inline", "/readme.txt"))
EXPECT_EQ("REST CONTENT FILE `/svc/inline/bin.dat` created successfully.", rest_info("CREATE REST CONTENT FILE `/bin.dat` ON SERVICE /svc CONTENT SET /inline BINARY CONTENT 'AAECAwQFBgc='"))
EXPECT_EQ("0001020304050607", file_hex("/inline", "/bin.dat"))
EXPECT_EQ("REST CONTENT FILE `/svc/inline/binaryFile` created successfully.", rest_info(f"CREATE REST CONTENT FILE /binaryFile ON CONTENT SET /inline BINARY CONTENT '{binary_test_base64}' DISABLED"))
EXPECT_EQ(binary_test_data.hex().upper(), file_hex("/inline", "/binaryFile"))
EXPECT_EQ("REST CONTENT FILE `/svc/inline/privateFile` created successfully.", rest_info("CREATE REST CONTENT FILE /privateFile ON CONTENT SET /inline CONTENT 'SELECT 1;' PRIVATE"))
# The REST SQL defaults: a content file requires authentication and is enabled
EXPECT_EQ([["/bin.dat", 8, "ENABLED"], ["/binaryFile", len(binary_test_data), "DISABLED"], ["/privateFile", 9, "PRIVATE"], ["/readme.txt", len(readme_txt), "ENABLED"]], rest_rows("SHOW REST CONTENT FILES FROM CONTENT SET /inline"))
EXPECT_EQ([[1], [0]], query_rows("SELECT requires_auth FROM mariadb_rest_service.content_file WHERE request_path IN ('/bin.dat', '/readme.txt') ORDER BY request_path"))

#@<> CREATE REST CONTENT FILE errors
EXPECT_THROWS(lambda: rest("CREATE REST CONTENT FILE `/bin.dat` ON CONTENT SET /inline CONTENT 'again'"), "Failed to create the REST CONTENT FILE `/svc/inline/bin.dat`. The request_path is already used by another entity.")
EXPECT_THROWS(lambda: rest("CREATE REST CONTENT FILE /x ON CONTENT SET /nope CONTENT 'x'"), "Failed to create the REST CONTENT FILE `/svc/nope/x`. CONTENT SET /nope not found.")
EXPECT_THROWS(lambda: rest("CREATE REST CONTENT FILE /x ON CONTENT SET /inline BINARY CONTENT 'not base64!'"), "Failed to create the REST CONTENT FILE `/svc/inline/x`. The content is not valid base64.")
EXPECT_THROWS(lambda: rest("CREATE REST CONTENT FILE /x ON SERVICE /other CONTENT SET /inline CONTENT 'x'"), "Failed to create the REST CONTENT FILE `/other/inline/x`. CONTENT SET /inline not found.")
EXPECT_EQ(4, len(rest_rows("SHOW REST CONTENT FILES FROM CONTENT SET /inline")))

#@<> CREATE REST CONTENT FILE IF NOT EXISTS and OR REPLACE
EXPECT_EQ("REST CONTENT FILE `/svc/inline/bin.dat` created successfully.", rest_info("CREATE REST CONTENT FILE IF NOT EXISTS `/bin.dat` ON CONTENT SET /inline CONTENT 'ignored'"))
EXPECT_EQ("0001020304050607", file_hex("/inline", "/bin.dat"))
EXPECT_EQ("REST CONTENT FILE `/svc/inline/bin.dat` created successfully.", rest_info("CREATE OR REPLACE REST CONTENT FILE `/bin.dat` ON CONTENT SET /inline BINARY CONTENT 'AAEC'"))
EXPECT_EQ("000102", file_hex("/inline", "/bin.dat"))
rest("CREATE OR REPLACE REST CONTENT FILE `/bin.dat` ON CONTENT SET /inline BINARY CONTENT 'AAECAwQFBgc='")

#@<> SHOW CREATE REST CONTENT FILE: text and binary content
EXPECT_EQ("""CREATE OR REPLACE REST CONTENT FILE `/readme.txt`
    ON SERVICE /svc CONTENT SET /inline
    CONTENT 'Line ''1''
Line "2"'
    OPTIONS {
        "a": 1
    }
    AUTHENTICATION NOT REQUIRED;""", rest_text("SHOW CREATE REST CONTENT FILE `/readme.txt` FROM CONTENT SET /inline"))
EXPECT_EQ("""CREATE OR REPLACE REST CONTENT FILE `/bin.dat`
    ON SERVICE /svc CONTENT SET /inline
    BINARY CONTENT 'AAECAwQFBgc='
    AUTHENTICATION REQUIRED;""", rest_text("SHOW CREATE REST CONTENT FILE `/bin.dat` ON SERVICE /svc CONTENT SET /inline"))
# A file that is mostly text is written as text, one with control characters as base64
EXPECT_IN("\n    CONTENT 'SELECT 1;'\n    PRIVATE\n", rest_text("SHOW CREATE REST CONTENT FILE /privateFile FROM CONTENT SET /inline"))
EXPECT_IN("\n    BINARY CONTENT '" + binary_test_base64 + "'\n", rest_text("SHOW CREATE REST CONTENT FILE /binaryFile FROM CONTENT SET /inline"))
EXPECT_IN("\n    DISABLED\n", rest_text("SHOW CREATE REST CONTENT FILE /binaryFile FROM CONTENT SET /inline"))
EXPECT_THROWS(lambda: rest("SHOW CREATE REST CONTENT FILE /nope FROM CONTENT SET /inline"), "Failed to get the REST CONTENT FILE `/svc/inline/nope`. The given REST content file `/svc/inline/nope` could not be found.")
EXPECT_THROWS(lambda: rest("SHOW CREATE REST CONTENT FILE /nope FROM CONTENT SET /nope"), "Failed to get the REST CONTENT FILE `/svc/nope/nope`. The given REST content set `/svc/nope` could not be found.")

#@<> SHOW CREATE REST CONTENT FILE FORMAT=JSON
res = rest("SHOW CREATE REST CONTENT FILE `/readme.txt` FROM CONTENT SET /inline FORMAT=JSON")
EXPECT_EQ(["CREATE REST CONTENT FILE"], res.get_column_names())
doc = json.loads(res.fetch_one()[0])
EXPECT_EQ({"request_path": "/readme.txt", "content_set_request_path": "/inline", "requires_auth": False, "enabled": 1, "size": len(readme_txt.encode()), "options": {"a": 1}}, {k: doc[k] for k in ["request_path", "content_set_request_path", "requires_auth", "enabled", "size", "options"]})
EXPECT_FALSE("content" in doc)

#@<> SHOW CREATE REST CONTENT SET FORMAT=JSON
res = rest("SHOW CREATE REST CONTENT SET /inline FORMAT=JSON")
EXPECT_EQ(["CREATE REST CONTENT SET"], res.get_column_names())
doc = json.loads(res.fetch_one()[0])
EXPECT_EQ({"request_path": "/inline", "content_type": "STATIC", "comments": "inline files", "requires_auth": False}, {k: doc[k] for k in ["request_path", "content_type", "comments", "requires_auth"]})

#@<> SHOW REST CONTENT SETS / FILES FORMAT=JSON
def json_list(sql, column):
    res = rest(sql)
    EXPECT_EQ([column], res.get_column_names())
    rows = res.fetch_all()
    EXPECT_EQ(1, len(rows))
    return json.loads(rows[0][0])

docs = json_list("SHOW REST CONTENT SETS FORMAT=JSON", "REST CONTENT SETS")
EXPECT_EQ([r[0] for r in rest_rows("SHOW REST CONTENT SETS")], [d["request_path"] for d in docs])
EXPECT_EQ(sorted(d["request_path"] for d in docs), [d["request_path"] for d in docs])
for doc in docs:
    EXPECT_EQ(json.loads(rest_text("SHOW CREATE REST CONTENT SET %s FORMAT=JSON" % doc["request_path"])), doc)
EXPECT_EQ([d["request_path"] for d in docs], [d["request_path"] for d in json_list("SHOW REST CONTENT SETS ON SERVICE /svc FORMAT=JSON", "REST CONTENT SETS")])
docs = json_list("SHOW REST CONTENT FILES FROM CONTENT SET /inline FORMAT=JSON", "REST CONTENT FILES")
EXPECT_EQ([r[0] for r in rest_rows("SHOW REST CONTENT FILES FROM CONTENT SET /inline")], [d["request_path"] for d in docs])
for doc in docs:
    EXPECT_EQ(json.loads(rest_text("SHOW CREATE REST CONTENT FILE `%s` FROM CONTENT SET /inline FORMAT=JSON" % doc["request_path"])), doc)
    EXPECT_FALSE("content" in doc)
EXPECT_EQ([], json_list("SHOW REST CONTENT FILES ON SERVICE /svc CONTENT SET /static FORMAT=JSON", "REST CONTENT FILES"))

#@<> SHOW CREATE REST CONTENT SET lists the set and its files
rest("DROP REST CONTENT FILE /binaryFile FROM CONTENT SET /inline")
rest("DROP REST CONTENT FILE /privateFile FROM CONTENT SET /inline")
inline_statement = """CREATE OR REPLACE REST CONTENT SET /inline
    ON SERVICE /svc
    COMMENT 'inline files'
    AUTHENTICATION NOT REQUIRED;

CREATE OR REPLACE REST CONTENT FILE `/bin.dat`
    ON SERVICE /svc CONTENT SET /inline
    BINARY CONTENT 'AAECAwQFBgc='
    AUTHENTICATION REQUIRED;

CREATE OR REPLACE REST CONTENT FILE `/readme.txt`
    ON SERVICE /svc CONTENT SET /inline
    CONTENT 'Line ''1''
Line "2"'
    OPTIONS {
        "a": 1
    }
    AUTHENTICATION NOT REQUIRED;"""
EXPECT_EQ(inline_statement, rest_text("SHOW CREATE REST CONTENT SET /inline"))
EXPECT_EQ(inline_statement, rest_text("SHOW CREATE REST CONTENT SET /inline ON SERVICE /svc"))
EXPECT_THROWS(lambda: rest("SHOW CREATE REST CONTENT SET /nope"), "Failed to get the REST CONTENT SET `/svc/nope`. The given REST content set `/svc/nope` could not be found.")

#@<> SHOW CREATE REST CONTENT SET round trip
rest("DROP REST CONTENT SET /inline")
EXPECT_EQ([["/assets", "DISABLED"], ["/empty", "ENABLED"], ["/static", "ENABLED"]], rest_rows("SHOW REST CONTENT SETS"))
rest_script(inline_statement)
EXPECT_EQ(inline_statement, rest_text("SHOW CREATE REST CONTENT SET /inline"))
EXPECT_EQ("0001020304050607", file_hex("/inline", "/bin.dat"))
EXPECT_EQ(readme_txt.encode().hex().upper(), file_hex("/inline", "/readme.txt"))
# Running it again replaces the set and its files
rest_script(inline_statement)
EXPECT_EQ(inline_statement, rest_text("SHOW CREATE REST CONTENT SET /inline"))
EXPECT_EQ(2, len(rest_rows("SHOW REST CONTENT FILES FROM CONTENT SET /inline")))
# The script reads the same with NO_BACKSLASH_ESCAPES: quotes are doubled,
# and text with a backslash is written as base64
rest("CREATE REST CONTENT FILE `/path.txt` ON CONTENT SET /inline CONTENT 'C:\\\\dir\\\\file'")
EXPECT_EQ(b"C:\\dir\\file".hex().upper(), file_hex("/inline", "/path.txt"))
EXPECT_IN("\n    BINARY CONTENT 'QzpcZGlyXGZpbGU='\n", rest_text("SHOW CREATE REST CONTENT FILE `/path.txt` FROM CONTENT SET /inline"))
script = rest_text("SHOW CREATE REST CONTENT SET /inline")
sql_mode = session.run_sql("SELECT @@SESSION.sql_mode").fetch_one()[0]
session.run_sql("SET SESSION sql_mode = CONCAT(@@SESSION.sql_mode, ',NO_BACKSLASH_ESCAPES')")
rest_script(script)
session.run_sql("SET SESSION sql_mode = ?", [sql_mode])
EXPECT_EQ(readme_txt.encode().hex().upper(), file_hex("/inline", "/readme.txt"))
EXPECT_EQ(b"C:\\dir\\file".hex().upper(), file_hex("/inline", "/path.txt"))
rest("DROP REST CONTENT FILE `/path.txt` FROM CONTENT SET /inline")

#@<> ALTER REST CONTENT SET
res = rest("ALTER REST CONTENT SET /assets NEW REQUEST PATH /assets2 ENABLED AUTHENTICATION REQUIRED COMMENT 'Renamed' OPTIONS {\"b\": 2}")
EXPECT_EQ(1, res.get_affected_items_count())
EXPECT_EQ([["/assets2", "ENABLED"], ["/empty", "ENABLED"], ["/inline", "ENABLED"], ["/static", "ENABLED"]], rest_rows("SHOW REST CONTENT SETS"))
EXPECT_EQ(["STATIC", 1, 1, "Renamed", {"b": 2}], content_set_row("/assets2"))
EXPECT_EQ(0, len(rest_rows("SHOW REST CONTENT FILES FROM CONTENT SET /assets2")))
rest("ALTER REST CONTENT SET /assets2 ON SERVICE /svc MERGE OPTIONS {\"c\": 3} PRIVATE")
EXPECT_EQ(["STATIC", 1, 2, "Renamed", {"b": 2, "c": 3}], content_set_row("/assets2"))
EXPECT_EQ([["/assets2", "PRIVATE"], ["/empty", "ENABLED"], ["/inline", "ENABLED"], ["/static", "ENABLED"]], rest_rows("SHOW REST CONTENT SETS"))
# MERGE OPTIONS into a set without options sets them
rest("ALTER REST CONTENT SET /empty MERGE OPTIONS {\"d\": 4}")
EXPECT_EQ(["STATIC", 1, 1, "", {"d": 4}], content_set_row("/empty"))
EXPECT_THROWS(lambda: rest("ALTER REST CONTENT SET /nope DISABLED"), "Failed to update the REST content set `/svc/nope`. The given REST content set `/svc/nope` could not be found.")
EXPECT_THROWS(lambda: rest("ALTER REST CONTENT SET /assets2 NEW REQUEST PATH /static"), "Failed to update the REST content set `/svc/assets2`. The request_path is already used by another entity.")
rest("ALTER REST CONTENT SET /assets2 NEW REQUEST PATH /assets")

#@<> ALTER REST CONTENT SET ... LOAD SCRIPTS needs MRS scripts and the build output
rest("CREATE REST CONTENT SET /noscripts")
rest("CREATE REST CONTENT FILE `/index.html` ON CONTENT SET /noscripts CONTENT '<html></html>'")
EXPECT_THROWS(lambda: rest("ALTER REST CONTENT SET /noscripts LOAD SCRIPTS"), "Failed to update the REST content set `/svc/noscripts`. The content set holds no MRS scripts: no TypeScript file defines an @Mrs.module class.")
rest("CREATE REST CONTENT FILE `/src/m.mts` ON CONTENT SET /noscripts CONTENT '@Mrs.module({ name: \"m\" }) class M { }'")
EXPECT_THROWS(lambda: rest("ALTER REST CONTENT SET /noscripts LOAD TYPESCRIPT SCRIPTS"), "No build folder (build, dist, out or output) was found for this TypeScript project.")
# A failing ALTER changes nothing
EXPECT_EQ(["STATIC", 1, 1, "", None], content_set_row("/noscripts"))
rest("DROP REST CONTENT SET /noscripts")

#@<> ALTER REST CONTENT SET ... LOAD TYPESCRIPT SCRIPTS registers the scripts
sales_mts = """// The sales scripts
@Mrs.module({ name: "sales", requestPath: "/sales", requiresAuth: false, options: { tag: "v1", }, })
class Sales {
    @Mrs.script({ requestPath: "/total", requiresAuth: false, grants: { privileges: "SELECT", schema: "mysql", object: "user" } })
    public static async total(year: number): Promise<number> {
        return year;
    }

    @Mrs.script({ name: "summaryOf" })
    public static async summary(region: string = "EU", detailed?: boolean): Promise<Summary[]> {
        return [];
    }

    // Not an MRS script
    public static helper(): string { return "}"; }
}

export interface Item {
    name: string;
}

export interface Summary {
    readonly total: number;
    region: string;
    items?: Item[];
}
"""
rest("CREATE REST CONTENT SET /app COMMENT 'The app'")
rest("CREATE REST CONTENT FILE `/src/sales.mts` ON CONTENT SET /app CONTENT '%s'" % sales_mts.replace("'", "\\'"))
rest("CREATE REST CONTENT FILE `/dist/sales.mjs` ON CONTENT SET /app CONTENT 'export class Sales {}'")
rest("CREATE REST CONTENT FILE `/static/index.html` ON CONTENT SET /app CONTENT '<html></html>'")
EXPECT_EQ("REST content set `/svc/app` updated successfully. 2 MRS script(s) of 1 module(s) registered.", rest_info("ALTER REST CONTENT SET /app LOAD TYPESCRIPT SCRIPTS"))
# The module is a REST schema of type SCRIPT_MODULE
EXPECT_EQ([["sales", "/sales", "SCRIPT_MODULE", 0, "v1"]], query_rows("SELECT name, request_path, schema_type, requires_auth, JSON_VALUE(options, '$.tag') FROM mariadb_rest_service.rest_schema WHERE schema_type = 'SCRIPT_MODULE'"))
# Each script is a REST object of type SCRIPT, with its parameters and result as objects
EXPECT_EQ([["summaryOf", "/summary", 1], ["total", "/total", 0]], query_rows("SELECT name, request_path, requires_auth FROM mariadb_rest_service.rest_object WHERE object_type = 'SCRIPT' ORDER BY name"))
EXPECT_EQ([["SvcSalesSummaryParams", "PARAMETERS"], ["SvcSalesSummaryResult", "RESULT"], ["SvcSalesTotalParams", "PARAMETERS"], ["SvcSalesTotalResult", "RESULT"]], query_rows("SELECT name, kind FROM mariadb_rest_service.data_mapping ORDER BY name"))
fields = query_rows("""SELECT o.name, f.name, f.position, JSON_VALUE(f.db_column, '$.datatype'), JSON_EXTRACT(f.db_column, '$.not_null'), f.represents_reference_id IS NOT NULL
    FROM mariadb_rest_service.data_mapping_field f JOIN mariadb_rest_service.data_mapping o ON o.id = f.data_mapping_id ORDER BY o.name, f.position""")
EXPECT_EQ([
    ["SvcSalesSummaryParams", "region", 0, "text", "false", 0],
    ["SvcSalesSummaryParams", "detailed", 1, "bit(1)", "false", 0],
    ["SvcSalesSummaryResult", "total", 0, "decimal", "true", 0],
    ["SvcSalesSummaryResult", "region", 1, "text", "true", 0],
    ["SvcSalesSummaryResult", "items", 2, "json", "false", 1],
    ["SvcSalesTotalParams", "year", 0, "decimal", "true", 0],
    ["SvcSalesTotalResult", "result", 0, "decimal", "true", 0]], fields)
EXPECT_EQ('"EU"', session.run_sql("SELECT JSON_EXTRACT(db_column, '$.default') FROM mariadb_rest_service.data_mapping_field WHERE name = 'region' AND JSON_EXTRACT(db_column, '$.in') = true").fetch_one()[0])
EXPECT_EQ(["1:n", "Item"], list(session.run_sql("SELECT JSON_VALUE(reference_mapping, '$.kind'), JSON_VALUE(reference_mapping, '$.referenced_schema') FROM mariadb_rest_service.data_mapping_reference").fetch_one()))
EXPECT_EQ("true", session.run_sql("SELECT JSON_EXTRACT(sdk_options, '$.returns_array') FROM mariadb_rest_service.data_mapping WHERE name = 'SvcSalesSummaryResult'").fetch_one()[0])
# The links of the content set to its scripts name the compiled module
EXPECT_EQ([["Script", "TypeScript", "Sales", "summary", "/dist/sales.mjs"], ["Script", "TypeScript", "Sales", "total", "/dist/sales.mjs"]], query_rows("SELECT kind, language, class_name, name, JSON_VALUE(options, '$.file_to_load') FROM mariadb_rest_service.content_set_has_rest_object ORDER BY name"))
# The grants a script declares are run
EXPECT_EQ("Select", session.run_sql("SELECT Table_priv FROM mysql.tables_priv WHERE User = 'mariadb_rest_service_data_provider' AND Db = 'mysql' AND Table_name = 'user'").fetch_one()[0])
# The set holds scripts now; its options carry what the daemon loads
row = content_set_row("/app")
EXPECT_EQ(["SCRIPTS", True, "TypeScript"], [row[0], row[4]["contains_mrs_scripts"], row[4]["mrs_scripting_language"]])
EXPECT_EQ([{"class_name": "Sales", "file_to_load": "/dist/sales.mjs"}], [{k: m[k] for k in ["class_name", "file_to_load"]} for m in row[4]["script_module_files"]])
definitions = row[4]["script_definitions"]
EXPECT_EQ(["Sales"], [m["class_name"] for m in definitions["script_modules"]])
EXPECT_EQ(["total", "summary"], [s["function_name"] for s in definitions["script_modules"][0]["scripts"]])
EXPECT_EQ(["Summary", "Item"], [i["name"] for i in definitions["interfaces"]])
EXPECT_EQ("dist", definitions["build_folder"])
EXPECT_EQ(["static"], definitions["static_content_folders"])
# Only the static folders are served; sources and build output are private
EXPECT_EQ([["/dist/sales.mjs", "PRIVATE"], ["/src/sales.mts", "PRIVATE"], ["/static/index.html", "ENABLED"]], [[r[0], r[2]] for r in rest_rows("SHOW REST CONTENT FILES FROM CONTENT SET /app")])

#@<> SHOW REST SCRIPTS lists the registered scripts
EXPECT_EQ(["REST DB Object", "enabled"], rest("SHOW REST SCRIPTS ON SCHEMA /sales").get_column_names())
EXPECT_EQ([["/summary", "ENABLED"], ["/total", "ENABLED"]], rest_rows("SHOW REST SCRIPTS FROM SERVICE /svc SCHEMA /sales"))
docs = json_list("SHOW REST SCRIPTS ON SCHEMA /sales FORMAT=JSON", "REST SCRIPTS")
EXPECT_EQ([["/summary", "summaryOf", "SCRIPT", "/sales"], ["/total", "total", "SCRIPT", "/sales"]], [[d["request_path"], d["name"], d["object_type"], d["schema_request_path"]] for d in docs])
EXPECT_FALSE("data_mappings" in docs[0])
EXPECT_EQ([True, False], [d["requires_auth"] for d in docs])
# The module is listed with its schema type
schemas = json_list("SHOW REST SCHEMAS FORMAT=JSON", "REST SCHEMAS")
EXPECT_EQ([["/sales", "SCRIPT_MODULE"]], [[d["request_path"], d["schema_type"]] for d in schemas if d["schema_type"] != "DATABASE_SCHEMA"])
# No views, procedures or functions in the module
EXPECT_EQ([], json_list("SHOW REST VIEWS ON SCHEMA /sales FORMAT=JSON", "REST VIEWS"))

#@<> LOAD SCRIPTS again replaces the registered scripts; the language is detected
EXPECT_EQ("REST content set `/svc/app` updated successfully. 2 MRS script(s) of 1 module(s) registered.", rest_info("ALTER REST CONTENT SET /app LOAD SCRIPTS"))
EXPECT_EQ(2, session.run_sql("SELECT COUNT(*) FROM mariadb_rest_service.rest_object WHERE object_type = 'SCRIPT'").fetch_one()[0])
EXPECT_EQ(1, session.run_sql("SELECT COUNT(*) FROM mariadb_rest_service.rest_schema WHERE schema_type = 'SCRIPT_MODULE'").fetch_one()[0])

#@<> LOAD SCRIPTS reports type errors and registers nothing then
rest("CREATE REST CONTENT FILE `/src/bad.ts` ON CONTENT SET /app CONTENT '@Mrs.module({ requestPath: \"/bad\" }) class Bad { @Mrs.script({}) public static async b(x: Unknown): Promise<Missing> { return null; } }'")
EXPECT_THROWS(lambda: rest("ALTER REST CONTENT SET /app LOAD SCRIPTS"), "The MRS scripts have errors:\nThe script b returns an unknown datatype `Missing`.\nUnknown datatype `Unknown` used for script parameter `x`.")
EXPECT_EQ(2, session.run_sql("SELECT COUNT(*) FROM mariadb_rest_service.rest_object WHERE object_type = 'SCRIPT'").fetch_one()[0])
rest("DROP REST CONTENT FILE `/src/bad.ts` FROM CONTENT SET /app")

#@<> SHOW CREATE REST CONTENT SET of a script set: the set, its files, then LOAD SCRIPTS
statement = rest_text("SHOW CREATE REST CONTENT SET /app")
EXPECT_TRUE(statement.startswith("""CREATE OR REPLACE REST CONTENT SET /app
    ON SERVICE /svc
    COMMENT 'The app'
    AUTHENTICATION REQUIRED;

CREATE OR REPLACE REST CONTENT FILE `/dist/sales.mjs`"""), statement)
EXPECT_TRUE(statement.endswith("""

ALTER REST CONTENT SET /app
    ON SERVICE /svc
    LOAD TYPESCRIPT SCRIPTS;"""), statement)
EXPECT_FALSE("script_definitions" in statement)

#@<> Dropping a script set removes its scripts and their module; the dump registers them again
rest("DROP REST CONTENT SET /app")
EXPECT_EQ(0, session.run_sql("SELECT COUNT(*) FROM mariadb_rest_service.rest_object WHERE object_type = 'SCRIPT'").fetch_one()[0])
EXPECT_EQ(0, session.run_sql("SELECT COUNT(*) FROM mariadb_rest_service.rest_schema WHERE schema_type = 'SCRIPT_MODULE'").fetch_one()[0])
rest_script(statement)
EXPECT_EQ(2, session.run_sql("SELECT COUNT(*) FROM mariadb_rest_service.rest_object WHERE object_type = 'SCRIPT'").fetch_one()[0])
EXPECT_EQ(statement, rest_text("SHOW CREATE REST CONTENT SET /app"))
rest("DROP REST CONTENT SET /app")

#@<> CLONE REST SERVICE copies the content files on the server, byte for byte
def service_files(service_path):
    return query_rows(f"""SELECT cs.request_path, f.request_path, HEX(f.content), f.size, f.enabled, f.options
        FROM mariadb_rest_service.content_file f
        JOIN mariadb_rest_service.content_set cs ON cs.id = f.content_set_id
        JOIN mariadb_rest_service.service se ON se.id = cs.service_id
        WHERE se.url_context_root = '{service_path}' ORDER BY 1, 2""")

originals = service_files("/svc")
EXPECT_TRUE(len(originals) > 0)
rest("CLONE REST SERVICE /svc NEW REQUEST PATH /svcClone")
EXPECT_EQ(originals, service_files("/svcClone"))
EXPECT_EQ(0, session.run_sql("""SELECT COUNT(*) FROM mariadb_rest_service.content_file a
    JOIN mariadb_rest_service.content_file b ON a.id = b.id AND a.content_set_id <> b.content_set_id""").fetch_one()[0])
rest("DROP REST SERVICE /svcClone")
rest("USE REST SERVICE /svc")
EXPECT_EQ(originals, service_files("/svc"))

#@<> DROP REST CONTENT FILE
EXPECT_EQ("REST CONTENT FILE `/svc/inline/readme.txt` dropped successfully.", rest_info("DROP REST CONTENT FILE `/readme.txt` FROM CONTENT SET /inline"))
EXPECT_EQ([["/bin.dat", 8, "ENABLED"]], rest_rows("SHOW REST CONTENT FILES FROM CONTENT SET /inline"))
EXPECT_THROWS(lambda: rest("DROP REST CONTENT FILE `/readme.txt` FROM CONTENT SET /inline"), "Failed to drop the REST CONTENT FILE `/svc/inline/readme.txt`. The REST content file /svc/inline/readme.txt was not found.")
EXPECT_THROWS(lambda: rest("DROP REST CONTENT FILE /x FROM CONTENT SET /nope"), "Failed to drop the REST CONTENT FILE `/svc/nope/x`. The REST content set /nope was not found.")
EXPECT_EQ("REST CONTENT FILE `/svc/inline/readme.txt` dropped successfully.", rest_info("DROP REST CONTENT FILE IF EXISTS `/readme.txt` FROM CONTENT SET /inline"))
EXPECT_EQ("REST CONTENT FILE `/svc/nope/x` dropped successfully.", rest_info("DROP REST CONTENT FILE IF EXISTS /x FROM CONTENT SET /nope"))
EXPECT_EQ("REST CONTENT FILE `/svc/inline/bin.dat` dropped successfully.", rest_info("DROP REST CONTENT FILE IF EXISTS `/bin.dat` FROM SERVICE /svc CONTENT SET /inline"))
EXPECT_EQ([], rest_rows("SHOW REST CONTENT FILES FROM CONTENT SET /inline"))

#@<> DROP REST CONTENT SET drops its files
EXPECT_EQ("REST CONTENT SET `/svc/static` dropped successfully.", rest_info("DROP REST CONTENT SET /static"))
EXPECT_EQ([["/assets", "PRIVATE"], ["/empty", "ENABLED"], ["/inline", "ENABLED"]], rest_rows("SHOW REST CONTENT SETS"))
EXPECT_THROWS(lambda: rest("DROP REST CONTENT SET /static"), "Failed to drop the REST CONTENT SET `/svc/static`. The given REST CONTENT SET `/svc/static` could not be found.")
EXPECT_EQ("REST CONTENT SET `/svc/static` dropped successfully.", rest_info("DROP REST CONTENT SET IF EXISTS /static"))
EXPECT_EQ("REST CONTENT SET `/svc/assets` dropped successfully.", rest_info("DROP REST CONTENT SET /assets FROM SERVICE /svc"))
EXPECT_EQ("REST CONTENT SET `/svc/empty` dropped successfully.", rest_info("DROP REST CONTENT SET IF EXISTS /empty"))
EXPECT_EQ("REST CONTENT SET `/svc/inline` dropped successfully.", rest_info("DROP REST CONTENT SET IF EXISTS /inline"))
EXPECT_EQ([], rest_rows("SHOW REST CONTENT SETS"))
EXPECT_EQ(0, session.run_sql("SELECT COUNT(*) FROM mariadb_rest_service.content_file").fetch_one()[0])
EXPECT_EQ(0, session.run_sql("SELECT COUNT(*) FROM mariadb_rest_service.content_set").fetch_one()[0])

#@<> SHOW REST CONTENT FILES errors
EXPECT_THROWS(lambda: rest("SHOW REST CONTENT FILES FROM CONTENT SET /nope"), "Cannot SHOW the REST CONTENT FILEs. The given REST content set `/svc/nope` could not be found.")
EXPECT_THROWS(lambda: rest("SHOW REST CONTENT SETS ON SERVICE /nope"), "Cannot SHOW the REST CONTENT SETs. Could not find the REST SERVICE /nope.")

#@<> The statements through the SQL mode of the shell
testutil.call_mysqlsh([__sandbox_uri1, "--sql", "-e", "USE REST SERVICE /svc; CREATE REST CONTENT SET /cli; CREATE REST CONTENT FILE `/index.html` ON CONTENT SET /cli CONTENT '<html></html>'; SHOW REST CONTENT SETS; SHOW REST CONTENT FILES FROM CONTENT SET /cli; DROP REST CONTENT SET /cli;"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("REST content set `/svc/cli` created successfully.")
# Without a terminal the results are printed tab separated
EXPECT_STDOUT_CONTAINS("/cli\tENABLED\n")
EXPECT_STDOUT_CONTAINS("/index.html\t13\tENABLED\n")
EXPECT_STDOUT_CONTAINS("REST CONTENT SET `/svc/cli` dropped successfully.")
WIPE_OUTPUT()

#@<> Cleanup
session.close()
testutil.destroy_sandbox(__mysql_sandbox_port1)
