#@<> entry point
# imports
from contextlib import ExitStack

# constants
current_helper = shell.options["credentialStore.helper"]
has_login_path = "login-path" in shell.list_credential_helpers()
has_secret_service = "secret-service" in shell.list_credential_helpers()

used_helpers = ["plaintext"]

if has_login_path:
    used_helpers.append("login-path")

if has_secret_service:
    used_helpers.append("secret-service")

test_key = "my-key"
test_secret = "my-secret"
test_group = "3f2a8c1e-0b6d-4e7a-9c1f-5d2e8b7a4c60"
other_group = "8d1c7b2a-4e5f-4a6b-9c8d-7e6f5a4b3c2d"
actual_secret = ""
actual_all_secrets = []

# helpers
def set_helper(helper):
    shell.options["credentialStore.helper"] = helper

def wipe_all_secrets():
    for helper in shell.list_credential_helpers():
        if helper in used_helpers:
            print("--> removing all secrets from:", helper)
            set_helper(helper)
            shell.delete_all_credentials()
            for s in shell.list_secrets({"allGroups": True}):
                shell.delete_secret(s["key"], {"group": s["group"]})

def read_secret(key):
    global actual_secret
    actual_secret = shell.read_secret(key)

def list_all_secrets():
    global actual_all_secrets
    actual_all_secrets = shell.list_secrets()

def TEST(helper):
    global actual_secret
    actual_secret = None
    global actual_all_secrets
    actual_all_secrets = None
    EXPECT_NO_THROWS(lambda: set_helper(helper))
    stack = ExitStack()
    stack.callback(lambda: EXPECT_NO_THROWS(lambda: wipe_all_secrets()))
    return stack

#@<> Setup
wipe_all_secrets()

#@<> WL16958-TSFR_1_2_1 - valid key
with TEST("plaintext"):
    # store
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, test_secret))
    # read
    EXPECT_NO_THROWS(lambda: read_secret(test_key))
    # test
    EXPECT_EQ(test_secret, actual_secret)
    EXPECT_EQ("str", type(actual_secret).__name__)

#@<> WL16958-TSFR_1_2_2 - login-path limitations {has_login_path and not __mariadb_build}
# the 78-character cap is a mysql_config_editor buffer bug; MariaDB builds write
# .mylogin.cnf themselves and are only bound by the 4k line cap of the format
with TEST("login-path"):
    EXPECT_THROWS(lambda: shell.store_secret(test_key, "x" * 100), "RuntimeError: Failed to save the secret: The login-path helper cannot store secrets longer than 78 characters.")

#@<> login-path stores a long secret {has_login_path and __mariadb_build}
with TEST("login-path"):
    long_secret = "x" * 1000
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, long_secret))
    EXPECT_NO_THROWS(lambda: read_secret(test_key))
    EXPECT_EQ(long_secret, actual_secret)

#@<> WL16958-TSFR_1_2_8 - there are no limitations on secret's value {has_login_path}
with TEST("login-path"):
    # store
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, "\r\n"))
    # read
    EXPECT_NO_THROWS(lambda: read_secret(test_key))
    # test
    EXPECT_EQ("\r\n", actual_secret)

#@<> WL16958-TSFR_1_2_3 - key - invalid type
with TEST("plaintext"):
    EXPECT_THROWS(lambda: shell.store_secret({}, test_secret), "TypeError: Argument #1 is expected to be a string")

#@<> WL16958-TSFR_1_2_4 - value - invalid type
with TEST("plaintext"):
    EXPECT_THROWS(lambda: shell.store_secret(test_key, {}), "TypeError: Argument #2 is expected to be a string")

#@<> WL16958-TSFR_1_2_5 - empty key
with TEST("plaintext"):
    EXPECT_THROWS(lambda: shell.store_secret("", test_secret), "ValueError: Key cannot be empty.")

#@<> WL16958-TSFR_1_2_6 - prompt for empty value
with TEST("plaintext"):
    # store
    testutil.expect_password("Please provide the secret to store: ", "")
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key))
    # read
    EXPECT_NO_THROWS(lambda: read_secret(test_key))
    # test
    EXPECT_EQ("", actual_secret)

#@<> WL16958-TSFR_1_2_7 - prompt for non-empty value
with TEST("plaintext"):
    # store
    testutil.expect_password("Please provide the secret to store: ", test_secret)
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key))
    # read
    EXPECT_NO_THROWS(lambda: read_secret(test_key))
    # test
    EXPECT_EQ(test_secret, actual_secret)

#@<> WL16958-TSFR_1_3_1 - overwrite secret
with TEST("plaintext"):
    # store then overwrite
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, test_secret))
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, 2 * test_secret))
    # read
    EXPECT_NO_THROWS(lambda: read_secret(test_key))
    # test
    EXPECT_EQ(2 * test_secret, actual_secret)

#@<> WL16958-TSFR_2_1_1 - invalid key
with TEST("plaintext"):
    EXPECT_THROWS(lambda: shell.read_secret({}), "TypeError: Argument #1 is expected to be a string")
    EXPECT_THROWS(lambda: shell.read_secret(""), "ValueError: Key cannot be empty.")

#@<> WL16958-TSFR_2_2_1 - unknown key
with TEST("plaintext"):
    EXPECT_THROWS(lambda: shell.read_secret(test_key), "RuntimeError: Failed to read the secret: Could not find the secret")

#@<> WL16958-TSFR_3_1_1 - invalid key
with TEST("plaintext"):
    EXPECT_THROWS(lambda: shell.delete_secret({}), "TypeError: Argument #1 is expected to be a string")
    EXPECT_THROWS(lambda: shell.delete_secret(""), "ValueError: Key cannot be empty.")

#@<> WL16958-TSFR_3_1_2 - read deleted secret
with TEST("plaintext"):
    # store then delete
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, test_secret))
    EXPECT_NO_THROWS(lambda: shell.delete_secret(test_key))
    # read fails
    EXPECT_THROWS(lambda: shell.read_secret(test_key), "RuntimeError: Failed to read the secret: Could not find the secret")

#@<> WL16958-TSFR_3_2_1 - unknown key
with TEST("plaintext"):
    EXPECT_THROWS(lambda: shell.delete_secret(test_key), "RuntimeError: Failed to delete the secret: Could not find the secret")

#@<> WL16958-TSFR_4_1 - delete all secrets with no secrets stored
with TEST("plaintext"):
    EXPECT_NO_THROWS(lambda: shell.delete_all_secrets())

#@<> WL16958-TSFR_4_2 - store some secrets, then delete all of them
with TEST("plaintext"):
    # store
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, test_secret))
    EXPECT_NO_THROWS(lambda: shell.store_secret(2 * test_key, 2 * test_secret))
    # delete
    EXPECT_NO_THROWS(lambda: shell.delete_all_secrets())
    # test
    EXPECT_NO_THROWS(lambda: list_all_secrets())
    EXPECT_EQ([], actual_all_secrets)
    EXPECT_THROWS(lambda: shell.read_secret(test_key), "RuntimeError: Failed to read the secret: Could not find the secret")

#@<> WL16958-TSFR_5_1 - store some secrets, then list them
with TEST("plaintext"):
    # store
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, test_secret))
    EXPECT_NO_THROWS(lambda: shell.store_secret(2 * test_key, 2 * test_secret))
    # list
    EXPECT_NO_THROWS(lambda: list_all_secrets())
    # test
    EXPECT_EQ(2, len(actual_all_secrets))
    EXPECT_EQ({test_key, 2 * test_key}, set(actual_all_secrets))

#@<> WL16958-TSFR_5_2 - store some secrets, store a credential, then list secrets
with TEST("plaintext"):
    # store
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, test_secret))
    EXPECT_NO_THROWS(lambda: shell.store_secret(2 * test_key, 2 * test_secret))
    EXPECT_NO_THROWS(lambda: shell.store_credential("user@host", "pass"))
    # list
    EXPECT_NO_THROWS(lambda: list_all_secrets())
    # test
    EXPECT_EQ(2, len(actual_all_secrets))
    EXPECT_EQ({test_key, 2 * test_key}, set(actual_all_secrets))

#@<> WL16958-TSFR_6_1 - store a secret and a credential using the same ID
with TEST("plaintext"):
    key = "user@host"
    # store
    EXPECT_NO_THROWS(lambda: shell.store_secret(key, test_secret))
    EXPECT_NO_THROWS(lambda: shell.store_credential(key, "pass"))
    # list + read
    EXPECT_NO_THROWS(lambda: list_all_secrets())
    EXPECT_NO_THROWS(lambda: read_secret(key))
    # test
    EXPECT_EQ([key], actual_all_secrets)
    EXPECT_EQ(test_secret, actual_secret)

#@<> WL16958-ET_1 - multiple helpers

def get_alternative_helper():
    if "windows" == __os_type:
        return "windows-credential"
    elif "macos" == __os_type:
        return "keychain"
    elif has_login_path:
        return "login-path"
    elif has_secret_service:
        return "secret-service"

with TEST("plaintext"):
    # store
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, test_secret))
    # switch to a different helper
    alternative_helper = get_alternative_helper()
    EXPECT_NO_THROWS(lambda: set_helper(alternative_helper))
    # read fails
    EXPECT_THROWS(lambda: shell.read_secret(test_key), "RuntimeError: Failed to read the secret: Could not find the secret")
    # switch back
    EXPECT_NO_THROWS(lambda: set_helper("plaintext"))
    # read succeeds
    EXPECT_NO_THROWS(lambda: read_secret(test_key))
    # test
    EXPECT_EQ(test_secret, actual_secret)

#@<> groups keep the same key apart
for helper in used_helpers:
    with TEST(helper):
        print("--> helper:", helper)
        # the same key in the default group and in two other groups
        EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, "default"))
        EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, "group", {"group": test_group}))
        EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, "other", {"group": other_group}))
        # each group reads its own value
        EXPECT_EQ("default", shell.read_secret(test_key))
        EXPECT_EQ("default", shell.read_secret(test_key, {"group": "generic"}))
        EXPECT_EQ("group", shell.read_secret(test_key, {"group": test_group}))
        EXPECT_EQ("other", shell.read_secret(test_key, {"group": other_group}))
        # each group lists its own keys
        EXPECT_NO_THROWS(lambda: shell.store_secret(2 * test_key, test_secret, {"group": test_group}))
        EXPECT_EQ([test_key], shell.list_secrets())
        EXPECT_EQ({test_key, 2 * test_key}, set(shell.list_secrets({"group": test_group})))
        EXPECT_EQ([test_key], shell.list_secrets({"group": other_group}))
        # deleting in one group leaves the others alone
        EXPECT_NO_THROWS(lambda: shell.delete_secret(test_key, {"group": other_group}))
        EXPECT_EQ("default", shell.read_secret(test_key))
        EXPECT_EQ("group", shell.read_secret(test_key, {"group": test_group}))
        EXPECT_THROWS(lambda: shell.read_secret(test_key, {"group": other_group}), "RuntimeError: Failed to read the secret: Could not find the secret")
        # there is no fallback to the default group
        EXPECT_THROWS(lambda: shell.read_secret(2 * test_key), "RuntimeError: Failed to read the secret: Could not find the secret")

#@<> groups - delete all secrets of a group
with TEST("plaintext"):
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, "default"))
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, "group", {"group": test_group}))
    EXPECT_NO_THROWS(lambda: shell.store_secret(2 * test_key, "group", {"group": test_group}))
    EXPECT_NO_THROWS(lambda: shell.delete_all_secrets({"group": test_group}))
    EXPECT_EQ([], shell.list_secrets({"group": test_group}))
    EXPECT_EQ([test_key], shell.list_secrets())
    # and the other way around
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, "group", {"group": test_group}))
    EXPECT_NO_THROWS(lambda: shell.delete_all_secrets())
    EXPECT_EQ([], shell.list_secrets())
    EXPECT_EQ([test_key], shell.list_secrets({"group": test_group}))

#@<> groups - list the secrets of all groups
with TEST("plaintext"):
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, "default"))
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, "group", {"group": test_group}))
    EXPECT_NO_THROWS(lambda: shell.store_credential("user@host", "pass"))
    secrets = shell.list_secrets({"allGroups": True})
    EXPECT_EQ(2, len(secrets))
    EXPECT_EQ({(test_key, "generic"), (test_key, test_group)}, {(s["key"], s["group"]) for s in secrets})
    # credentials are not secrets, and they are not affected by groups
    EXPECT_EQ(["user@host"], shell.list_credentials())

#@<> groups - a group is normalized to lower case
with TEST("plaintext"):
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, "group", {"group": test_group.upper()}))
    EXPECT_EQ("group", shell.read_secret(test_key, {"group": test_group}))
    EXPECT_EQ([{"key": test_key, "group": test_group}], shell.list_secrets({"allGroups": True}))

#@<> groups - prompt for the value
with TEST("plaintext"):
    testutil.expect_password("Please provide the secret to store: ", test_secret)
    EXPECT_NO_THROWS(lambda: shell.store_secret(test_key, None, {"group": test_group}))
    EXPECT_EQ(test_secret, shell.read_secret(test_key, {"group": test_group}))

#@<> groups - invalid options
with TEST("plaintext"):
    for group in ["", "password", "my-group", test_group + "0", test_group.replace("-", "")]:
        EXPECT_THROWS(lambda: shell.store_secret(test_key, test_secret, {"group": group}), f"ValueError: Argument #3: Option 'group' must be 'generic' or a UUID, got: '{group}'.")
        EXPECT_THROWS(lambda: shell.read_secret(test_key, {"group": group}), f"ValueError: Argument #2: Option 'group' must be 'generic' or a UUID, got: '{group}'.")
    EXPECT_THROWS(lambda: shell.read_secret(test_key, {"group": 1}), "TypeError: Argument #2: Option 'group' is expected to be of type String, but is Integer")
    EXPECT_THROWS(lambda: shell.list_secrets({"group": test_group, "allGroups": True}), "ValueError: Argument #1: The 'group' and 'allGroups' options cannot be used together.")
    EXPECT_THROWS(lambda: shell.list_secrets({"group": "generic", "allGroups": True}), "ValueError: Argument #1: The 'group' and 'allGroups' options cannot be used together.")
    # allGroups is only an option of list_secrets()
    EXPECT_THROWS(lambda: shell.read_secret(test_key, {"allGroups": True}), "ValueError: Argument #2: Invalid options: allGroups")
    EXPECT_THROWS(lambda: shell.delete_all_secrets({"allGroups": True}), "ValueError: Argument #1: Invalid options: allGroups")

#@<> Cleanup
wipe_all_secrets()
set_helper(current_helper)
