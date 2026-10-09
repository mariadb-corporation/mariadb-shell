#@ {__server_is_maria_db}
#@<> Initialization
# The REST metadata schema is named mariadb_rest_service, optionally with a
# prefix and a postfix (e.g. acme_mariadb_rest_service_eu), so a cloud
# provider can deploy one per customer into the same server. The roles carry
# the same prefix and postfix. A session uses the schema it chose with
# USE REST METADATA SCHEMA, else the default name if visible, else the only
# one visible. The test shell has no msm plugin, so the module deploys the
# bundled MSM project itself.

import json

testutil.deploy_sandbox(__mysql_sandbox_port1, "root")
shell.connect(__sandbox_uri1)

def rest(sql, s=None):
    return (s or session).run_sql(sql)

def rest_rows(sql, s=None):
    return [list(row) for row in rest(sql, s).fetch_all()]

def rest_info(sql, s=None):
    return rest(sql, s).get_info()

def status_schema(s=None):
    return rest("SHOW REST METADATA STATUS", s).fetch_one()[11]

def query_one(sql, s=None):
    return (s or session).run_sql(sql).fetch_one()[0]

def roles_like(pattern):
    return [row[0] for row in session.run_sql("SELECT User FROM mysql.user WHERE is_role = 'Y' AND User LIKE ? ORDER BY User", [pattern]).fetch_all()]

#@<> Nothing deployed: the default name is the one CONFIGURE would deploy
EXPECT_EQ("mariadb_rest_service", status_schema())
EXPECT_EQ([], rest_rows("SHOW REST METADATA SCHEMAS"))
EXPECT_EQ([["[]"]], rest_rows("SHOW REST METADATA SCHEMAS FORMAT=JSON"))
EXPECT_EQ(["REST METADATA SCHEMAS"], rest("SHOW REST METADATA SCHEMAS FORMAT=JSON").get_column_names())
EXPECT_THROWS(lambda: rest("SHOW REST SERVICES"), "The MRS metadata schema `mariadb_rest_service` is not installed. Run CONFIGURE REST METADATA first.")

#@<> CONFIGURE REST METADATA deploys the default schema and roles
EXPECT_EQ("REST metadata configured successfully.", rest_info("CONFIGURE REST METADATA"))
EXPECT_EQ("5.0.0", query_one("SELECT CONCAT(major, '.', minor, '.', patch) FROM mariadb_rest_service.msm_schema_version"))
EXPECT_EQ(["mariadb_rest_service_admin", "mariadb_rest_service_data_provider", "mariadb_rest_service_dev", "mariadb_rest_service_meta_provider", "mariadb_rest_service_schema_admin", "mariadb_rest_service_user"], roles_like("mariadb_rest_service_%"))
EXPECT_EQ([["mariadb_rest_service", "5.0.0", "YES"]], rest_rows("SHOW REST METADATA SCHEMAS"))
# MySQL's mysql_tasks role is not created
EXPECT_EQ([], roles_like("mysql_task%"))
rest("CREATE REST SERVICE /default")

#@<> CONFIGURE REST METADATA SCHEMA deploys a prefixed and postfixed schema
EXPECT_EQ("REST metadata configured successfully.", rest_info("CONFIGURE REST METADATA SCHEMA acme_mariadb_rest_service_eu"))
EXPECT_EQ(["acme_mariadb_rest_service_admin_eu", "acme_mariadb_rest_service_data_provider_eu", "acme_mariadb_rest_service_dev_eu", "acme_mariadb_rest_service_meta_provider_eu", "acme_mariadb_rest_service_schema_admin_eu", "acme_mariadb_rest_service_user_eu"], roles_like("acme_mariadb_rest_service_%"))
EXPECT_IN("GRANT SELECT, INSERT, UPDATE, DELETE ON `acme_mariadb_rest_service_eu`.`service` TO `acme_mariadb_rest_service_admin_eu`", [row[0] for row in session.run_sql("SHOW GRANTS FOR acme_mariadb_rest_service_admin_eu").fetch_all()])
# The routines, triggers and events are the same in both schemas
for table, column in [("ROUTINES", "ROUTINE_SCHEMA"), ("TRIGGERS", "TRIGGER_SCHEMA"), ("EVENTS", "EVENT_SCHEMA")]:
    counts = [query_one(f"SELECT COUNT(*) FROM information_schema.{table} WHERE {column} = '{schema}'") for schema in ["mariadb_rest_service", "acme_mariadb_rest_service_eu"]]
    EXPECT_EQ(counts[0], counts[1], table)

#@<> After CONFIGURE ... SCHEMA, the session uses that schema
EXPECT_EQ("acme_mariadb_rest_service_eu", status_schema())
EXPECT_EQ([], rest_rows("SHOW REST SERVICES"))
rest("CREATE REST SERVICE /acme")
EXPECT_EQ([["/acme", "ENABLED", "NO", ""]], rest_rows("SHOW REST SERVICES"))
EXPECT_EQ(1, query_one("SELECT COUNT(*) FROM acme_mariadb_rest_service_eu.service"))
EXPECT_EQ(1, query_one("SELECT COUNT(*) FROM mariadb_rest_service.service"))
EXPECT_EQ([["acme_mariadb_rest_service_eu", "5.0.0", "YES"], ["mariadb_rest_service", "5.0.0", "NO"]], rest_rows("SHOW REST METADATA SCHEMAS"))
EXPECT_EQ([{"schema_name": "acme_mariadb_rest_service_eu", "version": "5.0.0", "current": True}, {"schema_name": "mariadb_rest_service", "version": "5.0.0", "current": False}], json.loads(rest("SHOW REST METADATA SCHEMAS FORMAT=JSON").fetch_one()[0]))

#@<> A session that chose nothing uses the default name while it is visible
other = shell.open_session(__sandbox_uri1)
EXPECT_EQ("mariadb_rest_service", status_schema(other))
EXPECT_EQ([["/default", "ENABLED", "NO", ""]], rest_rows("SHOW REST SERVICES", other))

#@<> USE REST METADATA SCHEMA switches, and clears the current service
rest("USE REST SERVICE /default", other)
EXPECT_EQ("Now using REST METADATA SCHEMA `acme_mariadb_rest_service_eu`.", rest_info("USE REST METADATA SCHEMA acme_mariadb_rest_service_eu", other))
EXPECT_EQ([["/acme", "ENABLED", "NO", ""]], rest_rows("SHOW REST SERVICES", other))
EXPECT_THROWS(lambda: rest("SHOW REST SCHEMAS", other), "No REST SERVICE specified.")
EXPECT_THROWS(lambda: rest("USE REST METADATA SCHEMA nope_mariadb_rest_service", other), "Cannot USE the REST metadata schema `nope_mariadb_rest_service`. It is not a REST metadata schema, or not visible to the current account.")
EXPECT_THROWS(lambda: rest("USE REST METADATA SCHEMA sakila", other), "Invalid REST metadata schema name `sakila`.")

#@<> USE REST METADATA SCHEMA of the schema in use keeps the current service and schema
rest("CREATE REST SCHEMA /s ON SERVICE /acme FROM mysql", other)
rest("USE REST SERVICE /acme SCHEMA /s", other)
EXPECT_EQ("Now using REST METADATA SCHEMA `acme_mariadb_rest_service_eu`.", rest_info("USE REST METADATA SCHEMA acme_mariadb_rest_service_eu", other))
EXPECT_EQ([["/acme", "ENABLED", "YES", ""]], rest_rows("SHOW REST SERVICES", other))
EXPECT_EQ([True], [d["is_current"] for d in json.loads(rest("SHOW REST SERVICES FORMAT=JSON", other).fetch_one()[0])])
EXPECT_EQ("CREATE REST SCHEMA", rest("SHOW CREATE REST SCHEMA", other).get_column_names()[0])
# Switching to another schema still clears them
rest("USE REST METADATA SCHEMA mariadb_rest_service", other)
EXPECT_THROWS(lambda: rest("SHOW REST SCHEMAS", other), "No REST SERVICE specified.")
rest("USE REST METADATA SCHEMA acme_mariadb_rest_service_eu", other)
EXPECT_EQ([["/acme", "ENABLED", "NO", ""]], rest_rows("SHOW REST SERVICES", other))
rest("DROP REST SCHEMA /s FROM SERVICE /acme", other)
other.close()

#@<> A FORMAT=JSON list after USE REST METADATA SCHEMA in the same script
testutil.call_mysqlsh([__sandbox_uri1, "--sql", "-e", "USE REST METADATA SCHEMA acme_mariadb_rest_service_eu; SHOW REST SERVICES FORMAT=JSON; SHOW REST METADATA SCHEMAS FORMAT=JSON;"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("Now using REST METADATA SCHEMA `acme_mariadb_rest_service_eu`.")
EXPECT_STDOUT_CONTAINS("REST SERVICES")
EXPECT_STDOUT_CONTAINS('"full_service_path": "/acme"')
EXPECT_STDOUT_CONTAINS('"schema_name": "acme_mariadb_rest_service_eu"')
WIPE_OUTPUT()

#@<> Without the default schema, several visible schemas make the statements fail
rest("CONFIGURE REST METADATA SCHEMA beta_mariadb_rest_service")
session.run_sql("DROP SCHEMA mariadb_rest_service")
other = shell.open_session(__sandbox_uri1)
EXPECT_THROWS(lambda: rest("SHOW REST SERVICES", other), "There are several REST metadata schemas: `acme_mariadb_rest_service_eu`, `beta_mariadb_rest_service`. Choose one with USE REST METADATA SCHEMA <name>.")
EXPECT_THROWS(lambda: rest("SHOW REST METADATA STATUS", other), "There are several REST metadata schemas")
EXPECT_EQ([["acme_mariadb_rest_service_eu", "5.0.0", "NO"], ["beta_mariadb_rest_service", "5.0.0", "NO"]], rest_rows("SHOW REST METADATA SCHEMAS", other))
rest("USE REST METADATA SCHEMA beta_mariadb_rest_service", other)
EXPECT_EQ([], rest_rows("SHOW REST SERVICES", other))
EXPECT_EQ("beta_mariadb_rest_service", status_schema(other))
other.close()

#@<> An account that sees only its own metadata schema finds it without USE
# The privileges come with the customer's admin role, which has to be the
# account's default role: INFORMATION_SCHEMA only shows what active roles
# grant.
session.run_sql("CREATE USER tenant@'%' IDENTIFIED BY 'tenantpwd'")
session.run_sql("GRANT acme_mariadb_rest_service_admin_eu TO tenant@'%'")
session.run_sql("SET DEFAULT ROLE acme_mariadb_rest_service_admin_eu FOR tenant@'%'")
tenant = shell.open_session(f"tenant:tenantpwd@localhost:{__mysql_sandbox_port1}")
EXPECT_EQ([["acme_mariadb_rest_service_eu", "5.0.0", "YES"]], rest_rows("SHOW REST METADATA SCHEMAS", tenant))
EXPECT_EQ("acme_mariadb_rest_service_eu", status_schema(tenant))
EXPECT_EQ([["/acme", "ENABLED", "NO", ""]], rest_rows("SHOW REST SERVICES", tenant))
rest("CREATE REST SERVICE /tenant", tenant)
EXPECT_EQ(2, query_one("SELECT COUNT(*) FROM acme_mariadb_rest_service_eu.service"))
tenant.close()

#@<> Older versions cannot be updated
rest("USE REST METADATA SCHEMA beta_mariadb_rest_service")
session.run_sql("CREATE OR REPLACE VIEW beta_mariadb_rest_service.msm_schema_version (major, minor, patch) AS SELECT 4, 1, 5")
EXPECT_THROWS(lambda: rest("CONFIGURE REST METADATA UPDATE IF AVAILABLE"), "Failed to configure the REST metadata. Update of database schema `beta_mariadb_rest_service` to version 5.0.0 requested but the version 4.1.5 cannot be updated.")

#@<> Invalid names are refused
EXPECT_THROWS(lambda: rest("CONFIGURE REST METADATA SCHEMA mysql_rest_service_metadata"), "Invalid REST metadata schema name `mysql_rest_service_metadata`.")

#@<> Cleanup
session.close()
testutil.destroy_sandbox(__mysql_sandbox_port1)
