//@ {VER(>=8.0.11)}

var server_id1 = 1111;
var server_id2 = 2222;
var server_id3 = 3333;

var sb1 = hostname_ip + ":" + __mysql_sandbox_port1;
var sb2 = hostname_ip + ":" + __mysql_sandbox_port2;
var sb3 = hostname_ip + ":" + __mysql_sandbox_port3;

//@<> INCLUDE replication_accounts_utils.inc

//@<> Initialization
// Use the IP instead of the hostname, because we want to test authentication and IP will not reverse into hostname in many test environments.
testutil.deploySandbox(__mysql_sandbox_port1, "root", {report_host: hostname_ip, server_id: "" + server_id1});
testutil.deploySandbox(__mysql_sandbox_port2, "root", {report_host: hostname_ip, server_id: "" + server_id2});
testutil.deploySandbox(__mysql_sandbox_port3, "root", {report_host: hostname_ip, server_id: "" + server_id3});

//@<> create replicaset
shell.connect(__sandbox_uri1);
var rs = dba.createReplicaSet("rs");
rs.addInstance(__sandbox_uri2, {recoveryMethod: "incremental"});

//@<> add by clone {VER(>=8.0.17)}
rs.addInstance(__sandbox_uri3, {recoveryMethod: "clone"});
//@<> add by clone {VER(<8.0.17)}
rs.addInstance(__sandbox_uri3, {recoveryMethod: "incremental"});

// ensure topology is ONLINE
var s = rs.status();
EXPECT_EQ("ONLINE", s.replicaSet.topology[sb1]["status"]);
EXPECT_EQ("ONLINE", s.replicaSet.topology[sb2]["status"]);
EXPECT_EQ("ONLINE", s.replicaSet.topology[sb3]["status"]);

//@<> Validate resetReplicationAccountsPassword changes the passwords when all instances online
// NOTE: ReplicaSet internal accounts are mysql_innodb_rs_<server_id>.
var before = snapshot_replicaset_accounts([server_id1, server_id2, server_id3]);

WIPE_OUTPUT();
EXPECT_NO_THROWS(function() { rs.resetReplicationAccountsPassword(); });

EXPECT_OUTPUT_CONTAINS("The replication account passwords of all the ReplicaSet instances were successfully reset.");
EXPECT_OUTPUT_CONTAINS(`* Updating replication credentials on '${hostname_ip}:${__mysql_sandbox_port2}' (channel: ). The replication channel will be temporarily stopped and restarted to apply the new credentials.`);

var after = snapshot_replicaset_accounts([server_id1, server_id2, server_id3]);
expect_all_changed(before, after);

//@<> An error is thrown if instance not reachable and force option is not used (non-interactive)
testutil.killSandbox(__mysql_sandbox_port2);

WIPE_OUTPUT();
EXPECT_THROWS(function() { rs.resetReplicationAccountsPassword(); },
  `The instance '${hostname_ip}:${__mysql_sandbox_port2}' is 'UNREACHABLE' (it must be ONLINE).`);

EXPECT_OUTPUT_CONTAINS(`ERROR: The replication account passwords for instance '${hostname_ip}:${__mysql_sandbox_port2}' cannot be reset because it is in a 'UNREACHABLE' state. Replication account passwords can only be rotated for ONLINE instances. Ensure the instance is reachable and use <ReplicaSet>.rejoinInstance() to rejoin it and refresh its internal replication account credentials. You can also run <ReplicaSet>.resetReplicationAccountsPassword() with the force option enabled to skip instances that are not ONLINE.`);

//@<> An error is thrown if instance not reachable and we reply no to the interactive prompt
shell.options.useWizards=1;
testutil.expectPrompt("Do you want to continue anyway (the replication account passwords for the instance will not be reset)? [y/N]: ", "n");
EXPECT_THROWS(function() { rs.resetReplicationAccountsPassword(); },
  `The instance '${hostname_ip}:${__mysql_sandbox_port2}' is 'UNREACHABLE' (it must be ONLINE).`);

EXPECT_OUTPUT_CONTAINS(`ERROR: The replication account passwords for instance '${hostname_ip}:${__mysql_sandbox_port2}' cannot be reset because it is in a 'UNREACHABLE' state. Replication account passwords can only be rotated for ONLINE instances. Ensure the instance is reachable and use <ReplicaSet>.rejoinInstance() to rejoin it and refresh its internal replication account credentials. You can choose to proceed with the operation and skip resetting this instance's replication account passwords.`);

//@<> An error is thrown if instance not reachable and the force option is false (no prompt)
EXPECT_THROWS(function() { rs.resetReplicationAccountsPassword({force:false}); },
  `The instance '${hostname_ip}:${__mysql_sandbox_port2}' is 'UNREACHABLE' (it must be ONLINE).`);
EXPECT_OUTPUT_CONTAINS(`ERROR: The replication account passwords for instance '${hostname_ip}:${__mysql_sandbox_port2}' cannot be reset because it is in a 'UNREACHABLE' state. Replication account passwords can only be rotated for ONLINE instances. Ensure the instance is reachable and use <ReplicaSet>.rejoinInstance() to rejoin it and refresh its internal replication account credentials. You can also run <ReplicaSet>.resetReplicationAccountsPassword() with the force option enabled to skip instances that are not ONLINE.`);

shell.options.useWizards=0;

//@<> Warning is printed for all unreachable instances but no error is thrown if force option is used
testutil.killSandbox(__mysql_sandbox_port3);

var before = snapshot_replicaset_accounts([server_id1, server_id2, server_id3]);

shell.options.useWizards=1;
EXPECT_NO_THROWS(function() { rs.resetReplicationAccountsPassword({force:true});});
shell.options.useWizards=0;

EXPECT_OUTPUT_CONTAINS(`NOTE: Skipping reset of the replication account passwords for instance '${hostname_ip}:${__mysql_sandbox_port2}' because it is 'UNREACHABLE'.`);
EXPECT_OUTPUT_CONTAINS(`NOTE: Skipping reset of the replication account passwords for instance '${hostname_ip}:${__mysql_sandbox_port3}' because it is 'UNREACHABLE'.`);
EXPECT_OUTPUT_CONTAINS(`WARNING: Not all replication account passwords were successfully reset, the following instances were skipped: '${hostname_ip}:${__mysql_sandbox_port2}', '${hostname_ip}:${__mysql_sandbox_port3}'. Ensure these instances are reachable and use <ReplicaSet>.rejoinInstance() to rejoin them and refresh their internal replication account credentials.`);

var after = snapshot_replicaset_accounts([server_id1, server_id2, server_id3]);
// Only primary (reachable) changes.
expect_changed_mask(before, after, [true, false, false]);

//@<> Bring Instance 3 back online and validate force:true changes only reachable instances (1 and 3)
testutil.startSandbox(__mysql_sandbox_port3);
testutil.waitSandboxAlive(__mysql_sandbox_port3);

EXPECT_NO_THROWS(function() { rs.rejoinInstance(__sandbox_uri3); });
testutil.waitReplicationChannelState(__mysql_sandbox_port3, "", "ON");

var before = snapshot_replicaset_accounts([server_id1, server_id2, server_id3]);

WIPE_OUTPUT();
EXPECT_NO_THROWS(function() { rs.resetReplicationAccountsPassword({force:true}); });

EXPECT_OUTPUT_CONTAINS(`NOTE: Skipping reset of the replication account passwords for instance '${hostname_ip}:${__mysql_sandbox_port2}' because it is 'UNREACHABLE'.`);
EXPECT_OUTPUT_CONTAINS(`WARNING: Not all replication account passwords were successfully reset, the following instance was skipped: '${hostname_ip}:${__mysql_sandbox_port2}'. Ensure this instance is reachable and use <ReplicaSet>.rejoinInstance() to rejoin it and refresh its internal replication account credentials.`);

var after = snapshot_replicaset_accounts([server_id1, server_id2, server_id3]);
expect_changed_mask(before, after, [true, false, true]);

//@<> Bring Instance 2 back online and validate resetReplicationAccountsPassword works again for all
testutil.startSandbox(__mysql_sandbox_port2);
testutil.waitSandboxAlive(__mysql_sandbox_port2);

EXPECT_NO_THROWS(function() { rs.rejoinInstance(__sandbox_uri2); });
testutil.waitReplicationChannelState(__mysql_sandbox_port2, "", "ON");

var before = snapshot_replicaset_accounts([server_id1, server_id2, server_id3]);

WIPE_OUTPUT();
EXPECT_NO_THROWS(function() { rs.resetReplicationAccountsPassword(); });

EXPECT_OUTPUT_CONTAINS("The replication account passwords of all the ReplicaSet instances were successfully reset.");

var after = snapshot_replicaset_accounts([server_id1, server_id2, server_id3]);
expect_all_changed(before, after);

//@<> Cleanup
rs.disconnect();
session.close();
testutil.destroySandbox(__mysql_sandbox_port1);
testutil.destroySandbox(__mysql_sandbox_port2);
testutil.destroySandbox(__mysql_sandbox_port3);
