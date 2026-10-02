# End-to-end dump and load of the MariaDB-only object types and server features:
# sequences, check constraints under check_constraint_checks, Oracle-mode
# packages, hostless roles, GTID positions, BACKUP STAGE, system-versioned
# tables and the MariaDB column types. Each section is the scripted counterpart
# of a section of MARIADB_DUMP_LOAD.md, which is where the reasoning lives.

#@<> INCLUDE dump_utils.inc

#@<> Setup
if not __server_is_maria_db:
    testutil.skip("Tests MariaDB-only features")

import json
import os
import os.path
import time

outdir = os.path.join(__tmp_dir, "ldtest_mariadb")
wipe_dir(outdir)
testutil.mkdir(outdir)

for port in [__mysql_sandbox_port1, __mysql_sandbox_port2]:
    testutil.deploy_sandbox(port, "root", {"local_infile": "1"})

session1 = mysql.get_session(__sandbox_uri1)
session2 = mysql.get_session(__sandbox_uri2)

def dump_dir_for(name):
    path = os.path.join(outdir, name)
    wipe_dir(path)
    return path

def dump_schema(schema, dump_dir, options = {}):
    shell.connect(__sandbox_uri1)
    opts = { "showProgress": False }
    opts.update(options)
    util.dump_schemas([schema], dump_dir, opts)

def load(dump_dir, options = {}):
    shell.connect(__sandbox_uri2)
    opts = { "showProgress": False, "resetProgress": True }
    opts.update(options)
    util.load_dump(dump_dir, opts)

def fetch_value(session, query, args = []):
    return session.run_sql(query, args).fetch_one()[0]

def read_manifest(dump_dir, name = "@.json"):
    with open(os.path.join(dump_dir, name), encoding="utf-8") as f:
        return json.load(f)

def snapshot_sequences(session, schema):
    return [list(row) for row in session.run_sql("SELECT SEQUENCE_NAME, START_VALUE, MINIMUM_VALUE, MAXIMUM_VALUE, INCREMENT, CYCLE_OPTION FROM information_schema.SEQUENCES WHERE SEQUENCE_SCHEMA = ? ORDER BY SEQUENCE_NAME", [schema]).fetch_all()]

def snapshot_packages(session, schema):
    packages = {}
    for name, type in session.run_sql("SELECT ROUTINE_NAME, ROUTINE_TYPE FROM information_schema.ROUTINES WHERE ROUTINE_SCHEMA = ? AND ROUTINE_TYPE LIKE 'PACKAGE%' ORDER BY ROUTINE_NAME, ROUTINE_TYPE", [schema]).fetch_all():
        packages[f"{type} {name}"] = session.run_sql(f"SHOW CREATE {type} !.!", [schema, name]).fetch_one()[2]
    return packages

def show_create_table(session, schema, table):
    return session.run_sql("SHOW CREATE TABLE !.!", [schema, table]).fetch_one()[1]

#@<> sequences - setup
# MARIADB_DUMP_LOAD.md section 5.1. NOCACHE keeps the positions predictable: a
# cached sequence restores a whole cache ahead, which is correct but not exact.
session1.run_sql("CREATE SCHEMA seqtest")
session1.run_sql("CREATE SEQUENCE seqtest.s1 START WITH 100 INCREMENT BY 5 NOCACHE")
# a cycling sequence sitting past its maximum
session1.run_sql("CREATE SEQUENCE seqtest.s2 MINVALUE 1 MAXVALUE 1000 CYCLE NOCACHE")
session1.run_sql("SELECT SETVAL(seqtest.s2, 1000)")
session1.run_sql("CREATE SEQUENCE seqtest.q1 START WITH -10 MINVALUE -100 MAXVALUE 100 INCREMENT BY -2 CACHE 50")
session1.run_sql("CREATE SEQUENCE seqtest.q2")
session1.run_sql("CREATE TABLE seqtest.t1 (id INT PRIMARY KEY DEFAULT (NEXT VALUE FOR seqtest.s1), v VARCHAR(10))")
session1.run_sql("INSERT INTO seqtest.t1 (v) VALUES ('a'), ('b'), ('c')")

seq_dump = dump_dir_for("sequences")
EXPECT_NO_THROWS(lambda: dump_schema("seqtest", seq_dump), "dump")

#@<> sequences - round trip keeps the definitions and the position
EXPECT_NO_THROWS(lambda: load(seq_dump), "load")

EXPECT_EQ(snapshot_sequences(session1, "seqtest"), snapshot_sequences(session2, "seqtest"))
EXPECT_EQ(md5_table(session1, "seqtest", "t1"), md5_table(session2, "seqtest", "t1"))
# the table's DEFAULT resolves against the restored sequence, no gap, no duplicate
session2.run_sql("INSERT INTO seqtest.t1 (v) VALUES ('d')")
EXPECT_EQ(115, fetch_value(session2, "SELECT MAX(id) FROM seqtest.t1"))
EXPECT_EQ(1001, fetch_value(session2, "SELECT next_not_cached_value FROM seqtest.s2"))

#@<> sequences - a second load reports them as duplicates
EXPECT_THROWS(lambda: load(seq_dump), "Duplicate objects found in destination database")
EXPECT_STDOUT_CONTAINS("Schema `seqtest` already contains a sequence named `s1`")

#@<> sequences - dropExistingObjects recreates them
EXPECT_NO_THROWS(lambda: load(seq_dump, { "dropExistingObjects": True }), "load")
EXPECT_EQ(110, fetch_value(session2, "SELECT MAX(id) FROM seqtest.t1"))
EXPECT_EQ(115, fetch_value(session2, "SELECT NEXTVAL(seqtest.s1)"))

#@<> sequences - excludeTables filters them on the dump
wipeout_server(session2)
seq_filtered_dump = dump_dir_for("sequences_filtered")
EXPECT_NO_THROWS(lambda: dump_schema("seqtest", seq_filtered_dump, { "excludeTables": ["seqtest.q1", "seqtest.q2"] }), "dump")
EXPECT_STDOUT_CONTAINS("2 out of 4 sequences")
EXPECT_NO_THROWS(lambda: load(seq_filtered_dump), "load")
EXPECT_EQ(["s1", "s2"], [row[0] for row in snapshot_sequences(session2, "seqtest")])

#@<> sequences - excludeTables filters them on the load, SETVAL included
wipeout_server(session2)
EXPECT_NO_THROWS(lambda: load(seq_dump, { "excludeTables": ["seqtest.q1", "seqtest.q2"] }), "load")
EXPECT_EQ(["s1", "s2"], [row[0] for row in snapshot_sequences(session2, "seqtest")])
EXPECT_STDOUT_NOT_CONTAINS("ERROR")

#@<> sequences - dumpTables of a sequence
wipeout_server(session2)
seq_table_dump = dump_dir_for("sequences_table")
shell.connect(__sandbox_uri1)
EXPECT_NO_THROWS(lambda: util.dump_tables("seqtest", ["s2"], seq_table_dump, { "showProgress": False }), "dump")
EXPECT_NO_THROWS(lambda: load(seq_table_dump), "load")
EXPECT_EQ(["s2"], [row[0] for row in snapshot_sequences(session2, "seqtest")])
EXPECT_EQ(1001, fetch_value(session2, "SELECT next_not_cached_value FROM seqtest.s2"))

#@<> sequences - a DEFAULT on a sequence follows a renamed schema
# MARIADB_DUMP_LOAD.md section 5.1: SHOW CREATE TABLE qualifies the sequence with
# the source schema, which bound a restored copy to the original's sequence
wipeout_server(session2)
session2.run_sql("CREATE SCHEMA seqtest")
session2.run_sql("CREATE SEQUENCE seqtest.s1 START WITH 5000 NOCACHE")
EXPECT_NO_THROWS(lambda: load(seq_dump, { "schema": "seqcopy" }), "load")
session2.run_sql("INSERT INTO seqcopy.t1 (v) VALUES ('d')")
EXPECT_EQ(115, fetch_value(session2, "SELECT MAX(id) FROM seqcopy.t1"))
# the unrelated sequence of the source's name is untouched
EXPECT_EQ(5000, fetch_value(session2, "SELECT next_not_cached_value FROM seqtest.s1"))

#@<> sequences - a cross-schema DEFAULT keeps its qualifier
session1.run_sql("CREATE SCHEMA seqdep")
session1.run_sql("CREATE TABLE seqdep.t1 (id INT PRIMARY KEY DEFAULT (NEXT VALUE FOR seqtest.s1))")
wipeout_server(session2)
seqdep_dump = dump_dir_for("sequences_cross_schema")
EXPECT_NO_THROWS(lambda: dump_schema("seqdep", seqdep_dump), "dump")
EXPECT_STDOUT_CONTAINS("WARNING: Table `seqdep`.`t1` has a column DEFAULT which references sequence `seqtest`.`s1` in another schema.")
EXPECT_CONTAINS("`seqtest`.`s1`", show_create_table(session1, "seqdep", "t1"))

#@<> sequences - dumpTables warns about a sequence it does not carry
seqtable_dump = dump_dir_for("sequences_dependency")
shell.connect(__sandbox_uri1)
EXPECT_NO_THROWS(lambda: util.dump_tables("seqtest", ["t1"], seqtable_dump, { "showProgress": False }), "dump")
EXPECT_STDOUT_CONTAINS("WARNING: Table `seqtest`.`t1` has a column DEFAULT which references sequence `s1`. The sequence is not included in this dump")
EXPECT_THROWS(lambda: load(seqtable_dump), "Error loading dump")
EXPECT_STDOUT_CONTAINS("Table 'seqtest.s1' doesn't exist")

#@<> sequences - a cycling sequence keeps its cycle_count
# the round - how many times a CYCLE sequence wrapped - is SETVAL's fourth
# argument, which mariadb-dump does not write
session1.run_sql("CREATE SCHEMA seqcycle")
session1.run_sql("CREATE SEQUENCE seqcycle.c1 MINVALUE 1 MAXVALUE 3 NOCACHE CYCLE")
# 1, 2, 3, 1, 2, 3, 1: wrapped twice, 2 is next
for i in range(7):
    session1.run_sql("DO NEXTVAL(seqcycle.c1)")
# wrapped once, then made NOCYCLE: SETVAL refuses a round on a sequence which
# does not cycle, so none is written for it
session1.run_sql("CREATE SEQUENCE seqcycle.c2 MINVALUE 1 MAXVALUE 3 NOCACHE CYCLE")
for i in range(4):
    session1.run_sql("DO NEXTVAL(seqcycle.c2)")
session1.run_sql("ALTER SEQUENCE seqcycle.c2 NOCYCLE")
EXPECT_EQ([2, 2], list(session1.run_sql("SELECT cycle_count, next_not_cached_value FROM seqcycle.c1").fetch_one()))
# ALTER ... NOCYCLE resets the count, so there is no round to lose
EXPECT_EQ([0, 2], list(session1.run_sql("SELECT cycle_count, next_not_cached_value FROM seqcycle.c2").fetch_one()))

wipeout_server(session2)
seqcycle_dump = dump_dir_for("sequences_cycle")
EXPECT_NO_THROWS(lambda: dump_schema("seqcycle", seqcycle_dump), "dump")
EXPECT_NO_THROWS(lambda: load(seqcycle_dump), "load")

EXPECT_EQ([2, 2], list(session2.run_sql("SELECT cycle_count, next_not_cached_value FROM seqcycle.c1").fetch_one()))
EXPECT_EQ(2, fetch_value(session2, "SELECT NEXTVAL(seqcycle.c1)"))
EXPECT_EQ([0, 2], list(session2.run_sql("SELECT cycle_count, next_not_cached_value FROM seqcycle.c2").fetch_one()))
EXPECT_EQ(2, fetch_value(session2, "SELECT NEXTVAL(seqcycle.c2)"))

#@<> sequences - cleanup
session1.run_sql("DROP SCHEMA seqtest")
session1.run_sql("DROP SCHEMA seqdep")
session1.run_sql("DROP SCHEMA seqcycle")
wipeout_server(session2)

#@<> check constraints - setup
# MARIADB_DUMP_LOAD.md section 5.2: a MariaDB table can hold rows its own CHECK
# constraints reject, so the load has to switch enforcement off
session1.run_sql("CREATE SCHEMA cktest")
session1.run_sql("CREATE TABLE cktest.t (id INT PRIMARY KEY, a INT CHECK (a > 0), b INT, c INT, CONSTRAINT b_lt_c CHECK (b < c), CHECK (c < 100))")
session1.run_sql("INSERT INTO cktest.t VALUES (1, 1, 1, 2)")
session1.run_sql("SET SESSION check_constraint_checks = 0")
session1.run_sql("INSERT INTO cktest.t VALUES (2, -1, 5, 1)")
session1.run_sql("SET SESSION check_constraint_checks = 1")

ck_dump = dump_dir_for("check_constraints")
EXPECT_NO_THROWS(lambda: dump_schema("cktest", ck_dump), "dump")

#@<> check constraints - violating rows round trip
EXPECT_NO_THROWS(lambda: load(ck_dump), "load")
EXPECT_EQ(show_create_table(session1, "cktest", "t"), show_create_table(session2, "cktest", "t"))
EXPECT_EQ(md5_table(session1, "cktest", "t"), md5_table(session2, "cktest", "t"))
EXPECT_EQ(2, fetch_value(session2, "SELECT COUNT(*) FROM cktest.t"))

#@<> check constraints - importTable of a violating row
import_file = os.path.join(outdir, "ck.tsv")
with open(import_file, "w", encoding="utf-8") as f:
    f.write("3\t-5\t9\t1\n")
shell.connect(__sandbox_uri2)
EXPECT_NO_THROWS(lambda: util.import_table(import_file, { "schema": "cktest", "table": "t", "showProgress": False }), "import")
EXPECT_EQ(-5, fetch_value(session2, "SELECT a FROM cktest.t WHERE id = 3"))

#@<> check constraints - cleanup
session1.run_sql("DROP SCHEMA cktest")
wipeout_server(session2)

#@<> rolled-back deadlocks are retried - setup {__dbug}
# MARIADB_DUMP_LOAD.md section 7.3: a deadlock victim inside a statement whose
# errors are suppressed - LOAD DATA ... IGNORE, which LOAD DATA LOCAL without
# REPLACE implies - is refused at commit with 4060 (ER_ROLLBACK_ONLY) rather
# than 1213. The transaction was rolled back completely, so both are retried.
# The deadlock itself is timing dependent (none in three 4-thread imports into
# a WITHOUT OVERLAPS table on 13.1.1), so the error is injected, once.
rollback_only = { "code": 4060, "msg": "This transaction was rolled back and cannot be committed.", "state": "HY000", "onetime": True }
retry_file = os.path.join(outdir, "retry.tsv")
with open(retry_file, "w", encoding="utf-8") as f:
    for i in range(1, 1001):
        f.write(f"{i}\tx\n")
session2.run_sql("CREATE SCHEMA retrydb")
session2.run_sql("CREATE TABLE retrydb.t (id INT PRIMARY KEY, v VARCHAR(10))")

#@<> rolled-back deadlocks are retried - importTable {__dbug}
shell.connect(__sandbox_uri2)
testutil.set_trap("mysql", ["sql regex LOAD DATA LOCAL INFILE .* INTO TABLE `retrydb`.`t`.*"], rollback_only)
EXPECT_NO_THROWS(lambda: util.import_table(retry_file, { "schema": "retrydb", "table": "t", "showProgress": False }), "import")
testutil.clear_traps("mysql")
EXPECT_STDOUT_CONTAINS("The transaction was rolled back by the server, will retry: MySQL Error 4060")
EXPECT_EQ(1000, fetch_value(session2, "SELECT COUNT(*) FROM retrydb.t"))

#@<> rolled-back deadlocks are retried - loadDump {__dbug}
session1.run_sql("CREATE SCHEMA retrydb")
session1.run_sql("CREATE TABLE retrydb.t (id INT PRIMARY KEY, v VARCHAR(10))")
session1.run_sql("INSERT INTO retrydb.t VALUES (1, 'a'), (2, 'b')")
retry_dump = dump_dir_for("retry")
EXPECT_NO_THROWS(lambda: dump_schema("retrydb", retry_dump), "dump")
session2.run_sql("DROP SCHEMA retrydb")
testutil.set_trap("mysql", ["sql regex CREATE TABLE IF NOT EXISTS `t`.*"], rollback_only)
EXPECT_NO_THROWS(lambda: load(retry_dump), "load")
testutil.clear_traps("mysql")
EXPECT_STDOUT_CONTAINS("will retry after delay: MySQL Error 4060")
EXPECT_EQ(2, fetch_value(session2, "SELECT COUNT(*) FROM retrydb.t"))

#@<> rolled-back deadlocks are retried - cleanup {__dbug}
session1.run_sql("DROP SCHEMA IF EXISTS retrydb")
wipeout_server(session2)

#@<> packages - setup
# MARIADB_DUMP_LOAD.md section 5.3: a package has its own namespace, so a
# standalone function of the same name is a second object
session1.run_sql("CREATE SCHEMA pkgtest")
session1.run_sql("CREATE TABLE pkgtest.t (id INT PRIMARY KEY)")
session1.run_sql("CREATE FUNCTION pkgtest.pkg1(a INT) RETURNS INT DETERMINISTIC RETURN a")
session1.run_sql("CREATE FUNCTION pkgtest.plain() RETURNS INT DETERMINISTIC RETURN 1")
session1.run_sql("CREATE PROCEDURE pkgtest.proc() SELECT 1")
session1.run_sql("SET SESSION sql_mode = 'ORACLE'")
session1.run_sql("""CREATE PACKAGE pkgtest.pkg1 AS
  FUNCTION f1(a INT) RETURN INT;
END""")
session1.run_sql("""CREATE PACKAGE BODY pkgtest.pkg1 AS
  base INT := 10;
  FUNCTION f1(a INT) RETURN INT AS
  BEGIN
    RETURN a + base;
  END;
END""")
session1.run_sql("""CREATE PACKAGE pkgtest.`specOnly` AS
  PROCEDURE p1;
END""")
session1.run_sql("SET SESSION sql_mode = DEFAULT")

pkg_dump = dump_dir_for("packages")
EXPECT_NO_THROWS(lambda: dump_schema("pkgtest", pkg_dump), "dump")

#@<> packages - round trip keeps both namespaces
EXPECT_NO_THROWS(lambda: load(pkg_dump), "load")
EXPECT_EQ(snapshot_packages(session1, "pkgtest"), snapshot_packages(session2, "pkgtest"))
EXPECT_EQ(snapshot_functions(session1, "pkgtest"), snapshot_functions(session2, "pkgtest"))
EXPECT_EQ(snapshot_procedures(session1, "pkgtest"), snapshot_procedures(session2, "pkgtest"))
# the body's package variable survived, and the function of the same name did too
EXPECT_EQ(15, fetch_value(session2, "SELECT pkgtest.pkg1.f1(5)"))
EXPECT_EQ(7, fetch_value(session2, "SELECT pkgtest.pkg1(7)"))

#@<> packages - a second load reports them as duplicates
EXPECT_THROWS(lambda: load(pkg_dump), "Duplicate objects found in destination database")
EXPECT_STDOUT_CONTAINS("already contains a package named `pkg1`")
EXPECT_STDOUT_CONTAINS("already contains a package named `specOnly`")
EXPECT_STDOUT_CONTAINS("already contains a package body named `pkg1`")

#@<> packages - dropExistingObjects recreates them
EXPECT_NO_THROWS(lambda: load(pkg_dump, { "dropExistingObjects": True }), "load")
EXPECT_EQ(snapshot_packages(session1, "pkgtest"), snapshot_packages(session2, "pkgtest"))
EXPECT_EQ(15, fetch_value(session2, "SELECT pkgtest.pkg1.f1(5)"))

#@<> packages - excludeRoutines filters them on the dump
wipeout_server(session2)
pkg_filtered_dump = dump_dir_for("packages_filtered")
EXPECT_NO_THROWS(lambda: dump_schema("pkgtest", pkg_filtered_dump, { "excludeRoutines": ["pkgtest.pkg1"] }), "dump")
EXPECT_STDOUT_CONTAINS("3 out of 6 routines")
EXPECT_NO_THROWS(lambda: load(pkg_filtered_dump), "load")
EXPECT_EQ(["PACKAGE specOnly"], list(snapshot_packages(session2, "pkgtest").keys()))
EXPECT_EQ(["plain"], list(snapshot_functions(session2, "pkgtest").keys()))

#@<> packages - excludeRoutines filters them on the load, DROP included
# an unrelated package of the same name already on the target has to survive
wipeout_server(session2)
session2.run_sql("CREATE SCHEMA pkgtest")
session2.run_sql("SET SESSION sql_mode = 'ORACLE'")
session2.run_sql("""CREATE PACKAGE pkgtest.pkg1 AS
  FUNCTION f1(a INT) RETURN INT;
END""")
session2.run_sql("""CREATE PACKAGE BODY pkgtest.pkg1 AS
  FUNCTION f1(a INT) RETURN INT AS
  BEGIN
    RETURN -a;
  END;
END""")
session2.run_sql("SET SESSION sql_mode = DEFAULT")
EXPECT_NO_THROWS(lambda: load(pkg_dump, { "excludeRoutines": ["pkgtest.pkg1"], "dropExistingObjects": True }), "load")
EXPECT_EQ(-5, fetch_value(session2, "SELECT pkgtest.pkg1.f1(5)"))
EXPECT_TRUE("PACKAGE specOnly" in snapshot_packages(session2, "pkgtest"))

#@<> packages - util.copySchemas carries them
wipeout_server(session2)
shell.connect(__sandbox_uri1)
EXPECT_NO_THROWS(lambda: util.copy_schemas(["pkgtest"], __sandbox_uri2, { "schema": "pkgcopy", "showProgress": False }), "copy")
EXPECT_EQ(sorted(snapshot_packages(session1, "pkgtest").keys()), sorted(snapshot_packages(session2, "pkgcopy").keys()))
EXPECT_EQ(15, fetch_value(session2, "SELECT pkgcopy.pkg1.f1(5)"))

#@<> packages - cleanup
session1.run_sql("DROP SCHEMA pkgtest")
wipeout_server(session2)

#@<> roles - setup
# MARIADB_DUMP_LOAD.md sections 6.1 and 6.3: MariaDB roles are hostless, only
# DROP ROLE removes one, and a role granted to a dumped account is part of that
# account's privileges
session1.run_sql("CREATE SCHEMA rolesdb")
session1.run_sql("CREATE TABLE rolesdb.t (id INT PRIMARY KEY)")
session1.run_sql("INSERT INTO rolesdb.t VALUES (1), (2)")
session1.run_sql("CREATE TABLE rolesdb.pub (id INT PRIMARY KEY)")
session1.run_sql("CREATE PROCEDURE rolesdb.p() SELECT 1")
session1.run_sql("CREATE ROLE baserole")
session1.run_sql("CREATE ROLE midrole")
session1.run_sql("CREATE ROLE toprole")
session1.run_sql("GRANT SELECT ON rolesdb.t TO baserole")
session1.run_sql("GRANT baserole TO midrole")
session1.run_sql("GRANT midrole TO toprole")
session1.run_sql("GRANT EXECUTE ON PROCEDURE rolesdb.p TO toprole")
session1.run_sql("CREATE USER u_app@'%' IDENTIFIED BY 'pwd'")
session1.run_sql("GRANT toprole TO u_app@'%'")
session1.run_sql("SET DEFAULT ROLE toprole FOR u_app@'%'")
# a role and a user of one name are two objects
session1.run_sql("CREATE ROLE dup")
session1.run_sql("GRANT INSERT ON rolesdb.t TO dup")
session1.run_sql("CREATE USER dup@'%' IDENTIFIED BY 'pwd'")
session1.run_sql("GRANT UPDATE ON rolesdb.t TO dup@'%'")
session1.run_sql("CREATE ROLE unused")
session1.run_sql("GRANT DELETE ON rolesdb.t TO unused WITH GRANT OPTION")
session1.run_sql("GRANT unused TO dup@'%' WITH ADMIN OPTION")

#@<> roles - an included account brings its roles
roles_dump = dump_dir_for("roles_included")
shell.connect(__sandbox_uri1)
EXPECT_NO_THROWS(lambda: util.dump_instance(roles_dump, { "includeSchemas": ["rolesdb"], "includeUsers": ["u_app"], "showProgress": False }), "dump")
EXPECT_STDOUT_CONTAINS("NOTE: 3 roles are granted to the accounts being dumped and have been added to the dump: `baserole`, `midrole`, `toprole`.")
EXPECT_STDOUT_NOT_CONTAINS("which is not included in the dump")

EXPECT_NO_THROWS(lambda: load(roles_dump, { "loadUsers": True }), "load")
EXPECT_STDOUT_CONTAINS("4 accounts were loaded")
for role in ["baserole", "midrole", "toprole"]:
    compare_user_grants(session1, session2, f"`{role}`")
compare_user_grants(session1, session2, "'u_app'@'%'")

# live, not just textually: the default role is active and the chain works
app = mysql.get_session(f"u_app:pwd@localhost:{__mysql_sandbox_port2}")
EXPECT_EQ("toprole", fetch_value(app, "SELECT CURRENT_ROLE()"))
EXPECT_EQ(2, fetch_value(app, "SELECT COUNT(*) FROM rolesdb.t"))
app.close()

#@<> roles - an explicitly excluded role is not pulled in
wipeout_server(session2)
roles_excluded_dump = dump_dir_for("roles_excluded")
shell.connect(__sandbox_uri1)
EXPECT_NO_THROWS(lambda: util.dump_instance(roles_excluded_dump, { "includeSchemas": ["rolesdb"], "includeUsers": ["u_app"], "excludeUsers": ["baserole"], "showProgress": False }), "dump")
EXPECT_STDOUT_CONTAINS("WARNING: Role `baserole` is granted to `midrole` but is excluded from the dump.")
EXPECT_STDOUT_CONTAINS("have been added to the dump: `midrole`, `toprole`.")
# the grant naming the excluded role is reported by the grants pass as well
EXPECT_STDOUT_CONTAINS("WARNING: User `midrole` has a grant statement on a role `baserole` which is not included in the dump")
# while the roles which were pulled in are not
EXPECT_STDOUT_NOT_CONTAINS("has a grant statement on a role `midrole`")

#@<> roles - every account round trips
wipeout_server(session2)
all_roles_dump = dump_dir_for("roles_all")
shell.connect(__sandbox_uri1)
EXPECT_NO_THROWS(lambda: util.dump_instance(all_roles_dump, { "includeSchemas": ["rolesdb"], "excludeUsers": ["root"], "showProgress": False }), "dump")
EXPECT_NO_THROWS(lambda: load(all_roles_dump, { "loadUsers": True }), "load")
compare_users(session1, session2)

#@<> roles - a second load reports a role as a duplicate
# the pre-existing object check runs only for a load which includes DDL
EXPECT_THROWS(lambda: load(all_roles_dump, { "loadUsers": True }), "Duplicate objects found in destination database")
EXPECT_STDOUT_CONTAINS("Role `toprole` already exists")

#@<> roles - dropExistingObjects really drops a role
# DROP USER is a no-op on a role, so only DROP ROLE removes this extra grant
session2.run_sql("GRANT DELETE ON rolesdb.t TO toprole")
EXPECT_NO_THROWS(lambda: load(all_roles_dump, { "loadUsers": True, "dropExistingObjects": True }), "load")
compare_users(session1, session2)

#@<> roles - excludeUsers skips a role on the load
wipeout_server(session2)
session2.run_sql("CREATE ROLE toprole")
session2.run_sql("GRANT SELECT ON mysql.db TO toprole")
EXPECT_NO_THROWS(lambda: load(all_roles_dump, { "loadUsers": True, "excludeUsers": ["toprole", "u_app"] }), "load")
EXPECT_STDOUT_CONTAINS("Skipping CREATE ROLE statements for user `toprole`")
EXPECT_EQ(["GRANT USAGE ON *.* TO `toprole`", "GRANT SELECT ON `mysql`.`db` TO `toprole`"], [row[0] for row in session2.run_sql("SHOW GRANTS FOR toprole").fetch_all()])

#@<> roles - cleanup
session1.run_sql("DROP SCHEMA rolesdb")
wipeout_users(session1)
wipeout_server(session2)

#@<> DENY - setup
# MariaDB 13.1.1+ DENY, which SHOW GRANTS reports next to the GRANTs at every
# level (MARIADB_DUMP_LOAD.md section 6.4). Each DENY overrides a
# GRANT it is paired with, so that the load is checked by effect, not only by
# SHOW GRANTS.
def supports_deny(session):
    session.run_sql("CREATE USER IF NOT EXISTS deny_probe@localhost")
    try:
        session.run_sql("DENY SELECT ON deny_probe_db.* TO deny_probe@localhost")
        return True
    except Exception:
        return False
    finally:
        # dropping the account drops its DENYs with it
        session.run_sql("DROP USER IF EXISTS deny_probe@localhost")

server_supports_deny = supports_deny(session1) and supports_deny(session2)

#@<> DENY - dump and load {server_supports_deny}
session1.run_sql("CREATE SCHEMA denydb")
session1.run_sql("CREATE TABLE denydb.t1 (c1 INT, c2 INT)")
session1.run_sql("INSERT INTO denydb.t1 VALUES (1, 1)")
session1.run_sql("CREATE FUNCTION denydb.f1() RETURNS INT DETERMINISTIC RETURN 1")
session1.run_sql("CREATE FUNCTION denydb.f2() RETURNS INT DETERMINISTIC RETURN 2")
session1.run_sql("CREATE USER u_deny@'%' IDENTIFIED BY 'pwd'")
session1.run_sql("GRANT SELECT, INSERT, UPDATE ON denydb.t1 TO u_deny@'%'")
session1.run_sql("GRANT EXECUTE ON denydb.* TO u_deny@'%'")
session1.run_sql("DENY INSERT ON denydb.* TO u_deny@'%'")
session1.run_sql("DENY UPDATE (c2) ON denydb.t1 TO u_deny@'%'")
session1.run_sql("DENY EXECUTE ON FUNCTION denydb.f1 TO u_deny@'%'")
session1.run_sql("DENY PROCESS ON *.* TO u_deny@'%'")
session1.run_sql("CREATE ROLE r_deny")
session1.run_sql("GRANT DELETE ON denydb.t1 TO r_deny")
session1.run_sql("DENY DELETE ON denydb.* TO r_deny")

deny_dump = dump_dir_for("deny")
shell.connect(__sandbox_uri1)
EXPECT_NO_THROWS(lambda: util.dump_instance(deny_dump, { "includeSchemas": ["denydb"], "includeUsers": ["u_deny", "r_deny"], "showProgress": False }), "dump")
with open(os.path.join(deny_dump, "@.users.sql"), encoding="utf-8") as f:
    users_sql = f.read()
EXPECT_CONTAINS("DENY INSERT ON `denydb`.* TO `u_deny`@`%`", users_sql)
EXPECT_CONTAINS("DENY DELETE ON `denydb`.* TO `r_deny`", users_sql)

EXPECT_NO_THROWS(lambda: load(deny_dump, { "loadUsers": True }), "load")
compare_user_grants(session1, session2, "'u_deny'@'%'")
compare_user_grants(session1, session2, "`r_deny`")

#@<> DENY - the loaded DENYs still override the GRANTs {server_supports_deny}
deny_user = mysql.get_session(f"u_deny:pwd@localhost:{__mysql_sandbox_port2}")
EXPECT_EQ(1, fetch_value(deny_user, "SELECT COUNT(*) FROM denydb.t1"))
EXPECT_THROWS(lambda: deny_user.run_sql("INSERT INTO denydb.t1 VALUES (2, 2)"), "denied")
EXPECT_NO_THROWS(lambda: deny_user.run_sql("UPDATE denydb.t1 SET c1 = 5"), "update c1")
EXPECT_THROWS(lambda: deny_user.run_sql("UPDATE denydb.t1 SET c2 = 5"), "denied")
EXPECT_EQ(2, fetch_value(deny_user, "SELECT denydb.f2()"))
EXPECT_THROWS(lambda: deny_user.run_sql("SELECT denydb.f1()"), "denied")
deny_user.close()

#@<> DENY - dropExistingObjects recreates them {server_supports_deny}
EXPECT_NO_THROWS(lambda: load(deny_dump, { "loadUsers": True, "dropExistingObjects": True }), "load")
compare_user_grants(session1, session2, "'u_deny'@'%'")
compare_user_grants(session1, session2, "`r_deny`")

#@<> DENY - cleanup {server_supports_deny}
session1.run_sql("DROP SCHEMA denydb")
wipeout_users(session1)
wipeout_server(session2)

#@<> missing authentication plugin - setup
# MARIADB_DUMP_LOAD.md section 6.5: an account whose plugin the target has not
# installed aborts the load, which says what to do about it
for s in [session1, session2]:
    if not fetch_value(s, "SELECT COUNT(*) FROM information_schema.PLUGINS WHERE PLUGIN_NAME = 'ed25519'"):
        s.run_sql("INSTALL SONAME 'auth_ed25519'")
session1.run_sql("CREATE USER a_plain@'%' IDENTIFIED BY 'pwd'")
session1.run_sql("CREATE USER z_ed25519@'%' IDENTIFIED VIA ed25519 USING PASSWORD('pwd')")
auth_dump = dump_dir_for("auth_plugin")
shell.connect(__sandbox_uri1)
EXPECT_NO_THROWS(lambda: util.dump_instance(auth_dump, { "includeSchemas": [], "includeUsers": ["a_plain", "z_ed25519"], "showProgress": False }), "dump")
session2.run_sql("UNINSTALL SONAME 'auth_ed25519'")

#@<> missing authentication plugin - the load says what to install
EXPECT_THROWS(lambda: load(auth_dump, { "loadUsers": True }), "Plugin 'ed25519' is not loaded")
EXPECT_STDOUT_CONTAINS("NOTE: The target server does not have the 'ed25519' authentication plugin installed, which the account 'z_ed25519'@'%' requires.")
EXPECT_STDOUT_CONTAINS("1 account was created before this failure and is left on the target.")

#@<> missing authentication plugin - loading again after installing it completes
session2.run_sql("INSTALL SONAME 'auth_ed25519'")
EXPECT_NO_THROWS(lambda: load(auth_dump, { "loadUsers": True, "resetProgress": False }), "load")
EXPECT_STDOUT_CONTAINS("2 accounts were loaded")
EXPECT_EQ("ed25519", fetch_value(session2, "SELECT plugin FROM mysql.user WHERE user = 'z_ed25519'"))

#@<> missing authentication plugin - cleanup
wipeout_users(session1)
wipeout_server(session2)
for s in [session1, session2]:
    s.run_sql("UNINSTALL SONAME 'auth_ed25519'")

#@<> GTID - setup
# MARIADB_DUMP_LOAD.md section 4: the dump carries gtid_current_pos, and the
# load restores it into gtid_slave_pos
wipeout_server(session1)
wipeout_server(session2)
session1.run_sql("CREATE SCHEMA gtidtest")
session1.run_sql("CREATE TABLE gtidtest.t (id INT PRIMARY KEY)")
session1.run_sql("INSERT INTO gtidtest.t VALUES (1)")
dumped_position = fetch_value(session1, "SELECT @@gtid_current_pos")
gtid_dump = dump_dir_for("gtid")
EXPECT_NO_THROWS(lambda: dump_schema("gtidtest", gtid_dump), "dump")
# two transactions the dump does not have
session1.run_sql("INSERT INTO gtidtest.t VALUES (2)")
session1.run_sql("INSERT INTO gtidtest.t VALUES (3)")

#@<> GTID - the dump records the position
manifest = read_manifest(gtid_dump)
EXPECT_EQ(dumped_position, manifest["gtidExecuted"])
EXPECT_FALSE(manifest["gtidExecutedInconsistent"])

#@<> GTID - replace provisions a replica
EXPECT_NO_THROWS(lambda: load(gtid_dump, { "updateGtidSet": "replace" }), "load")
EXPECT_EQ(dumped_position, fetch_value(session2, "SELECT @@gtid_slave_pos"))

session2.run_sql(f"CHANGE MASTER TO master_host = '127.0.0.1', master_port = {__mysql_sandbox_port1}, master_user = 'root', master_password = 'root', master_use_gtid = slave_pos")
session2.run_sql("START SLAVE")

# only the two missing transactions are replicated
for i in range(60):
    if fetch_value(session2, "SELECT @@gtid_slave_pos") == fetch_value(session1, "SELECT @@gtid_current_pos"):
        break
    time.sleep(1)
EXPECT_EQ(fetch_value(session1, "SELECT @@gtid_current_pos"), fetch_value(session2, "SELECT @@gtid_slave_pos"))
EXPECT_EQ(3, fetch_value(session2, "SELECT COUNT(*) FROM gtidtest.t"))

#@<> GTID - updateGtidSet is refused while replicating
EXPECT_THROWS(lambda: load(gtid_dump, { "updateGtidSet": "replace", "loadDdl": False, "loadData": False }), "The updateGtidSet option cannot be used while replication is running on the target server.")

session2.run_sql("STOP SLAVE")
session2.run_sql("RESET SLAVE ALL")

#@<> GTID - replace cannot drop a replicated domain
wipeout_server(session2)
session2.run_sql("SET GLOBAL gtid_slave_pos = '1-200-5'")
EXPECT_THROWS(lambda: load(gtid_dump, { "updateGtidSet": "replace" }), "The updateGtidSet:'replace' option can only be used if the dumped GTID position is a superset of the current value of gtid_slave_pos on target")

#@<> GTID - append adds the dumped domain
wipeout_server(session2)
session2.run_sql("SET GLOBAL gtid_slave_pos = '1-200-5'")
EXPECT_NO_THROWS(lambda: load(gtid_dump, { "updateGtidSet": "append" }), "load")
EXPECT_EQ(sorted((dumped_position + ",1-200-5").split(",")), sorted(fetch_value(session2, "SELECT @@gtid_slave_pos").split(",")))

#@<> GTID - append cannot overlap a domain
wipeout_server(session2)
session2.run_sql("SET GLOBAL gtid_slave_pos = '0-200-5'")
EXPECT_THROWS(lambda: load(gtid_dump, { "updateGtidSet": "append" }), "The updateGtidSet:'append' option can only be used if gtid_slave_pos on target server does not contain any of the replication domains")

#@<> GTID - cleanup
wipeout_server(session1)
wipeout_server(session2)

#@<> backup lock - setup
# MARIADB_DUMP_LOAD.md sections 3.1 and 3.3
session1.run_sql("CREATE SCHEMA locktest")
session1.run_sql("CREATE TABLE locktest.t (id INT PRIMARY KEY)")
session1.run_sql("INSERT INTO locktest.t VALUES (1)")

#@<> backup lock - a consistent dump takes it and releases it
lock_dump = dump_dir_for("backup_lock")
EXPECT_NO_THROWS(lambda: dump_schema("locktest", lock_dump), "dump")
EXPECT_STDOUT_CONTAINS("Locking instance for backup")
# a backup stage is a server-wide singleton, so taking one proves it was released
blocker = mysql.get_session(__sandbox_uri1)
EXPECT_NO_THROWS(lambda: blocker.run_sql("BACKUP STAGE START"), "stage released")
blocker.run_sql("BACKUP STAGE END")

#@<> backup lock - a dump which cannot take it says what is holding it {__dbug}
# the bound is five minutes, so the timeout itself is injected - what is under
# test is the report, and that the stage is ended rather than left half-taken
sleeper = testutil.call_mysqlsh_async([__sandbox_uri1, "--sql", "-e", "SELECT SLEEP(5) AS sleeper"])
for i in range(30):
    if fetch_value(session1, "SELECT COUNT(*) FROM information_schema.PROCESSLIST WHERE INFO LIKE 'SELECT SLEEP(5)%'"):
        break
    time.sleep(0.5)
testutil.set_trap("mysql", ["sql == BACKUP STAGE BLOCK_DDL"], { "code": 1205, "msg": "Lock wait timeout exceeded; try restarting transaction", "state": "HY000" })
lock_timeout_dump = dump_dir_for("backup_lock_timeout")
EXPECT_THROWS(lambda: dump_schema("locktest", lock_timeout_dump), "Lock wait timeout exceeded")
testutil.clear_traps("mysql")
testutil.wait_mysqlsh_async(sleeper, 30)
EXPECT_STDOUT_CONTAINS("NOTE: The backup lock waited 300 seconds for the locks it needs")
EXPECT_STDOUT_CONTAINS("seconds: SELECT SLEEP(5) AS sleeper")
EXPECT_STDOUT_CONTAINS("ERROR: Could not acquire the backup lock: MySQL Error 1205 (HY000): Lock wait timeout exceeded")
EXPECT_NO_THROWS(lambda: blocker.run_sql("BACKUP STAGE START"), "stage released")
blocker.run_sql("BACKUP STAGE END")

#@<> backup lock - consistent: false does not need it
blocker.run_sql("BACKUP STAGE START")
lock_free_dump = dump_dir_for("backup_lock_free")
EXPECT_NO_THROWS(lambda: dump_schema("locktest", lock_free_dump, { "consistent": False }), "dump")
EXPECT_STDOUT_NOT_CONTAINS("Locking instance for backup")
blocker.run_sql("BACKUP STAGE END")
blocker.close()

#@<> backup lock - cleanup
session1.run_sql("DROP SCHEMA locktest")

#@<> system versioning - setup
# MARIADB_DUMP_LOAD.md section 5.4. History is not carried: the dump
# reads the current version of every row, as mariadb-dump does without
# --dump-history, and says so for every such table.
session1.run_sql("CREATE SCHEMA sysver")
session1.run_sql("CREATE TABLE sysver.t1 (id INT PRIMARY KEY, a INT) WITH SYSTEM VERSIONING")
session1.run_sql("INSERT INTO sysver.t1 VALUES (1, 1), (2, 2)")
session1.run_sql("UPDATE sysver.t1 SET a = 10 WHERE id = 1")
session1.run_sql("CREATE TABLE sysver.p1 (id INT PRIMARY KEY, a INT) WITH SYSTEM VERSIONING PARTITION BY SYSTEM_TIME (PARTITION h0 HISTORY, PARTITION pc CURRENT)")
session1.run_sql("INSERT INTO sysver.p1 VALUES (1, 1), (2, 2)")
session1.run_sql("UPDATE sysver.p1 SET a = 10 WHERE id = 1")
session1.run_sql("CREATE TABLE sysver.t_novers (id INT PRIMARY KEY, a INT, b INT WITHOUT SYSTEM VERSIONING) WITH SYSTEM VERSIONING")
session1.run_sql("INSERT INTO sysver.t_novers VALUES (1, 1, 1)")
session1.run_sql("""CREATE TABLE sysver.t_rowcols (id INT PRIMARY KEY, a INT,
  rs TIMESTAMP(6) GENERATED ALWAYS AS ROW START, re TIMESTAMP(6) GENERATED ALWAYS AS ROW END,
  PERIOD FOR SYSTEM_TIME(rs, re)) WITH SYSTEM VERSIONING""")
session1.run_sql("INSERT INTO sysver.t_rowcols (id, a) VALUES (1, 1)")
session1.run_sql("CREATE VIEW sysver.v_all AS SELECT * FROM sysver.t1 FOR SYSTEM_TIME ALL")
session1.run_sql("CREATE VIEW sysver.v_asof AS SELECT * FROM sysver.t1 FOR SYSTEM_TIME AS OF TIMESTAMP '2037-01-01 00:00:00'")
session1.run_sql("CREATE VIEW sysver.v_between AS SELECT * FROM sysver.t1 FOR SYSTEM_TIME BETWEEN TIMESTAMP '1970-01-02 00:00:00' AND TIMESTAMP '2037-01-01 00:00:00'")
sysver_tables = ["t1", "p1", "t_novers", "t_rowcols"]

sysver_dump = dump_dir_for("system_versioning")
EXPECT_NO_THROWS(lambda: dump_schema("sysver", sysver_dump), "dump")
EXPECT_STDOUT_CONTAINS("WARNING: The definition of view `sysver`.`v_all` could not be parsed, so the tables it uses are unknown")
for table in sysver_tables:
    EXPECT_STDOUT_CONTAINS(f"WARNING: Table `sysver`.`{table}` is system-versioned: only its current rows are dumped, not its history. The loaded rows start a new history from the time of the load.")

#@<> system versioning - no history warning when no data is dumped
EXPECT_NO_THROWS(lambda: dump_schema("sysver", dump_dir_for("system_versioning_ddl"), { "ddlOnly": True }), "dump")
EXPECT_STDOUT_NOT_CONTAINS("is system-versioned")

#@<> system versioning - a partitioned table is dumped whole, without history
data_files = [f for f in os.listdir(sysver_dump) if f.startswith("sysver@p1@") and not f.endswith(".json") and not f.endswith(".sql")]
EXPECT_NE([], data_files)
EXPECT_EQ([], [f for f in data_files if "@h0@" in f or "@pc@" in f])

#@<> system versioning - round trip
EXPECT_NO_THROWS(lambda: load(sysver_dump), "load")
for table in sysver_tables:
    EXPECT_EQ(show_create_table(session1, "sysver", table), show_create_table(session2, "sysver", table), table)
    EXPECT_EQ("SYSTEM VERSIONED", fetch_value(session2, "SELECT TABLE_TYPE FROM information_schema.TABLES WHERE TABLE_SCHEMA = 'sysver' AND TABLE_NAME = ?", [table]), table)
    EXPECT_EQ(fetch_value(session1, "SELECT COUNT(*) FROM !.!", ["sysver", table]), fetch_value(session2, "SELECT COUNT(*) FROM !.!", ["sysver", table]), table)
EXPECT_EQ([[1, 10], [2, 2]], [list(r) for r in session2.run_sql("SELECT id, a FROM sysver.p1 ORDER BY id").fetch_all()])
# the history is not carried, so every view sees the current rows only
for view in ["v_all", "v_asof", "v_between"]:
    EXPECT_EQ(2, fetch_value(session2, "SELECT COUNT(*) FROM !.!", ["sysver", view]), view)

#@<> system versioning - its partitions cannot be selected
# MariaDB refuses partition selection on a system-versioned table, so the
# 'partitions' option cannot be honoured for one and is refused: ignoring it
# would dump the current rows for a request naming the HISTORY partition h0.
session1.run_sql("CREATE TABLE sysver.r (id INT PRIMARY KEY) PARTITION BY RANGE (id) (PARTITION a VALUES LESS THAN (10), PARTITION b VALUES LESS THAN MAXVALUE)")
session1.run_sql("INSERT INTO sysver.r VALUES (1), (20)")
shell.connect(__sandbox_uri1)
for parts in [["pc"], ["h0"]]:
    EXPECT_THROWS(lambda: util.dump_tables("sysver", ["p1", "r"], dump_dir_for("sysver_partitions"), { "partitions": { "sysver.p1": parts, "sysver.r": ["a"] }, "showProgress": False }), "Invalid partitions")
    EXPECT_STDOUT_CONTAINS("ERROR: Table 'sysver'.'p1' is system-versioned, and MariaDB does not allow selecting its partitions. Remove it from the 'partitions' option to dump the whole table (its current rows).")

# without it, the versioned table is dumped whole and the ordinary one by the
# partition asked for
sysver_partitions_dump = dump_dir_for("sysver_partitions")
EXPECT_NO_THROWS(lambda: util.dump_tables("sysver", ["p1", "r"], sysver_partitions_dump, { "partitions": { "sysver.r": ["a"] }, "showProgress": False }), "dump")
wipeout_server(session2)
EXPECT_NO_THROWS(lambda: load(sysver_partitions_dump), "load")
EXPECT_EQ([[1, 10], [2, 2]], [list(r) for r in session2.run_sql("SELECT id, a FROM sysver.p1 ORDER BY id").fetch_all()])
EXPECT_EQ([[1]], [list(r) for r in session2.run_sql("SELECT id FROM sysver.r").fetch_all()])

#@<> system versioning - cleanup
session1.run_sql("DROP SCHEMA sysver")
wipeout_server(session2)

#@<> MariaDB column types and table options - setup
# MARIADB_DUMP_LOAD.md section 5.5
session1.run_sql("CREATE SCHEMA feat")
session1.run_sql("CREATE TABLE feat.t_vector (id INT PRIMARY KEY, v VECTOR(3) NOT NULL, VECTOR INDEX (v))")
session1.run_sql("INSERT INTO feat.t_vector VALUES (1, VEC_FromText('[1,2,3]')), (2, VEC_FromText('[0.5,-1,2.25]'))")
session1.run_sql("CREATE TABLE feat.t_inet (id INT PRIMARY KEY, u UUID, i4 INET4, i6 INET6)")
session1.run_sql("INSERT INTO feat.t_inet VALUES (1, '123e4567-e89b-12d3-a456-426655440000', '192.168.0.1', '2001:db8::ff00:42:8329'), (2, UUID(), '10.0.0.255', '::ffff:10.0.0.1'), (3, NULL, NULL, NULL)")
session1.run_sql("CREATE TABLE feat.t_columns (id INT PRIMARY KEY, inv INT INVISIBLE DEFAULT 7, c TEXT COMPRESSED, g1 INT AS (id * 2) VIRTUAL, g2 INT AS (id * 3) PERSISTENT)")
session1.run_sql("INSERT INTO feat.t_columns (id, inv, c) VALUES (1, 70, REPEAT('compressed ', 100)), (2, DEFAULT, 'x')")
session1.run_sql("CREATE TABLE feat.t_ignored (id INT PRIMARY KEY, a INT, KEY ka (a) IGNORED)")
session1.run_sql("INSERT INTO feat.t_ignored VALUES (1, 1), (2, 2)")
session1.run_sql("CREATE TABLE feat.t_period (id INT, s DATE, e DATE, PERIOD FOR p(s, e))")
session1.run_sql("INSERT INTO feat.t_period VALUES (1, '2020-01-01', '2020-06-01'), (1, '2020-03-01', '2020-09-01')")
session1.run_sql("CREATE TABLE feat.t_overlaps (id INT, s DATE, e DATE, PERIOD FOR p(s, e), UNIQUE (id, p WITHOUT OVERLAPS))")
session1.run_sql("INSERT INTO feat.t_overlaps VALUES (1, '2020-01-01', '2020-06-01'), (1, '2020-06-01', '2020-09-01'), (2, '2020-01-01', '2020-06-01')")
session1.run_sql("CREATE TABLE feat.t_aria (id INT PRIMARY KEY, a VARCHAR(10)) ENGINE=Aria TRANSACTIONAL=1 PAGE_CHECKSUM=1 ROW_FORMAT=PAGE")
session1.run_sql("INSERT INTO feat.t_aria VALUES (1, 'a'), (2, 'b')")
session1.run_sql("CREATE TABLE feat.t_subpart (id INT, d DATE) PARTITION BY RANGE (YEAR(d)) SUBPARTITION BY HASH (id) SUBPARTITIONS 2 (PARTITION p0 VALUES LESS THAN (2020), PARTITION p1 VALUES LESS THAN MAXVALUE)")
session1.run_sql("INSERT INTO feat.t_subpart VALUES (1, '2019-01-01'), (2, '2019-06-01'), (3, '2021-01-01'), (4, '2021-06-01')")

feat_dump = dump_dir_for("features")
EXPECT_NO_THROWS(lambda: dump_schema("feat", feat_dump), "dump")

#@<> MariaDB column types and table options - round trip
EXPECT_NO_THROWS(lambda: load(feat_dump, { "threads": 4 }), "load")
EXPECT_JSON_EQ(snapshot_schema(session1, "feat"), snapshot_schema(session2, "feat"), "schema")
# the values the checksum reads through a conversion, compared as the server renders them
EXPECT_EQ(session1.run_sql("SELECT id, u, i4, i6 FROM feat.t_inet ORDER BY id").fetch_all().__str__(), session2.run_sql("SELECT id, u, i4, i6 FROM feat.t_inet ORDER BY id").fetch_all().__str__())
EXPECT_EQ("[1,2,3]", fetch_value(session2, "SELECT VEC_ToText(v) FROM feat.t_vector WHERE id = 1"))
EXPECT_EQ([[1, 70, 2, 3], [2, 7, 4, 6]], [list(r) for r in session2.run_sql("SELECT id, inv, g1, g2 FROM feat.t_columns ORDER BY id").fetch_all()])

#@<> MariaDB column types and table options - cleanup
session1.run_sql("DROP SCHEMA feat")
wipeout_server(session2)

#@<> feature combinations - setup
# MARIADB_DUMP_LOAD.md section 5.8: PAGE_COMPRESSED
# tables, dynamic columns, and features in combination rather than one per
# table. Each table must come back with the same DDL and data, and the load
# verifies the dump's checksums.
session1.run_sql("CREATE SCHEMA combo")
combo_setup = [
    # PAGE_COMPRESSED, also with subpartitions
    "CREATE TABLE combo.pc (id INT PRIMARY KEY, v TEXT) PAGE_COMPRESSED=1 PAGE_COMPRESSION_LEVEL=9",
    "INSERT INTO combo.pc SELECT seq, REPEAT('abc', 200) FROM combo.seq_1_to_2000",
    "CREATE TABLE combo.spc (id INT, d DATE) PAGE_COMPRESSED=1 PARTITION BY RANGE (YEAR(d)) SUBPARTITION BY HASH (id) SUBPARTITIONS 2 (PARTITION p0 VALUES LESS THAN (2020), PARTITION p1 VALUES LESS THAN MAXVALUE)",
    "INSERT INTO combo.spc VALUES (1, '2019-01-01'), (2, '2021-01-01'), (3, '2022-01-01')",
    # dynamic columns are a BLOB holding MariaDB's packed format
    "CREATE TABLE combo.dc (id INT PRIMARY KEY, attrs BLOB)",
    "INSERT INTO combo.dc VALUES (1, COLUMN_CREATE('color', 'blue', 'size', 10, 'price', 9.99)), (2, COLUMN_CREATE('nested', COLUMN_CREATE('a', 1))), (3, NULL)",
    # system versioning with an application-time period and a WITHOUT OVERLAPS key
    "CREATE TABLE combo.vpo (id INT, s DATE, e DATE, v INT, PERIOD FOR p(s, e), UNIQUE (id, p WITHOUT OVERLAPS)) WITH SYSTEM VERSIONING",
    "INSERT INTO combo.vpo VALUES (1, '2020-01-01', '2020-06-01', 1), (1, '2020-06-01', '2021-01-01', 2)",
    "UPDATE combo.vpo SET v = 5 WHERE v = 2",
    # versioned and partitioned, with a COMPRESSED and an INVISIBLE column
    "CREATE TABLE combo.vpc (id INT PRIMARY KEY, c TEXT COMPRESSED, inv INT INVISIBLE DEFAULT 3) WITH SYSTEM VERSIONING PARTITION BY SYSTEM_TIME (PARTITION h HISTORY, PARTITION c CURRENT)",
    "INSERT INTO combo.vpc (id, c) VALUES (1, REPEAT('z', 500)), (2, 'x')",
    "UPDATE combo.vpc SET c = 'y' WHERE id = 2",
    # a sequence DEFAULT beside UUID, INET6 and a vector index
    "CREATE SEQUENCE combo.sq NOCACHE",
    "CREATE TABLE combo.svu (id INT PRIMARY KEY DEFAULT (NEXT VALUE FOR combo.sq), u UUID NOT NULL, i6 INET6, v VECTOR(2) NOT NULL, VECTOR INDEX (v), UNIQUE (u))",
    "INSERT INTO combo.svu (u, i6, v) VALUES (UUID(), '::1', VEC_FromText('[1,2]')), (UUID(), NULL, VEC_FromText('[3,4]'))",
    # a CHECK constraint on a generated column over JSON
    "CREATE TABLE combo.cgj (id INT PRIMARY KEY, j JSON, k INT AS (JSON_VALUE(j, '$.k')) PERSISTENT, CHECK (k > 0))",
    "INSERT INTO combo.cgj (id, j) VALUES (1, '{\"k\": 5}')",
    # a system-versioned Aria table
    "CREATE TABLE combo.av (id INT PRIMARY KEY, a INT) ENGINE=Aria WITH SYSTEM VERSIONING",
    "INSERT INTO combo.av VALUES (1, 1)",
]
for statement in combo_setup:
    session1.run_sql(statement)
combo_tables = ["pc", "spc", "dc", "vpo", "vpc", "svu", "cgj", "av"]

#@<> feature combinations - round trip, verified by checksum
combo_dump = dump_dir_for("combo")
EXPECT_NO_THROWS(lambda: dump_schema("combo", combo_dump, { "checksum": True }), "dump")
wipeout_server(session2)
EXPECT_NO_THROWS(lambda: load(combo_dump, { "checksum": True }), "load")
EXPECT_STDOUT_NOT_CONTAINS("Checksum verification failed")
for table in combo_tables:
    EXPECT_EQ(show_create_table(session1, "combo", table), show_create_table(session2, "combo", table), table)
    EXPECT_EQ(md5_table(session1, "combo", table), md5_table(session2, "combo", table), table)
EXPECT_EQ(2000, fetch_value(session2, "SELECT COUNT(*) FROM combo.pc"))
# the packed format survived as bytes, not only as a checksum
EXPECT_EQ("blue", fetch_value(session2, "SELECT COLUMN_GET(attrs, 'color' AS CHAR) FROM combo.dc WHERE id = 1"))
EXPECT_EQ(1, fetch_value(session2, "SELECT COLUMN_GET(COLUMN_GET(attrs, 'nested' AS BINARY), 'a' AS INT) FROM combo.dc WHERE id = 2"))

#@<> feature combinations - cleanup
session1.run_sql("DROP SCHEMA combo")
wipeout_server(session2)

#@<> encrypted tables - setup
# needs the file_key_management plugin, which a trimmed server package does not
# ship: these chunks run only where the plugin is there. Both the source and the
# target get their own key file, as encryption is per server - the dump carries
# the ENCRYPTED / ENCRYPTION_KEY_ID options, never the data encrypted.
server_has_key_management = os.path.exists(os.path.join(fetch_value(session1, "SELECT @@plugin_dir"), "file_key_management.so"))
if server_has_key_management:
    key_file = os.path.join(outdir, "keys.txt")
    with open(key_file, "w", encoding="ascii") as f:
        f.write("1;" + "a" * 64 + "\n2;" + "b" * 64 + "\n")
    for port in [__mysql_sandbox_port3, __mysql_sandbox_port4]:
        testutil.deploy_sandbox(port, "root", { "local_infile": "1", "plugin_load_add": "file_key_management", "file_key_management_filename": key_file })
    enc_source = mysql.get_session(__sandbox_uri3)
    enc_target = mysql.get_session(__sandbox_uri4)

#@<> encrypted tables - round trip {server_has_key_management}
enc_source.run_sql("CREATE SCHEMA encdb")
enc_source.run_sql("CREATE TABLE encdb.t1 (id INT PRIMARY KEY, v VARCHAR(20)) ENCRYPTED=YES")
enc_source.run_sql("CREATE TABLE encdb.t2 (id INT PRIMARY KEY, v VARCHAR(20)) ENCRYPTED=YES ENCRYPTION_KEY_ID=2")
enc_source.run_sql("INSERT INTO encdb.t1 VALUES (1, 'secret'), (2, 'more')")
enc_source.run_sql("INSERT INTO encdb.t2 VALUES (1, 'key two')")
enc_dump = dump_dir_for("encrypted")
shell.connect(__sandbox_uri3)
EXPECT_NO_THROWS(lambda: util.dump_schemas(["encdb"], enc_dump, { "checksum": True, "showProgress": False }), "dump")
shell.connect(__sandbox_uri4)
EXPECT_NO_THROWS(lambda: util.load_dump(enc_dump, { "checksum": True, "showProgress": False }), "load")
for table in ["t1", "t2"]:
    EXPECT_EQ(show_create_table(enc_source, "encdb", table), show_create_table(enc_target, "encdb", table), table)
    EXPECT_EQ(md5_table(enc_source, "encdb", table), md5_table(enc_target, "encdb", table), table)
EXPECT_CONTAINS("`ENCRYPTION_KEY_ID`=2", show_create_table(enc_target, "encdb", "t2"))

#@<> encrypted tables - a target without key management refuses them {server_has_key_management}
# MARIADB_DUMP_LOAD.md section 5.8: the server's own refusal, which does not
# mention encryption
wipeout_server(session2)
shell.connect(__sandbox_uri2)
EXPECT_THROWS(lambda: util.load_dump(enc_dump, { "showProgress": False, "resetProgress": True }), "Error loading dump")
EXPECT_STDOUT_CONTAINS("Can't create table `encdb`.`t1` (errno: 140 \"Wrong create options\")")

#@<> encrypted tables - cleanup {server_has_key_management}
enc_source.close()
enc_target.close()
for port in [__mysql_sandbox_port3, __mysql_sandbox_port4]:
    testutil.destroy_sandbox(port)
wipeout_server(session2)

#@<> XMLTYPE - setup
# MariaDB 13.1.1+: stored like a LONGBLOB but with a character set, so it is
# dumped as text (MARIADB_DUMP_LOAD.md section 5.5).
session1.run_sql("CREATE SCHEMA xmldb")
try:
    session1.run_sql("CREATE TABLE xmldb.t (id INT PRIMARY KEY, x XMLTYPE)")
    server_supports_xmltype = True
except Exception:
    server_supports_xmltype = False
    session1.run_sql("DROP SCHEMA xmldb")

#@<> XMLTYPE - round trip, verified by checksum {server_supports_xmltype}
session1.run_sql("""INSERT INTO xmldb.t VALUES (1, '<a b="c">d</a>'), (2, '<r><n>\u00e9\u00e8 \u4e2d\u6587</n><e/></r>'), (3, NULL)""")
xml_dump = dump_dir_for("xmltype")
EXPECT_NO_THROWS(lambda: dump_schema("xmldb", xml_dump, { "checksum": True }), "dump")
EXPECT_NO_THROWS(lambda: load(xml_dump, { "checksum": True }), "load")
EXPECT_STDOUT_NOT_CONTAINS("Checksum verification failed")
EXPECT_EQ(show_create_table(session1, "xmldb", "t"), show_create_table(session2, "xmldb", "t"))
EXPECT_EQ(md5_table(session1, "xmldb", "t"), md5_table(session2, "xmldb", "t"))

#@<> XMLTYPE - cleanup {server_supports_xmltype}
session1.run_sql("DROP SCHEMA xmldb")
wipeout_server(session2)

#@<> schema case of routines and events - setup
# MARIADB_DUMP_LOAD.md section 5.9: with lower_case_table_names=2, MariaDB
# reports the schema of a routine and of an event lower-cased, the schema of a
# table as given
lower_case_table_names_2 = 2 == fetch_value(session1, "SELECT @@lower_case_table_names")

if lower_case_table_names_2:
    session1.run_sql("CREATE SCHEMA CaseT")
    session1.run_sql("CREATE TABLE CaseT.Contacts (id INT PRIMARY KEY, f_name VARCHAR(45), l_name VARCHAR(45))")
    session1.run_sql("INSERT INTO CaseT.Contacts VALUES (1, 'John', 'Doe')")
    session1.run_sql("CREATE FUNCTION CaseT.format_name(a VARCHAR(45), b VARCHAR(45)) RETURNS VARCHAR(91) DETERMINISTIC RETURN CONCAT(a, ' ', b)")
    session1.run_sql("CREATE PROCEDURE CaseT.Proc1() SELECT 1")
    session1.run_sql("CREATE EVENT CaseT.Ev1 ON SCHEDULE EVERY 1 DAY DISABLE DO SELECT 1")
    # the symptom: the load fails on a view which calls a routine left out
    session1.run_sql("CREATE SQL SECURITY INVOKER VIEW CaseT.CN AS SELECT CaseT.format_name(f_name, l_name) AS name, id FROM CaseT.Contacts")

def EXPECT_CASE_SCHEMA_OBJECTS(session):
    EXPECT_EQ([["format_name", "FUNCTION"], ["Proc1", "PROCEDURE"]], [list(r) for r in session.run_sql("SELECT ROUTINE_NAME, ROUTINE_TYPE FROM information_schema.ROUTINES WHERE ROUTINE_SCHEMA = 'CaseT' ORDER BY ROUTINE_NAME").fetch_all()])
    EXPECT_EQ(["Ev1"], [r[0] for r in session.run_sql("SELECT EVENT_NAME FROM information_schema.EVENTS WHERE EVENT_SCHEMA = 'CaseT'").fetch_all()])
    EXPECT_EQ("John Doe", fetch_value(session, "SELECT name FROM CaseT.CN"))

#@<> schema case of routines and events - the dump carries them {lower_case_table_names_2}
case_dump = dump_dir_for("schema_case")
EXPECT_NO_THROWS(lambda: dump_schema("CaseT", case_dump), "dump")
manifest = read_manifest(case_dump, "CaseT.json")
EXPECT_EQ(["format_name"], manifest["functions"])
EXPECT_EQ(["Proc1"], manifest["procedures"])
EXPECT_EQ(["Ev1"], manifest["events"])

#@<> schema case of routines and events - round trip {lower_case_table_names_2}
EXPECT_NO_THROWS(lambda: load(case_dump), "load")
EXPECT_CASE_SCHEMA_OBJECTS(session2)

#@<> schema case of routines and events - the routine and event filters match the schema {lower_case_table_names_2}
filtered_dump = dump_dir_for("schema_case_filtered")
EXPECT_NO_THROWS(lambda: dump_schema("CaseT", filtered_dump, { "includeRoutines": ["CaseT.format_name"], "excludeEvents": ["CaseT.Ev1"] }), "dump")
manifest = read_manifest(filtered_dump, "CaseT.json")
EXPECT_EQ(["format_name"], manifest["functions"])
EXPECT_EQ([], manifest["procedures"])
EXPECT_EQ([], manifest["events"])

#@<> schema case of routines and events - util.copySchemas carries them {lower_case_table_names_2}
wipeout_server(session2)
shell.connect(__sandbox_uri1)
EXPECT_NO_THROWS(lambda: util.copy_schemas(["CaseT"], __sandbox_uri2, { "showProgress": False }), "copy")
EXPECT_CASE_SCHEMA_OBJECTS(session2)

#@<> schema case of routines and events - cleanup {lower_case_table_names_2}
session1.run_sql("DROP SCHEMA CaseT")
wipeout_server(session2)

#@<> an unknown column type stops the dump and says where {__dbug}
# the list of types is closed on purpose: a type nobody has checked may not
# round-trip as text or as bytes, and a guess could change data silently
session1.run_sql("CREATE SCHEMA unk")
session1.run_sql("CREATE TABLE unk.t (x INT)")
testutil.dbug_set("+d,dumper_unknown_column_type")
EXPECT_THROWS(lambda: dump_schema("unk", dump_dir_for("unknown_type")), "Column `x` of `unk`.`t` has type UNKNOWNTYPE, which this version of the Shell cannot dump safely. Exclude it with the 'excludeTables' option to dump the rest.")
testutil.dbug_set("")
session1.run_sql("DROP SCHEMA unk")

#@<> an older dump format names the Shell which created the dump
# only MariaDB Shell records the source vendor, so its presence tells the two
# Shells' dumps apart
session1.run_sql("CREATE SCHEMA oldfmt")
session1.run_sql("CREATE TABLE oldfmt.t (id INT PRIMARY KEY)")
old_format_dump = dump_dir_for("old_format")
dump_schema("oldfmt", old_format_dump)
with open(os.path.join(old_format_dump, "oldfmt.sql"), encoding="utf-8") as f:
    EXPECT_TRUE(f.readline().startswith("-- MariaDB Shell dump "), "SQL file header names MariaDB Shell")
manifest = read_manifest(old_format_dump)
EXPECT_EQ("mariadb", manifest["source"]["vendor"])
manifest["version"] = "1.0.0"
with open(os.path.join(old_format_dump, "@.json"), "w", encoding="utf-8") as f:
    json.dump(manifest, f)
EXPECT_NO_THROWS(lambda: load(old_format_dump), "load")
EXPECT_STDOUT_CONTAINS("NOTE: Dump format has version 1.0.0 and was created by an older version of MariaDB Shell. If you experience problems using it, please recreate the dump using the current version of MariaDB Shell and try again.")
session1.run_sql("DROP SCHEMA oldfmt")
wipeout_server(session2)

#@<> Cleanup
testutil.destroy_sandbox(__mysql_sandbox_port1)
testutil.destroy_sandbox(__mysql_sandbox_port2)
wipe_dir(outdir)
