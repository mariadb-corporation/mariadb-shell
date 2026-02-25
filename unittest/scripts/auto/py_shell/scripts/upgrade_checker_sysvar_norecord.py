#@<> WL16262-TSFR_1_1 {VER(<8.0.0)}
util.check_for_server_upgrade(__mysql_uri, {"targetVersion": "8.1.0", "list": "true"})
EXPECT_STDOUT_NOT_CONTAINS("- sysVarsNewDefaults")
EXPECT_STDOUT_NOT_CONTAINS("- removedSysVars")
EXPECT_STDOUT_NOT_CONTAINS("- removedSysLogVars")
EXPECT_STDOUT_NOT_CONTAINS("- sysvarAllowedValues")
EXPECT_STDOUT_CONTAINS("- sysVars")
WIPE_OUTPUT()


#@<> WL16262-TSFR3_1 {VER(<8.0.0)}
util.check_for_server_upgrade(__mysql_uri, {"targetVersion": "8.4.0", "include":["sysVarsNewDefaults", "removedSysVars", "removedSysLogVars", "sysvarAllowedValues"]})
EXPECT_STDERR_CONTAINS("ValueError: Option include contains unknown values 'removedSysLogVars', 'removedSysVars', 'sysVarsNewDefaults', 'sysvarAllowedValues'")
WIPE_OUTPUT()

util.check_for_server_upgrade(__mysql_uri, {"targetVersion": "8.4.0", "exclude":["sysVarsNewDefaults", "removedSysVars", "removedSysLogVars", "sysvarAllowedValues"]})
EXPECT_STDERR_CONTAINS("ValueError: Option exclude contains unknown values 'removedSysLogVars', 'removedSysVars', 'sysVarsNewDefaults', 'sysvarAllowedValues'")
WIPE_OUTPUT()


#@<> Forced SET sysvar correctly validates invalid values {VER(<8.4.0) and VER(>8.0.0)}
testutil.deploy_raw_sandbox(__mysql_sandbox_port1, 'root')
shell.connect(__sandbox_uri1)
session.run_sql("set global ssl_cipher='invalid_value'")
util.check_for_server_upgrade(None, {'include': ['sysVars']})

EXPECT_OUTPUT_CONTAINS_MULTILINE('''1) System variable check for deprecation, removal, changes in defaults values
or invalid values. (sysVars)
   Following system variables that were detected as being used ware changed,
   deprecated or removed. Please update  your system accordingly before the
   upgrade.''')

EXPECT_OUTPUT_CONTAINS_MULTILINE('''   Error: The following system variables that were detected as being used are
   using values that are  invalid in the target version.
   - ssl_cipher: variable is set to 'invalid_value', allowed values include:
   ECDHE-ECDSA-AES128-GCM-SHA256, ECDHE-ECDSA-AES256-GCM-SHA384,
   ECDHE-RSA-AES128-GCM-SHA256, ECDHE-RSA-AES256-GCM-SHA384,
   ECDHE-ECDSA-CHACHA20-POLY1305, ECDHE-RSA-CHACHA20-POLY1305,
   ECDHE-ECDSA-AES256-CCM, ECDHE-ECDSA-AES128-CCM, DHE-RSA-AES128-GCM-SHA256,
   DHE-RSA-AES256-GCM-SHA384, DHE-RSA-AES256-CCM, DHE-RSA-AES128-CCM,
   DHE-RSA-CHACHA20-POLY1305, .
''')

testutil.destroy_sandbox(__mysql_sandbox_port1)