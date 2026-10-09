#@<> utils CLI calls {__have_upgrade_checker}
rc = testutil.call_mysqlsh(["--", "util", "check-for-server-upgrade"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("ERROR: ArgumentError: Please connect the shell to the MySQL server to be checked or specify the server URI as a parameter.")
WIPE_OUTPUT()

rc = testutil.call_mysqlsh(["--", "util", "check-for-server-upgrade", "--outputFormat=whatever"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("ERROR: ArgumentError: Please connect the shell to the MySQL server to be checked or specify the server URI as a parameter.")
WIPE_OUTPUT()

rc = testutil.call_mysqlsh(["--", "util", "check-for-server-upgrade", "--dummyOption=whatever"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("The following option is invalid: --dummyOption")
WIPE_OUTPUT()

#@<> Utils CLI calls with an options file {VER(<8.0.0)}
testutil.deploy_raw_sandbox(__mysql_sandbox_port1, 'root')

testutil.call_mysqlsh(['--sql', '--vertical', '-e', 'select * from scti_test.t;'], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"]);

options_file = f"""
[client]
user=root
port={__mysql_sandbox_port1}
password=root
"""
testutil.create_file("my.cnf", options_file)

#@<> Testing upgrade checker with no parameters{VER(<8.0.0)}
# NOTE: This test would simply use the global session as no explicit session parameter was set
rc = testutil.call_mysqlsh(["--", "util", "check-for-server-upgrade"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor", "MYSQL_HOME=."])
EXPECT_EQ(0, rc)
WIPE_OUTPUT()

#@<> Testing upgrade checker passing option but skipping session {VER(<8.0.0)}
# NOTE: This test would simply use the global session because an explicit default parameter (null) was passed from CLI mapper
rc = testutil.call_mysqlsh(["--", "util", "check-for-server-upgrade", "--outputFormat=whatever"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor", "MYSQL_HOME=."])
EXPECT_STDOUT_CONTAINS("ERROR: ArgumentError: Allowed values for outputFormat parameter are TEXT or JSON")
EXPECT_NE(0, rc)
WIPE_OUTPUT()

rc = testutil.call_mysqlsh(["--", "util", "check-for-server-upgrade", "--outputFormat=JSON"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor", "MYSQL_HOME=."])
EXPECT_EQ(0, rc)
WIPE_OUTPUT()

rc = testutil.call_mysqlsh(["--", "util", "check-for-server-upgrade", "--dummyOption=whatever"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor", "MYSQL_HOME=."])
EXPECT_STDOUT_CONTAINS("ERROR: The following option is invalid: --dummyOption")
EXPECT_NE(0, rc)
WIPE_OUTPUT()

testutil.rmfile("my.cnf")
testutil.destroy_sandbox(__mysql_sandbox_port1)

#@<> shell CLI calls
rc = testutil.call_mysqlsh(["--", "shell", "status"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_EQ(0, rc)
rc = testutil.call_mysqlsh(["--", "shell", "connect"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("ERROR: Invalid operation for shell object: connect")
EXPECT_NE(0, rc)
WIPE_OUTPUT()

#@<> cluster CLI calls {__have_admin_api}
rc = testutil.call_mysqlsh(["--", "cluster", "status"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("ERROR: RuntimeError: An open session is required to perform this operation.")
EXPECT_NE(0, rc)
WIPE_OUTPUT()

#@<> dba CLI calls {__have_admin_api}
rc = testutil.call_mysqlsh(["--", "dba", "drop-metadata-schema", "--force"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("ERROR: RuntimeError: An open session is required to perform this operation.")
EXPECT_NE(0, rc)
WIPE_OUTPUT()

rc = testutil.call_mysqlsh(["--", "dba", "deploy-sandbox-instance", "invalid-port"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("ERROR: Argument error at 'invalid-port': Integer expected, but value is String")
EXPECT_NE(0, rc)
WIPE_OUTPUT()

#@<> Dictionary options with schema.table keys
import os
import shutil

shell.connect(__mysql_uri)
session.run_sql("drop schema if exists scti_where")
session.run_sql("create schema scti_where")
session.run_sql("create table scti_where.t (id int primary key)")
session.run_sql("insert into scti_where.t values (1), (2), (3), (4), (5)")

dump_dir = os.path.join(__tmp_dir, "scti_where_dump")

def dump_with(where_arg):
    shutil.rmtree(dump_dir, True)
    WIPE_OUTPUT()
    rc = testutil.call_mysqlsh([__mysqluripwd, "--", "util", "dump-tables", "scti_where", "t", "--output-url=" + dump_dir, "--show-progress=false", where_arg], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
    EXPECT_EQ(0, rc)

dump_with("--where=scti_where.t=id > 3")
EXPECT_STDOUT_CONTAINS("Rows written: 2")

dump_with('--where={"scti_where.t": "id < 4"}')
EXPECT_STDOUT_CONTAINS("Rows written: 3")

dump_with('--where:json={"scti_where.t": "id = 5"}')
EXPECT_STDOUT_CONTAINS("Rows written: 1")

shutil.rmtree(dump_dir, True)
session.run_sql("drop schema scti_where")
session.close()
del dump_with
