#@<> INCLUDE dump_utils.inc

#@<> Setup
import os
import os.path

outdir = os.path.join(__tmp_dir, "dump_chunking")
wipe_dir(outdir)
testutil.mkdir(outdir)

testutil.deploy_sandbox(__mysql_sandbox_port1, "root", {"local_infile": "1"})
src = mysql.get_session(__sandbox_uri1)

#@<> sparse composite integer chunking uses bounded adaptive probes
# constants
dump_dir = os.path.join(outdir, "composite_integer_chunking_right_cluster")
test_schema = "test_composite_integer_chunking_right_cluster"
test_table = "test_table"
# This test models a bad case for adaptive chunk sizing: rows are clustered to
# the right side of the key range, so early estimated key ranges are empty.
#
# Assuming chunks of 5 rows, a table with this shape can spend many probes on
# empty left-side ranges before finding the clustered rows:
#
#   | pk1 |  pk2 |
#   |-----|------|
#   |   1 |    1 |
#   | 200 |    1 |
#   | 200 |    2 |
#   | 200 |    3 |
#   | 200 |    4 |
#   | 200 |    5 |
#   | 200 |  ... |
#   | 200 |  999 |
#   | 200 | 1000 |
#
#   bad linear probing behavior = many empty-range probes before the cluster at
#                                 pk1 = 200
#
# The exact number of probes is implementation-dependent, but the contract is
# that adaptive chunking remains bounded for sparse left-side ranges.
max_id = 200
clustered_rows = 1000
payload_size = 24 * 1024
max_explain_probes = 50

# setup
src.run_sql("DROP SCHEMA IF EXISTS !", [ test_schema ])
src.run_sql("CREATE SCHEMA !", [ test_schema ])
src.run_sql("""CREATE TABLE !.! (
  a INT NOT NULL,
  b INT NOT NULL,
  payload VARBINARY(24576) NOT NULL,
  PRIMARY KEY (a, b)
)
DEFAULT CHARACTER SET = utf8mb4
""", [ test_schema, test_table ])

src.run_sql("INSERT INTO !.! (a, b, payload) VALUES (?, ?, REPEAT('x', ?))",
                 [ test_schema, test_table, 1, 1, payload_size ])

for i in range(1, clustered_rows + 1):
    src.run_sql("INSERT INTO !.! (a, b, payload) VALUES (?, ?, REPEAT('x', ?))",
                     [ test_schema, test_table, max_id, i, payload_size ])

src.run_sql("ANALYZE TABLE !.!", [ test_schema, test_table ])
src.run_sql("FLUSH TABLES !.!", [ test_schema, test_table ])

#@<> sparse composite integer chunking uses bounded adaptive probes - test
old_log_sql = shell.options["logSql"]
shell.options["logSql"] = "unfiltered"
WIPE_SHELL_LOG()

try:
    shell.connect(__sandbox_uri1)
    EXPECT_NO_THROWS(lambda: util.dump_schemas([ test_schema ], dump_dir, { "bytesPerChunk": "128k", "compression": "none", "showProgress": False }), "Dump should not throw")
    explain_probe = f"EXPLAIN FORMAT=JSON SELECT {'SQL_NO_CACHE ' if __version_num < 80000 else ''}COUNT(*) FROM `{test_schema}`.`{test_table}`"
    explain_probe_count = len(testutil.grep_file(testutil.get_shell_log_path(), explain_probe))
    EXPECT_GE(max_explain_probes, explain_probe_count, f"Adaptive chunking should keep EXPLAIN probes bounded for sparse left-side key ranges")
finally:
    shell.options["logSql"] = old_log_sql

#@<> sparse composite integer chunking uses bounded adaptive probes - cleanup
src.run_sql("DROP SCHEMA IF EXISTS !", [ test_schema ])
wipe_dir(outdir)

#@<> Cleanup
src.close()
testutil.destroy_sandbox(__mysql_sandbox_port1)
