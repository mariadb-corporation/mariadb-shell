#@<> Preparation
import json
import base64

shell.connect(__mysqluripwd)

session.run_sql("drop schema if exists json_shell")
session.run_sql("create schema json_shell")
session.run_sql("create table json_shell.sample(id int, data varchar(30))")
session.run_sql("insert into json_shell.sample values (1, ?)", ["john doe"])
session.run_sql("insert into json_shell.sample values (2, ?)", ["jane doe"])

def mysqlsh(args):
    testutil.call_mysqlsh(["--interactive=full","--quiet-start=2"] + args, "", ["MARIADB_SHELL_JSON_SHELL=1", "MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])

#@<> Validating result from SQL execution in SQL mode
mysqlsh(["--sql", "-e", '{"execute":"SELECT * FROM json_shell.sample"}', __mysqluripwd])
EXPECT_STDOUT_CONTAINS('{"Field 1":{"Name":"`id`"')
EXPECT_STDOUT_CONTAINS(',"Field 2":{"Name":"`data`"')
EXPECT_STDOUT_CONTAINS('{"hasData":true,"rows":[{"id":1,"data":"john doe"},{"id":2,"data":"jane doe"}]')

#@<> Validating result from SQL execution through the session object JS {__have_js}
mysqlsh(["--js", "-e", '{"execute":"session.runSql(\'SELECT * FROM json_shell.sample\');"}', __mysqluripwd])
EXPECT_STDOUT_CONTAINS('{"Field 1":{"Name":"`id`"')
EXPECT_STDOUT_CONTAINS(',"Field 2":{"Name":"`data`"')
EXPECT_STDOUT_CONTAINS('{"hasData":true,"rows":[{"id":1,"data":"john doe"},{"id":2,"data":"jane doe"}]')

#@<> Validating result from SQL execution through the session object PY
mysqlsh(["--py", "-e", '{"execute":"session.run_sql(\'SELECT * FROM json_shell.sample\');"}', __mysqluripwd])
EXPECT_STDOUT_CONTAINS('{"Field 1":{"Name":"`id`"')
EXPECT_STDOUT_CONTAINS(',"Field 2":{"Name":"`data`"')
EXPECT_STDOUT_CONTAINS('{"hasData":true,"rows":[{"id":1,"data":"john doe"},{"id":2,"data":"jane doe"}]')

#@<> Validating result from shell dumpRowsm {__have_js}
mysqlsh(["--js", "-e", '{"execute":"let res = session.runSql(\'SELECT * FROM json_shell.sample\'); shell.dumpRows(res);"}', __mysqluripwd])
EXPECT_STDOUT_CONTAINS('{"Field 1":{"Name":"`id`"')
EXPECT_STDOUT_CONTAINS(',"Field 2":{"Name":"`data`"')
EXPECT_STDOUT_CONTAINS('{"hasData":true,"rows":[{"id":1,"data":"john doe"},{"id":2,"data":"jane doe"}]')


#@<> Validating result from \show report {__have_js}
mysqlsh(["--js", "-e", '{"execute":"\\\\show threads"}', __mysqluripwd])
EXPECT_STDOUT_CONTAINS('{"Field 1":{"Name":"`tid`"')
EXPECT_STDOUT_CONTAINS(',"Field 2":{"Name":"`cid`"')
EXPECT_STDOUT_CONTAINS(',"Field 3":{"Name":"`user`"')
EXPECT_STDOUT_CONTAINS('{"hasData":true,"rows":[{"tid"')


#@<> Validating result from SQL in json/raw format
# NOTE: The result data is JSON wrapped inside JSON so it is properly processed by the GUI
mysqlsh(["--result-format=json/raw", "--sql", "-e", '{"execute":"SELECT * FROM json_shell.sample"}', __mysqluripwd])
EXPECT_STDOUT_NOT_CONTAINS('{"Field 1":{"Name":"`id`"')
EXPECT_STDOUT_CONTAINS('{"info":"{\\\"hasData\\\":true,\\\"rows\\\":[{\\\"id\\\":1,\\\"data\\\":\\\"john doe\\\"}')


#@<> Validating result from SQL in json/raw format, including column type info
# NOTE: The column metadata and the result data is JSON wrapped inside JSON so it is properly processed by the GUI
mysqlsh(["--column-type-info", "--result-format=json/raw", "--sql", "-e", '{"execute":"SELECT * FROM json_shell.sample"}', __mysqluripwd])
EXPECT_STDOUT_CONTAINS('{"info":"{\\\"Field 1\\\":{\\\"Name\\\":\\\"`id`\\\"')
EXPECT_STDOUT_CONTAINS('{"info":"{\\\"hasData\\\":true,\\\"rows\\\":[{\\\"id\\\":1,\\\"data\\\":\\\"john doe\\\"}')

#@<> DECIMAL values keep all their digits in the JSON formats
decimal_query = "SELECT CAST(24.90 AS DECIMAL(10,2)) AS d, CAST(12345678.91 AS DECIMAL(12,2)) AS e, CAST(-0.5 AS DECIMAL(3,1)) AS n, 12345678901234567890.123456789 AS big, CAST(NULL AS DECIMAL(5,2)) AS nul"
for result_format in ["json", "ndjson", "json/raw", "json/array"]:
    testutil.call_mysqlsh([__mysqluripwd, "--sql", f"--result-format={result_format}", "-e", decimal_query], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
    EXPECT_STDOUT_CONTAINS('"d":24.90' if result_format != "json" else '"d": 24.90')
    EXPECT_STDOUT_CONTAINS('12345678.91')
    EXPECT_STDOUT_CONTAINS('-0.5')
    EXPECT_STDOUT_CONTAINS('12345678901234567890.123456789')
    EXPECT_STDOUT_NOT_CONTAINS('24.8999')
    WIPE_OUTPUT()

r = session.run_sql(decimal_query)
shell.dump_rows(r, "ndjson")
EXPECT_STDOUT_CONTAINS('{"d":24.90,"e":12345678.91,"n":-0.5,"big":12345678901234567890.123456789,"nul":null}')

#@<> Cleanup
session.run_sql("DROP SCHEMA json_shell")
shell.disconnect()
