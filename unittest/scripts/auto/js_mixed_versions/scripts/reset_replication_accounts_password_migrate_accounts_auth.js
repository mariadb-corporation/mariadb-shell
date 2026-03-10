//@ {DEF(MYSQLD_SECONDARY_SERVER_A) && VER(>=8.0.27) && testutil.versionCheck(MYSQLD_SECONDARY_SERVER_A.version, "between", "[8.0.27,9.0.0)")}

//@<> INCLUDE clusterset_utils.inc

//@<> Setup
var pwdAdmin = "C0mPL1CAT3D_pa22w0rd_adm1n";

if (testutil.versionCheck(MYSQLD_SECONDARY_SERVER_A.version, '<', __version)) {
    testutil.deployRawSandbox(__mysql_sandbox_port1, __secure_password,
        {report_host: hostname},
        {mysqldPath: MYSQLD_SECONDARY_SERVER_A.path});
    testutil.deploySandbox(__mysql_sandbox_port2, __secure_password,
        {report_host: hostname},
        {mysqldPath: MYSQLD_SECONDARY_SERVER_A.path});

    // All instances in this test use the secondary binary (<9.0).
    testutil.changeSandboxConf(__mysql_sandbox_port1, "mysql_native_password", "ON");
    testutil.changeSandboxConf(__mysql_sandbox_port2, "mysql_native_password", "ON");
} else {
    testutil.deployRawSandbox(__mysql_sandbox_port1, __secure_password,
        {report_host: hostname},
        {mysqldPath: MYSQLD_SECONDARY_SERVER_A.path});
    testutil.deploySandbox(__mysql_sandbox_port2, __secure_password,
        {report_host: hostname},
        {mysqldPath: MYSQLD_SECONDARY_SERVER_A.path});

    // All instances in this test use the secondary binary (<9.0).
    testutil.changeSandboxConf(__mysql_sandbox_port1, "mysql_native_password", "ON");
    testutil.changeSandboxConf(__mysql_sandbox_port2, "mysql_native_password", "ON");
}

EXPECT_NO_THROWS(function() {
    dba.configureInstance(__sandbox_uri_secure_password1,
        {clusterAdmin: "admin", clusterAdminPassword: pwdAdmin});
});
EXPECT_NO_THROWS(function() {
    dba.configureInstance(__sandbox_uri_secure_password2,
        {clusterAdmin: "admin", clusterAdminPassword: pwdAdmin});
});

var __sandbox_uri1 = `mysql://admin:${pwdAdmin}@localhost:${__mysql_sandbox_port1}`;
var __sandbox_uri2 = `mysql://admin:${pwdAdmin}@localhost:${__mysql_sandbox_port2}`;

testutil.restartSandbox(__mysql_sandbox_port1);
testutil.restartSandbox(__mysql_sandbox_port2);

//@<> Create Cluster + ClusterSet + ReplicaCluster
shell.connect(__sandbox_uri1);
testutil.dbugSet("+d,dba_create_user_force_mysql_native_password");
var cluster = dba.createCluster("cluster", {"ipAllowlist":"127.0.0.1," + hostname_ip, "communicationStack": "XCOM"});

var cs = cluster.createClusterSet("clusterset");

var rc = cs.createReplicaCluster(__sandbox_uri2, "replicacluster",
    {recoveryMethod: "incremental", "ipAllowlist":"127.0.0.1," + hostname_ip, "communicationStack": "XCOM"});

CHECK_PRIMARY_CLUSTER([__sandbox_uri1], cluster);
CHECK_REPLICA_CLUSTER([__sandbox_uri2], cluster, rc, undefined, __secure_password);

testutil.dbugSet("");

//@<> Validate migration from mysql_native_password to caching_sha2_password
shell.connect(__sandbox_uri1);

var native_pwd_accounts = session.runSql(
  "SELECT COUNT(*) FROM mysql.user " +
  "WHERE user LIKE 'mysql_innodb_c%' AND plugin='mysql_native_password'"
).fetchOne()[0];

var all_internal_accounts = session.runSql(
  "SELECT COUNT(*) FROM mysql.user " +
  "WHERE user LIKE 'mysql_innodb_c%'"
).fetchOne()[0];

// All accounts are using mysql_native_password
EXPECT_EQ(4, all_internal_accounts);
EXPECT_EQ(all_internal_accounts, native_pwd_accounts);

EXPECT_NO_THROWS(function() { cs.resetReplicationAccountsPassword({recreate:true}); });

shell.connect(__sandbox_uri1);

var native_pwd_accounts = session.runSql(
  "SELECT COUNT(*) FROM mysql.user " +
  "WHERE user LIKE 'mysql_innodb_c%' AND plugin='mysql_native_password'"
).fetchOne()[0];

var caching_sha2_accounts = session.runSql(
  "SELECT COUNT(*) FROM mysql.user " +
  "WHERE user LIKE 'mysql_innodb_c%' AND plugin='caching_sha2_password'"
).fetchOne()[0];

var all_internal_accounts = session.runSql(
  "SELECT COUNT(*) FROM mysql.user " +
  "WHERE user LIKE 'mysql_innodb_c%'"
).fetchOne()[0];

// All accounts are using caching_sha2_password
EXPECT_EQ(4, all_internal_accounts);
EXPECT_EQ(0, native_pwd_accounts);
EXPECT_EQ(4, caching_sha2_accounts);

//@<> Cleanup
testutil.destroySandbox(__mysql_sandbox_port1);
testutil.destroySandbox(__mysql_sandbox_port2);
