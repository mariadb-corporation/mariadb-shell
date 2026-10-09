#@<> Initialization
# The authentication statements of the mrs module: REST AUTH APP, REST USER,
# REST ROLE, GRANT and REVOKE. They run through the SQL handler the module
# registers, so session.run_sql() returns their results like those of any
# SQL statement.
import json

testutil.deploy_sandbox(__mysql_sandbox_port1, "root")
shell.connect(__sandbox_uri1)

def rest(sql):
    return session.run_sql(sql)

def rest_rows(sql):
    return [list(row) for row in rest(sql).fetch_all()]

def rest_info(sql):
    return rest(sql).get_info()

def rest_text(sql):
    return rest(sql).fetch_one()[0]

def query_one(sql):
    return session.run_sql(sql).fetch_one()[0]

# The options column of SHOW REST ROLES holds JSON; it is compared parsed,
# as the server may reformat the text it stores
def with_parsed_options(rows):
    for row in rows:
        row[3] = json.loads(row[3]) if row[3] is not None else None
    return rows

def roles_rows(sql):
    return with_parsed_options(rest_rows(sql))

rest("CONFIGURE REST METADATA ENABLED")
rest("CREATE REST SERVICE /svc")
rest("CREATE REST SERVICE /other")

#@<> Role statements need a service
EXPECT_THROWS(lambda: rest("CREATE REST ROLE \"reader\""), "Failed to create the REST ROLE `reader`. No REST SERVICE specified.")
EXPECT_THROWS(lambda: rest("GRANT REST READ TO \"reader\""), "Failed to grant privileges for REST role `reader`. No REST SERVICE specified.")
EXPECT_THROWS(lambda: rest("SHOW REST GRANTS FOR \"reader\""), "Cannot SHOW REST GRANTs. No REST SERVICE specified.")
EXPECT_THROWS(lambda: rest("DROP REST ROLE \"reader\""), "Failed to drop the REST ROLE `reader`. No REST SERVICE specified.")
rest("USE REST SERVICE /svc")

#@<> CREATE REST AUTH APP
EXPECT_EQ("REST AUTH APP `MRS App` created successfully.", rest_info("CREATE REST AUTH APP \"MRS App\" VENDOR MRS"))
EXPECT_EQ("REST AUTH APP `MRS App` created successfully.", rest_info("CREATE REST AUTH APP IF NOT EXISTS \"MRS App\" VENDOR MRS"))
EXPECT_EQ("REST AUTH APP `MRS App` created successfully.", rest_info("CREATE OR REPLACE REST AUTH APP \"MRS App\" VENDOR MRS"))
EXPECT_THROWS(lambda: rest("CREATE REST AUTH APP \"MRS App\" VENDOR MRS"), "Failed to create the REST AUTH APP `MRS App`.")
EXPECT_EQ("REST AUTH APP `MariaDB App` created successfully.", rest_info("""CREATE REST AUTH APP "MariaDB App" VENDOR MariaDB
    DISABLED
    COMMENT "mariadb accounts"
    ALLOW NEW USERS TO REGISTER
    DEFAULT ROLE "Full Access\""""))
EXPECT_EQ(1, query_one("SELECT COUNT(*) FROM mariadb_rest_service.auth_app WHERE name = 'MRS App' AND auth_vendor_id = '30000000-0000-0000-0000-000000000000' AND limit_to_registered_users = 1 AND enabled = 1 AND default_role_id = '31000000-0000-0000-0000-000000000000'"))
EXPECT_EQ(1, query_one("SELECT COUNT(*) FROM mariadb_rest_service.auth_app WHERE name = 'MariaDB App' AND auth_vendor_id = '31000000-0000-0000-0000-000000000000' AND limit_to_registered_users = 0 AND enabled = 0 AND description = 'mariadb accounts'"))

#@<> CREATE REST AUTH APP errors
EXPECT_THROWS(lambda: rest("CREATE REST AUTH APP \"nope\" VENDOR \"Nope\""), "Failed to create the REST AUTH APP `nope`. The vendor `Nope` was not found.")
EXPECT_THROWS(lambda: rest("CREATE REST AUTH APP \"nope\" VENDOR MRS DEFAULT ROLE \"nope\""), "Failed to create the REST AUTH APP `nope`. Given role \"nope\" not found.")
EXPECT_EQ(0, query_one("SELECT COUNT(*) FROM mariadb_rest_service.auth_app WHERE name = 'nope'"))

#@<> OAuth2 vendors need URL, APP ID and APP SECRET
EXPECT_THROWS(lambda: rest("CREATE REST AUTH APP \"fb\" VENDOR \"Facebook\""), "Failed to create the REST AUTH APP `fb`. The OAuth2 vendor `Facebook` requires the URL option to be specified.")
EXPECT_THROWS(lambda: rest("CREATE REST AUTH APP \"fb\" VENDOR \"Facebook\" URL \"https://fb.example.com\""), "The OAuth2 vendor `Facebook` requires the APP/CLIENT ID option to be specified.")
EXPECT_THROWS(lambda: rest("CREATE REST AUTH APP \"fb\" VENDOR \"Facebook\" URL \"https://fb.example.com\" APP ID \"id1\""), "The OAuth2 vendor `Facebook` requires the APP/CLIENT SECRET option to be specified.")
EXPECT_EQ("REST AUTH APP `fb` created successfully.", rest_info("CREATE REST AUTH APP \"fb\" VENDOR \"Facebook\" URL \"https://fb.example.com\" CLIENT ID \"id1\" CLIENT SECRET \"secret1\""))
EXPECT_EQ(["https://fb.example.com", "id1", "secret1"], list(session.run_sql("SELECT url, app_id, access_token FROM mariadb_rest_service.auth_app WHERE name = 'fb'").fetch_one()))
EXPECT_EQ("""CREATE OR REPLACE REST AUTH APP `fb`
    VENDOR `Facebook`
    DEFAULT ROLE `Full Access`;""", rest_text("SHOW CREATE REST AUTH APP \"fb\""))
rest("DROP REST AUTH APP \"fb\"")

#@<> SHOW REST AUTH APPS and the service links
# The current service /svc has no auth apps linked yet
EXPECT_EQ([], rest_rows("SHOW REST AUTH APPS"))
EXPECT_EQ(["MariaDB", "MariaDB App", "MRS App"], [row[0] for row in session.run_sql("SELECT name FROM mariadb_rest_service.auth_app ORDER BY name").fetch_all()])
EXPECT_EQ([], rest_rows("SHOW REST AUTH APPS FROM SERVICE /svc"))
res = rest("ALTER REST SERVICE /svc ADD AUTH APP \"MRS App\" ADD AUTH APP \"MariaDB App\"")
EXPECT_EQ(1, res.get_affected_items_count())
EXPECT_EQ([["MariaDB App", "MariaDB Internal", "mariadb accounts", "DISABLED"], ["MRS App", "MRS", None, "ENABLED"]], rest_rows("SHOW REST AUTH APPS FROM SERVICE /svc"))
EXPECT_EQ(["REST AUTH APP name", "vendor", "comments", "enabled"], rest("SHOW REST AUTH APPS").get_column_names())
EXPECT_EQ([["MariaDB App", "MariaDB Internal", "mariadb accounts", "DISABLED"], ["MRS App", "MRS", None, "ENABLED"]], rest_rows("SHOW REST AUTH APPS ON SERVICE /svc"))
EXPECT_EQ([], rest_rows("SHOW REST AUTH APPS FROM SERVICE /other"))
EXPECT_THROWS(lambda: rest("ALTER REST SERVICE /svc ADD AUTH APP \"MRS App\""), "Failed to update the REST SERVICE `/svc`. The REST auth app has already been added to the REST service.")
EXPECT_THROWS(lambda: rest("ALTER REST SERVICE /svc ADD AUTH APP \"nope\""), "Failed to update the REST SERVICE `/svc`. The given REST authentication app `nope` was not found.")
EXPECT_NO_THROWS(lambda: rest("ALTER REST SERVICE /svc ADD AUTH APP \"nope\" IF EXISTS"))
EXPECT_THROWS(lambda: rest("ALTER REST SERVICE /other REMOVE AUTH APP \"MariaDB App\""), "Failed to update the REST SERVICE `/other`. The REST auth app cannot be removed as it is not assigned to the REST service.")
rest("ALTER REST SERVICE /svc REMOVE AUTH APP \"MariaDB App\"")
EXPECT_EQ([["MRS App", "MRS", None, "ENABLED"]], rest_rows("SHOW REST AUTH APPS FROM SERVICE /svc"))
EXPECT_EQ([["/other", "ENABLED", "NO", ""], ["/svc", "ENABLED", "YES", "MRS App"]], rest_rows("SHOW REST SERVICES"))

#@<> SHOW REST SERVICES FOR AUTH APP
EXPECT_EQ([["/svc", "ENABLED", "YES", "MRS App"]], rest_rows("SHOW REST SERVICES FOR AUTH APP \"MRS App\""))
EXPECT_EQ(["REST SERVICE Path", "enabled", "current", "auth_apps"], rest("SHOW REST SERVICES FOR AUTH APP \"mrs app\"").get_column_names())
EXPECT_EQ([], rest_rows("SHOW REST SERVICES FOR AUTH APP \"MariaDB App\""))
EXPECT_THROWS(lambda: rest("SHOW REST SERVICES FOR AUTH APP \"nope\""), "Cannot SHOW the REST services. The given REST AUTH APP `nope` could not be found.")

#@<> SHOW REST AUTH VENDORS
EXPECT_EQ(["REST AUTH VENDOR name", "comments", "enabled"], rest("SHOW REST AUTH VENDORS").get_column_names())
vendors = [list(row) for row in session.run_sql("SELECT name, comments, IF(enabled, 'ENABLED', 'DISABLED') FROM mariadb_rest_service.auth_vendor ORDER BY name").fetch_all()]
EXPECT_EQ(vendors, rest_rows("SHOW REST AUTH VENDORS"))
EXPECT_EQ(["MRS", "Built-in user management of MRS", "ENABLED"], [row for row in vendors if row[0] == "MRS"][0])

#@<> SHOW CREATE REST AUTH APP
EXPECT_EQ("""CREATE OR REPLACE REST AUTH APP `MRS App`
    VENDOR MRS
    DEFAULT ROLE `Full Access`;""", rest_text("SHOW CREATE REST AUTH APP \"MRS App\""))
EXPECT_EQ("""CREATE OR REPLACE REST AUTH APP `MariaDB App`
    VENDOR MARIADB
    DISABLED
    COMMENT 'mariadb accounts'
    ALLOW NEW USERS TO REGISTER
    DEFAULT ROLE `Full Access`;""", rest_text("SHOW CREATE REST AUTH APP \"MariaDB App\""))
# The name is matched case-insensitively
EXPECT_EQ(["CREATE REST AUTH APP"], rest("SHOW CREATE REST AUTH APP \"mrs app\"").get_column_names())
EXPECT_THROWS(lambda: rest("SHOW CREATE REST AUTH APP \"nope\""), "Failed to get the REST AUTH APP `nope`. The given REST AUTH APP `nope` could not be found.")

#@<> SHOW CREATE REST AUTH APP FORMAT=JSON
res = rest("SHOW CREATE REST AUTH APP \"MRS App\" FORMAT=JSON")
EXPECT_EQ(["CREATE REST AUTH APP"], res.get_column_names())
doc = json.loads(res.fetch_one()[0])
EXPECT_EQ({"name": "MRS App", "auth_vendor": "MRS", "auth_vendor_id": "30000000-0000-0000-0000-000000000000", "enabled": True, "limit_to_registered_users": True, "default_role_id": "31000000-0000-0000-0000-000000000000", "services": ["/svc"], "has_app_secret": False}, {k: doc[k] for k in ["name", "auth_vendor", "auth_vendor_id", "enabled", "limit_to_registered_users", "default_role_id", "services", "has_app_secret"]})
EXPECT_EQ([], json.loads(rest_text("SHOW CREATE REST AUTH APP \"MariaDB App\" FORMAT=JSON"))["services"])
# The app secret is never returned
rest("CREATE REST AUTH APP \"fb\" VENDOR \"Facebook\" URL \"https://fb.example.com\" CLIENT ID \"id1\" CLIENT SECRET \"secret1\"")
doc = json.loads(rest_text("SHOW CREATE REST AUTH APP \"fb\" FORMAT=JSON"))
EXPECT_EQ(["id1", True], [doc["app_id"], doc["has_app_secret"]])
EXPECT_FALSE("access_token" in doc)
EXPECT_FALSE("secret1" in rest_text("SHOW CREATE REST AUTH APP \"fb\" FORMAT=JSON"))
rest("DROP REST AUTH APP \"fb\"")

#@<> ALTER REST AUTH APP
res = rest("ALTER REST AUTH APP \"MRS App\" NEW NAME \"MRS Auth\" COMMENT \"mrs users\" ALLOW NEW USERS TO REGISTER DISABLED")
EXPECT_EQ("REST AUTH APP `MRS App` updated successfully.", res.get_info())
EXPECT_EQ(1, res.get_affected_items_count())
EXPECT_EQ("""CREATE OR REPLACE REST AUTH APP `MRS Auth`
    VENDOR MRS
    DISABLED
    COMMENT 'mrs users'
    ALLOW NEW USERS TO REGISTER
    DEFAULT ROLE `Full Access`;""", rest_text("SHOW CREATE REST AUTH APP \"MRS Auth\""))
EXPECT_THROWS(lambda: rest("ALTER REST AUTH APP \"MRS App\" ENABLED"), "Failed to update the REST AUTH APP `MRS App`. The given REST AUTH APP `MRS App` could not be found.")
EXPECT_THROWS(lambda: rest("ALTER REST AUTH APP \"MRS Auth\" DEFAULT ROLE \"nope\""), "Failed to update the REST AUTH APP `MRS Auth`. Given role \"nope\" not found.")
rest("ALTER REST AUTH APP \"MRS Auth\" NEW NAME \"MRS App\" ENABLED DO NOT ALLOW NEW USERS TO REGISTER URL \"https://mrs.example.com\" APP ID \"app\" APP SECRET \"secret\"")
EXPECT_EQ("""CREATE OR REPLACE REST AUTH APP `MRS App`
    VENDOR MRS
    COMMENT 'mrs users'
    DEFAULT ROLE `Full Access`;""", rest_text("SHOW CREATE REST AUTH APP \"MRS App\""))
EXPECT_EQ(["https://mrs.example.com", "app", "secret", 1], list(session.run_sql("SELECT url, app_id, access_token, limit_to_registered_users FROM mariadb_rest_service.auth_app WHERE name = 'MRS App'").fetch_one()))
# The renamed app keeps its service link
EXPECT_EQ([["MRS App", "MRS", "mrs users", "ENABLED"]], rest_rows("SHOW REST AUTH APPS FROM SERVICE /svc"))

#@<> CREATE REST USER
EXPECT_EQ("REST USER `:\"mike\"@\"MRS App\"` created successfully.", rest_info("CREATE REST USER \"mike\"@\"MRS App\" IDENTIFIED BY \"MariaDBR0cks!\""))
EXPECT_EQ("REST USER `:\"mike\"@\"MRS App\"` created successfully.", rest_info("CREATE REST USER IF NOT EXISTS \"mike\"@\"MRS App\" IDENTIFIED BY \"MariaDBR0cks!\""))
EXPECT_THROWS(lambda: rest("CREATE REST USER \"mike\"@\"MRS App\" IDENTIFIED BY \"MariaDBR0cks!\""), "This name has already been used.")
first_id = query_one("SELECT id FROM mariadb_rest_service.mrs_user WHERE name = 'mike'")
EXPECT_EQ("REST USER `:\"mike\"@\"MRS App\"` created successfully.", rest_info("CREATE OR REPLACE REST USER \"mike\"@\"MRS App\" IDENTIFIED BY \"MariaDBR0cks!\""))
EXPECT_NE(first_id, query_one("SELECT id FROM mariadb_rest_service.mrs_user WHERE name = 'mike'"))
EXPECT_EQ(1, query_one("SELECT COUNT(*) FROM mariadb_rest_service.mrs_user WHERE name = 'mike'"))
# The password is stored as a salted PBKDF2 derived key, as the router expects it
auth_string = query_one("SELECT auth_string FROM mariadb_rest_service.mrs_user WHERE name = 'mike'")
parts = auth_string.split("$")
EXPECT_EQ(5, len(parts), auth_string)
EXPECT_EQ(["", "A", "005"], parts[:3])
EXPECT_EQ(28, len(parts[3]), "20 salt bytes in base64")
EXPECT_EQ(44, len(parts[4]), "32 key bytes in base64")
# The key matches the scheme the router verifies (and the Python plugin's
# cypher_auth_string): SHA256(HMAC(PBKDF2-HMAC-SHA256(password, salt, 5000), "Client Key"))
import base64, hashlib, hmac
salted = hashlib.pbkdf2_hmac("sha256", b"MariaDBR0cks!", base64.b64decode(parts[3]), 5000)
client_key = hmac.new(salted, b"Client Key", hashlib.sha256).digest()
EXPECT_EQ(base64.b64encode(hashlib.sha256(client_key).digest()).decode(), parts[4])
EXPECT_EQ(1, query_one("SELECT login_permitted FROM mariadb_rest_service.mrs_user WHERE name = 'mike'"))
# Users of a MariaDB auth app have no password
EXPECT_EQ("REST USER `:\"root\"@\"MariaDB App\"` created successfully.", rest_info("CREATE REST USER \"root\"@\"MariaDB App\""))
EXPECT_EQ(None, query_one("SELECT auth_string FROM mariadb_rest_service.mrs_user WHERE name = 'root'"))

#@<> CREATE REST USER errors
EXPECT_THROWS(lambda: rest("CREATE REST USER \"joe\"@\"nope\" IDENTIFIED BY \"MariaDBR0cks!\""), "Failed to create the REST USER `:\"joe\"@\"nope\"`. The given REST AUTH APP for :\"joe\"@\"nope\" was not found.")
EXPECT_THROWS(lambda: rest("CREATE REST USER \"joe\"@\"MRS App\""), "Failed to create the REST USER `:\"joe\"@\"MRS App\"`. The authentication string is required for this app.")
EXPECT_THROWS(lambda: rest("CREATE REST USER \"joe\"@\"MRS App\" IDENTIFIED BY \"\""), "Failed to create the REST USER `:\"joe\"@\"MRS App\"`. The password must not be empty.")
EXPECT_THROWS(lambda: rest("CREATE REST USER \"joe\"@\"MRS App\" IDENTIFIED BY \"Ab1!\""), "The minimum authentication string length is 8 characters.")
EXPECT_THROWS(lambda: rest("CREATE REST USER \"joe\"@\"MRS App\" IDENTIFIED BY \"weakpassword\""), "The authentication string needs to contain at least one uppercase, lowercase, a special and a numeric character.")
EXPECT_THROWS(lambda: rest("CREATE REST USER \"joe\"@\"MariaDB App\" IDENTIFIED BY \"MariaDBR0cks!\""), "Failed to create the REST USER `:\"joe\"@\"MariaDB App\"`. Password changing not supported for this authentication method")
EXPECT_EQ(0, query_one("SELECT COUNT(*) FROM mariadb_rest_service.mrs_user WHERE name = 'joe'"))

#@<> CREATE REST USER with ACCOUNT LOCK and options
# The email, vendor_user_id and mapped_user_id keys of OPTIONS go into
# columns of their own
rest("""CREATE REST USER "boss"@"MRS App" IDENTIFIED BY "MariaDBR0cks!" ACCOUNT LOCK OPTIONS {
    "email": "boss@example.com",
    "vendor_user_id": "vendor",
    "mapped_user_id": "vendorboss123",
    "custom": "custom value"
} APP OPTIONS {"myoption": 12345}""")
EXPECT_EQ(["boss@example.com", "vendor", "vendorboss123", 0, "custom value", "12345"], list(session.run_sql("SELECT email, vendor_user_id, mapped_user_id, login_permitted, JSON_VALUE(options, '$.custom'), JSON_VALUE(app_options, '$.myoption') FROM mariadb_rest_service.mrs_user WHERE name = 'boss'").fetch_one()))
EXPECT_EQ(1, query_one("SELECT JSON_LENGTH(options) FROM mariadb_rest_service.mrs_user WHERE name = 'boss'"))

#@<> SHOW REST USERS
EXPECT_EQ(["REST USER name", "auth_app", "email", "vendor_user_id", "mapped_user_id", "login_permitted"], rest("SHOW REST USERS").get_column_names())
# The current service /svc has the MRS App linked, the MariaDB App is linked
# to no service
mrs_app_users = [["boss", "MRS App", "boss@example.com", "vendor", "vendorboss123", "NO"],
                 ["mike", "MRS App", None, None, None, "YES"]]
EXPECT_EQ(mrs_app_users, rest_rows("SHOW REST USERS"))
EXPECT_EQ(mrs_app_users, rest_rows("SHOW REST USERS ON SERVICE /svc"))
EXPECT_EQ(mrs_app_users, rest_rows("SHOW REST USERS FROM /svc FOR AUTH APP \"MRS App\""))
EXPECT_EQ([], rest_rows("SHOW REST USERS ON SERVICE /other"))
# An auth app without a service ignores the current service
EXPECT_EQ([["root", "MariaDB App", None, None, None, "YES"]], rest_rows("SHOW REST USERS FOR AUTH APP \"MariaDB App\""))
EXPECT_EQ([], rest_rows("SHOW REST USERS ON SERVICE /svc FOR AUTH APP \"MariaDB App\""))
EXPECT_THROWS(lambda: rest("SHOW REST USERS FOR AUTH APP \"nope\""), "Cannot SHOW the REST users. The given REST AUTH APP `nope` could not be found.")
EXPECT_THROWS(lambda: rest("SHOW REST USERS ON SERVICE /nope"), "Cannot SHOW the REST users. Could not find the REST SERVICE /nope.")
# Without a current service all users are listed
other_session = shell.open_session(__sandbox_uri1)
EXPECT_EQ([["root", "MariaDB App", None, None, None, "YES"]] + mrs_app_users, [list(row) for row in other_session.run_sql("SHOW REST USERS").fetch_all()])
other_session.close()

#@<> SHOW CREATE REST USER
EXPECT_EQ(["CREATE REST USER"], rest("SHOW CREATE REST USER \"mike\"@\"MRS App\"").get_column_names())
EXPECT_EQ("""CREATE OR REPLACE REST USER `mike`@`MRS App`
    IDENTIFIED BY '[Stored Password]';""", rest_text("SHOW CREATE REST USER \"mike\"@\"MRS App\""))
EXPECT_EQ("""CREATE OR REPLACE REST USER `boss`@`MRS App`
    ACCOUNT LOCK
    IDENTIFIED BY '[Stored Password]'
    OPTIONS {
        "custom": "custom value",
        "email": "boss@example.com",
        "vendor_user_id": "vendor",
        "mapped_user_id": "vendorboss123"
    }
    APP OPTIONS {
        "myoption": 12345
    };""", rest_text("SHOW CREATE REST USER \"boss\"@\"MRS App\""))
EXPECT_EQ("CREATE OR REPLACE REST USER `root`@`MariaDB App`;", rest_text("SHOW CREATE REST USER \"root\"@\"MariaDB App\""))
EXPECT_THROWS(lambda: rest("SHOW CREATE REST USER \"nobody\"@\"MRS App\""), "Failed to get the REST USER ``nobody`@`MRS App``. User ``nobody`@`MRS App`` was not found.")
EXPECT_THROWS(lambda: rest("SHOW CREATE REST USER \"mike\"@\"nope\""), "User ``mike`@`nope`` was not found.")

#@<> ALTER REST USER
res = rest("ALTER REST USER \"boss\"@\"MRS App\" ACCOUNT UNLOCK OPTIONS {\"email\": \"boss@example2.com\"}")
EXPECT_EQ("REST USER `:\"boss\"@\"MRS App\"` updated successfully.", res.get_info())
EXPECT_EQ(1, res.get_affected_items_count())
# Only the email changed; an OPTIONS document of column keys alone leaves the stored options
EXPECT_EQ("""CREATE OR REPLACE REST USER `boss`@`MRS App`
    IDENTIFIED BY '[Stored Password]'
    OPTIONS {
        "custom": "custom value",
        "email": "boss@example2.com",
        "vendor_user_id": "vendor",
        "mapped_user_id": "vendorboss123"
    }
    APP OPTIONS {
        "myoption": 12345
    };""", rest_text("SHOW CREATE REST USER \"boss\"@\"MRS App\""))
rest("ALTER REST USER \"boss\"@\"MRS App\" MERGE OPTIONS {\"x\": 1} APP OPTIONS {\"anything\": [32]}")
EXPECT_EQ("""CREATE OR REPLACE REST USER `boss`@`MRS App`
    IDENTIFIED BY '[Stored Password]'
    OPTIONS {
        "custom": "custom value",
        "x": 1,
        "email": "boss@example2.com",
        "vendor_user_id": "vendor",
        "mapped_user_id": "vendorboss123"
    }
    APP OPTIONS {
        "anything": [
            32
        ]
    };""", rest_text("SHOW CREATE REST USER \"boss\"@\"MRS App\""))
rest("ALTER REST USER \"boss\"@\"MRS App\" OPTIONS {\"y\": 2, \"vendor_user_id\": \"vendor2\", \"mapped_user_id\": \"vendor123\"}")
EXPECT_EQ("""CREATE OR REPLACE REST USER `boss`@`MRS App`
    IDENTIFIED BY '[Stored Password]'
    OPTIONS {
        "y": 2,
        "email": "boss@example2.com",
        "vendor_user_id": "vendor2",
        "mapped_user_id": "vendor123"
    }
    APP OPTIONS {
        "anything": [
            32
        ]
    };""", rest_text("SHOW CREATE REST USER \"boss\"@\"MRS App\""))

#@<> ALTER REST USER password
old_auth_string = query_one("SELECT auth_string FROM mariadb_rest_service.mrs_user WHERE name = 'mike'")
rest("ALTER REST USER \"mike\"@\"MRS App\" IDENTIFIED BY \"MariaDBR0cks!!\"")
new_auth_string = query_one("SELECT auth_string FROM mariadb_rest_service.mrs_user WHERE name = 'mike'")
EXPECT_NE(old_auth_string, new_auth_string)
EXPECT_EQ("$A$005$", new_auth_string[:7])
EXPECT_THROWS(lambda: rest("ALTER REST USER \"mike\"@\"MRS App\" IDENTIFIED BY \"\""), "Failed to update the REST USER `:\"mike\"@\"MRS App\"`. The password must not be empty.")
EXPECT_THROWS(lambda: rest("ALTER REST USER \"mike\"@\"MRS App\" IDENTIFIED BY \"weakpassword\""), "The authentication string needs to contain at least one uppercase, lowercase, a special and a numeric character.")
EXPECT_THROWS(lambda: rest("ALTER REST USER \"mike\"@\"MRS App\" IDENTIFIED BY \"Ab1!\""), "The minimum authentication string length is 8 characters.")
EXPECT_THROWS(lambda: rest("ALTER REST USER \"root\"@\"MariaDB App\" IDENTIFIED BY \"MariaDBR0cks!\""), "Failed to update the REST USER `:\"root\"@\"MariaDB App\"`. Password change not supported for the authentication method")
EXPECT_THROWS(lambda: rest("ALTER REST USER \"nobody\"@\"MRS App\" ACCOUNT LOCK"), "Failed to update the REST USER `:\"nobody\"@\"MRS App\"`. Invalid REST user \"nobody\"@\"MRS App\"")
EXPECT_THROWS(lambda: rest("ALTER REST USER \"mike\"@\"nope\" ACCOUNT LOCK"), "Failed to update the REST USER `:\"mike\"@\"nope\"`. The given REST AUTH APP for :\"mike\"@\"nope\" was not found.")
EXPECT_EQ(new_auth_string, query_one("SELECT auth_string FROM mariadb_rest_service.mrs_user WHERE name = 'mike'"))

#@<> DROP REST USER
EXPECT_THROWS(lambda: rest("DROP REST USER \"nobody\"@\"MRS App\""), "Failed to drop the REST USER `:\"nobody\"@\"MRS App\"`. User was not found.")
EXPECT_EQ("REST USER `:\"nobody\"@\"MRS App\"` dropped successfully.", rest_info("DROP REST USER IF EXISTS \"nobody\"@\"MRS App\""))
EXPECT_THROWS(lambda: rest("DROP REST USER \"mike\"@\"nope\""), "Failed to drop the REST USER `:\"mike\"@\"nope\"`. The given REST AUTH APP for :\"mike\"@\"nope\" was not found.")
EXPECT_EQ("REST USER `:\"mike\"@\"nope\"` dropped successfully.", rest_info("DROP REST USER IF EXISTS \"mike\"@\"nope\""))
EXPECT_EQ("REST USER `:\"root\"@\"MariaDB App\"` dropped successfully.", rest_info("DROP REST USER \"root\"@\"MariaDB App\""))
EXPECT_EQ(0, query_one("SELECT COUNT(*) FROM mariadb_rest_service.mrs_user WHERE name = 'root'"))
EXPECT_EQ(2, query_one("SELECT COUNT(*) FROM mariadb_rest_service.mrs_user WHERE auth_app_id = (SELECT id FROM mariadb_rest_service.auth_app WHERE name = 'MRS App')"))

#@<> CREATE REST ROLE
EXPECT_EQ("REST ROLE `reader` created successfully.", rest_info("CREATE REST ROLE \"reader\""))
EXPECT_EQ("REST ROLE `reader` created successfully.", rest_info("CREATE REST ROLE IF NOT EXISTS \"reader\""))
EXPECT_THROWS(lambda: rest("CREATE REST ROLE \"reader\""), "Failed to create the REST ROLE `reader`. DUPLICATION ERROR: The REST role `reader` has already been defined for the given service. Use the SHOW REST ROLES; command to display all existing roles.")
# A role of the same name for any service is a different role
EXPECT_EQ("REST ROLE `reader` created successfully.", rest_info("CREATE REST ROLE \"reader\" ON ANY SERVICE"))
EXPECT_THROWS(lambda: rest("CREATE REST ROLE \"reader\" ON ANY SERVICE"), "Failed to create the REST ROLE `reader`. DUPLICATION ERROR: The REST role `reader` has already been defined for the given service.")
EXPECT_EQ("REST ROLE `reader` created successfully.", rest_info("CREATE OR REPLACE REST ROLE \"reader\" ON ANY SERVICE"))
EXPECT_EQ("REST ROLE `writer` created successfully.", rest_info("CREATE REST ROLE \"writer\" EXTENDS \"reader\" COMMENT \"writes\""))
EXPECT_THROWS(lambda: rest("CREATE REST ROLE \"nope\" EXTENDS \"nope\""), "Failed to create the REST ROLE `nope`. Invalid parent role 'nope'")
# The parent role has to belong to the same service
EXPECT_THROWS(lambda: rest("CREATE REST ROLE \"nope\" EXTENDS \"writer\" ON SERVICE /other"), "Failed to create the REST ROLE `nope`. Invalid parent role 'writer'")
EXPECT_EQ("REST ROLE `otherRole` created successfully.", rest_info("CREATE REST ROLE \"otherRole\" ON SERVICE /other"))
EXPECT_EQ("REST ROLE `optioned` created successfully.", rest_info("CREATE REST ROLE \"optioned\" ON ANY SERVICE OPTIONS {\"a\": [1, 2]}"))
EXPECT_THROWS(lambda: rest("CREATE REST ROLE \"nope\" ON SERVICE /nope"), "Failed to create the REST ROLE `nope`. Could not find the REST SERVICE /nope.")
EXPECT_EQ(2, query_one("SELECT COUNT(*) FROM mariadb_rest_service.mrs_role WHERE caption = 'reader'"))
EXPECT_EQ(1, query_one("SELECT COUNT(*) FROM mariadb_rest_service.mrs_role WHERE caption = 'writer' AND description = 'writes' AND derived_from_role_id = (SELECT id FROM mariadb_rest_service.mrs_role WHERE caption = 'reader' AND specific_to_service_id IS NOT NULL)"))

#@<> SHOW REST ROLES
EXPECT_EQ(["REST role", "derived_from_role", "description", "options", "specific_to_service"], rest("SHOW REST ROLES").get_column_names())
# The roles of the current service and the global ones
EXPECT_EQ([
    ["Full Access", "", "Full access to all db_objects", None, ""],
    ["optioned", "", "", {"a": [1, 2]}, ""],
    ["reader", "", "", {}, ""],
    ["reader", "", "", {}, "/svc"],
    ["writer", "reader", "writes", {}, "/svc"]], roles_rows("SHOW REST ROLES"))
EXPECT_EQ([
    ["Full Access", "", "Full access to all db_objects", None, ""],
    ["optioned", "", "", {"a": [1, 2]}, ""],
    ["otherRole", "", "", {}, "/other"],
    ["reader", "", "", {}, ""]], roles_rows("SHOW REST ROLES ON SERVICE /other"))
EXPECT_EQ(["Full Access", "optioned", "otherRole", "reader", "reader", "writer"], [row[0] for row in roles_rows("SHOW REST ROLES ON ANY SERVICE")])
EXPECT_EQ(["Full Access", "optioned", "otherRole", "reader", "reader", "writer"], [row[0] for row in roles_rows("SHOW REST ROLES FROM ANY SERVICE")])
EXPECT_THROWS(lambda: rest("SHOW REST ROLES ON SERVICE /nope"), "Cannot SHOW REST ROLES. Could not find the REST SERVICE /nope.")

#@<> SHOW CREATE REST ROLE
EXPECT_EQ(["CREATE REST ROLE"], rest("SHOW CREATE REST ROLE \"reader\"").get_column_names())
EXPECT_EQ("CREATE REST ROLE `reader` ON SERVICE /svc;", rest_text("SHOW CREATE REST ROLE \"reader\""))
EXPECT_EQ("CREATE REST ROLE `reader` ON ANY SERVICE;", rest_text("SHOW CREATE REST ROLE \"reader\" ON ANY SERVICE"))
EXPECT_EQ("""CREATE REST ROLE `writer` EXTENDS `reader` ON SERVICE /svc
    COMMENT 'writes';""", rest_text("SHOW CREATE REST ROLE \"writer\" ON SERVICE /svc"))
EXPECT_EQ("""CREATE REST ROLE `optioned` ON ANY SERVICE
    OPTIONS {
        "a": [
            1,
            2
        ]
    };""", rest_text("SHOW CREATE REST ROLE \"optioned\" ON ANY SERVICE"))
EXPECT_EQ("CREATE REST ROLE `otherRole` ON SERVICE /other;", rest_text("SHOW CREATE REST ROLE \"otherRole\" ON /other"))
EXPECT_THROWS(lambda: rest("SHOW CREATE REST ROLE \"nope\""), "Failed to get the REST ROLE `nope`. Role `nope` was not found.")
EXPECT_THROWS(lambda: rest("SHOW CREATE REST ROLE \"writer\" ON ANY SERVICE"), "Failed to get the REST ROLE `writer`. Role `writer` was not found.")

#@<> SHOW CREATE REST ROLE FORMAT=JSON
doc = json.loads(rest_text("SHOW CREATE REST ROLE \"writer\" ON SERVICE /svc FORMAT=JSON"))
EXPECT_EQ({"caption": "writer", "derived_from_role_caption": "reader", "specific_to_service": "/svc", "description": "writes"}, {k: doc[k] for k in ["caption", "derived_from_role_caption", "specific_to_service", "description"]})
EXPECT_EQ([], doc["privileges"])
EXPECT_EQ({"a": [1, 2]}, json.loads(rest_text("SHOW CREATE REST ROLE \"optioned\" ON ANY SERVICE FORMAT=JSON"))["options"])

#@<> GRANT REST privileges
res = rest("GRANT REST READ ON SERVICE /svc SCHEMA /sakila TO \"reader\"")
EXPECT_EQ("GRANT to `reader` added successfully.", res.get_info())
# Grants on the same paths extend the privilege
rest("GRANT REST CREATE, UPDATE ON SERVICE /svc SCHEMA /sakila TO \"reader\"")
EXPECT_EQ(1, query_one("SELECT COUNT(*) FROM mariadb_rest_service.mrs_privilege WHERE role_id = (SELECT id FROM mariadb_rest_service.mrs_role WHERE caption = 'reader' AND specific_to_service_id IS NOT NULL)"))
rest("GRANT REST DELETE ON SERVICE /svc SCHEMA /sakila OBJECT /actor TO \"reader\"")
rest("GRANT REST READ TO \"reader\" ON ANY SERVICE")
rest("GRANT REST READ ON SERVICE `*` SCHEMA `` OBJECT `` TO \"writer\"")
rest("GRANT REST READ, DELETE ON SERVICE /svc SCHEMA /sakila OBJECT /actor TO \"writer\" ON SERVICE /svc")
EXPECT_THROWS(lambda: rest("GRANT REST READ TO \"nope\""), "Failed to grant privileges for REST role `nope`. Role `nope` was not found.")
EXPECT_THROWS(lambda: rest("GRANT REST READ ON SERVICE /other TO \"otherRole\""), "Failed to grant privileges for REST role `otherRole`. Role `otherRole` was not found.")
EXPECT_EQ("GRANT to `otherRole` added successfully.", rest_info("GRANT REST READ ON SERVICE /other TO \"otherRole\" ON SERVICE /other"))

#@<> SHOW REST GRANTS
EXPECT_EQ(["REST grants for reader"], rest("SHOW REST GRANTS FOR \"reader\"").get_column_names())
EXPECT_EQ([
    ["GRANT REST CREATE,READ,UPDATE ON SERVICE /svc SCHEMA /sakila OBJECT `*` TO `reader` ON SERVICE /svc"],
    ["GRANT REST DELETE ON SERVICE /svc SCHEMA /sakila OBJECT /actor TO `reader` ON SERVICE /svc"]], rest_rows("SHOW REST GRANTS FOR \"reader\""))
EXPECT_EQ([["GRANT REST READ ON SERVICE `*` SCHEMA `*` OBJECT `*` TO `reader` ON ANY SERVICE"]], rest_rows("SHOW REST GRANTS FOR \"reader\" ON ANY SERVICE"))
EXPECT_EQ([
    ["GRANT REST READ ON SERVICE `*` SCHEMA `` OBJECT `` TO `writer` ON SERVICE /svc"],
    ["GRANT REST READ,DELETE ON SERVICE /svc SCHEMA /sakila OBJECT /actor TO `writer` ON SERVICE /svc"]], rest_rows("SHOW REST GRANTS FOR \"writer\" ON SERVICE /svc"))
EXPECT_EQ([["GRANT REST READ ON SERVICE /other SCHEMA `*` OBJECT `*` TO `otherRole` ON SERVICE /other"]], rest_rows("SHOW REST GRANTS FOR \"otherRole\" FROM SERVICE /other"))
# The built-in role
EXPECT_EQ([["GRANT REST CREATE,READ,UPDATE,DELETE ON SERVICE `*` SCHEMA `*` OBJECT `*` TO `Full Access` ON ANY SERVICE"]], rest_rows("SHOW REST GRANTS FOR \"Full Access\" ON ANY SERVICE"))
EXPECT_THROWS(lambda: rest("SHOW REST GRANTS FOR \"nope\""), "Cannot SHOW REST GRANTs. No such role nope")

#@<> SHOW CREATE REST ROLE FORMAT=JSON lists the privileges
doc = json.loads(rest_text("SHOW CREATE REST ROLE \"reader\" FORMAT=JSON"))
EXPECT_EQ([[["CREATE", "READ", "UPDATE"], "/svc", "/sakila", "*"], [["DELETE"], "/svc", "/sakila", "/actor"]], sorted([[p["crud_operations"], p["service_path"], p["schema_path"], p["object_path"]] for p in doc["privileges"]], key=lambda p: p[3]))

#@<> REVOKE REST privileges
EXPECT_EQ("REVOKE from `reader` executed successfully.", rest_info("REVOKE REST CREATE ON SERVICE /svc SCHEMA /sakila FROM \"reader\""))
EXPECT_EQ([
    ["GRANT REST READ,UPDATE ON SERVICE /svc SCHEMA /sakila OBJECT `*` TO `reader` ON SERVICE /svc"],
    ["GRANT REST DELETE ON SERVICE /svc SCHEMA /sakila OBJECT /actor TO `reader` ON SERVICE /svc"]], rest_rows("SHOW REST GRANTS FOR \"reader\""))
# Revoking the last operations removes the privilege
rest("REVOKE REST READ, UPDATE ON SERVICE /svc SCHEMA /sakila FROM \"reader\"")
EXPECT_EQ([["GRANT REST DELETE ON SERVICE /svc SCHEMA /sakila OBJECT /actor TO `reader` ON SERVICE /svc"]], rest_rows("SHOW REST GRANTS FOR \"reader\""))
# Operations the privilege does not hold are ignored
rest("REVOKE REST CREATE ON SERVICE /svc SCHEMA /sakila OBJECT /actor FROM \"reader\"")
EXPECT_EQ([["GRANT REST DELETE ON SERVICE /svc SCHEMA /sakila OBJECT /actor TO `reader` ON SERVICE /svc"]], rest_rows("SHOW REST GRANTS FOR \"reader\""))
EXPECT_THROWS(lambda: rest("REVOKE REST READ ON SERVICE /nope FROM \"reader\""), "Failed to revoke privileges for REST role `reader`. There is no such grant for role reader")
EXPECT_THROWS(lambda: rest("REVOKE REST READ FROM \"nope\""), "Failed to revoke privileges for REST role `nope`. Role `nope` was not found.")
rest("REVOKE REST READ FROM \"reader\" ON ANY SERVICE")
EXPECT_EQ([], rest_rows("SHOW REST GRANTS FOR \"reader\" ON ANY SERVICE"))

#@<> GRANT REST ROLE
EXPECT_EQ("GRANT ROLE to `mike`@`MRS App` added successfully.", rest_info("GRANT REST ROLE \"reader\" TO \"mike\"@\"MRS App\" COMMENT \"Hello\""))
EXPECT_EQ("GRANT ROLE to `boss`@`MRS App` added successfully.", rest_info("GRANT REST ROLE \"reader\" ON SERVICE /svc TO \"boss\"@\"MRS App\""))
EXPECT_EQ("GRANT ROLE to `mike`@`MRS App` added successfully.", rest_info("GRANT REST ROLE \"reader\" ON ANY SERVICE TO \"mike\"@\"MRS App\""))
EXPECT_THROWS(lambda: rest("GRANT REST ROLE \"reader\" TO \"mike\"@\"MRS App\""), "Failed to grant REST role `reader` to `mike`@`MRS App`.")
EXPECT_THROWS(lambda: rest("GRANT REST ROLE \"nope\" TO \"mike\"@\"MRS App\""), "Failed to grant REST role `nope` to `mike`@`MRS App`. Role `nope` was not found.")
EXPECT_THROWS(lambda: rest("GRANT REST ROLE \"reader\" TO \"nobody\"@\"MRS App\""), "Failed to grant REST role `reader` to `nobody`@`MRS App`. User \"nobody\"@\"MRS App\" was not found.")
EXPECT_THROWS(lambda: rest("GRANT REST ROLE \"reader\" TO \"mike\"@\"nope\""), "User \"mike\"@\"nope\" was not found.")
EXPECT_EQ(3, query_one("SELECT COUNT(*) FROM mariadb_rest_service.mrs_user_has_role"))
EXPECT_EQ("Hello", query_one("SELECT comments FROM mariadb_rest_service.mrs_user_has_role WHERE user_id = (SELECT id FROM mariadb_rest_service.mrs_user WHERE name = 'mike') AND role_id = (SELECT id FROM mariadb_rest_service.mrs_role WHERE caption = 'reader' AND specific_to_service_id IS NOT NULL)"))

#@<> SHOW CREATE REST USER FORMAT=JSON
res = rest("SHOW CREATE REST USER \"mike\"@\"MRS App\" FORMAT=JSON")
EXPECT_EQ(["CREATE REST USER"], res.get_column_names())
doc = json.loads(res.fetch_one()[0])
EXPECT_EQ({"name": "mike", "auth_app_name": "MRS App", "login_permitted": True, "has_password": True}, {k: doc[k] for k in ["name", "auth_app_name", "login_permitted", "has_password"]})
EXPECT_FALSE("auth_string" in doc)
EXPECT_EQ([["reader", None], ["reader", "Hello"]], sorted([[r["caption"], r["comments"]] for r in doc["roles"]], key=lambda r: r[1] or ""))
doc = json.loads(rest_text("SHOW CREATE REST USER \"boss\"@\"MRS App\" FORMAT=JSON"))
EXPECT_EQ({"email": "boss@example2.com", "login_permitted": True}, {k: doc[k] for k in ["email", "login_permitted"]})

#@<> SHOW REST ROLES FOR a user
res = rest("SHOW REST ROLES FOR \"mike\"@\"MRS App\"")
EXPECT_EQ(["REST roles for `mike`@`MRS App`", "derived_from_role", "description", "options", "comments"], res.get_column_names())
EXPECT_EQ([["reader", "", "", {}, ""], ["reader", "", "", {}, "Hello"]], with_parsed_options([list(row) for row in res.fetch_all()]))
EXPECT_EQ([["reader", "", "", {}, ""]], roles_rows("SHOW REST ROLES FOR \"boss\"@\"MRS App\""))
EXPECT_THROWS(lambda: rest("SHOW REST ROLES FOR \"nobody\"@\"MRS App\""), "Cannot SHOW REST ROLES. User `nobody`@`MRS App` not found")
EXPECT_THROWS(lambda: rest("SHOW REST ROLES FOR \"mike\"@\"nope\""), "Cannot SHOW REST ROLES. User `mike`@`nope` not found")
# For any service the users holding the roles are listed
res = rest("SHOW REST ROLES ON ANY SERVICE FOR \"mike\"@\"MRS App\"")
EXPECT_EQ(["REST roles for `mike`@`MRS App`", "derived_from_role", "description", "options", "specific_to_service", "comments", "users"], res.get_column_names())
EXPECT_EQ([["reader", "", "", {}, "", "", "mike@MRS App"], ["reader", "", "", {}, "/svc", "", "mike@MRS App"]], with_parsed_options([list(row) for row in res.fetch_all()]))
# For an auth app the roles granted to any of its users
res = rest("SHOW REST ROLES FOR @\"MRS App\"")
EXPECT_EQ(["REST roles for @MRS App", "derived_from_role", "description", "options", "specific_to_service", "users"], res.get_column_names())
EXPECT_EQ([["reader", "", "", {}, "", "mike@MRS App"], ["reader", "", "", {}, "/svc", "boss@MRS App, mike@MRS App"]], with_parsed_options([list(row) for row in res.fetch_all()]))
EXPECT_EQ([["reader", "", "", {}, "", "mike@MRS App"]], roles_rows("SHOW REST ROLES ON SERVICE /other FOR @\"MRS App\""))
EXPECT_EQ([], roles_rows("SHOW REST ROLES FOR @\"MariaDB App\""))

#@<> REVOKE REST ROLE
EXPECT_EQ("REVOKE ROLE from `mike`@`MRS App` executed successfully.", rest_info("REVOKE REST ROLE \"reader\" FROM \"mike\"@\"MRS App\""))
EXPECT_EQ([["reader", "", "", {}, ""]], roles_rows("SHOW REST ROLES FOR \"mike\"@\"MRS App\""))
EXPECT_EQ("REVOKE ROLE from `mike`@`MRS App` executed successfully.", rest_info("REVOKE REST ROLE \"reader\" ON ANY SERVICE FROM \"mike\"@\"MRS App\""))
EXPECT_EQ([], roles_rows("SHOW REST ROLES FOR \"mike\"@\"MRS App\""))
EXPECT_THROWS(lambda: rest("REVOKE REST ROLE \"nope\" FROM \"mike\"@\"MRS App\""), "Failed to REVOKE REST role `nope` from `mike`@`MRS App`. Role `nope` was not found.")
EXPECT_THROWS(lambda: rest("REVOKE REST ROLE \"reader\" FROM \"nobody\"@\"MRS App\""), "Failed to REVOKE REST role `reader` from `nobody`@`MRS App`. The given user `nobody` was not found.")
EXPECT_EQ(1, query_one("SELECT COUNT(*) FROM mariadb_rest_service.mrs_user_has_role"))

#@<> SHOW CREATE REST SERVICE includes the roles of the service
script = rest_text("SHOW CREATE REST SERVICE /svc INCLUDING DATABASE ENDPOINTS")
EXPECT_TRUE("CREATE OR REPLACE REST SERVICE /svc\n" in script, script)
# The endpoints act on the service the script names once, with USE
EXPECT_TRUE("\n\nUSE REST SERVICE /svc;\n\nCREATE REST ROLE `reader`;\n\nCREATE REST ROLE `writer` EXTENDS `reader`\n    COMMENT 'writes';" in script, script)
EXPECT_FALSE("`optioned`" in script, script)
EXPECT_FALSE("`otherRole`" in script, script)

#@<> DROP REST ROLE
# A role other roles extend cannot be dropped
EXPECT_THROWS(lambda: rest("DROP REST ROLE \"reader\""), "Failed to drop the REST ROLE `reader`. REFERENCE ERROR: This role is referenced by other roles. Please drop those roles first. Use the SHOW REST ROLES; command to display all existing roles.")
EXPECT_EQ("REST ROLE `writer` dropped successfully.", rest_info("DROP REST ROLE \"writer\""))
# The privileges and user grants go with the role
EXPECT_EQ("REST ROLE `reader` dropped successfully.", rest_info("DROP REST ROLE \"reader\" ON SERVICE /svc"))
EXPECT_EQ(0, query_one("SELECT COUNT(*) FROM mariadb_rest_service.mrs_user_has_role"))
EXPECT_EQ(0, query_one("SELECT COUNT(*) FROM mariadb_rest_service.mrs_privilege WHERE role_id NOT IN (SELECT id FROM mariadb_rest_service.mrs_role)"))
EXPECT_THROWS(lambda: rest("DROP REST ROLE \"nope\""), "Failed to drop the REST ROLE `nope`. Role `nope` was not found.")
EXPECT_EQ("REST ROLE `nope` dropped successfully.", rest_info("DROP REST ROLE IF EXISTS \"nope\""))
EXPECT_EQ("REST ROLE `reader` dropped successfully.", rest_info("DROP REST ROLE \"reader\" ON ANY SERVICE"))
EXPECT_EQ("REST ROLE `optioned` dropped successfully.", rest_info("DROP REST ROLE \"optioned\" ON ANY SERVICE"))
EXPECT_EQ("REST ROLE `otherRole` dropped successfully.", rest_info("DROP REST ROLE \"otherRole\" ON /other"))
EXPECT_EQ([["Full Access", "", "Full access to all db_objects", None, ""]], roles_rows("SHOW REST ROLES ON ANY SERVICE"))

#@<> DROP REST AUTH APP
EXPECT_THROWS(lambda: rest("DROP REST AUTH APP \"nope\""), "Failed to drop the REST AUTH APP `nope`. The given REST AUTH APP `nope` could not be found.")
EXPECT_EQ("REST AUTH APP `nope` dropped successfully.", rest_info("DROP REST AUTH APP IF EXISTS \"nope\""))
# The users and the service links go with the app
EXPECT_EQ("REST AUTH APP `MRS App` dropped successfully.", rest_info("DROP REST AUTH APP \"MRS App\""))
EXPECT_EQ(0, query_one("SELECT COUNT(*) FROM mariadb_rest_service.mrs_user WHERE name IN ('mike', 'boss')"))
EXPECT_EQ([], rest_rows("SHOW REST AUTH APPS FROM SERVICE /svc"))
EXPECT_EQ("REST AUTH APP `mariadb app` dropped successfully.", rest_info("DROP REST AUTH APP IF EXISTS \"mariadb app\""))
EXPECT_EQ(["MariaDB"], [row[0] for row in session.run_sql("SELECT name FROM mariadb_rest_service.auth_app ORDER BY name").fetch_all()])

#@<> Cleanup
rest("DROP REST SERVICE /svc")
rest("DROP REST SERVICE /other")
session.close()
testutil.destroy_sandbox(__mysql_sandbox_port1)
