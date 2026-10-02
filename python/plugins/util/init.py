# Copyright (c) 2025, Oracle and/or its affiliates.
# Copyright (c) 2026, MariaDB plc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# This program is designed to work with certain software (including
# but not limited to OpenSSL) that is licensed under separate terms,
# as designated in a particular file or component or in included license
# documentation.  The authors of MySQL hereby grant you an additional
# permission to link the program and your derivative works with the
# separately licensed software that they have either included with
# the program or referenced in the documentation.
#
# This program is distributed in the hope that it will be useful,  but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
# the GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

from mysqlsh.plugin_manager import plugin, plugin_function
from mysqlsh import globals, Error
from mysqlsh.mysql import split_account

import util.lib.common as common
import util.lib.account as accountlib

def prompt_confirmed_password(newPassword, is_new: bool, custom_description: str = None) -> str:
    if newPassword is not None:
        return newPassword
    
    is_new = "new " if is_new else ""

    description = [custom_description] if custom_description else []
    for _ in range(3):
        new_password = globals.shell.prompt(
            f"Enter {is_new}password", {"type": "password", "description": description})
        confirm = globals.shell.prompt(
            f"Confirm {is_new}password", {"type": "password"})
        if new_password == confirm:
            return new_password
        description = ["Passwords don't match."]

    raise Error("Failed to enter matching passwords.")


def check_mysql_native_method(session, account_data):
    if not accountlib.is_auth_mysql_native(session, account_data):
        globals.shell.print(
            f"Account {account_data['account']} is not using \"mysql_native_password\". No changes were made.", "note")
        return False
    return True


def password_extension_unsupported(session, functionality: str) -> Error:
    if session.server_vendor == "MySQL":
        return Error(f"{functionality} functionality is only supported from server version at least 8.0.")
    return Error(f"{functionality} functionality is not supported by {session.server_vendor} servers.")


def discard_dual_if_present(discardOld: bool, account_data: dict):
    if discardOld:
        session = globals.shell.get_session()
        if not accountlib.is_password_extensions_supported(session):
            raise password_extension_unsupported(session, "Dual password")
        try:
            if accountlib.discard_dual_password(account_data):
                globals.shell.print("Old (dual) password was succesfully discarded.", "note")
            else:
                globals.shell.print("Account " + account_data["account"] +
                      " does not have a dual password present.", "note")
        except Error as e:
            globals.shell.print(f"Failed to discard old password: {e.msg}", "error")
        return False
    return True


def check_dual_password(session, account_data: dict, dual: bool, auth_replace : bool = False):
    if not accountlib.is_password_extensions_supported(session):
        if dual:
            raise password_extension_unsupported(session, "Dual password")
        # No dual passwords on this server, and has_dual_password() would query
        # a mysql.user column that does not exist on it.
        return

    has_dual = False
    try:
        has_dual = accountlib.has_dual_password(session, account_data)
    except Error as e:
        globals.shell.log(
                "WARNING", f"Could not retrieve dual password status for {account_data['account']}: {e.msg}")
    
    if has_dual:
        message = f"The account {account_data['account']} has a retained old (dual) password."
        if auth_replace:
            message += " It will be lost on auth method replacement."
        type = "warning"
        suggestion = "It can be discarded"
        instruction = "using util.<<<changePassword>>>({\"account\":\"" + account_data["account"] + "\", \"discardOld\": True})."
        if dual or auth_replace:
            type = "error"
            suggestion = "Please discard it first"

        globals.shell.print(f"{message} {suggestion} {instruction}", type)

        if type == "error":
            raise Error("Unable to discard existing dual password implicitly")


def check_before_change_password(session, account_data: dict, dual: bool, random : bool):
    # The MariaDB build does not register util.upgradeAuthMethod (see below), so
    # there is nothing to point the user to. Keep is_auth_mysql_native() first:
    # its SHOW CREATE USER is also what rejects an unprivileged caller before the
    # new password is prompted for.
    if accountlib.is_auth_mysql_native(session, account_data) and accountlib.is_server_auth_method_upgradable(session) and not common.is_mariadb_build():
        globals.shell.print("The account " + account_data["account"] +
            " is using the deprecated mysql_native_password authentication plugin. "
            "Please use the \"util.<<<changeAuthMethod>>>\" function to upgrade it to caching_sha2_password.", "warning")

    check_dual_password(session, account_data, dual)

    if random and not accountlib.is_password_extensions_supported(session):
        raise password_extension_unsupported(session, "Random password")


def internal_change_password(
        account: str = None,
        random: bool = False,
        dual: bool = False,
        discardOld: bool = False,
        newPassword: str = None
) -> str:
    if not globals.shell.get_session():
        print("Not connected!")
        return None

    if random and newPassword:
        raise Error("random and newPassword options are mutually exclusive.")

    session = globals.shell.get_session()

    account_data, account, current_user = common.collect_account_data(account)

    if not discard_dual_if_present(discardOld, account_data):
        return None

    print("Changing password for " + account + ".")

    check_before_change_password(session, account_data, dual, random)

    description = None
    for _ in range(3):
        if not random:
            newPassword = prompt_confirmed_password(
                newPassword, True, description)
        try:
            result = accountlib.change_password(
                session, account_data, random, dual, newPassword, current_user)

            if random:
                globals.shell.print(
                    "Password has been successfully updated to a random one.", "note")
                return result
            else:
                if dual:
                    globals.shell.print(
                        "Password changed successfully, the current password will remain valid (dual password enabled).", "note")
                    globals.shell.print(
                        "To discard the old password, use util.<<<changePassword>>>({\"account\":\"" + account + "\", \"discardOld\":True}).")
                else:
                    globals.shell.print(
                        "Password has been successfully updated.", "note")

            return None
        except Error as e:
            description = e.msg
            if e.code == 1819 and not random:  # password policy error code
                newPassword = None
                continue
            else:
                break

    raise Error(f"Failed to change password: {description}")


def internal_upgrade_auth_method(
        account: str = None,
        password: str = None
):

    session = globals.shell.get_session()

    if not session:
        print("Not connected!")
        return

    if not accountlib.is_server_auth_method_upgradable(session):
        if session.server_vendor != "MySQL":
            raise Error(f"Upgrading the authentication method is not needed on {session.server_vendor} servers: "
                        "mysql_native_password is their default authentication plugin and is not deprecated.")
        raise Error("Unsupported server version. "
                    "To upgrade authentication method to caching_sha2_password, please upgrade the server to version at least 8.0.")

    account_data, _, _ = common.collect_account_data(account)

    if not check_mysql_native_method(session, account_data):
        return

    check_dual_password(session, account_data, False, True)

    description = None
    for _ in range(3):
        password = prompt_confirmed_password(password, False, description)

        print(
            "Switching authentication plugin from mysql_native_password to caching_sha2_password for account " + account_data["account"] + ".")

        try:
            accountlib.upgrade_auth_method(session, account_data, password)

            globals.shell.print(
                "Authentication and password has been successfully updated.", "note")

            return
        except Error as e:
            description = e.msg
            if e.code == 1819:
                password = None
                continue
            else:
                break

    raise Error(f"Failed to change authentication method: {description}")


# The random, dual and discardOld options rely on MySQL 8.0 statements that
# MariaDB does not have (RANDOM PASSWORD, RETAIN CURRENT PASSWORD, DISCARD OLD
# PASSWORD), so the MariaDB build registers util.changePassword without them;
# the registrar then rejects them as invalid options.
if common.is_mariadb_build():
    @plugin_function("util.changePassword")
    def change_password(**options):
        """Changes password for an account.

        Changes the password of an account. If no account is specified, the
        currently authenticated user is used. The new password is prompted for,
        twice, unless it is given in the newPassword option.

        Changing the password of another account requires the CREATE USER
        privilege, or the UPDATE privilege on the mysql schema.

        Examples:
            util.<<<changePassword>>>() - changes the password of the current user.

            util.<<<changePassword>>>({"account":"user@localhost"}) - changes the password of "user@localhost".

            util.<<<changePassword>>>({"newPassword":"xxxx"}) - changes the password of the current user to "xxxx" without prompting.

        Args:
            **options (dict): Optional arguments

        Keyword Args:
            account (str): account whose password will be changed. If not set, the currently authenticated user is used (default not set)
            newPassword (str): password to change to for the specified account. If not set, it will be prompted (default not set)
        """
        internal_change_password(**options)
else:
    @plugin_function("util.changePassword")
    def change_password(**options) -> str:
        """Changes password for an account.

        Changes the password of an account.
        If no account is specified, currently authenticated user is used.

        Examples:
            util.<<<changePassword>>>() - changes the password of current user.
            util.<<<changePassword>>>({"account":"user@localhost", "random":True}) - changes the password of "user@localhost" to a random one.
            util.<<<changePassword>>>({"newPassword":"xxxx", "dual":True"}) - changes password to newPassword without prompting, retains current as dual password.
            util.<<<changePassword>>>({"discardOld": True, "account": "other@localhost"}) - Discard retained password for account "other@localhost".

        Args:
            **options (dict): Optional arguments

        Keyword Args:
            account (str): account of wchich password will be changed. If not set, currently authenticated user will be used (default not set)
            random (bool): changes password to a random one and returns it. Cannot be used with "newPassword" option (default False)
            dual (bool): retains current password after changing (default False)
            discardOld (bool):  discards retained old password if present. If True, will ignore other options (beside account) and not change current password. (default False)
            newPassword (str): password to change to for the specified account. If not set, it will be prompted. Cannot be used with "random" option (default not set)
        """
        return internal_change_password(**options)


# util.upgradeAuthMethod is MySQL-only: it migrates accounts off
# mysql_native_password, which MySQL deprecated in 8.0 and removed in 9.0, to
# caching_sha2_password. In MariaDB mysql_native_password is the default and is
# not deprecated, so the MariaDB build does not register it - the same as the
# \help mysql_native_password topic in modules/util/mod_util.cc.
if not common.is_mariadb_build():
    @plugin_function("util.upgradeAuthMethod")
    def upgrade_auth_method(**options):
        """Upgrades authentication plugin of an account.

        Checks and upgrades authentication method of an account from deprecated mysql_native_password to caching_sha2_password.
        If no account is specified, currently authenticated user is used.
        If targeted account does not use mysql_native_password as its authentication method, function exits.

        Args:
            **options (dict): Optional arguments

        Keyword Args:
            account (str): account to wchich upgrade will be performed; if not set, currently authenticated user will be used (default not set)
            password (str): password of currently authenticated user, that will be making changes; if not set, it will be prompted (default not set)
        """
        internal_upgrade_auth_method(**options)
