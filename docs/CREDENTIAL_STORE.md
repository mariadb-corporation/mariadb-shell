# Credential store setup (Linux)

MariaDB Shell can remember the passwords you type when connecting, so you are not
prompted again for the same account. On Linux this requires a **Secret Service
provider** — a keyring daemon that the shell talks to over D-Bus. This is a hard
requirement: without it, credential storage stays disabled and every connection
prompts for the password again.

Windows and macOS need no setup — the shell uses the Windows Credential Manager
and the macOS keychain, which are always present.

## 1. What the shell needs

On Linux the shell stores passwords through its `secret-service` helper, which
calls the `secret-tool` command, which in turn talks to whatever daemon owns the
`org.freedesktop.secrets` name on your **D-Bus session bus**. So three things must
be in place:

| Requirement | Provided by |
| --- | --- |
| The `secret-tool` command | `libsecret-tools` / `libsecret` package |
| A Secret Service provider (keyring daemon) | `gnome-keyring` (GNOME, and works fine elsewhere) |
| A D-Bus **session** bus, with the keyring **unlocked** | Your desktop session, or set up manually (§4) |

Inside a normal desktop session all three are usually already true — see §3 to
confirm. Over plain SSH, in a container, or on a CI runner, none of them are, and
§4 applies.

## 2. Install the packages

**Debian / Ubuntu**

```bash
sudo apt-get install -y gnome-keyring libsecret-tools
```

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

## 3. Verify (desktop session)

Log in graphically, open a terminal, and check the whole chain bottom-up:

```bash
# 1. a session bus exists
echo "$DBUS_SESSION_BUS_ADDRESS"

# 2. the Secret Service answers
secret-tool search --all secret_store secret-service; echo "exit=$?"

# 3. the shell sees its helper
mysqlsh --py -e "print(shell.list_credential_helpers())"

# 4. the shell resolved the default helper
mysqlsh --py -e "print(shell.options['credentialStore.helper'])"
```

Expected: a non-empty bus address; step 2 exits `0` (no output yet is normal —
nothing is stored); step 3 prints `["secret-service"]`; step 4 prints `default`.

If step 4 prints `<invalid>`, the helper could not be initialized — go to §6.

## 4. Headless: SSH, containers, CI

Without a desktop session there is no keyring daemon running and, often, no
session bus at all. Create both around the command you want to run:

```bash
dbus-run-session -- sh -c '
  printf "my-keyring-password\n" | gnome-keyring-daemon --unlock --components=secrets >/dev/null 2>&1
  mysqlsh --py -e "print(shell.options[\"credentialStore.helper\"])"'
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

**In CI**, the same applies: a CI runner started as a service has no session bus
and no keyring, so the shell's credential-store test suites
(`Mysqlsh_credential_store.*`, `Mysql_secret_store*`) — which store and retrieve
real secrets — fail with the helper reported as `<invalid>`.

If a single step both sets up and runs, the wrapper above is enough. If setup and
tests are separate steps, it is not: the bus `dbus-run-session` creates lives only
as long as the command it wraps, and each step is a separate process. Fork the bus
in the setup step instead and publish its address, as
[`.github/workflows/shell-ci.yml`](../.github/workflows/shell-ci.yml) does:

```yaml
- name: Setup Test Environment
  id: setup
  run: |
    pid_file="$(mktemp)"
    DBUS_SESSION_BUS_ADDRESS="$(dbus-daemon --session --fork \
      --print-address=1 --print-pid=3 3>"$pid_file")"
    export DBUS_SESSION_BUS_ADDRESS
    echo "DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS" >> $GITHUB_ENV
    echo "dbus_pid=$(cat "$pid_file")" >> $GITHUB_OUTPUT
    rm -f "$pid_file"

    export XDG_DATA_HOME="$RUNNER_TEMP/keyring-home"
    rm -rf "$XDG_DATA_HOME"
    mkdir -p "$XDG_DATA_HOME/keyrings"

    printf 'ci\n' | gnome-keyring-daemon --unlock --components=secrets --foreground >/dev/null 2>&1 &
    echo "keyring_pid=$!" >> $GITHUB_OUTPUT

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
  fails. The loop also closes the race between the daemon starting and it
  claiming `org.freedesktop.secrets`.
* Point `XDG_DATA_HOME` at a scratch dir so the keyring is created fresh per run.
  Otherwise the shared `$HOME` decides the outcome: a leftover `login.keyring`
  with a different password refuses to unlock. Only the daemon needs this
  variable, so it is deliberately not published to `$GITHUB_ENV`.
* Stop both daemons in their own teardown step. They start before anything else
  in setup, so a teardown guarded on later setup work (a deployed sandbox, say)
  would leak a bus and a keyring per run — which matters on a long-lived
  self-hosted runner.

## 5. Using the credential store

By default the shell **asks** whether to save each new password
(`credentialStore.savePasswords` = `prompt`). To save without being asked,
persisting the setting to your shell configuration:

```bash
mysqlsh --py -e "shell.options.set_persist('credentialStore.savePasswords', 'always')"
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
  (`secret-service` on Linux). `<disabled>` turns the mechanism off entirely.
* `credentialStore.excludeFilters` — URL patterns whose passwords are never
  stored, e.g. `["*@myhost*"]`.

These can also be set per invocation with `--credential-store-helper=<helper>` and
`--save-passwords=<value>`, or via the `MYSQLSH_CREDENTIAL_STORE_HELPER` and
`MYSQLSH_CREDENTIAL_STORE_SAVE_PASSWORDS` environment variables.

To inspect what the shell stored, using the keyring directly:

```bash
secret-tool search --all secret_store secret-service
```

## 6. Troubleshooting

Start by asking the helper itself — it prints the real reason, which the shell
only writes to its log:

```bash
mysql-secret-store-secret-service version; echo "exit=$?"
```

The helper lives next to the `mysqlsh` binary. Exit `0` means it is healthy; exit
`1` prints the cause on stderr:

| Symptom | Cause | Fix |
| --- | --- | --- |
| `secret-tool: The name org.freedesktop.secrets was not provided by any .service files` | No keyring daemon installed — `secret-tool` is only a client | Install `gnome-keyring` (§2) |
| `Cannot autolaunch D-Bus without X11 $DISPLAY`, or empty `DBUS_SESSION_BUS_ADDRESS` | No D-Bus session bus (SSH, container, service) | Use `dbus-run-session` (§4) |
| `Prompt was dismissed` / `Cannot prompt` on store | Keyring is locked and nothing can ask for the password | Unlock it explicitly (§4) |
| `Object does not exist at path "/org/freedesktop/secrets/collection/login"` on store, while `secret-tool search` exits `0` | Daemon is running but the login keyring — the default collection — was never created | Unlock with a non-empty, newline-terminated password (§4); an empty password creates no keyring |
| `shell.options['credentialStore.helper']` is `<invalid>` | The default helper failed to initialize; storage is disabled and passwords are prompted every time | Run the helper command above to get the reason |
| `mysql-secret-store-secret-service: command not found` | Helper not installed alongside the shell | Reinstall the shell package |

The shell logs the underlying error at startup. To see it:

```bash
mysqlsh --log-level=debug --py -e "print(shell.options['credentialStore.helper'])"
grep -i "helper" ~/.mysqlsh/mysqlsh.log | tail
```

Look for `Failed to initialize the default helper "secret-service"` followed by
the reason.

An occasional `The secret was transferred or encrypted in an invalid way` from
newer gnome-keyring is a known transient fault; the helper retries automatically
and it is not a configuration problem.

## Note on `login-path`

Upstream MySQL Shell also ships a `login-path` helper, which stores passwords in
the obfuscated `.mylogin.cnf` file and needs no keyring. It is **not** available
in MariaDB Shell, so `secret-service` is the only credential store on Linux and
the setup above is required. (The `plaintext` helper you may see in a development
build tree is a test fixture — it is never installed and must not be used to hold
real passwords.)
