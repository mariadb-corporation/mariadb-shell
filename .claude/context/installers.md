# Installers: `install.sh` and `install.ps1`

Part of [../PROJECT_CONTEXT.md](../PROJECT_CONTEXT.md).

## What they are

- `install.sh` (POSIX sh, macOS and Linux, also run as `curl … | bash`) and `install.ps1` (Windows PowerShell 5.1 and PowerShell 7, also run as `irm … | iex`) install a release from GitHub: pick the package from the release's `SHA256SUMS`, verify it, unpack it into `<prefix>/<version>`, link (`install.sh`) or write `.cmd` shims (`install.ps1`) into the bin directory, and prune to two versions.
- Their callers rely on the interface: the MariaDB VS Code extension parses the `==>` lines on stdout to show progress, and the dev@mariadb plugin's MCP launcher (ai-plugins, `scripts/mariadb-mcp-launcher.sh`) runs `install.sh` from `main` once per agent session. Keep the parameters, environment variables, `==>` lines and exit codes stable.
- User docs: `docs-ref/content/community/mariadb-shell/installation/` (`linux-and-macos.md`, `windows.md`). `publish-release.yml` attaches both scripts to each release as an archival copy; users run the ones on `main`.

## Concurrent installs into one prefix

Several installers can run at once into the same prefix (the editor extension and several agent sessions). Unpacking straight into the prefix made them destroy each other's files (bsdtar "File exists" / "Can't unlink", every run failing under `set -e`). The design:

- **Private unpack folder per run**, inside the prefix so the final move is a rename on one filesystem/volume: `$PREFIX/.unpack.<pid>.XXXXXX` (sh, `mktemp -d`), `$Prefix\.unpack-<pid>-<guid>` (ps1). Removed by the EXIT trap / `finally`. The PID in the name lets a later run, under the lock, remove folders of runs that were killed.
- **Lock around everything shared**: replacing `<prefix>/<version>`, removing `current`, the links/shims, the prune. Download, checksum and unpack stay parallel.
  - sh: `mkdir "$PREFIX/.install.lock"` (macOS has no `flock(1)`), PID written to `.install.lock/pid`, 1 s polling, one "waiting for another install" line on stderr, `die` after 300 s. A lock whose PID fails `kill -0` is cleared; so is one with no PID for 5 polls (holder killed between `mkdir` and writing the PID). Clearing is serialised by a second `mkdir` lock, `.install.lock.break`, rechecking the PID inside it, so two waiters cannot both clear and both acquire; a `.break` older than 10 s of waiting is removed.
  - ps1: named mutex `Global\mariadb-shell-install-<first 8 bytes of SHA-256 of the lower-cased full prefix>` (Global so a user's desktop and SSH sessions see each other; falls back to `Local\` on `UnauthorizedAccessException`, for a prefix shared between users). `WaitOne(0)`, then one stderr line and `WaitOne(300 s)`. `AbandonedMutexException` counts as acquired: Windows releases a dead holder's mutex, so a killed install never blocks the next one. Released after the prune and again (if still held) in `finally`.
- **Keep, don't replace, an identical build**: under the lock, if `<prefix>/<version>/bin/mariadb-shell[.exe]` is byte-identical to the one just unpacked (`cmp -s` / `Get-FileHash`), the folder is kept and only links/shims are rewritten, printing `==> Keeping <path>, already installed from this build`. Replacing it would change nothing and break a running shell or MCP server (on Windows it cannot be done at all). Comparing the binary rather than a stamp file also covers folders an older installer made. Any other folder of that name is replaced as before.
- **Prune skips the dot folders**: the `*` glob does not match them and `is_version_dir` / the ps1 regex accept only digits and dots.
- **Signals (sh)**: `trap cleanup EXIT` plus `HUP`/`INT`/`TERM` traps that `exit 129/130/143`. A trap that only cleaned up would let the script carry on after Ctrl-C without its lock.

## Windows: folders in use

- Windows lets a folder be **renamed** while a program runs from it, and every file not mapped into a process be deleted. So "rename fails when in use" is not a usable test: a prune that tried it moved a running version aside and deleted its Python library from under it.
- `Test-DirInUse` checks `Get-Process` for a process whose `Path` is under the folder. `Remove-VersionDir` refuses such a folder, otherwise moves it to `.remove-<guid>` and deletes that (a deletion that stops part way leaves a `.remove-*` for a later run, not a half-deleted version).
- Prune of an in-use version: `install.ps1: warning: could not remove …` on stderr, install still exits 0. Replacing an in-use, *different* build of the version being installed: `Die` (exit 1) asking to close the shells running from it.
- The stale-folder sweep under the mutex skips `.unpack-*` whose PID is alive and any folder in use.

## Testing them

- `scripts/test_install_concurrent.sh <mirror-dir> [<work-dir>]` (macOS/Linux) and `scripts\test_install_concurrent.ps1 -Mirror <dir> [-Work <dir>] [-Exe <powershell>]` (Windows; `-Exe` picks 5.1 or 7, default the one running the script) are manual tests, not run by CI. Exit 1 on any failed check.
  - `<mirror-dir>` holds one release's `SHA256SUMS` and this machine's package (`gh release download <tag> -D <dir> -p SHA256SUMS -p '<package glob>'`); the tag is read from the package name. Each script builds a test copy of the repository's installer that reads the mirror instead of GitHub, and refuses to run if that substitution no longer matches the installer's source.
  - Work goes into a new temporary folder, removed at the end; never the default prefix.
  - Both cover: 1, 2 and 3 installs started together (all exit 0, no dot folders left, the link/shim runs the version), 3 upgrades together over two fake older versions (`0.0.2` kept, `0.0.1` pruned), a rerun that keeps the folder, a run after a kill mid-unpack. sh also: a stale lock with a dead PID and one with no PID. ps1 also: installs while that version's and while an older version's `mariadb-shell.exe` runs, and a mutex holder (started via `-EncodedCommand`) killed while the installer waits.
- The mirror is needed because PowerShell 7's `Invoke-WebRequest` does **not** support `file://` ("The 'file' scheme is not supported"; 5.1 does), so the ps1 test copy uses `Copy-Item` in `Get-Asset`; the sh copy uses a `file://` URL, which curl reads.
- Running the old installer three at once reproduces the race (bsdtar/`tar.exe` "Error exit delayed from previous errors", the folder never renamed).
- Each unpacked shell is a few hundred MB, so the ps1 test deletes each scenario's folders as soon as it is checked, and the `%TEMP%\mariadb-shell-install-*` folder (with the package) of the installer it kills with `taskkill /F`, whose `finally` never runs. A full disk made a small Windows test VM stop answering SSH.
- Bounded waits only: a test that waits for an `.unpack-*` folder that never comes (an installer failing early, e.g. on a download) hangs forever otherwise.

## Gotchas

- PowerShell parses `"$target: text"` as a drive-qualified variable (parse error for the whole script); write `${target}:`.
- PowerShell 5.1: a `Start-Process -PassThru` process reports an empty `ExitCode` unless `.Handle` was read while it ran.
- Over SSH the Windows default shell is `cmd`: chain with `&`, not `;`, and `%` must be `%%`. Inline `-Command` quoting through cmd is fragile; copy a `.ps1` over and run it with `-File`.

## Next steps

- Confirm the VS Code extension's `==>` parser ignores the new `==> Keeping …` line.
- Possibly run `scripts/test_install_concurrent.*` in CI after a release is published (the release's own assets as the mirror), on a Linux, a macOS and a Windows runner.
- The installation docs describe the prune; add that `install.ps1` leaves a version in use in place with a warning, and that concurrent installs wait for each other.
