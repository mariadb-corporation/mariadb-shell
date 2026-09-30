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
# MARIADB_DUMP_LOAD.md section 16. NOCACHE keeps the positions predictable: a
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
# MARIADB_DUMP_LOAD.md section 24: SHOW CREATE TABLE qualifies the sequence with
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
# MARIADB_DUMP_LOAD.md section 17: a MariaDB table can hold rows its own CHECK
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

#@<> packages - setup
# MARIADB_DUMP_LOAD.md section 19: a package has its own namespace, so a
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
# MARIADB_DUMP_LOAD.md sections 20 and 26: MariaDB roles are hostless, only
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

#@<> missing authentication plugin - setup
# MARIADB_DUMP_LOAD.md section 25: an account whose plugin the target has not
# installed still aborts the load, which now says what to do about it
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
# MARIADB_DUMP_LOAD.md section 15: the dump carries gtid_current_pos, and the
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
# MARIADB_DUMP_LOAD.md sections 14 and 29
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
# MARIADB_DUMP_LOAD.md sections 27 and 30. History is not carried: the dump
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

#@<> system versioning - cleanup
session1.run_sql("DROP SCHEMA sysver")
wipeout_server(session2)

#@<> MariaDB column types and table options - setup
# MARIADB_DUMP_LOAD.md section 31
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

#@<> Cleanup
testutil.destroy_sandbox(__mysql_sandbox_port1)
testutil.destroy_sandbox(__mysql_sandbox_port2)
wipe_dir(outdir)
