function get_user_auth_string(user, host /* optional */) {
  var sql = "SELECT authentication_string FROM mysql.user WHERE user=?";
  var args = [user];

  if (host != null && host !== "") {
    sql += " AND host=?";
    args.push(host);
  }

  var row = session.runSql(sql, args).fetchOne();
  if (row == null) {
    testutil.fail(`No mysql.user row for '${user}'`);
  }
  return row[0];
}

function user_mysql_innodb_cluster(server_id) {
  return "mysql_innodb_cluster_" + server_id;
}
function user_mysql_innodb_replica(server_id) {
  return "mysql_innodb_replica_" + server_id;
}
function user_mysql_innodb_rs(server_id) {
  return "mysql_innodb_rs_" + server_id;
}

function snapshot_users_auth_strings(users) {
  var out = [];
  for (var i = 0; i < users.length; i++) out.push(get_user_auth_string(users[i]));
  return out;
}

function snapshot_cluster_accounts(server_ids) {
  var users = [];
  for (var i = 0; i < server_ids.length; i++) users.push(user_mysql_innodb_cluster(server_ids[i]));
  return snapshot_users_auth_strings(users);
}

function snapshot_replicaset_accounts(server_ids) {
  var users = [];
  for (var i = 0; i < server_ids.length; i++) users.push(user_mysql_innodb_rs(server_ids[i]));
  return snapshot_users_auth_strings(users);
}

function snapshot_read_replica_accounts(server_ids) {
  var users = [];
  for (var i = 0; i < server_ids.length; i++) users.push(user_mysql_innodb_replica(server_ids[i]));
  return snapshot_users_auth_strings(users);
}

function expect_all_changed(oldv, newv) {
  EXPECT_EQ(oldv.length, newv.length);
  for (var i = 0; i < oldv.length; i++) EXPECT_NE(oldv[i], newv[i]);
}

function expect_changed_mask(oldv, newv, mask) {
  EXPECT_EQ(oldv.length, newv.length);
  EXPECT_EQ(oldv.length, mask.length);
  for (var i = 0; i < oldv.length; i++) {
    if (mask[i]) EXPECT_NE(oldv[i], newv[i]);
    else         EXPECT_EQ(oldv[i], newv[i]);
  }
}

function get_replication_channel_user(channel_name) {
  // Prefer P_S (8.0+)
  try {
    var row = session.runSql(
      "SELECT user FROM performance_schema.replication_connection_configuration WHERE channel_name=?",
      [channel_name]
    ).fetchOne();
    if (row != null) return row[0];
  } catch (e) {}

  // Fallback
  var row2 = session.runSql(
    "SELECT user_name FROM mysql.slave_master_info WHERE channel_name=?",
    [channel_name]
  ).fetchOne();
  if (row2 == null) testutil.fail("Could not read channel user: " + channel_name);
  return row2[0];
}

function get_clusterset_repl_account_for_cluster(cluster_name) {
  var row = session.runSql(
    "SELECT " +
      "c.attributes->>'$.replicationAccountUser' AS u, " +
      "c.attributes->>'$.replicationAccountHost' AS h " +
    "FROM mysql_innodb_cluster_metadata.clusters c " +
    "WHERE c.cluster_name = ?",
    [cluster_name]
  ).fetchOne();

  if (row == null) {
    testutil.fail("Could not find cluster in metadata: " + cluster_name);
  }
  if (row[0] == null || row[0] === "") {
    testutil.fail("Metadata has empty replicationAccountUser for cluster: " + cluster_name);
  }
  return { user: row[0], host: row[1] };
}

function snapshot_clusterset_repl_account_auth_string(cluster_name) {
  var acct = get_clusterset_repl_account_for_cluster(cluster_name);

  if (acct.host == null || acct.host === "") {
    return get_user_auth_string(acct.user);
  }
  return get_user_auth_string(acct.user, acct.host);
}
