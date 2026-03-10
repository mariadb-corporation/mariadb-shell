var server_id1 = 11111;
var server_id2 = 22222;
var server_id3 = 33333;
var server_id4 = 44444;

//@<> INCLUDE replication_accounts_utils.inc

//@<> Initialization
testutil.deploySandbox(__mysql_sandbox_port1, "root", { report_host: hostname,server_id: server_id1 });
testutil.deploySandbox(__mysql_sandbox_port2, "root", { report_host: hostname, server_id: server_id2 });
testutil.deploySandbox(__mysql_sandbox_port3, "root", { report_host: hostname, server_id: server_id3 });
testutil.deploySandbox(__mysql_sandbox_port4, "root", { report_host: hostname, server_id: server_id4 });

shell.connect(__sandbox_uri1);
var cluster = dba.createCluster("Cluster", { gtidSetIsComplete: true });
cluster.addInstance(__sandbox_uri2);
testutil.waitMemberState(__mysql_sandbox_port2, "ONLINE");

var cset = cluster.createClusterSet("cset");

shell.connect(__sandbox_uri1);
var rcluster = cset.createReplicaCluster(__sandbox_uri3, "rcluster", { recoveryMethod: "clone" });
rcluster.addInstance(__sandbox_uri4, { recoveryMethod: "clone" });

//@<> Verify ClusterSet.resetReplicationAccountsPassword() rotates all replication passwords (including ClusterSet channel) when all instances are ONLINE
shell.connect(__sandbox_uri1);
var before_primary = snapshot_cluster_accounts([server_id1, server_id2]);

shell.connect(__sandbox_uri3);
var before_replica = snapshot_cluster_accounts([server_id3, server_id4]);

shell.connect(__sandbox_uri1);
var before_cs_channel_auth = snapshot_clusterset_repl_account_auth_string("rcluster");

shell.connect(__sandbox_uri1);
var acct_before = get_clusterset_repl_account_for_cluster("rcluster");

shell.connect(__sandbox_uri3);
EXPECT_EQ(acct_before.user, get_replication_channel_user("clusterset_replication"));

shell.connect(__sandbox_uri1);
EXPECT_NO_THROWS(function() { cset.resetReplicationAccountsPassword(); });
EXPECT_OUTPUT_CONTAINS("The replication account passwords of all the ClusterSet instances were successfully reset.");
EXPECT_OUTPUT_CONTAINS(`* Updating ClusterSet replication credentials on '${hostname}:${__mysql_sandbox_port3}' (channel: clusterset_replication). The replication channel will be temporarily stopped and restarted to apply the new credentials.`);

shell.connect(__sandbox_uri1);
var after_primary = snapshot_cluster_accounts([server_id1, server_id2]);
expect_all_changed(before_primary, after_primary);

shell.connect(__sandbox_uri3);
var after_replica = snapshot_cluster_accounts([server_id3, server_id4]);
expect_all_changed(before_replica, after_replica);

shell.connect(__sandbox_uri1);
var after_cs_channel_auth = snapshot_clusterset_repl_account_auth_string("rcluster");
EXPECT_NE(before_cs_channel_auth, after_cs_channel_auth);

shell.connect(__sandbox_uri1);
var acct_after = get_clusterset_repl_account_for_cluster("rcluster");
EXPECT_EQ(acct_before.user, acct_after.user);

shell.connect(__sandbox_uri3);
EXPECT_EQ(acct_after.user, get_replication_channel_user("clusterset_replication"));
testutil.waitReplicationChannelState(__mysql_sandbox_port3, "clusterset_replication", "ON");

//@<> Verify ClusterSet.resetReplicationAccountsPassword(recreate:true) recreates Cluster and ClusterSet accounts when all instances are ONLINE
shell.connect(__sandbox_uri1);
var expected_primary_users = session.runSql(
  "select group_concat(concat(user,'@',host) order by user) " +
  "from mysql.user where user in ('mysql_innodb_cluster_11111', 'mysql_innodb_cluster_22222')"
).fetchOne()[0];

shell.connect(__sandbox_uri3);
var expected_replica_users = session.runSql(
  "select group_concat(concat(user,'@',host) order by user) " +
  "from mysql.user where user in ('mysql_innodb_cluster_33333', 'mysql_innodb_cluster_44444')"
).fetchOne()[0];

shell.connect(__sandbox_uri1);
session.runSql(
  "DROP USER " +
  "'mysql_innodb_cluster_33333'@'%', " +
  "'mysql_innodb_cluster_44444'@'%'"
);

EXPECT_EQ(null, session.runSql(
  "select group_concat(concat(user,'@',host) order by user) " +
  "from mysql.user where user in ('mysql_innodb_cluster_33333', 'mysql_innodb_cluster_44444')"
).fetchOne()[0]);

var cs_acct_before_recreate = get_clusterset_repl_account_for_cluster("rcluster");
var cs_acct_before_recreate_uh = cs_acct_before_recreate.user + "@" + cs_acct_before_recreate.host;
var expected_cs_user = session.runSql(
  "select group_concat(concat(user,'@',host) order by user) from mysql.user where user=? and host=?",
  [cs_acct_before_recreate.user, cs_acct_before_recreate.host]
).fetchOne()[0];

session.runSql(
  "DROP USER " +
  "'mysql_innodb_cluster_11111'@'%', " +
  "'mysql_innodb_cluster_22222'@'%', " +
  "'" + cs_acct_before_recreate.user + "'@'" + cs_acct_before_recreate.host + "'"
);

EXPECT_EQ(null, session.runSql(
  "select group_concat(concat(user,'@',host) order by user) " +
  "from mysql.user where user in ('mysql_innodb_cluster_11111', 'mysql_innodb_cluster_22222')"
).fetchOne()[0]);
EXPECT_EQ(null, session.runSql(
  "select group_concat(concat(user,'@',host) order by user) from mysql.user where user=? and host=?",
  [cs_acct_before_recreate.user, cs_acct_before_recreate.host]
).fetchOne()[0]);

shell.connect(__sandbox_uri1);
WIPE_OUTPUT();
EXPECT_NO_THROWS(function() { cset.resetReplicationAccountsPassword({recreate:true}); });

EXPECT_OUTPUT_CONTAINS("The replication account passwords of all the ClusterSet instances were successfully recreated.");
EXPECT_OUTPUT_CONTAINS(`* Updating ClusterSet replication credentials on '${hostname}:${__mysql_sandbox_port3}' (channel: clusterset_replication). The replication channel will be temporarily stopped and restarted to apply the new credentials.`);

shell.connect(__sandbox_uri1);
EXPECT_EQ(expected_primary_users, session.runSql(
  "select group_concat(concat(user,'@',host) order by user) " +
  "from mysql.user where user in ('mysql_innodb_cluster_11111', 'mysql_innodb_cluster_22222')"
).fetchOne()[0]);

var cs_acct_after_recreate = get_clusterset_repl_account_for_cluster("rcluster");
EXPECT_EQ(expected_cs_user, session.runSql(
  "select group_concat(concat(user,'@',host) order by user) from mysql.user where user=? and host=?",
  [cs_acct_before_recreate.user, cs_acct_before_recreate.host]
).fetchOne()[0]);
EXPECT_EQ(cs_acct_before_recreate_uh, cs_acct_after_recreate.user + "@" + cs_acct_after_recreate.host);

shell.connect(__sandbox_uri3);
EXPECT_EQ(expected_replica_users, session.runSql(
  "select group_concat(concat(user,'@',host) order by user) " +
  "from mysql.user where user in ('mysql_innodb_cluster_33333', 'mysql_innodb_cluster_44444')"
).fetchOne()[0]);
EXPECT_EQ(cs_acct_after_recreate.user, get_replication_channel_user("clusterset_replication"));
testutil.waitReplicationChannelState(__mysql_sandbox_port3, "clusterset_replication", "ON");

// Prove it still replicates after rotation
shell.connect(__sandbox_uri1);
session.runSql("CREATE DATABASE IF NOT EXISTS cs_pwd_test");
session.runSql("CREATE TABLE IF NOT EXISTS cs_pwd_test.t (id INT PRIMARY KEY)");
session.runSql("INSERT IGNORE INTO cs_pwd_test.t VALUES (1)");

testutil.waitMemberTransactions(__mysql_sandbox_port3, __mysql_sandbox_port1);

shell.connect(__sandbox_uri3);
EXPECT_EQ(1, session.runSql("SELECT COUNT(*) FROM cs_pwd_test.t WHERE id=1").fetchOne()[0]);

// Sanity test: recovery creds still work
shell.connect(__sandbox_uri1);
restart_gr_plugin(__mysql_sandbox_port2);
testutil.waitMemberState(__mysql_sandbox_port2, "ONLINE");

shell.connect(__sandbox_uri3);
restart_gr_plugin(__mysql_sandbox_port4);
testutil.waitMemberState(__mysql_sandbox_port4, "ONLINE");

//@<> Simulate one missing member in the PRIMARY cluster and one missing member in the REPLICA cluster
session.close();
shell.connect(__sandbox_uri2);
session.runSql("STOP group_replication");
session.close();
shell.connect(__sandbox_uri1);
testutil.waitMemberState(__mysql_sandbox_port2, "(MISSING)");

session.close();
shell.connect(__sandbox_uri4);
session.runSql("STOP group_replication");
session.close();
shell.connect(__sandbox_uri3);
testutil.waitMemberState(__mysql_sandbox_port4, "(MISSING)");

//@<> Verify non-interactive mode fails fast on the first member that is not ONLINE
EXPECT_THROWS(function() { cset.resetReplicationAccountsPassword(); },
  `The instance '${hostname}:${__mysql_sandbox_port2}' is '(MISSING)' (it must be ONLINE).`);

EXPECT_OUTPUT_CONTAINS(`ERROR: The replication account passwords for instance '${hostname}:${__mysql_sandbox_port2}' cannot be reset because it is in a '(MISSING)' state. Replication account passwords can only be rotated for ONLINE instances. Ensure the instance is reachable and use <Cluster>.rejoinInstance() to rejoin it and refresh its internal replication account credentials. You can also run <Cluster>.resetReplicationAccountsPassword() with the force option enabled to skip instances that are not ONLINE.`);

//@<> Verify interactive mode prompts and aborts when the user answers 'no'
shell.options.useWizards=1;
testutil.expectPrompt("Do you want to continue anyway (the replication account passwords for the instance will not be reset)? [y/N]: ", "n");
EXPECT_THROWS(function() { cset.resetReplicationAccountsPassword(); },
  `The instance '${hostname}:${__mysql_sandbox_port2}' is '(MISSING)' (it must be ONLINE).`);

//@<> Verify interactive mode prompts per missing instance (answer 'yes' then 'no')
shell.options.useWizards=1;
testutil.expectPrompt("Do you want to continue anyway (the replication account passwords for the instance will not be reset)? [y/N]: ", "y");
testutil.expectPrompt("Do you want to continue anyway (the replication account passwords for the instance will not be reset)? [y/N]: ", "n");
EXPECT_THROWS(function() { cset.resetReplicationAccountsPassword(); },
  `The instance '${hostname}:${__mysql_sandbox_port4}' is '(MISSING)' (it must be ONLINE).`);

EXPECT_OUTPUT_CONTAINS(`ERROR: The replication account passwords for instance '${hostname}:${__mysql_sandbox_port4}' cannot be reset because it is in a '(MISSING)' state. Replication account passwords can only be rotated for ONLINE instances. Ensure the instance is reachable and use <Cluster>.rejoinInstance() to rejoin it and refresh its internal replication account credentials. You can choose to proceed with the operation and skip resetting this instance's replication account passwords.`);

//@<> Verify force:false disables prompting and fails if any member is not ONLINE
EXPECT_THROWS(function() { cset.resetReplicationAccountsPassword({force:false}); },
  `The instance '${hostname}:${__mysql_sandbox_port2}' is '(MISSING)' (it must be ONLINE).`);
EXPECT_OUTPUT_CONTAINS(`ERROR: The replication account passwords for instance '${hostname}:${__mysql_sandbox_port2}' cannot be reset because it is in a '(MISSING)' state. Replication account passwords can only be rotated for ONLINE instances. Ensure the instance is reachable and use <Cluster>.rejoinInstance() to rejoin it and refresh its internal replication account credentials. You can also run <Cluster>.resetReplicationAccountsPassword() with the force option enabled to skip instances that are not ONLINE.`);

shell.options.useWizards=0;

//@<> Verify ClusterSet.resetReplicationAccountsPassword() fails if any Cluster has global status != OK
// Bring back instance 2 (so primary cluster is OK again)
session.close();
shell.connect(__sandbox_uri2);
session.runSql("START group_replication");
session.close();
shell.connect(__sandbox_uri1);
testutil.waitMemberState(__mysql_sandbox_port2, "ONLINE");

// Stop the ClusterSet replication channel on the replica cluster (expect OK_NOT_REPLICATING)
session.close();
shell.connect(__sandbox_uri3);
session.runSql("STOP REPLICA");

EXPECT_THROWS(function() { cset.resetReplicationAccountsPassword({force:false}); },
  `Cluster 'rcluster' has global status 'OK_NOT_REPLICATING' (it must be OK).`);

EXPECT_OUTPUT_CONTAINS(`ERROR: The Cluster 'rcluster' has a global status of 'OK_NOT_REPLICATING' (it must be OK). Ensure the Cluster is healthy and run <ClusterSet>.resetReplicationAccountsPassword() again.`);

//@<> Cleanup
session.close();
testutil.destroySandbox(__mysql_sandbox_port1);
testutil.destroySandbox(__mysql_sandbox_port2);
testutil.destroySandbox(__mysql_sandbox_port3);
testutil.destroySandbox(__mysql_sandbox_port4);
