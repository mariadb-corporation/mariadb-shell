#@<> Initialization
# The REST SQL grammar test of the former Python mrs_plugin
# (scripts/run_grammar_test.sh): every statement of grammar_test.sql runs
# against a sandbox with the sakila schema, and none of them may fail.
import os

testutil.deploy_sandbox(__mysql_sandbox_port1, "root")

data_dir = os.path.join(__data_path, "mrs")

#@<> Load the sakila schema and the test setup
testutil.import_data(__sandbox_uri1, os.path.join(__data_path, "sql", "sakila-schema.sql"))
testutil.call_mysqlsh([__sandbox_uri1, "--sql", "-f", os.path.join(data_dir, "grammar_test_setup.sql")], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_NOT_CONTAINS("ERROR")
WIPE_OUTPUT()

#@<> Run the grammar test
# The content set statements load files from the test data directory
with open(os.path.join(data_dir, "grammar_test.sql")) as f:
    script = f.read().replace("{{MRS_DATA_DIR}}", data_dir.replace("\\", "/"))
script_file = os.path.join(__tmp_dir, "mrs_grammar_test.sql")
testutil.create_file(script_file, script)

# --interactive=full keeps going after a failed statement, so every error
# of the whole script is reported
testutil.call_mysqlsh([__sandbox_uri1, "--sql", "--interactive=full", "-f", script_file], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_NOT_CONTAINS("ERROR")
EXPECT_STDOUT_NOT_CONTAINS("Syntax Error")
EXPECT_STDOUT_NOT_CONTAINS("Failed to ")
EXPECT_STDOUT_NOT_CONTAINS("Cannot ")
EXPECT_STDOUT_NOT_CONTAINS("not implemented")
EXPECT_STDOUT_CONTAINS("REST metadata configured successfully.")
WIPE_OUTPUT()

#@<> Cleanup
os.remove(script_file)
testutil.destroy_sandbox(__mysql_sandbox_port1)
