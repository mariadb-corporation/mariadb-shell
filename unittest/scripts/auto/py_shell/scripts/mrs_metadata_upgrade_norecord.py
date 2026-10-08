#@<> Initialization
# CONFIGURE REST METADATA UPDATE IF AVAILABLE updates a metadata schema 4.1.6,
# whose ids are BINARY(16), to 5.0.0, whose ids are UUIDs. The ids of the
# existing rows keep their 16 bytes. The test shell has no msm plugin, so the
# module deploys the bundled MSM project's deployment script itself, the way
# msm's deploy_schema() does (log, backup, restore).
import os

# The MSM project bundled with the shell: <prefix>/share/mariadb-shell/mrs/
msm_project = os.path.join(os.path.dirname(os.path.dirname(__mysqlsh)), "share", "mariadb-shell", "mrs", "mysql_rest_service_metadata.msm.project")

testutil.deploy_sandbox(__mysql_sandbox_port1, "root")
shell.connect(__sandbox_uri1)

def rest(sql):
    return session.run_sql(sql)

def rest_rows(sql):
    return [list(row) for row in rest(sql).fetch_all()]

def rest_info(sql):
    return rest(sql).get_info()

def query_one(sql):
    return session.run_sql(sql).fetch_one()[0]

#@<> Deploy the 4.1.6 metadata schema and add a service with a known id
testutil.call_mysqlsh([__sandbox_uri1, "--sql", "-f", os.path.join(msm_project, "releases", "versions", "mysql_rest_service_metadata_4.1.6.sql")], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
WIPE_OUTPUT()
EXPECT_EQ("4.1.6", query_one("SELECT CONCAT(major, '.', minor, '.', patch) FROM mysql_rest_service_metadata.msm_schema_version"))
# The empty host the REST statements use exists already
session.run_sql("INSERT INTO mysql_rest_service_metadata.service (id, url_host_id, url_context_root, enabled) SELECT 0x1112131415161718191a1b1c1d1e1f20, id, '/old', 1 FROM mysql_rest_service_metadata.url_host WHERE name = ''")

#@<> The statements need the 5.0.0 schema
EXPECT_THROWS(lambda: rest("SHOW REST SERVICES"), "The MRS metadata schema version 4.1.6 is too old to be managed by this version of MariaDB Shell. Run CONFIGURE REST METADATA UPDATE IF AVAILABLE to update it to version 5.0.0.")
row = rest("SHOW REST METADATA STATUS").fetch_one()
EXPECT_EQ("true", row[2])
EXPECT_EQ("4.1.6", row[7])
EXPECT_EQ("5.0.0", row[8])

#@<> CONFIGURE REST METADATA without UPDATE IF AVAILABLE does not update
EXPECT_THROWS(lambda: rest("CONFIGURE REST METADATA"), "The MRS metadata version 4.1.6 is too old to be managed by this version of MariaDB Shell.")
EXPECT_EQ("4.1.6", query_one("SELECT CONCAT(major, '.', minor, '.', patch) FROM mysql_rest_service_metadata.msm_schema_version"))

#@<> CONFIGURE REST METADATA UPDATE IF AVAILABLE updates to 5.0.0
EXPECT_EQ("REST metadata configured successfully.", rest_info("CONFIGURE REST METADATA UPDATE IF AVAILABLE"))
EXPECT_EQ("5.0.0", query_one("SELECT CONCAT(major, '.', minor, '.', patch) FROM mysql_rest_service_metadata.msm_schema_version"))
EXPECT_EQ("uuid", query_one("SELECT DATA_TYPE FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = 'mysql_rest_service_metadata' AND TABLE_NAME = 'service' AND COLUMN_NAME = 'id'"))
EXPECT_EQ("REST Metadata updated successfully.", rest_info("CONFIGURE REST METADATA UPDATE IF AVAILABLE"))

#@<> The existing rows keep their ids and work with the REST statements
EXPECT_EQ("11121314-1516-1718-191a-1b1c1d1e1f20", query_one("SELECT id FROM mysql_rest_service_metadata.service WHERE url_context_root = '/old'"))
EXPECT_EQ([["/old", "ENABLED", "NO", ""]], rest_rows("SHOW REST SERVICES"))
rest("USE REST SERVICE /old")
EXPECT_EQ("REST SCHEMA `/old/sys` created successfully.", rest_info("CREATE REST SCHEMA /sys FROM sys"))
EXPECT_EQ("11121314-1516-1718-191a-1b1c1d1e1f20", query_one("SELECT service_id FROM mysql_rest_service_metadata.db_schema WHERE request_path = '/sys'"))
EXPECT_EQ("31000000-0000-0000-0000-000000000000", query_one("SELECT id FROM mysql_rest_service_metadata.auth_vendor WHERE name = 'MySQL Internal'"))

#@<> New ids are UUIDs
rest("CREATE REST SERVICE /new")
new_id = query_one("SELECT id FROM mysql_rest_service_metadata.service WHERE url_context_root = '/new'")
EXPECT_EQ(36, len(new_id))
EXPECT_EQ(["8", "4", "4", "4", "12"], [str(len(part)) for part in new_id.split("-")])

#@<> Older versions cannot be updated
session.run_sql("CREATE OR REPLACE VIEW mysql_rest_service_metadata.msm_schema_version (major, minor, patch) AS SELECT 4, 1, 5")
EXPECT_THROWS(lambda: rest("CONFIGURE REST METADATA UPDATE IF AVAILABLE"), "Failed to configure the REST metadata. Update of database schema `mysql_rest_service_metadata` to version 5.0.0 requested but the version 4.1.5 cannot be updated.")

#@<> Cleanup
session.close()
testutil.destroy_sandbox(__mysql_sandbox_port1)
