#@ {supports_dynamic_data_masking("mysql://" + __mysqluripwd)}

#@<> INCLUDE dump_utils.inc

#@<> Setup
import os.path
import re
import shutil

def initialize_sandbox(uri, options = {}):
    port = shell.parse_uri(uri)["port"]
    testutil.deploy_sandbox(port, "root", options)
    testutil.wait_sandbox_alive(uri)
    sess = mysql.get_session(uri)
    sess.run_sql("SET NAMES 'utf8mb4'")
    install_dynamic_data_masking(uri, sess=sess)
    return sess

src_session = initialize_sandbox(__sandbox_uri1)
tgt_session = initialize_sandbox(__sandbox_uri2, { "local_infile": 1, "innodb_doublewrite": "OFF" })

current_user = f"'{__user}'@'{__host}'"

target_schema = "ddm"
src_session.run_sql("CREATE SCHEMA !", [target_schema])

table_with_allow_role_policy = "user_data_1"
table_with_deny_role_policy = "user_data_2"
table_with_allow_user_policy = "user_data_3"
table_with_deny_user_policy = "user_data_4"
table_without_policy = "user_data_5"

table_with_allow_role_policy_quoted = quote_identifier(target_schema, table_with_allow_role_policy)
table_with_deny_role_policy_quoted = quote_identifier(target_schema, table_with_deny_role_policy)
table_with_allow_user_policy_quoted = quote_identifier(target_schema, table_with_allow_user_policy)
table_with_deny_user_policy_quoted = quote_identifier(target_schema, table_with_deny_user_policy)
table_without_policy_quoted = quote_identifier(target_schema, table_without_policy)

outdir = os.path.join(__tmp_dir, "ddm-test")
wipe_dir(outdir)
testutil.mkdir(outdir)

dump_dir = os.path.join(outdir, "wl17279")
metadata_file = os.path.join(dump_dir, "@.json")
ddm_file = os.path.join(dump_dir, "@.ddm.sql")

def prepare_for_dump():
    global dump_dir
    shutil.rmtree(dump_dir, True)
    shell.connect(__sandbox_uri1)

def prepare_for_copy(wipe_target=True):
    if wipe_target:
        wipeout_server(tgt_session)
    shell.connect(__sandbox_uri1)

def prepare_error_msg(error, msg):
    is_re = is_re_instance(msg)
    full_msg = "{0}: {1}".format(re.escape(error) if is_re else error, msg.pattern if is_re else msg)
    if is_re:
        full_msg = re.compile("^" + full_msg)
    return full_msg

def grant_ddm_privilege(sess):
    sess.run_sql(f"GRANT MANAGE_DATA_MASKING_POLICY ON *.* TO {current_user}")

def revoke_ddm_privilege(sess):
    sess.run_sql(f"REVOKE MANAGE_DATA_MASKING_POLICY ON *.* FROM {current_user}")

def EXPECT_SUCCESS(options = {}):
    global dump_dir
    global target_schema
    if not "includeSchemas" in options and not "excludeSchemas" in options:
        options["includeSchemas"] = [target_schema]
    if not "showProgress" in options:
        options["showProgress"] = False
    prepare_for_dump()
    WIPE_STDOUT()
    EXPECT_NO_THROWS(lambda: util.dump_instance(dump_dir, options))

def EXPECT_FAIL(error, msg, options = {}):
    global dump_dir
    if not "showProgress" in options:
        options["showProgress"] = False
    prepare_for_dump()
    WIPE_STDOUT()
    EXPECT_THROWS(lambda: util.dump_instance(dump_dir, options), prepare_error_msg(error, msg))

def EXPECT_DDM_DUMPED():
    global metadata_file
    global ddm_file
    # WL17279-FR1.4 - capability is present
    EXPECT_CAPABILITIES(metadata_file, [ dynamic_data_masking_capability ])
    # WL17279 - SQL file is written
    EXPECT_TRUE(os.path.exists(ddm_file))

def EXPECT_DDM_NOT_DUMPED():
    global metadata_file
    global ddm_file
    # WL17279-FR1.4 - capability is not present
    EXPECT_NO_CAPABILITIES(metadata_file, [ dynamic_data_masking_capability ])
    # WL17279 - SQL file is not written
    EXPECT_FALSE(os.path.exists(ddm_file))

def EXPECT_LOAD_SUCCESS(options = {}, wipe_target=True):
    global dump_dir
    if not "showProgress" in options:
        options["showProgress"] = False
    if not "resetProgress" in options:
        options["resetProgress"] = True
    shell.connect(__sandbox_uri2)
    if wipe_target:
        wipeout_server(session)
    WIPE_STDOUT()
    EXPECT_NO_THROWS(lambda: util.load_dump(dump_dir, options))

def EXPECT_LOAD_FAIL(error, msg, options = {}, wipe_target=True):
    global dump_dir
    if not "showProgress" in options:
        options["showProgress"] = False
    if not "resetProgress" in options:
        options["resetProgress"] = True
    shell.connect(__sandbox_uri2)
    if wipe_target:
        wipeout_server(session)
    WIPE_STDOUT()
    EXPECT_THROWS(lambda: util.load_dump(dump_dir, options), prepare_error_msg(error, msg))

#@<> WL17279-FR1.1 - 'dataMaskingPolicies' option - option is explicitly enabled, but there are no policies
EXPECT_SUCCESS({ "dataMaskingPolicies": True })
EXPECT_DDM_NOT_DUMPED()

#@<> WL17279-FR1 - create some policies
src_session.run_sql("""CREATE MASKING POLICY mask_ssn(ssn)
  CASE WHEN CURRENT_ROLE_IN('gdpr')
       THEN ssn
       ELSE ssn % 1000
  END""")

src_session.run_sql("""CREATE MASKING POLICY unmask_ssn(ssn)
  CASE WHEN CURRENT_ROLE_IN('dev')
       THEN ssn % 1000
       ELSE (ssn)
  END""")

src_session.run_sql("""CREATE MASKING POLICY mask_ssn_user(ssn)
  CASE WHEN CURRENT_USER_IN("'admin'@'10.20.30.40'")
       THEN ssn
       ELSE ssn % 1000
  END""")

src_session.run_sql(f"""CREATE MASKING POLICY unmask_ssn_user(ssn)
  CASE WHEN CURRENT_USER_IN("{current_user}")
       THEN ssn % 1000
       ELSE (ssn)
  END""")

# create and grant role which has access to unmasked data
src_session.run_sql("CREATE ROLE gdpr")
src_session.run_sql(f"GRANT gdpr TO {current_user}")

# create and grant role which does not have access to unmasked data
src_session.run_sql("CREATE ROLE dev")
src_session.run_sql(f"GRANT dev TO {current_user}")

# set this role as active
src_session.run_sql(f"SET DEFAULT ROLE dev TO {current_user}")

#@<> WL17279-FR1.1.1 - 'dataMaskingPolicies' option - default value is True
EXPECT_SUCCESS()
EXPECT_STDOUT_CONTAINS("4 data masking policies will be dumped.")
EXPECT_DDM_DUMPED()

#@<> WL17279-FR1.1 - 'dataMaskingPolicies' option - option is explicitly disabled
EXPECT_SUCCESS({ "dataMaskingPolicies": False })
EXPECT_STDOUT_NOT_CONTAINS("masking policies")
EXPECT_DDM_NOT_DUMPED()

#@<> WL17279-FR1 - create tables which use policies
src_session.run_sql("""CREATE TABLE !.!(
  id INT NOT NULL,
  `ssn-mask` INT NOT NULL MASKING POLICY mask_ssn
)""", [target_schema, table_with_allow_role_policy])

src_session.run_sql("""CREATE TABLE !.!(
  id INT NOT NULL,
  `ssn-unmask` INT NOT NULL MASKING POLICY unmask_ssn
)""", [target_schema, table_with_deny_role_policy])

src_session.run_sql("""CREATE TABLE !.!(
  id INT NOT NULL,
  `ssn-mask` INT NOT NULL MASKING POLICY mask_ssn_user
)""", [target_schema, table_with_allow_user_policy])

src_session.run_sql("""CREATE TABLE !.!(
  id INT NOT NULL,
  `ssn-unmask` INT NOT NULL MASKING POLICY unmask_ssn_user
)""", [target_schema, table_with_deny_user_policy])

# regular table
src_session.run_sql("""CREATE TABLE !.!(
  id INT NOT NULL,
  ssn INT NOT NULL
)""", [target_schema, table_without_policy])

for table in [table_with_allow_role_policy, table_with_deny_role_policy, table_with_allow_user_policy, table_with_deny_user_policy, table_without_policy]:
    src_session.run_sql("INSERT INTO !.! VALUES (1, 112233445), (2, 566778899), (3, 101112131)", [target_schema, table])

#@<> WL17279-FR1.2 - 'allowDataMasking' option - default value is False
EXPECT_FAIL("Shell Error (52042)", "Unable to dump unmasked table data")

EXPECT_STDOUT_CONTAINS(f"""
ERROR: One or more data masking policies deny access to table data.

Dump contains tables which use dynamic data masking, while current user {current_user} and its active role ('dev'@'%') do not have access to unmasked data due to the following policies:
 * Policy `mask_ssn` allows access for role: 'gdpr'@'%'
 * Policy `mask_ssn_user` allows access for user: 'admin'@'10.20.30.40'
 * Policy `unmask_ssn` denies access for role: 'dev'@'%'
 * Policy `unmask_ssn_user` denies access for user: {current_user}

To continue with the dump you must do one of the following:
 * Use an account which fulfills all the listed policies and ensure that all required roles are activated when a new session is created.
 * Enable the "allowDataMasking" dump option to ignore this issue. Please note that dump will contain masked data.
""")

#@<> WL17279-FR1.2 - 'allowDataMasking' option - explicitly set to True
# remove all default roles
src_session.run_sql(f"SET DEFAULT ROLE NONE TO {current_user}")

EXPECT_FAIL("Shell Error (52042)", "Unable to dump unmasked table data", {"dataMaskingPolicies": True})

EXPECT_STDOUT_CONTAINS(f"""
ERROR: One or more data masking policies deny access to table data.

Dump contains tables which use dynamic data masking, while current user {current_user} does not have access to unmasked data due to the following policies:
 * Policy `mask_ssn` allows access for role: 'gdpr'@'%'
 * Policy `mask_ssn_user` allows access for user: 'admin'@'10.20.30.40'
 * Policy `unmask_ssn_user` denies access for user: {current_user}

To continue with the dump you must do one of the following:
 * Use an account which fulfills all the listed policies and ensure that all required roles are activated when a new session is created.
 * Enable the "allowDataMasking" dump option to ignore this issue. Please note that dump will contain masked data.
""")

#@<> WL17279-FR1.2 - 'allowDataMasking' option - explicitly set to True and use a role which has access to unmasked data
# remove all default roles
src_session.run_sql(f"SET DEFAULT ROLE gdpr TO {current_user}")

EXPECT_FAIL("Shell Error (52042)", "Unable to dump unmasked table data", {"dataMaskingPolicies": True})

EXPECT_STDOUT_CONTAINS(f"""
ERROR: One or more data masking policies deny access to table data.

Dump contains tables which use dynamic data masking, while current user {current_user} and its active role ('gdpr'@'%') do not have access to unmasked data due to the following policies:
 * Policy `mask_ssn_user` allows access for user: 'admin'@'10.20.30.40'
 * Policy `unmask_ssn_user` denies access for user: {current_user}

To continue with the dump you must do one of the following:
 * Use an account which fulfills all the listed policies and ensure that all required roles are activated when a new session is created.
 * Enable the "allowDataMasking" dump option to ignore this issue. Please note that dump will contain masked data.
""")

#@<> WL17279-FR1.2 - 'allowDataMasking' option - when set to True, dump succeeds
EXPECT_SUCCESS({ "dataMaskingPolicies": False, "allowDataMasking": True })

#@<> WL17279-FR1.3 - revoke MANAGE_DATA_MASKING_POLICY privilege
revoke_ddm_privilege(src_session)

#@<> WL17279-FR1.3 - missing privilege and dumping policies
EXPECT_FAIL("Shell Error (52007)", f"User {current_user} is missing the following global privilege(s): MANAGE_DATA_MASKING_POLICY.", { "dataMaskingPolicies": True })

#@<> WL17279-FR1.3 - missing privilege and not dumping policies
EXPECT_SUCCESS({ "dataMaskingPolicies": False, "allowDataMasking": True })

#@<> WL17279-FR1.3 - missing privilege, not dumping policies, dumping tables which use policies and do not allow data to be masked
EXPECT_FAIL("Shell Error (52041)", "The current user does not have the 'MANAGE_DATA_MASKING_POLICY' policy, unable to check whether dumped data will be masked.", { "dataMaskingPolicies": False, "allowDataMasking": False })

#@<> WL17279-FR1.3 - missing privilege, not dumping policies, not dumping tables which use policies and do not allow data to be masked
EXPECT_SUCCESS({ "dataMaskingPolicies": False, "allowDataMasking": False, "includeTables": [table_without_policy_quoted] })
EXPECT_DDM_NOT_DUMPED()

#@<> WL17279-FR1.3 - missing privilege, not dumping policies, dumping tables which use policies and allow data to be masked
EXPECT_SUCCESS({ "dataMaskingPolicies": False, "allowDataMasking": True, "targetVersion": "9.6.0" })

# WL17279-FR1.4 - capability is present
EXPECT_CAPABILITIES(metadata_file, [ dynamic_data_masking_capability ])

# WL17279 - SQL file is not written
EXPECT_FALSE(os.path.exists(ddm_file))

# WL17279-FR1.4.1 - capability is present, 'targetVersion' does not support DDM, a warning is reported
EXPECT_STDOUT_CONTAINS("WARNING: The dump contains dynamic data masking DDL, however the 'targetVersion' option is set to 9.6.0, where this feature is not supported.")

# WL17279 - dump has tables with masked columns and data is allowed to be masked, a warning is reported
EXPECT_STDOUT_CONTAINS("WARNING: Dump contains tables with masked columns, current user does not have the 'MANAGE_DATA_MASKING_POLICY' privilege and the 'allowDataMasking' option is enabled, skipping unmasked data access check.")

#@<> WL17279-FR1.3 - missing privilege, not dumping policies, not dumping tables which use policies and allow data to be masked
EXPECT_SUCCESS({ "dataMaskingPolicies": False, "allowDataMasking": True, "includeTables": [table_without_policy_quoted], "targetVersion": "9.6.0" })
EXPECT_DDM_NOT_DUMPED()

# WL17279-FR1.4.1 - capability is not present, 'targetVersion' does not support DDM, a warning is not reported
# WL17279 - dump does not have tables with masked columns and data is allowed to be masked, a warning is not reported
EXPECT_STDOUT_NOT_CONTAINS("mask")

#@<> WL17279-FR1.3 - missing privilege, not dumping DLL, dumping tables which use policies
EXPECT_FAIL("Shell Error (52041)", "The current user does not have the 'MANAGE_DATA_MASKING_POLICY' policy, unable to check whether dumped data will be masked.", { "dataOnly": True, "dataMaskingPolicies": True })

#@<> WL17279-FR1.3 - missing privilege, not dumping DLL, not dumping tables which use policies
EXPECT_SUCCESS({ "dataOnly": True, "dataMaskingPolicies": True, "includeTables": [table_without_policy_quoted] })
EXPECT_DDM_NOT_DUMPED()

#@<> WL17279-FR1.3 - restore MANAGE_DATA_MASKING_POLICY privilege
grant_ddm_privilege(src_session)

#@<> WL17279-FR1.5 - not dumping DLL, policies are not dumped
EXPECT_SUCCESS({ "dataOnly": True, "dataMaskingPolicies": True, "allowDataMasking": True })
EXPECT_DDM_NOT_DUMPED()

#@<> WL17279-FR2 - prepare a dump to be loaded
EXPECT_SUCCESS({"allowDataMasking": True})

#@<> WL17279-FR2.1 - target version does not support DDM - {__dbug}
testutil.dbug_set("+d,dump_loader_ddm_unsupported_version")

EXPECT_LOAD_FAIL("Shell Error (53038)", "The dump contains dynamic data masking DDL which requires server 9.7.0 or newer.")

testutil.dbug_set("")

#@<> WL17279-FR2.1 - target version does not support DDM, but we're not loading DDL - {__dbug}
testutil.dbug_set("+d,dump_loader_ddm_unsupported_version")

# dry run, because target instance is empty
EXPECT_LOAD_SUCCESS({"loadDdl": False, "dryRun": True})

testutil.dbug_set("")

#@<> WL17279-FR2.2 - unload the component
disable_dynamic_data_masking(tgt_session)

#@<> WL17279-FR2.2 - load when component is not installed
EXPECT_LOAD_SUCCESS()

EXPECT_STDOUT_CONTAINS("WARNING: The dump contains data masking policy DDL, but the dynamic data masking component is not installed on the target instance. Data masking policies will not be loaded.")
EXPECT_STDOUT_CONTAINS("WARNING: The dump contains table DDL with masked columns, but the dynamic data masking component is not installed on the target instance. Queries against these tables will fail to execute, unless this component is installed.")

#@<> WL17279-FR2.2 - load the component
enable_dynamic_data_masking(tgt_session)

#@<> WL17279-FR2.3 - revoke MANAGE_DATA_MASKING_POLICY privilege
revoke_ddm_privilege(tgt_session)

#@<> WL17279-FR2.3 - load when user is missing the privilege
EXPECT_LOAD_FAIL("MySQL Error (1227)", "Access denied; you need (at least one of) the MANAGE_DATA_MASKING_POLICY privilege(s) for this operation")

#@<> WL17279-FR2.3 - restore MANAGE_DATA_MASKING_POLICY privilege, load DDL, revoke again
grant_ddm_privilege(tgt_session)

EXPECT_LOAD_SUCCESS({"loadData": False})

revoke_ddm_privilege(tgt_session)

#@<> WL17279-FR2.3 - load data when user is missing the privilege
EXPECT_LOAD_SUCCESS({"loadDdl": False}, wipe_target=False)

#@<> WL17279-FR2.3 - restore MANAGE_DATA_MASKING_POLICY privilege
grant_ddm_privilege(tgt_session)

#@<> WL17279-FR2.4 - create a masking policy with the same name but different expression
wipeout_server(tgt_session)

tgt_session.run_sql("""CREATE MASKING POLICY mask_ssn(ssn)
  CASE WHEN CURRENT_ROLE_IN('hippa')
       THEN ssn
       ELSE 0
  END""")

#@<> WL17279-FR2.4 - load when target has a policy with the same name
EXPECT_LOAD_FAIL("Shell Error (53021)", "While 'Checking for pre-existing objects': Duplicate objects found in destination database", wipe_target=False)

#@<> WL17279-FR2.4 - load when target has a policy with the same name - ignore existing
EXPECT_LOAD_SUCCESS({"ignoreExistingObjects": True}, wipe_target=False)
EXPECT_STDOUT_CONTAINS("NOTE: Data masking policy `mask_ssn` already exists, ignoring...")

# existing policy was not changed
EXPECT_IN("hippa", snapshot_masking_policies(tgt_session)["mask_ssn"]["ddl"])

#@<> WL17279-FR2.5 - load when target has a policy with the same name - data only
tgt_session.run_sql("DROP MASKING POLICY IF EXISTS unmask_ssn")

EXPECT_LOAD_SUCCESS({"loadDdl": False}, wipe_target=False)

# policies were not loaded
EXPECT_NOT_CONTAINS(["unmask_ssn"], snapshot_masking_policies(tgt_session))

#@<> WL17279-FR2.4 - load when target has a policy with the same name - drop existing
EXPECT_LOAD_SUCCESS({"dropExistingObjects": True}, wipe_target=False)
EXPECT_STDOUT_CONTAINS("NOTE: Data masking policy `mask_ssn` already exists, dropping...")

# existing policy was changed
EXPECT_IN("gdpr", snapshot_masking_policies(tgt_session)["mask_ssn"]["ddl"])

#@<> WL17279-FR2 - dry run
EXPECT_LOAD_SUCCESS({"dryRun": True})
EXPECT_FALSE(snapshot_masking_policies(tgt_session))

#@<> WL17279-FR2 - regular load
EXPECT_LOAD_SUCCESS()
EXPECT_JSON_EQ(snapshot_masking_policies(src_session), snapshot_masking_policies(tgt_session))
EXPECT_JSON_EQ(snapshot_schemas(src_session), snapshot_schemas(tgt_session))

#@<> WL17279-FR3 - dry run
prepare_for_copy()

EXPECT_NO_THROWS(lambda: util.copy_instance(__sandbox_uri2, {"dryRun": True, "allowDataMasking": True, "excludeUsers": ["root"], "showProgress": False}))

EXPECT_FALSE(snapshot_masking_policies(tgt_session))

#@<> WL17279-FR3 - regular load
prepare_for_copy()

EXPECT_NO_THROWS(lambda: util.copy_instance(__sandbox_uri2, {"allowDataMasking": True, "excludeUsers": ["root"], "showProgress": False}))

EXPECT_JSON_EQ(snapshot_masking_policies(src_session), snapshot_masking_policies(tgt_session))
EXPECT_JSON_EQ(snapshot_schemas(src_session), snapshot_schemas(tgt_session))

#@<> WL17279-FR4 - util.dumpSchemas() - do not allow data masking
prepare_for_dump()
EXPECT_THROWS(lambda: util.dump_schemas([target_schema], dump_dir, {"allowDataMasking": False, "showProgress": False}), "Shell Error (52042): Unable to dump unmasked table data")

#@<> WL17279-FR4 - util.dumpSchemas() - allow data masking
prepare_for_dump()
EXPECT_NO_THROWS(lambda: util.dump_schemas([target_schema], dump_dir, {"allowDataMasking": True, "showProgress": False}))

EXPECT_STDOUT_CONTAINS(f"""
WARNING: One or more data masking policies deny access to table data.

Dump contains tables which use dynamic data masking, while current user {current_user} and its active role ('gdpr'@'%') do not have access to unmasked data due to the following policies:
 * Policy `mask_ssn_user` allows access for user: 'admin'@'10.20.30.40'
 * Policy `unmask_ssn_user` denies access for user: {current_user}

The "allowDataMasking" dump option is enabled, ignoring this issue.
""")

#@<> WL17279-FR4 - util.dumpTables() - do not allow data masking
prepare_for_dump()
EXPECT_THROWS(lambda: util.dump_tables(target_schema, [table_with_allow_user_policy], dump_dir, {"allowDataMasking": False, "showProgress": False}), "Shell Error (52042): Unable to dump unmasked table data")

#@<> WL17279-FR4 - util.dumpTables() - allow data masking
prepare_for_dump()
EXPECT_NO_THROWS(lambda: util.dump_tables(target_schema, [table_with_allow_user_policy], dump_dir, {"allowDataMasking": True, "showProgress": False}))

EXPECT_STDOUT_CONTAINS(f"""
WARNING: One or more data masking policies deny access to table data.

Dump contains tables which use dynamic data masking, while current user {current_user} and its active role ('gdpr'@'%') do not have access to unmasked data due to the following policies:
 * Policy `mask_ssn_user` allows access for user: 'admin'@'10.20.30.40'

The "allowDataMasking" dump option is enabled, ignoring this issue.
""")

#@<> WL17279-FR4 - util.exportTable() - do not allow data masking
prepare_for_dump()
EXPECT_THROWS(lambda: util.export_table(table_with_allow_user_policy_quoted, dump_dir, {"allowDataMasking": False, "showProgress": False}), "Shell Error (52042): Unable to dump unmasked table data")

#@<> WL17279-FR4 - util.exportTable() - allow data masking
prepare_for_dump()
EXPECT_NO_THROWS(lambda: util.export_table(table_with_allow_user_policy_quoted, dump_dir, {"allowDataMasking": True, "showProgress": False}))

EXPECT_STDOUT_CONTAINS(f"""
WARNING: One or more data masking policies deny access to table data.

Dump contains tables which use dynamic data masking, while current user {current_user} and its active role ('gdpr'@'%') do not have access to unmasked data due to the following policies:
 * Policy `mask_ssn_user` allows access for user: 'admin'@'10.20.30.40'

The "allowDataMasking" dump option is enabled, ignoring this issue.
""")

#@<> WL17279-FR4 - util.copySchemas() - do not allow data masking
prepare_for_copy()
EXPECT_THROWS(lambda: util.copy_schemas([target_schema], __sandbox_uri2, {"allowDataMasking": False, "showProgress": False}), "Shell Error (52042): Unable to dump unmasked table data")

#@<> WL17279-FR4 - util.copySchemas() - allow data masking
prepare_for_copy()
EXPECT_NO_THROWS(lambda: util.copy_schemas([target_schema], __sandbox_uri2, {"allowDataMasking": True, "showProgress": False}))

EXPECT_STDOUT_CONTAINS(f"""
WARNING: SRC: One or more data masking policies deny access to table data.
SRC: 
SRC: Dump contains tables which use dynamic data masking, while current user {current_user} and its active role ('gdpr'@'%') do not have access to unmasked data due to the following policies:
 * Policy `mask_ssn_user` allows access for user: 'admin'@'10.20.30.40'
 * Policy `unmask_ssn_user` denies access for user: {current_user}

SRC: The "allowDataMasking" dump option is enabled, ignoring this issue.
""")

#@<> WL17279-FR4 - util.copyTables() - do not allow data masking
prepare_for_copy()
EXPECT_THROWS(lambda: util.copy_tables(target_schema, [table_with_allow_user_policy], __sandbox_uri2, {"allowDataMasking": False, "showProgress": False}), "Shell Error (52042): Unable to dump unmasked table data")

#@<> WL17279-FR4 - util.copyTables() - allow data masking
prepare_for_copy()
EXPECT_NO_THROWS(lambda: util.copy_tables(target_schema, [table_with_allow_user_policy], __sandbox_uri2, {"allowDataMasking": True, "showProgress": False}))

EXPECT_STDOUT_CONTAINS(f"""
WARNING: SRC: One or more data masking policies deny access to table data.
SRC: 
SRC: Dump contains tables which use dynamic data masking, while current user {current_user} and its active role ('gdpr'@'%') do not have access to unmasked data due to the following policies:
 * Policy `mask_ssn_user` allows access for user: 'admin'@'10.20.30.40'

SRC: The "allowDataMasking" dump option is enabled, ignoring this issue.
""")

#@<> Cleanup
testutil.destroy_sandbox(__mysql_sandbox_port1)
testutil.destroy_sandbox(__mysql_sandbox_port2)
wipe_dir(outdir)
