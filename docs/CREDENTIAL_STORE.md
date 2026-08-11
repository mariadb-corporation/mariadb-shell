# Credential store setup (Linux)

MariaDB Shell can remember the passwords you type when connecting, so you are not
prompted again for the same account. **No setup is required on any platform.** On
Linux the default helper is `login-path`, which keeps the passwords in
`~/.mylogin.cnf` and needs no external command and no background daemon. Windows
and macOS use the Windows Credential Manager and the macOS keychain, which are
always present.

Everything below is about the **optional** `secret-service` helper, which stores
passwords in a keyring daemon instead. Choose it if you want your MariaDB Shell
passwords protected by the same keyring as the rest of your desktop session — the
`login-path` file is only obfuscated (see §0). It costs a keyring daemon and a
D-Bus session bus as runtime dependencies, which is why it is not the default.

## 0. The default: `login-path`

`login-path` writes `~/.mylogin.cnf`, the same file MySQL's `mysql_config_editor`
produces, in the same on-disk format. MariaDB Shell reads and writes it itself —
`mysql_config_editor` is not needed and is not shipped.

**The file is obfuscated, not encrypted.** Its contents are AES-encrypted, but the
key is stored in the file's own header, so anyone who can read the file can
recover the passwords. This is inherited from the MySQL format and is deliberate:
the protection is the file's permissions, not the cipher. MariaDB Shell creates it
mode `0600`. If you have an older file with looser permissions the shell still
reads it, but you should tighten it:

```bash
chmod 600 ~/.mylogin.cnf
```

Treat `~/.mylogin.cnf` as you would a private key: do not copy it to shared hosts,
do not commit it, and do not include it in backups you would not trust with
plaintext passwords. If that is not acceptable for your environment, switch to
`secret-service` (§1-§4) or turn the credential store off entirely:

```bash
mariadb-shell --py -e "shell.options.set_persist('credentialStore.helper', '<disabled>')"
```

Point the shell at a different file with the `MYSQL_TEST_LOGIN_FILE` environment
variable (the same variable MySQL's tooling honours).

## 1. What the `secret-service` helper needs

The `secret-service` helper calls the `secret-tool` command, which in turn talks to
whatever daemon owns the `org.freedesktop.secrets` name on your **D-Bus session
bus**. So three things must be in place:

| Requirement | Provided by |
| --- | --- |
| The `secret-tool` command | `libsecret-tools` / `libsecret` package |
| A Secret Service provider (keyring daemon) | `gnome-keyring` (GNOME, and works fine elsewhere) |
| A D-Bus **session** bus, with the keyring **unlocked** | Your desktop session, or set up manually (§4) |

Inside a normal desktop session all three are usually already true — see §3 to
confirm. Over plain SSH, in a container, or on a CI runner, none of them are, and
§4 applies. Select the helper once the chain works:

```bash
mariadb-shell --py -e "shell.options.set_persist('credentialStore.helper', 'secret-service')"
```

## 2. Install the packages

**Debian / Ubuntu**

```bash
sudo apt-get install -y gnome-keyring libsecret-tools
```

On a headless host add `dbus-bin` — the CI setup in §4 needs `dbus-send`, and
`dbus-run-session` lives there too.

**Fedora / RHEL / CentOS**

```bash
sudo dnf install -y gnome-keyring libsecret
```

**openSUSE**

```bash
sudo zypper install -y gnome-keyring libsecret-tools
```

**Arch**

```bash
sudo pacman -S --needed gnome-keyring libsecret
```

`secret-tool` alone is *not* enough — it is only a client. If the daemon is
missing you will get `The name org.freedesktop.secrets was not provided by any
.service files` (see §6).

## 3. Verify `secret-service` (desktop session)

Log in graphically, open a terminal, and check the whole chain bottom-up:

```bash
# 1. a session bus exists
echo "$DBUS_SESSION_BUS_ADDRESS"

# 2. the Secret Service answers
secret-tool search --all secret_store secret-service; echo "exit=$?"

# 3. the shell sees the helper
mariadb-shell --py -e "print(shell.list_credential_helpers())"

# 4. the shell can use it
mariadb-shell --credential-store-helper=secret-service --py \
  -e "print(shell.options['credentialStore.helper'])"
```

Expected: a non-empty bus address; step 2 exits `0` (no output yet is normal —
nothing is stored); step 3 lists `secret-service` alongside `login-path`; step 4
prints `secret-service`.

If step 3 does not list it, or step 4 prints `<invalid>`, the helper could not be
initialized — go to §6. Note that `login-path` keeps working either way, so the
shell will not stop remembering passwords; it just will not use the keyring.

## 4. Headless: SSH, containers, CI

Without a desktop session there is no keyring daemon running and, often, no
session bus at all. This is the case where `login-path` — the default — is simply
the better answer, and nothing in this section is needed. Follow it only if you
specifically want `secret-service` on a headless host. Create both around the
command you want to run:

```bash
dbus-run-session -- sh -c '
  printf "my-keyring-password\n" | gnome-keyring-daemon --unlock --components=secrets >/dev/null 2>&1
  mariadb-shell --py -e "print(shell.options[\"credentialStore.helper\"])"'
```

* `dbus-run-session` starts a private session bus, so this works whether or not
  the environment already has one. It ships in `dbus-bin` (older distros:
  `dbus-x11`).
* `gnome-keyring-daemon --unlock` reads one password **line** from stdin, and
  creates the login keyring if it does not exist yet — the same path PAM takes
  when you log in graphically. That keyring is the default collection everything
  stores into, so getting this wrong means stores fail with `Object does not
  exist at path "/org/freedesktop/secrets/collection/login"` even though the
  daemon is running and answering. Use a **non-empty, newline-terminated**
  password: an empty one or a missing newline leaves you with no keyring.
* The explicit unlock matters: D-Bus activation alone would start the daemon with
  a **locked** keyring, and with no desktop to show an unlock prompt every store
  would fail.
* `--components=secrets` starts only the Secret Service, not the SSH/PKCS#11 agents.

Secrets stored this way live in the keyring created inside that session. To keep
them across sessions, reuse the same keyring password and the same `$HOME`.

**In CI**, the credential-store test suites (`Mysqlsh_credential_store.*`,
`Mysql_secret_store*`) no longer *need* any of this — they exercise whichever
helpers are available, and `login-path` always is. Setting up a keyring only adds
`secret-service` coverage. If you do want it, note that a CI runner started as a
service has no session bus and no keyring, and the helper would be reported as
`<invalid>`.

If a single step both sets up and runs, the wrapper above is enough. If setup and
tests are separate steps, it is not: the bus `dbus-run-session` creates lives only
as long as the command it wraps, and each step is a separate process. Fork the bus
in the setup step instead and publish its address, as
[`.github/workflows/shell-ci.yml`](../.github/workflows/shell-ci.yml) does:

```yaml
- name: Setup Test Environment
  id: setup
  run: |
    # before the bus fork: dbus-daemon hands this environment to whatever it activates
    export XDG_DATA_HOME="$RUNNER_TEMP/keyring-home"
    rm -rf "$XDG_DATA_HOME"
    mkdir -p "$XDG_DATA_HOME/keyrings"

    pid_file="$(mktemp)"
    DBUS_SESSION_BUS_ADDRESS="$(dbus-daemon --session --fork \
      --print-address=1 --print-pid=3 3>"$pid_file")"
    export DBUS_SESSION_BUS_ADDRESS
    echo "DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS" >> $GITHUB_ENV
    echo "dbus_pid=$(cat "$pid_file")" >> $GITHUB_OUTPUT
    rm -f "$pid_file"

    keyring_log="$RUNNER_TEMP/gnome-keyring.log"
    printf 'ci\n' | gnome-keyring-daemon --unlock --replace --components=secrets --foreground >"$keyring_log" 2>&1 &
    keyring_pid=$!
    echo "keyring_pid=$keyring_pid" >> $GITHUB_OUTPUT

    # wait for *this* daemon to own the name (see below), then probe
    secrets_owner_pid() {
      local owner
      owner="$(dbus-send --session --dest=org.freedesktop.DBus --print-reply=literal \
        /org/freedesktop/DBus org.freedesktop.DBus.GetNameOwner \
        string:org.freedesktop.secrets 2>/dev/null | tr -d '[:space:]')"
      [ -n "$owner" ] || return 0
      dbus-send --session --dest=org.freedesktop.DBus --print-reply=literal \
        /org/freedesktop/DBus org.freedesktop.DBus.GetConnectionUnixProcessID \
        string:"$owner" 2>/dev/null | awk 'END{print $NF}'
    }
    for _ in $(seq 1 40); do
      kill -0 "$keyring_pid" 2>/dev/null || { cat "$keyring_log"; exit 1; }
      if [ "$(secrets_owner_pid)" = "$keyring_pid" ] &&
         [ -s "$XDG_DATA_HOME/keyrings/login.keyring" ]; then
        owned=1
        break
      fi
      sleep 0.25
    done
    [ -n "$owned" ] || { cat "$keyring_log"; exit 1; }

    # fail here rather than as puzzling credential-store test failures
    for _ in $(seq 1 20); do
      if printf 'probe\n' | secret-tool store --label=ci-probe ci-probe 1 >/dev/null 2>&1; then
        ready=1
        break
      fi
      sleep 0.5
    done
    if [ -z "$ready" ]; then
      echo "Secret Service did not become usable. Last attempt:"
      printf 'probe\n' | secret-tool store --label=ci-probe ci-probe 1 || true
      exit 1
    fi
    secret-tool clear ci-probe 1 >/dev/null 2>&1 || true

- name: Teardown Secret Service
  if: always() && steps.setup.outputs.dbus_pid != ''
  run: |
    for pid in "${{ steps.setup.outputs.keyring_pid }}" "${{ steps.setup.outputs.dbus_pid }}"; do
      [ -n "$pid" ] && kill "$pid" 2>/dev/null || true
    done
```

* `$GITHUB_ENV` exports the address to every later step in the job, so the test
  step needs no wrapper of its own.
* Use `dbus-daemon`, not `dbus-launch`: the three launchers ship in three
  different packages — `dbus-daemon` in the base `dbus` (which `gnome-keyring`
  itself depends on, so it is always present), `dbus-run-session` in `dbus-bin`,
  and `dbus-launch` in `dbus-x11`. Having one does not imply the others.
* Write both `--print-*` options in the `=FD` form. The bare form takes the
  *next* argument as its descriptor, so `--print-address --print-pid=3` fails
  with `Invalid file descriptor: "--print-pid=3"`. Sending the PID to fd 3 leaves
  stdout to the address, which the command substitution captures.
* Check for `secret-tool` and `gnome-keyring-daemon` up front (the workflow does).
  A missing tool otherwise surfaces as `command not found` buried mid-step,
  followed ten seconds later by the unrelated-looking poll timeout.
* Probe with a real `secret-tool store`, not `secret-tool search`. Search
  succeeds against a *missing* default collection, so it cannot tell a working
  keyring from an unusable one — it will wave through a daemon whose every store
  fails.
* **Wait for your own daemon to own `org.freedesktop.secrets` before the first
  `secret-tool` call, and pass `--replace`.** gnome-keyring installs
  `/usr/share/dbus-1/services/org.freedesktop.secrets.service`
  (`Exec=… --start --foreground --components=secrets`), so the first client call
  on the bus *activates a second daemon* — one that never received the unlock
  password and therefore has no `login` collection. Whichever instance claims the
  name first keeps it. When the activated one wins, the unlocking daemon prints
  `another secret service is running`, stops serving secrets, and stays alive
  while every store in the job fails against the activated daemon. A retry loop
  cannot recover from this: it only ever talks to the name owner. Waiting on the
  daemon's PID or on `login.keyring` does not help either — both are satisfied in
  exactly that broken state. Ask the bus who owns the name instead. Ordering the
  wait before the first store also means nothing ever triggers the activation.
* Do **not** pass `--control-directory`. `--replace` displaces the other daemon
  through the control socket in `$XDG_RUNTIME_DIR/keyring`; a private control
  directory makes that daemon undiscoverable and silently degrades `--replace`
  back to `another secret service is running`. A *stale* `control` socket left by
  a killed daemon is harmless — the daemon recovers from it.
* Keep the daemon's stdout/stderr in a log file. `>/dev/null 2>&1` hides
  `another secret service is running`, which is the only trace of a lost race;
  without it the failure looks like a plain timeout.
* Point `XDG_DATA_HOME` at a scratch dir so the keyring is created fresh per run.
  Otherwise the shared `$HOME` decides the outcome: a leftover `login.keyring`
  with a different password refuses to unlock. Export it **before** forking the
  bus: `dbus-daemon` passes its own environment to everything it activates, so
  exporting it afterwards leaves an activated daemon looking in the real `$HOME`.
  Only daemons need the variable, so it is deliberately not published to
  `$GITHUB_ENV`.
* Because `--replace` works per user, not per job, two jobs running concurrently
  as the same user on one self-hosted runner will displace each other's daemon.
  Serialize them (a workflow-level `concurrency` group) if the host can run more
  than one at a time.
* Stop both daemons in their own teardown step. They start before anything else
  in setup, so a teardown guarded on later setup work (a deployed sandbox, say)
  would leak a bus and a keyring per run — which matters on a long-lived
  self-hosted runner.

## 5. Using the credential store

By default the shell **asks** whether to save each new password
(`credentialStore.savePasswords` = `prompt`). To save without being asked,
persisting the setting to your shell configuration:

```bash
mariadb-shell --py -e "shell.options.set_persist('credentialStore.savePasswords', 'always')"
```

or, from an interactive session:

```
\option --persist credentialStore.savePasswords always
```

Values are `always`, `prompt` (the default — ask each time whether to save) and
`never`.

Managing stored credentials — Python names shown; in JavaScript use the camelCase
form (`shell.listCredentials()`):

```python
shell.store_credential("user@host:3306")       # prompts for the password
shell.list_credentials()                        # URLs with a stored password
shell.delete_credential("user@host:3306")
shell.delete_all_credentials()
shell.list_credential_helpers()                 # helpers available on this system
```

Related options:

* `credentialStore.helper` — which helper to use. Leave it at `default`
  (`login-path` on Linux, `keychain` on macOS, `windows-credential` on Windows).
  `<disabled>` turns the mechanism off entirely.
* `credentialStore.excludeFilters` — URL patterns whose passwords are never
  stored, e.g. `["*@myhost*"]`.

These can also be set per invocation with `--credential-store-helper=<helper>` and
`--save-passwords=<value>`, or via the `MARIADB_SHELL_CREDENTIAL_STORE_HELPER` and
`MARIADB_SHELL_CREDENTIAL_STORE_SAVE_PASSWORDS` environment variables.

To inspect what the shell stored, using the keyring directly:

```bash
secret-tool search --all secret_store secret-service
```

## 6. Troubleshooting

Start by asking the helper itself — it prints the real reason, which the shell
only writes to its log:

```bash
mariadb-secret-store-login-path version; echo "exit=$?"       # the default
mariadb-secret-store-secret-service version; echo "exit=$?"   # if you selected it
```

The helpers live next to the `mariadb-shell` binary. Exit `0` means healthy; exit
`1` prints the cause on stderr:

| Symptom | Cause | Fix |
| --- | --- | --- |
| `Failed to open the login file` / `exists but cannot be read` from `login-path` | `~/.mylogin.cnf` is owned by another user, or its directory is not writable | Fix the ownership, or remove the file to start over |
| `is corrupted, invalid record length` / `may be corrupted` from `login-path` | `~/.mylogin.cnf` was truncated or hand-edited | Remove the file; the shell recreates it and you re-enter the passwords |
| `secret-tool: The name org.freedesktop.secrets was not provided by any .service files` | No keyring daemon installed — `secret-tool` is only a client | Install `gnome-keyring` (§2) |
| `Cannot autolaunch D-Bus without X11 $DISPLAY`, or empty `DBUS_SESSION_BUS_ADDRESS` | No D-Bus session bus (SSH, container, service) | Use `dbus-run-session` (§4) |
| `Prompt was dismissed` / `Cannot prompt` on store | Keyring is locked and nothing can ask for the password | Unlock it explicitly (§4) |
| `Object does not exist at path "/org/freedesktop/secrets/collection/login"` on store, while `secret-tool search` exits `0` | Daemon is running but the login keyring — the default collection — was never created | Unlock with a non-empty, newline-terminated password (§4); an empty password creates no keyring |
| The same error, on every retry, from a script that *did* unlock the keyring — and `gnome-keyring-daemon` logged `another secret service is running` | A D-Bus–activated daemon won `org.freedesktop.secrets`; your unlocked daemon is alive but not serving | Start it with `--replace` and wait for it to own the name before storing (§4) |
| `shell.options['credentialStore.helper']` is `<invalid>` | The default helper failed to initialize; storage is disabled and passwords are prompted every time | Run the helper command above to get the reason |
| `mariadb-secret-store-secret-service: command not found` | Helper not installed alongside the shell | Reinstall the shell package |

The shell logs the underlying error at startup. To see it:

```bash
mariadb-shell --log-level=debug --py -e "print(shell.options['credentialStore.helper'])"
grep -i "helper" ~/.mariadb-shell/mariadb-shell.log | tail
```

Look for `Failed to initialize the default helper` followed by the reason.

An occasional `The secret was transferred or encrypted in an invalid way` from
newer gnome-keyring is a known transient fault; the `secret-service` helper retries
automatically and it is not a configuration problem.

## Note on interoperability and on `plaintext`

MariaDB Shell's `~/.mylogin.cnf` is bit-compatible with MySQL's: MySQL's
`mysql_config_editor` and `my_print_defaults` read what the shell writes, and the
shell reads what they write. Nothing else in the MariaDB toolset reads the file —
MariaDB has no `.mylogin.cnf` support of its own — so compatibility with the MySQL
tooling is the only reason the format was kept.

MariaDB Shell does **not** implement a `--login-path=` command-line option; the
file is used only as the credential store's backing file.

The `plaintext` helper you may see in a development build tree is a test fixture —
it is never installed and must not be used to hold real passwords.
