#@ {not __server_is_maria_db}
# -----------------------------------------------------------------------------
# Logging in to MySQL accounts that use sha256_password and
# caching_sha2_password. The MariaDB build's client side of both is a loadable
# plugin bundled in lib/mariadb/plugins; MariaDB has no server side of
# sha256_password, so this is the only place it is exercised (the MariaDB side
# of caching_sha2_password is in authentication_mariadb_plugins_norecord.py).
# Each login is checked in-process and in a separate mariadb-shell, over TLS and
# without it: without TLS the password goes RSA-encrypted with the server's
# public key, which the client asks the server for.

# -----------------------------------------------------------------------------
# helpers

def check_login(user, password, options=""):
    uri = f"{user}:{password}@{__host}:{__mysql_port}{options}"
    s = mysql.get_classic_session(uri)
    EXPECT_EQ(f"{user}@%", s.run_sql("SELECT current_user()").fetch_one()[0])
    s.close()
    testutil.call_mysqlsh([uri, "--sql", "-e", "SELECT concat('logged in as ', current_user())"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
    EXPECT_STDOUT_CONTAINS(f"logged in as {user}@%")
    EXPECT_STDOUT_NOT_CONTAINS("could not be loaded")
    WIPE_OUTPUT()

def check_wrong_password(user):
    EXPECT_THROWS(lambda: mysql.get_classic_session(f"{user}:wrong@{__host}:{__mysql_port}"), f"Access denied for user '{user}'")

__no_tls = "?ssl-mode=DISABLED&get-server-public-key=true"

# -----------------------------------------------------------------------------
# setup

#@<> setup
shell.connect(__mysqluripwd)
session.run_sql("DROP USER IF EXISTS sha256_user@'%', sha2_user@'%'")
session.run_sql("CREATE USER sha256_user@'%' IDENTIFIED WITH sha256_password BY 'sha256_pwd'")
session.run_sql("CREATE USER sha2_user@'%' IDENTIFIED WITH caching_sha2_password BY 'sha2_pwd'")

#@<> sha256_password
check_login("sha256_user", "sha256_pwd")

#@<> sha256_password, without TLS
check_login("sha256_user", "sha256_pwd", __no_tls)

#@<> sha256_password, wrong password
check_wrong_password("sha256_user")

#@<> caching_sha2_password
check_login("sha2_user", "sha2_pwd")

#@<> caching_sha2_password, without TLS
# FLUSH PRIVILEGES empties the server's cache, so this is a full
# authentication, the one that needs the RSA key, not the cached fast path.
session.run_sql("FLUSH PRIVILEGES")
check_login("sha2_user", "sha2_pwd", __no_tls)

#@<> caching_sha2_password, wrong password
check_wrong_password("sha2_user")

#@<> cleanup
session.run_sql("DROP USER IF EXISTS sha256_user@'%', sha2_user@'%'")
session.close()
