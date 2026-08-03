function get_sandbox_version(uri) {
    let version_session = mysql.getSession(uri);
    let version = version_session.runSql('SELECT @@version').fetchOne()[0];
    version_session.close();
    return version.split('-')[0];
}

var replication_version_compatibility = {
    default_server_version: null,
    secondary_server_version: MYSQLD_SECONDARY_SERVER_A.version.replace(/[-_].*$/, ''),
    default_to_secondary_replication_incompatible: false,
    higher_to_lower_replication_downgrade_only: false,
    secondary_server_is_higher: false,
};

function current_to_secondary_replication_is_incompatible() {
    return replication_version_compatibility.default_to_secondary_replication_incompatible;
}

function higher_to_lower_replication_is_downgrade_only() {
    return replication_version_compatibility.higher_to_lower_replication_downgrade_only;
}

function deploy_sandbox(port, use_secondary_server) {
    if (use_secondary_server) {
        testutil.deploySandbox(port, "root", { report_host: hostname }, { mysqldPath: MYSQLD_SECONDARY_SERVER_A.path });
    } else {
        testutil.deploySandbox(port, "root", { report_host: hostname });
    }
}

function deploy_high_version_sandbox(port) {
    deploy_sandbox(port, replication_version_compatibility.secondary_server_is_higher);
}

function deploy_low_version_sandbox(port) {
    deploy_sandbox(port, !replication_version_compatibility.secondary_server_is_higher);
}

function determine_replication_version_compatibility() {
    let compatibility = replication_version_compatibility;

    testutil.deploySandbox(__mysql_sandbox_port1, "root", { report_host: hostname });
    compatibility.default_server_version = get_sandbox_version(__sandbox_uri1);
    compatibility.secondary_server_is_higher =
        testutil.versionCheck(compatibility.secondary_server_version, '>', compatibility.default_server_version);
    compatibility.default_to_secondary_replication_incompatible =
        testutil.versionCheck(compatibility.default_server_version, "between", "[26.7.0,28.4.0)") &&
        testutil.versionCheck(compatibility.secondary_server_version, "between", "[9.7.0,10.0.0)");
    compatibility.higher_to_lower_replication_downgrade_only =
        testutil.versionCheck(compatibility.secondary_server_is_higher ?
            compatibility.secondary_server_version : compatibility.default_server_version, "between", "[9.7.0,10.0.0)") &&
        testutil.versionCheck(compatibility.secondary_server_is_higher ?
            compatibility.default_server_version : compatibility.secondary_server_version, "between", "[8.4.0,9.0.0)");
    testutil.destroySandbox(__mysql_sandbox_port1);
    WIPE_OUTPUT()
    WIPE_SHELL_LOG()
}
