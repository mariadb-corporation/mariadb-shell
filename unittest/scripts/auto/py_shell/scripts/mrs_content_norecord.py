#@<> Initialization
# The REST CONTENT SET and REST CONTENT FILE statements of the mrs module:
# static content loaded from a directory, from a file, from inline text and
# from base64. They run through the SQL handler the module registers, so
# session.run_sql() returns their results like those of any SQL statement.
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

def file_hex(content_set, request_path):
    return session.run_sql(f"""SELECT HEX(f.content) FROM mysql_rest_service_metadata.content_file f
        JOIN mysql_rest_service_metadata.content_set cs ON cs.id = f.content_set_id
        WHERE cs.request_path = '{content_set}' AND f.request_path = '{request_path}'""").fetch_one()[0]

def content_set_row(request_path):
    # The options are compared as a document, their text layout varies
    row = list(session.run_sql(f"""SELECT content_type, requires_auth, enabled, comments, options
        FROM mysql_rest_service_metadata.content_set WHERE request_path = '{request_path}'""").fetch_one())
    if row[4] is not None:
        row[4] = json.loads(row[4])
    return row

# The directory a content set is loaded from. The default IGNORE list skips
# node_modules and dot files, so those are only loaded when asked.
content_dir = os.path.join(__tmp_dir, "mrs_content")
testutil.mkdir(content_dir, True)
index_html = "<html><body>Hello</body></html>\n"
style_css = "body { color: red; }\n"
notes_txt = "notes"
readme_txt = "Line '1'\nLine \"2\""
testutil.create_file(os.path.join(content_dir, "index.html"), index_html)
testutil.create_file(os.path.join(content_dir, "css", "style.css"), style_css)
testutil.create_file(os.path.join(content_dir, "notes.txt"), notes_txt)
testutil.create_file(os.path.join(content_dir, "node_modules", "mod.js"), "module.exports = {};")
testutil.create_file(os.path.join(content_dir, ".hidden", "secret"), "secret")
# A backslash inside a REST SQL string literal is an escape character
content_dir_sql = content_dir.replace("\\", "/")

binary_test_file = os.path.join(__data_path, "mrs", "binary_test_file")
text_test_file = os.path.join(__data_path, "mrs", "text_test_file.sql")
with open(binary_test_file, "rb") as f:
    binary_test_data = f.read()
with open(text_test_file, "rb") as f:
    text_test_data = f.read()

#@<> Setup
rest("CONFIGURE REST METADATA ENABLED")
rest("CREATE REST SERVICE /svc")
rest("CREATE REST SERVICE /other")
rest("USE REST SERVICE /svc")

#@<> CREATE REST CONTENT SET from a directory with the default IGNORE list
EXPECT_EQ("REST content set `/svc/static` created successfully. 3 file(s) added.", rest_info(f"CREATE REST CONTENT SET /static FROM '{content_dir_sql}'"))
EXPECT_EQ([["/static", "ENABLED"]], rest_rows("SHOW REST CONTENT SETS"))
EXPECT_EQ([["/css/style.css", len(style_css), "ENABLED"], ["/index.html", len(index_html), "ENABLED"], ["/notes.txt", len(notes_txt), "ENABLED"]], rest_rows("SHOW REST CONTENT FILES FROM CONTENT SET /static"))
EXPECT_EQ(index_html.encode().hex().upper(), file_hex("/static", "/index.html"))
# The REST SQL defaults: a content set requires authentication
EXPECT_EQ(["STATIC", 1, 1, "", None], content_set_row("/static"))
EXPECT_EQ(1, session.run_sql("SELECT requires_auth FROM mysql_rest_service_metadata.content_file WHERE request_path = '/index.html'").fetch_one()[0])

#@<> The loaded files carry their modification time in the options
options = json.loads(session.run_sql("SELECT options FROM mysql_rest_service_metadata.content_file WHERE request_path = '/index.html'").fetch_one()[0])
EXPECT_EQ(["last_modification"], list(options.keys()))
EXPECT_TRUE(options["last_modification"].startswith("20"))
EXPECT_EQ(23, len(options["last_modification"]))

#@<> CREATE REST CONTENT SET with an explicit IGNORE list and options
EXPECT_EQ("REST content set `/svc/assets` created successfully. 2 file(s) added.", rest_info(f"""CREATE REST CONTENT SET /assets ON SERVICE /svc
    FROM "{content_dir_sql}/"
    IGNORE "*.txt, */node_modules/*, */.*"
    DISABLED
    AUTHENTICATION NOT REQUIRED
    COMMENT 'Static assets'
    OPTIONS {{"a": 1}}"""))
EXPECT_EQ([["/css/style.css", len(style_css), "ENABLED"], ["/index.html", len(index_html), "ENABLED"]], rest_rows("SHOW REST CONTENT FILES ON SERVICE /svc CONTENT SET /assets"))
EXPECT_EQ(["STATIC", 0, 0, "Static assets", {"a": 1}], content_set_row("/assets"))
EXPECT_EQ([["/assets", "DISABLED"], ["/static", "ENABLED"]], rest_rows("SHOW REST CONTENT SETS ON SERVICE /svc"))

#@<> An IGNORE list that leaves nothing to load fails and leaves no content set behind
EXPECT_THROWS(lambda: rest(f"CREATE REST CONTENT SET /nothing FROM '{content_dir_sql}' IGNORE '*'"), f"Failed to create the REST CONTENT SET `/svc/nothing`. There are no files in '")
EXPECT_EQ([["/assets", "DISABLED"], ["/static", "ENABLED"]], rest_rows("SHOW REST CONTENT SETS"))

#@<> Nothing is ignored with an empty IGNORE list
EXPECT_EQ("REST content set `/svc/all` created successfully. 5 file(s) added.", rest_info(f"CREATE REST CONTENT SET /all FROM '{content_dir_sql}' IGNORE ''"))
EXPECT_EQ([["/.hidden/secret", 6, "ENABLED"], ["/css/style.css", len(style_css), "ENABLED"], ["/index.html", len(index_html), "ENABLED"], ["/node_modules/mod.js", 20, "ENABLED"], ["/notes.txt", len(notes_txt), "ENABLED"]], rest_rows("SHOW REST CONTENT FILES FROM CONTENT SET /all"))
rest("DROP REST CONTENT SET /all")

#@<> CREATE REST CONTENT SET errors
EXPECT_THROWS(lambda: rest("CREATE REST CONTENT SET /static"), "Failed to create the REST CONTENT SET `/svc/static`. The request_path is already used by another entity.")
EXPECT_THROWS(lambda: rest(f"CREATE REST CONTENT SET /missing FROM '{content_dir_sql}/does_not_exist'"), f"Failed to create the REST CONTENT SET `/svc/missing`. The given path {content_dir_sql}/does_not_exist does not exist.")
EXPECT_THROWS(lambda: rest("CREATE REST CONTENT SET /x ON SERVICE /nope"), "Failed to create the REST CONTENT SET `/nope/x`. Could not find the REST SERVICE /nope.")
EXPECT_EQ([["/assets", "DISABLED"], ["/static", "ENABLED"]], rest_rows("SHOW REST CONTENT SETS"))

#@<> CREATE REST CONTENT SET IF NOT EXISTS and OR REPLACE
EXPECT_EQ("REST content set `/svc/static` created successfully. 0 file(s) added.", rest_info(f"CREATE REST CONTENT SET IF NOT EXISTS /static FROM '{content_dir_sql}'"))
EXPECT_EQ(3, len(rest_rows("SHOW REST CONTENT FILES FROM CONTENT SET /static")))
EXPECT_EQ("REST content set `/svc/static` created successfully. 2 file(s) added.", rest_info(f"CREATE OR REPLACE REST CONTENT SET /static FROM '{content_dir_sql}' IGNORE '*.txt, */node_modules/*, */.*'"))
EXPECT_EQ([["/css/style.css", len(style_css), "ENABLED"], ["/index.html", len(index_html), "ENABLED"]], rest_rows("SHOW REST CONTENT FILES FROM CONTENT SET /static"))
EXPECT_EQ("REST content set `/svc/empty` created successfully. 0 file(s) added.", rest_info("CREATE REST CONTENT SET IF NOT EXISTS /empty"))
EXPECT_EQ([], rest_rows("SHOW REST CONTENT FILES FROM CONTENT SET /empty"))

#@<> CREATE REST CONTENT FILE from inline text, base64 and files
rest("CREATE REST CONTENT SET /inline AUTHENTICATION NOT REQUIRED COMMENT 'inline files'")
EXPECT_EQ("REST CONTENT FILE `/svc/inline/readme.txt` created successfully.", rest_info("""CREATE REST CONTENT FILE `/readme.txt` ON CONTENT SET /inline
    CONTENT 'Line \\'1\\'\nLine "2"'
    AUTHENTICATION NOT REQUIRED
    OPTIONS {"a": 1}"""))
EXPECT_EQ(readme_txt.encode().hex().upper(), file_hex("/inline", "/readme.txt"))
EXPECT_EQ("REST CONTENT FILE `/svc/inline/bin.dat` created successfully.", rest_info("CREATE REST CONTENT FILE `/bin.dat` ON SERVICE /svc CONTENT SET /inline BINARY CONTENT 'AAECAwQFBgc='"))
EXPECT_EQ("0001020304050607", file_hex("/inline", "/bin.dat"))
EXPECT_EQ("REST CONTENT FILE `/svc/inline/fromBinaryFile` created successfully.", rest_info(f"CREATE REST CONTENT FILE /fromBinaryFile ON CONTENT SET /inline FROM '{binary_test_file.replace(chr(92), '/')}' DISABLED"))
EXPECT_EQ(binary_test_data.hex().upper(), file_hex("/inline", "/fromBinaryFile"))
EXPECT_EQ("REST CONTENT FILE `/svc/inline/fromTextFile` created successfully.", rest_info(f"CREATE REST CONTENT FILE /fromTextFile ON CONTENT SET /inline FROM \"{text_test_file.replace(chr(92), '/')}\" PRIVATE"))
EXPECT_EQ(text_test_data.hex().upper(), file_hex("/inline", "/fromTextFile"))
# The REST SQL defaults: a content file requires authentication and is enabled
EXPECT_EQ([["/bin.dat", 8, "ENABLED"], ["/fromBinaryFile", len(binary_test_data), "DISABLED"], ["/fromTextFile", len(text_test_data), "PRIVATE"], ["/readme.txt", len(readme_txt), "ENABLED"]], rest_rows("SHOW REST CONTENT FILES FROM CONTENT SET /inline"))
EXPECT_EQ([[1], [0]], [list(r) for r in session.run_sql("SELECT requires_auth FROM mysql_rest_service_metadata.content_file WHERE request_path IN ('/bin.dat', '/readme.txt') ORDER BY request_path").fetch_all()])

#@<> CREATE REST CONTENT FILE errors
EXPECT_THROWS(lambda: rest("CREATE REST CONTENT FILE `/bin.dat` ON CONTENT SET /inline CONTENT 'again'"), "Failed to create the REST CONTENT FILE `/svc/inline/bin.dat`. The request_path is already used by another entity.")
EXPECT_THROWS(lambda: rest("CREATE REST CONTENT FILE /x ON CONTENT SET /nope CONTENT 'x'"), "Failed to create the REST CONTENT FILE `/svc/nope/x`. CONTENT SET /nope not found.")
EXPECT_THROWS(lambda: rest(f"CREATE REST CONTENT FILE /x ON CONTENT SET /inline FROM '{content_dir_sql}/nope.txt'"), f"Failed to create the REST CONTENT FILE `/svc/inline/x`. File '{content_dir_sql}/nope.txt' does not exist.")
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
    CONTENT 'Line \\'1\\'
Line \\"2\\"'
    OPTIONS {
        "a": 1
    }
    AUTHENTICATION NOT REQUIRED;""", rest_text("SHOW CREATE REST CONTENT FILE `/readme.txt` FROM CONTENT SET /inline"))
EXPECT_EQ("""CREATE OR REPLACE REST CONTENT FILE `/bin.dat`
    ON SERVICE /svc CONTENT SET /inline
    BINARY CONTENT 'AAECAwQFBgc='
    AUTHENTICATION REQUIRED;""", rest_text("SHOW CREATE REST CONTENT FILE `/bin.dat` ON SERVICE /svc CONTENT SET /inline"))
# A file that is mostly text is written as text, one with control characters as base64
EXPECT_CONTAINS("\n    CONTENT 'CREATE DATABASE  IF NOT EXISTS `test`", rest_text("SHOW CREATE REST CONTENT FILE /fromTextFile FROM CONTENT SET /inline"))
EXPECT_CONTAINS("\n    PRIVATE\n", rest_text("SHOW CREATE REST CONTENT FILE /fromTextFile FROM CONTENT SET /inline"))
EXPECT_CONTAINS("\n    BINARY CONTENT '" + base64.b64encode(binary_test_data).decode() + "'\n", rest_text("SHOW CREATE REST CONTENT FILE /fromBinaryFile FROM CONTENT SET /inline"))
EXPECT_CONTAINS("\n    DISABLED\n", rest_text("SHOW CREATE REST CONTENT FILE /fromBinaryFile FROM CONTENT SET /inline"))
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

#@<> SHOW CREATE REST CONTENT SET lists the set and its files
rest("DROP REST CONTENT FILE /fromBinaryFile FROM CONTENT SET /inline")
rest("DROP REST CONTENT FILE /fromTextFile FROM CONTENT SET /inline")
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
    CONTENT 'Line \\'1\\'
Line \\"2\\"'
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

#@<> SHOW CREATE REST CONTENT SET of a directory based set
statement = rest_text("SHOW CREATE REST CONTENT SET /assets")
EXPECT_CONTAINS("""CREATE OR REPLACE REST CONTENT SET /assets
    ON SERVICE /svc
    DISABLED
    COMMENT 'Static assets'
    OPTIONS {
        "a": 1
    }
    AUTHENTICATION NOT REQUIRED;

CREATE OR REPLACE REST CONTENT FILE `/css/style.css`
    ON SERVICE /svc CONTENT SET /assets
    CONTENT 'body { color: red; }
'
    OPTIONS {
        "last_modification": \"""", statement)
EXPECT_CONTAINS("""CREATE OR REPLACE REST CONTENT FILE `/index.html`
    ON SERVICE /svc CONTENT SET /assets
    CONTENT '<html><body>Hello</body></html>
'
    OPTIONS {
        "last_modification": \"""", statement)
# The round trip keeps the files and their options
rest_script(statement)
EXPECT_EQ(statement, rest_text("SHOW CREATE REST CONTENT SET /assets"))

#@<> ALTER REST CONTENT SET
res = rest("ALTER REST CONTENT SET /assets NEW REQUEST PATH /assets2 ENABLED AUTHENTICATION REQUIRED COMMENT 'Renamed' OPTIONS {\"b\": 2}")
EXPECT_EQ(1, res.get_affected_items_count())
EXPECT_EQ([["/assets2", "ENABLED"], ["/empty", "ENABLED"], ["/inline", "ENABLED"], ["/static", "ENABLED"]], rest_rows("SHOW REST CONTENT SETS"))
EXPECT_EQ(["STATIC", 1, 1, "Renamed", {"b": 2}], content_set_row("/assets2"))
EXPECT_EQ(2, len(rest_rows("SHOW REST CONTENT FILES FROM CONTENT SET /assets2")))
rest("ALTER REST CONTENT SET /assets2 ON SERVICE /svc MERGE OPTIONS {\"c\": 3} PRIVATE")
EXPECT_EQ(["STATIC", 1, 2, "Renamed", {"b": 2, "c": 3}], content_set_row("/assets2"))
EXPECT_EQ([["/assets2", "PRIVATE"], ["/empty", "ENABLED"], ["/inline", "ENABLED"], ["/static", "ENABLED"]], rest_rows("SHOW REST CONTENT SETS"))
# MERGE OPTIONS into a set without options sets them
rest("ALTER REST CONTENT SET /empty MERGE OPTIONS {\"d\": 4}")
EXPECT_EQ(["STATIC", 1, 1, "", {"d": 4}], content_set_row("/empty"))
EXPECT_THROWS(lambda: rest("ALTER REST CONTENT SET /nope DISABLED"), "Failed to update the REST content set `/svc/nope`. The given REST content set `/svc/nope` could not be found.")
EXPECT_THROWS(lambda: rest("ALTER REST CONTENT SET /assets2 NEW REQUEST PATH /static"), "Failed to update the REST content set `/svc/assets2`. The request_path is already used by another entity.")
rest("ALTER REST CONTENT SET /assets2 NEW REQUEST PATH /assets")

#@<> LOAD SCRIPTS marks a script content set; the scripts are not analysed
EXPECT_EQ("REST content set `/svc/scripts` created successfully. 0 file(s) added.", rest_info("CREATE REST CONTENT SET /scripts LOAD TYPESCRIPT SCRIPTS OPTIONS {\"x\": 1}"))
EXPECT_EQ(["SCRIPTS", 1, 1, "", {"x": 1, "contains_mrs_scripts": True, "mrs_scripting_language": "TypeScript"}], content_set_row("/scripts"))
EXPECT_EQ("REST content set `/svc/scripts2` created successfully. 0 file(s) added.", rest_info("CREATE REST CONTENT SET /scripts2 LOAD SCRIPTS"))
EXPECT_EQ(["SCRIPTS", 1, 1, "", {"contains_mrs_scripts": True}], content_set_row("/scripts2"))
# SHOW CREATE REST CONTENT SET does not write the LOAD SCRIPTS clause
EXPECT_EQ("""CREATE OR REPLACE REST CONTENT SET /scripts
    ON SERVICE /svc
    OPTIONS {
        "x": 1,
        "contains_mrs_scripts": true,
        "mrs_scripting_language": "TypeScript"
    }
    AUTHENTICATION REQUIRED;""", rest_text("SHOW CREATE REST CONTENT SET /scripts"))
# An ALTER with LOAD SCRIPTS has to keep the scripting language
rest("ALTER REST CONTENT SET /empty LOAD TYPESCRIPT SCRIPTS")
EXPECT_EQ(["SCRIPTS", 1, 1, "", {"contains_mrs_scripts": True, "mrs_scripting_language": "TypeScript"}], content_set_row("/empty"))
EXPECT_THROWS(lambda: rest("ALTER REST CONTENT SET /empty LOAD SCRIPTS"), "Failed to update the REST content set `/svc/empty`. Failed to update REST content set. The options are missing the `mrs_scripting_language` setting.")
rest("DROP REST CONTENT SET /scripts")
rest("DROP REST CONTENT SET /scripts2")

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
EXPECT_EQ(2, session.run_sql("SELECT COUNT(*) FROM mysql_rest_service_metadata.content_file").fetch_one()[0])
EXPECT_THROWS(lambda: rest("DROP REST CONTENT SET /static"), "Failed to drop the REST CONTENT SET `/svc/static`. The given REST CONTENT SET `/svc/static` could not be found.")
EXPECT_EQ("REST CONTENT SET `/svc/static` dropped successfully.", rest_info("DROP REST CONTENT SET IF EXISTS /static"))
EXPECT_EQ("REST CONTENT SET `/svc/assets` dropped successfully.", rest_info("DROP REST CONTENT SET /assets FROM SERVICE /svc"))
EXPECT_EQ("REST CONTENT SET `/svc/empty` dropped successfully.", rest_info("DROP REST CONTENT SET IF EXISTS /empty"))
EXPECT_EQ("REST CONTENT SET `/svc/inline` dropped successfully.", rest_info("DROP REST CONTENT SET IF EXISTS /inline"))
EXPECT_EQ([], rest_rows("SHOW REST CONTENT SETS"))
EXPECT_EQ(0, session.run_sql("SELECT COUNT(*) FROM mysql_rest_service_metadata.content_file").fetch_one()[0])
EXPECT_EQ(0, session.run_sql("SELECT COUNT(*) FROM mysql_rest_service_metadata.content_set").fetch_one()[0])

#@<> SHOW REST CONTENT FILES errors
EXPECT_THROWS(lambda: rest("SHOW REST CONTENT FILES FROM CONTENT SET /nope"), "Cannot SHOW the REST CONTENT FILEs. The given REST content set `/svc/nope` could not be found.")
EXPECT_THROWS(lambda: rest("SHOW REST CONTENT SETS ON SERVICE /nope"), "Cannot SHOW the REST CONTENT SETs. Could not find the REST SERVICE /nope.")

#@<> The statements through the SQL mode of the shell
testutil.call_mysqlsh([__sandbox_uri1, "--sql", "-e", f"USE REST SERVICE /svc; CREATE REST CONTENT SET /cli FROM '{content_dir_sql}'; SHOW REST CONTENT SETS; SHOW REST CONTENT FILES FROM CONTENT SET /cli; DROP REST CONTENT SET /cli;"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("REST content set `/svc/cli` created successfully. 3 file(s) added.")
# Without a terminal the results are printed tab separated
EXPECT_STDOUT_CONTAINS("/cli\tENABLED\n")
EXPECT_STDOUT_CONTAINS("/index.html\t%d\tENABLED\n" % len(index_html))
EXPECT_STDOUT_CONTAINS("REST CONTENT SET `/svc/cli` dropped successfully.")
WIPE_OUTPUT()

#@<> Cleanup
session.close()
testutil.rmdir(content_dir, True)
testutil.destroy_sandbox(__mysql_sandbox_port1)
