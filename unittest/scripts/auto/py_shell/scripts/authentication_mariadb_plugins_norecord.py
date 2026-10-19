#@ {__server_is_maria_db}
# -----------------------------------------------------------------------------
# Logging in to accounts that use the authentication plugins whose client side
# is a loadable plugin bundled in lib/mariadb/plugins (client_ed25519.so,
# parsec.so, dialog.so, mysql_clear_password.so, caching_sha2_password.so);
# without it the login fails with "Plugin <name> could not be loaded". Each
# login is checked in-process and in a separate mariadb-shell, which resolves
# the plugin dir from its own home the way an installed shell does.
#
# A chunk runs only if the server has the server side of its plugin: dialog and
# mysql_clear_password are exercised through the test plugins of a full server
# install (dialog_examples, auth_test_plugin), which the sandbox server packages
# leave out; caching_sha2_password needs auth_mysql_sha2. The test plugins are of
# maturity "unknown", which a server loads only when started with
# --plugin-maturity=unknown, so the test deploys a sandbox of its own.

# -----------------------------------------------------------------------------
# helpers

def load_server_plugin(name):
    """Makes the server side of an auth plugin available: already active,
    or loaded here from the library the server lists it in. Returns whether
    the server has it at all."""
    row = session.run_sql("SELECT plugin_status, plugin_library FROM information_schema.all_plugins WHERE plugin_name = ?", [name]).fetch_one()
    if not row:
        return False
    if row[0] != "ACTIVE":
        session.run_sql(f"INSTALL SONAME '{row[1]}'")
    return True

def check_login(user, password):
    uri = f"{user}:{password}@localhost:{__mysql_sandbox_port1}"
    s = mysql.get_classic_session(uri)
    EXPECT_EQ(f"{user}@%", s.run_sql("SELECT current_user()").fetch_one()[0])
    s.close()
    testutil.call_mysqlsh([uri, "--sql", "-e", "SELECT concat('logged in as ', current_user())"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
    EXPECT_STDOUT_CONTAINS(f"logged in as {user}@%")
    EXPECT_STDOUT_NOT_CONTAINS("could not be loaded")
    WIPE_OUTPUT()

def check_wrong_password(user):
    EXPECT_THROWS(lambda: mysql.get_classic_session(f"{user}:wrong@localhost:{__mysql_sandbox_port1}"), f"Access denied for user '{user}'")

# -----------------------------------------------------------------------------
# setup

#@<> setup
testutil.deploy_sandbox(__mysql_sandbox_port1, "root", {"plugin-maturity": "unknown"})
shell.connect(__sandbox_uri1)
__have_ed25519 = load_server_plugin("ed25519")
# parsec is new in MariaDB 11.6 (and built in from then on)
__have_parsec = load_server_plugin("parsec")
# three_attempts asks for the password through the dialog client plugin
__have_dialog = load_server_plugin("three_attempts")
# cleartext_plugin_server makes the client send the password with
# mysql_clear_password
__have_clear = load_server_plugin("cleartext_plugin_server")
__have_sha2 = load_server_plugin("caching_sha2_password")

#@<> ed25519 {__have_ed25519}
session.run_sql("CREATE USER ed25519_user@'%' IDENTIFIED VIA ed25519 USING PASSWORD('ed25519_pwd')")
check_login("ed25519_user", "ed25519_pwd")

#@<> ed25519, wrong password {__have_ed25519}
check_wrong_password("ed25519_user")

#@<> parsec {__have_parsec}
session.run_sql("CREATE USER parsec_user@'%' IDENTIFIED VIA parsec USING PASSWORD('parsec_pwd')")
check_login("parsec_user", "parsec_pwd")

#@<> parsec, wrong password {__have_parsec}
check_wrong_password("parsec_user")

# A wrong password isn't checked for dialog: three_attempts then asks again,
# and the dialog plugin would prompt on the terminal.
#@<> dialog {__have_dialog}
session.run_sql("CREATE USER dialog_user@'%' IDENTIFIED VIA three_attempts USING 'dialog_pwd'")
check_login("dialog_user", "dialog_pwd")

#@<> mysql_clear_password {__have_clear}
session.run_sql("CREATE USER clear_user@'%' IDENTIFIED VIA cleartext_plugin_server USING 'clear_pwd'")
check_login("clear_user", "clear_pwd")

#@<> mysql_clear_password, wrong password {__have_clear}
check_wrong_password("clear_user")

#@<> caching_sha2_password {__have_sha2}
session.run_sql("CREATE USER sha2_user@'%' IDENTIFIED VIA caching_sha2_password USING PASSWORD('sha2_pwd')")
check_login("sha2_user", "sha2_pwd")

#@<> caching_sha2_password, wrong password {__have_sha2}
check_wrong_password("sha2_user")

#@<> cleanup
session.close()
testutil.destroy_sandbox(__mysql_sandbox_port1)
