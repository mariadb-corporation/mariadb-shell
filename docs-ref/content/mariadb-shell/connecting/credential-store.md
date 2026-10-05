---
description: >-
  Let MariaDB Shell remember the passwords you type, choose the helper that
  stores them on each platform, and manage stored passwords and other secrets.
---

# Credential Store

MariaDB Shell can remember the password of each account you connect with, so that it doesn't prompt you again. It hands the passwords to a credential helper, a small program that keeps them in the platform's password storage, such as the macOS keychain. The same helpers also store arbitrary secrets for your scripts, such as API keys.

No setup is needed: every platform has a default helper that works out of the box.

## How It Works

When you connect without giving a password, MariaDB Shell first asks the helper for a stored password for the account. If there is one, it connects with it. If there is none, MariaDB Shell prompts for the password and, after a successful connection, offers to save it:

```text
Please provide the password for 'dba@db1.example.com:3306': ********
Save password for 'dba@db1.example.com:3306'? [Y]es/[N]o/Ne[v]er (default No): y
```

* `Yes` saves the password.
* `No` doesn't save it this time.
* `Never` doesn't save it and stops asking for this account for the rest of the session. To make that permanent, add the account to `credentialStore.excludeFilters`.

If the server rejects a stored password, for example after the password was changed, MariaDB Shell deletes the stored password and prompts again.

Passwords are stored under a key of the form `user@host:port`, or `user@socket` for socket connections, without the scheme and the schema. SSH passwords for [tunnels](ssh-tunnels.md#authentication) are stored under `ssh://user@host:port`.

MariaDB Shell doesn't save passwords that it reads with `--passwords-from-stdin`, and with `--no-wizard` it doesn't ask, so in the default `prompt` mode it saves nothing.

## Helpers

| Helper | Platform | Storage |
| --- | --- | --- |
| `keychain` | macOS (default) | The macOS keychain. |
| `login-path` | Linux (default), macOS | The file `~/.mylogin.cnf`. |
| `secret-service` | Linux | A keyring daemon, such as GNOME Keyring, through the Secret Service D-Bus API. |
| `windows-credential` | Windows (default) | The Windows Credential Manager. |

The helpers are separate executables named `mariadb-secret-store-<helper>`, installed next to the `mariadb-shell` binary. To see which helpers work on your system, call `shell.list_credential_helpers()`:

```python
print(shell.list_credential_helpers())
```

On macOS, for example, this prints `["login-path", "keychain"]`.

A development build may also list a `plaintext` helper. It is a test fixture, isn't part of the packages, and must not be used for real passwords.

### The login-path Helper

On Linux, the default helper keeps passwords in `~/.mylogin.cnf`. It needs no daemon and no external tool: MariaDB Shell reads and writes the file itself, in the format that MySQL's `mysql_config_editor` uses, so that tool and MariaDB Shell can read each other's entries.

{% hint style="warning" %}
The passwords in `~/.mylogin.cnf` are obfuscated, not encrypted. The file is AES-encrypted, but the key is stored in the file itself, so anyone who can read the file can recover the passwords. Its protection is the file permissions. MariaDB Shell creates the file with mode `0600`. If you have an older file with looser permissions, tighten them with `chmod 600 ~/.mylogin.cnf`, and don't copy the file to shared hosts or into backups that you wouldn't trust with plain-text passwords.
{% endhint %}

To use a different file, set the `MYSQL_TEST_LOGIN_FILE` environment variable to its path.

MariaDB Shell doesn't apply the connection settings in `~/.mylogin.cnf` at startup, and has no `--login-path` option. See [Option Files and Login Paths](option-files-and-login-paths.md#login-paths).

### The secret-service Helper

If you prefer that your passwords are protected by your desktop keyring, select the `secret-service` helper. It needs the following:

* The `secret-tool` command, from the `libsecret-tools` package (Debian, Ubuntu, openSUSE) or the `libsecret` package (Fedora, RHEL, Arch).
* A Secret Service provider, such as `gnome-keyring`.
* A D-Bus session bus with an unlocked keyring. A graphical desktop session provides both. Over SSH, in containers, and on CI runners, you have to start them yourself, for example with `dbus-run-session` and `gnome-keyring-daemon --unlock`.

`shell.list_credential_helpers()` lists `secret-service` only if `secret-tool` can reach the keyring. Once it does, select the helper:

```sh
mariadb-shell --py -e "shell.options.set_persist('credentialStore.helper', 'secret-service')"
```

## Options

| Option | Type | Default | Description |
| --- | --- | --- | --- |
| `credentialStore.helper` | string | `default` | The helper to use. `default` selects the platform default, and `<disabled>` turns the credential store off. Any other value must be a name that `shell.list_credential_helpers()` returns. |
| `credentialStore.savePasswords` | string | `prompt` | When to save passwords: `always`, `prompt` (ask each time), or `never`. |
| `credentialStore.excludeFilters` | array | `[]` | Accounts whose passwords are never saved, as patterns of the form `user@host:port`. The wildcards `*` and `?` are supported. |

You can also set the first two for a single run, with command-line options or environment variables:

| Option | Command line | Environment variable |
| --- | --- | --- |
| `credentialStore.helper` | `--credential-store-helper=<helper>` | `MARIADB_SHELL_CREDENTIAL_STORE_HELPER` |
| `credentialStore.savePasswords` | `--save-passwords=<value>` | `MARIADB_SHELL_CREDENTIAL_STORE_SAVE_PASSWORDS` |

Examples:

```python
shell.options.set_persist("credentialStore.savePasswords", "always")
shell.options.set_persist("credentialStore.excludeFilters", ["root@*", "*@prod-*"])
shell.options.set_persist("credentialStore.helper", "<disabled>")
```

If the configured helper can't be initialized, `credentialStore.helper` reads `<invalid>`. MariaDB Shell then prompts for every password and saves none. The reason is in the shell log. See [Troubleshooting](#troubleshooting).

## Managing Stored Passwords

The following functions of the `shell` object manage stored passwords. They take a key of the form `user@host[:port]`, or `user@socket`. Write a socket path in parentheses or with encoded slashes, as in a URI.

| Function | Description |
| --- | --- |
| `shell.store_credential(url[, password])` | Stores a password, replacing any stored one. Prompts for the password if you leave it out. |
| `shell.list_credentials()` | Returns the keys of all stored passwords. The passwords themselves aren't returned. |
| `shell.delete_credential(url)` | Deletes the stored password for one key. |
| `shell.delete_all_credentials()` | Deletes all stored passwords of the configured helper. |
| `shell.list_credential_helpers()` | Returns the names of the helpers available on this system. |

```python
shell.store_credential("dba@db1.example.com:3306")   # prompts for the password
shell.store_credential("app@(/run/mysqld/mysqld.sock)", "S3cret!")
shell.list_credentials()
shell.delete_credential("dba@db1.example.com:3306")
```

To find the exact keys that MariaDB Shell saved, call `shell.list_credentials()`.

From a system shell, use [command line integration](../using-mariadb-shell/command-line-integration.md):

```sh
mariadb-shell -- shell list-credentials
mariadb-shell -- shell delete-credential dba@db1.example.com:3306
```

## Storing Secrets

Scripts and plugins often need secrets other than database passwords, such as object storage keys or API tokens. The secret functions store them with the same helper, under a key that you choose:

| Function | Description |
| --- | --- |
| `shell.store_secret(key[, value])` | Stores a secret, replacing any stored value. Prompts for the value if you leave it out. |
| `shell.read_secret(key)` | Returns the secret stored under the key. |
| `shell.list_secrets()` | Returns the keys of all stored secrets. |
| `shell.delete_secret(key)` | Deletes one secret. |
| `shell.delete_all_secrets()` | Deletes all secrets of the configured helper. |

```python
shell.store_secret("reports/smtp-password")      # prompts for the value
password = shell.read_secret("reports/smtp-password")
shell.list_secrets()
shell.delete_secret("reports/smtp-password")
```

Secrets are kept apart from stored passwords: `shell.list_credentials()` doesn't show them, and `shell.delete_all_credentials()` doesn't delete them.

## Helper Limitations

The helpers restrict which characters a stored password may contain. These limits don't apply to values stored with `shell.store_secret()`.

| Helper | Not allowed |
| --- | --- |
| `login-path` | The control characters `\0`, `0x03`, `0x04`, `\n`, `\r`, `0x0F`, `0x11`, `0x12`, `0x13`, `0x15`, `0x16`, `0x17`, `0x19`, `0x1A`, `0x1C`, and `0x7F`. |
| `keychain` | `\0` and `\n`. |
| `secret-service` | Anything that isn't valid UTF-8. |

## Troubleshooting

To find out why a helper doesn't work, run it directly with the `version` command. It exits with `0` when it is healthy, and otherwise prints the reason:

```sh
mariadb-secret-store-login-path version; echo "exit=$?"
mariadb-secret-store-secret-service version; echo "exit=$?"
```

| Symptom | Cause | Solution |
| --- | --- | --- |
| `login-path` reports that `~/.mylogin.cnf` can't be opened or read. | The file belongs to another user, or its directory isn't writable. | Fix the ownership, or remove the file and save the passwords again. |
| `login-path` reports that the file is corrupted. | The file was truncated or edited by hand. | Remove the file. MariaDB Shell creates a new one. |
| `The name org.freedesktop.secrets was not provided by any .service files` | No keyring daemon is installed. | Install `gnome-keyring`. |
| `Cannot autolaunch D-Bus without X11 $DISPLAY`, or `DBUS_SESSION_BUS_ADDRESS` is empty. | No D-Bus session bus, for example over SSH. | Start one with `dbus-run-session`, or use `login-path`. |
| `Prompt was dismissed` or `Cannot prompt` when saving. | The keyring is locked, and nothing can ask for its password. | Unlock it with `gnome-keyring-daemon --unlock`. |
| `credentialStore.helper` reads `<invalid>`. | The helper failed to start. | Run the helper with `version`, and look for `Failed to initialize the default helper` in the shell log. |

To see the error that the shell logged at startup:

```sh
mariadb-shell --log-level=debug --py -e "print(shell.options['credentialStore.helper'])"
grep -i helper ~/.mariadb-shell/mariadb-shell.log | tail
```

For more about the log, see [Logging and Debugging](../logging-and-debugging.md).
