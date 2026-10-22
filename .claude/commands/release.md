---
description: Run the MariaDB Shell release end to end -- version bump, plugins version and sandbox server index, release/candidate build and verification, publish. Usage: /release [--official] [--dry-run] [--help]
argument-hint: "[--official] [--dry-run] [--help]"
---

# Release Command

Arguments as typed: `$ARGUMENTS`

Takes MariaDB Shell from "main is ready" to a published GitHub release, across
two repos:

| Repo | Role |
|---|---|
| `mariadb-corporation/mariadb-shell` (`SHELL`) | `MYSQL_VERSION`; the workflows `shell-release.yml` (build), `verify-packages.yml` (verification), `publish-release.yml` (publish), `sandbox-server.yml` (server packages), `shell-ci-publish.yml` (runs on every push to main) |
| `mariadb-corporation/mariadb-shell-plugins` (`PLUGINS`) | `VERSION`, the sandbox server index, `npm run update-version` (in `tools/`), and `trigger-shell-release.yml`, which starts a shell build on every push to its release/candidate |

`scripts/release.py` does the checks and the waiting:
`preflight`, `shell-bump-pr`, `push-rc`, `find-run` and `watch`. Run it as
`python3 scripts/release.py <subcommand>`. Run `--help` on a
subcommand for its flags.

## Arguments

Parse `$ARGUMENTS`:

| Flag | Effect | Default |
|---|---|---|
| `--official` | publish with `prerelease=false` | pre-release (`prerelease=true`) |
| `--dry-run` | publish with `dry_run=true`: assembled and checked, nothing released | off |
| `--help` | print the help below and stop | — |

With `--help`, print the block below exactly as written and stop. Run
nothing, not even `preflight`, and ignore any other flag given with it. If
there is any other unknown argument, stop and print the usage line,
`Usage: /release [--official] [--dry-run] [--help]`. Before step 1, print the
mode in effect, for example: `Mode: pre-release, real publish`.

```
/release [--official] [--dry-run] [--help]

Takes MariaDB Shell from "main is ready" to a published GitHub release,
across mariadb-shell and mariadb-shell-plugins.

  --official   publish an official release (default: pre-release)
  --dry-run    run everything, but publish-release assembles and checks
               the release without creating it (default: off)
  --help       show this help

Steps, in order. Each one checks first and does only what is missing, so
running /release again resumes where the last run stopped.

  0 Preflight   report versions, release/candidate branches and sandbox
                server tags
  1 Shell       main's MYSQL_VERSION must be newer than the last published
    version     release and in the current month (yy.mm.p); otherwise open
                and merge a [bypass-ci] bump PR, then wait for
                MariaDB-Shell-CI-Publish on main
  2 Plugins &   newest MariaDB/server 11.x/12.x/13.x tags vs the sandbox
    sandboxes   index: build only new or pending tags (unchanged ones are
                never rebuilt); run update-version; open and merge a plugins
                PR if anything changed, after Shell Plugins CI passes
  3 Build &     fast-forward release/candidate (plugins, then shell), or
    verify      reuse an existing build; wait for MariaDB-Shell-Release and
                Verify Packages
  4 Publish     show a pre-publish summary and ask before a real publish;
                run publish-release and report the release

It asks you before: a real publish, an index that disagrees with its
release (rebuild or stop), a version that skips ahead, a release/candidate
that cannot fast-forward, and a build that main has moved past.

It changes: PRs on both repos (version bump, plugins version/index), both
release/candidate branches (fast-forward only, never forced), workflow runs,
and the release itself. It never force-pushes, never merges past a failing
check, and leaves a failed run as it is for you to inspect.

Details: .claude/commands/release.md · helper: scripts/release.py --help
```

## Rules for the whole run

- **Steps run in strict order.** A step starts only when the step before it
  ended `done` or `skipped`.
- **Report after every step** in this shape, before starting the next one:

  ```
  ── Step N: <name> — <done | skipped | failed | waiting for you>
     Checks:    <each verification made and what it found>
     Decisions: <each choice made and why>
     Actions:   <PRs opened/merged, branches moved, runs started/cancelled, with links>
  ```

  Each step below lists what its report must show, besides that.
- **Name the workflow with every run number**, in reports and the summary
  alike: `MariaDB-Shell-Release run 39236200396`, never a bare `run
  39236200396` or `BUILD`. The names are:

  | File | Workflow name |
  |---|---|
  | `shell-release.yml` | MariaDB-Shell-Release |
  | `verify-packages.yml` | Verify Packages |
  | `publish-release.yml` | Publish Release |
  | `sandbox-server.yml` | MariaDB-Sandbox-Server |
  | `shell-ci-publish.yml` | MariaDB-Shell-CI-Publish |
  | `shell-ci.yml` | MariaDB-Shell-CI |
  | PLUGINS `shell-plugins-ci.yml` | Shell Plugins CI |
  | PLUGINS `trigger-shell-release.yml` | Trigger MariaDB Shell Release |

  `release.py` already prints run numbers this way.
- **Every step checks before it acts**, so running `/release` again resumes
  where a previous run stopped. Nothing needs remembering between runs except
  what GitHub already holds.
- **Wait on runs in the background, never with sleep loops.** Start
  `release.py watch <run-id>` (or `gh pr checks … --watch`) with Bash
  `run_in_background: true`. Several independent runs can be watched at
  once, one background command each. When a watch exits, read its output and
  report it. `watch` prints each job as it starts and finishes. It exits at
  the first failed job and prints that job's failed step and the log leading
  up to its first error.
- **On any failure, stop the release.** Report what failed, with the job, the
  failed step, the error excerpt and the run link, and what to fix. Do not
  cancel the failed run, and do not retry on your own. Tell the user that
  `/release` resumes once it is fixed.
- **Never force anything.** No force-push, no `--admin` merges, no merging
  past a failing check, no `git push --force` to release/candidate. When a
  precondition fails in a way these steps don't cover (a diverged
  release/candidate, an open PR that doesn't match), stop and ask.
- Temporary clones go in a fresh directory under the session scratchpad, never
  in the user's checkouts.
- Commits created here carry no `Co-Authored-By` or other attribution
  trailer.
- **Keep a record for the final summary** as you go: every version, tag, run,
  PR, branch move and decision reported along the way.

## Step 0: Preflight

```
python3 scripts/release.py preflight          # human summary
python3 scripts/release.py preflight --json   # same data, for decisions
```

Show the summary to the user. It gives the last published version, the
version for this release, both repos' versions on main and on
release/candidate, and the state of each sandbox server tag. Steps 1–3 take
their decisions from it.

## Step 1: Shell version bumped on main

Versions are `yy.mm.p`. The month is written without a leading zero, as
`MYSQL_VERSION_MINOR` and every tag so far write it (`26.9.5`, `26.10.0`).
`p` counts the month's releases from 0. The last published version is the
highest `v<version>` among published releases, prereleases included.

Main's version is **bumped** only when it is newer than the last published
version **and** is not from an earlier month than today. With 26.10.2
published and 26.10.3 on main, the release is 26.10.3 on 2026-10-19. On
2026-11-01, main needs a bump to 26.11.0. `preflight` applies this rule and
gives `release_version`, the version this release goes out as.

The report must show, on lines of their own:

```
   Last published version:   <last_release.version> (<pre-release|release>, <published_at>)
   Version for this release: <release_version>
```

- `step1_shell_version.bumped` is true: **done**. If it carries a note (main
  is newer, but is not the next `yy.mm.p`, for example it skips a number or
  is in a month still to come), show the note and ask before going on.
- Not bumped: open the bump PR to `release_version`:

  ```
  python3 scripts/release.py shell-bump-pr <release_version> --last <last_release.version>
  ```

  This creates branch `wip/version-<v>` with a single commit, `Updated version
  to <v>`, which changes only MYSQL_VERSION. It opens the PR as
  `[bypass-ci] Updated version to <v>`, since no test depends on the version.
  If a previous run already opened that PR, it reuses it.
  1. Wait in the background for the required checks, which `[bypass-ci]`
     reports quickly:
     `gh pr checks <N> -R mariadb-corporation/mariadb-shell --required --watch --fail-fast --interval 60`.
  2. Merge it with
     `gh pr merge <N> -R mariadb-corporation/mariadb-shell --squash --delete-branch`.
  3. Get the merge commit from
     `gh pr view <N> -R mariadb-corporation/mariadb-shell --json mergeCommit --jq .mergeCommit.oid`.
     The push to main starts MariaDB-Shell-CI-Publish. Find that run with
     `find-run --workflow shell-ci-publish.yml --event push --sha <merge sha>`
     and watch it. **Do not go on until it completes successfully.**
  4. Re-run `preflight` and confirm that main now has `<v>`.

## Step 2: Plugins version and sandbox server index

### 2.1 / 2.2 Sandbox server builds

`step2_servers` holds, for each of 11.x, 12.x and 13.x, the newest
`mariadb-X.Y.Z` tag of MariaDB/server, which is the same tag `update-version`
picks. It compares that tag with the sandbox server index on the plugins'
main and gives a `series_status`, decided the same way `update-version`
decides it.

**Sandbox servers are decided by tag, never by build artifacts.** A series
whose newest tag is already in the index, from a published release whose
`SERVER_SHA256SUMS` matches, is left alone: no build, no index change, no
`server_NN_tag`. That holds however old its build is, because release assets
don't expire and Actions artifacts do (after 90 days). Rebuilding it would
not even reproduce it: the builds aren't bit-for-bit reproducible, so the
same server version would be shipped again under new checksums.

The report must first list the tags found, one line per series, before any
build starts:

```
   Newest MariaDB/server tags: 11.x mariadb-11.8.9 · 12.x mariadb-12.3.3 · 13.x mariadb-13.1.1
```

Then act on each series:

| `series_status` | Meaning | Action |
|---|---|---|
| `unchanged` | the index has this version, from a published release whose `SERVER_SHA256SUMS` matches | none |
| `new` | a newer tag than the index's, or a series not in the index | needs packages from a build (below) |
| `pending` | the index has this version, but in a release that isn't published (a resumed or abandoned release) | needs packages from a build (below) |
| `inconsistent` | the index has this version, but the published release doesn't carry it under those checksums: someone edited the index or the release by hand | **ask the user** (below) |
| `problem` | no release tag for the series, or the tag is older than the index | stop: report it |

For `new` and `pending`, `decision` says whether a usable build exists:

| `decision` ends with | Action |
|---|---|
| `built by MariaDB-Sandbox-Server run N` | reuse it |
| `still running, wait for it` | watch that run (`pending_run`) |
| `never built`, `no successful build`, `artifacts expired` | build it |

For `inconsistent`, show the reason and ask with AskUserQuestion. Give two
options: **Rebuild `<tag>` (Recommended)**, because a fresh build is the way
to be sure the index and the release agree, or **Stop**, so the user can fix
it by hand. On *Rebuild*, build the tag as below, and pass `--refresh
<major>` to `update-version` in 2.3, which replaces that series' packages
with the new build's. On *Stop*, stop the release.

To build a tag, say which tag is being built and why first:
`Building mariadb-12.3.4 (12.x): never built`. Then note the time (UTC,
ISO-8601) and run:

```
gh workflow run sandbox-server.yml -R mariadb-corporation/mariadb-shell --ref main -f mariadb_version=<tag>
python3 scripts/release.py find-run --workflow sandbox-server.yml \
    --event workflow_dispatch --title "Sandbox Server <tag>" --created-after <time>
```

Then watch it, and report each build as `<tag>: MariaDB-Sandbox-Server run N
— <result>`. Start all the builds and watchers together, since the series
are independent. Keep the list of tags built in this step (`BUILT_TAGS`). When
every `new` and `pending` series shows `built by`, and every `inconsistent`
one the user chose to rebuild has a new build, go on. A re-run of `preflight`
confirms it.

### 2.3 Run update-version, PR the result

Run this even when the versions already match: a new server tag changes the
index without changing a version.

```
gh repo clone mariadb-corporation/mariadb-shell-plugins <scratch>/plugins -- --depth 1
cd <scratch>/plugins/tools && npm run update-version [-- --refresh <major> ...] 2>&1 | tee <scratch>/update-version.log
```

Add `--refresh <major>` for each series rebuilt from an `inconsistent`
state. `update-version` downloads artifacts only for `new`, `pending` and
refreshed series. It prints one `<major>.x <tag>: <status> -- <reason>` line
per series, and its status must match the one `preflight` gave. If they
differ, stop and report both.

- Non-zero exit: stop. Its "Refusing to write anything" list says why. Report
  it.
- The report must show:
  - **Plugins version: `<before>` → `<after>`.** Take them from update-version's
    `Current version:` and `New plugin version:` lines, and say `unchanged`
    when they match.
  - **Sandbox index changes**, one line per series, from the lines
    update-version prints under each tag:
    - `new or changed, to publish in v<v>: <platforms>`: these packages are
      updated in the index and get this release's URLs.
    - `unchanged, kept in v<x>: <platforms>`: these entries stay as they are.

    Then add the index summary line it prints
    (`sandbox_server_versions.json: <old versions> (from …) -> <new> (from …)`)
    and, when the file changed, the server versions that `git diff` shows
    added or removed.
  - The `server_NN_tag = mariadb-…` lines (`INDEX_TAGS`) and the
    artifact-expiry deadline.
- `git status --porcelain` is empty: **skipped**. Main already has this
  version and this index.
- Otherwise check for an open PR from `wip/version-<v>` on PLUGINS. If one is
  open, compare its diff with this one: reuse it if they match, ask if they
  differ. If none is open, create the branch, commit everything as `Updating
  version to <v>` (or `Update the sandbox server index for v<v>` when VERSION
  did not change), push it, and open the PR. **No `[bypass-ci]`**: the
  plugins' tests run on it. Put the update-version summary lines in the PR
  body.
  1. Wait for Shell Plugins CI to complete on the PR: every check, not only
     the required ones, so the `wip/**` push run counts too.
     `gh pr checks <N> -R mariadb-corporation/mariadb-shell-plugins --watch --fail-fast --interval 60`.
     Do not merge until all of them pass.
  2. Merge with `--squash --delete-branch`. Shell Plugins CI does not run on
     main, so the merge itself starts nothing to wait for.
  3. Re-run `preflight`.

The server tags to publish with are **`INDEX_TAGS`**: exactly the `new`,
`pending` and refreshed series, whose index entries now point at this
release. `unchanged` series are never passed. Show the set and each tag's
status.

Every tag in `BUILT_TAGS` must be in `INDEX_TAGS`. A build made for a series
the index doesn't take means the two disagree: stop and report it.

## Step 3: Build and verify on release/candidate

From a fresh `preflight`:

1. **Require the versions to match**: `plugins.main_version ==
   shell.main_version`. If they differ, stop. `shell-release.yml`'s
   check-versions job would refuse the build anyway.
2. **Require fast-forwards**: `can_fast_forward` must be true for both
   repos. If release/candidate has diverged from main, stop and ask. Never
   force it.
3. **Reuse a build that already exists.** If both release/candidate branches
   are `identical` to main, list the MariaDB-Shell-Release runs for
   `shell.main_sha`
   (`find-run --workflow shell-release.yml --sha <sha> --timeout 0`). Take
   the newest one created after the plugins release/candidate head commit,
   whose date is from
   `gh api repos/mariadb-corporation/mariadb-shell-plugins/commits/<plugins rc sha> --jq .commit.committer.date`.
   - If it succeeded, use it as `BUILD` and go to 6.
   - If it is still running, watch it.
   - If there is none, or only failed ones, trigger a build without moving a
     branch: `gh api repos/mariadb-corporation/mariadb-shell/dispatches -f
     event_type=updated-shell-plugins-rc`. Then find the
     `repository_dispatch` run.
4. **Move release/candidate, plugins first, then the shell.** Note the time,
   then fast-forward each branch that is behind to the exact SHA `preflight`
   reported. If main moved since, re-run `preflight` first.

   ```
   python3 scripts/release.py push-rc plugins <plugins main sha>
   python3 scripts/release.py push-rc shell   <shell main sha>
   ```

   For the plugins, `push-rc` first reads the state of PLUGINS'
   Trigger MariaDB Shell Release workflow. That workflow would answer the
   push by starting a second, redundant shell build. If it is enabled,
   `push-rc` disables it, pushes, waits a minute, checks that the workflow
   started no run, and enables it again. It always enables it again, even
   when the push fails. If it was already disabled, it is left disabled. The
   report must show that state before and after, and whether a trigger run
   started anyway.
5. **Identify the build run.**
   - **The shell was pushed:** that push starts the build. Find it with
     `find-run --workflow shell-release.yml --event push --sha <shell sha> --created-after <time>`.
     If a `repository_dispatch` MariaDB-Shell-Release run also started after
     `<time>`, because the trigger fired despite `push-rc`, cancel it with
     `gh run cancel` and report that as a decision. It builds the same
     commits.
   - **Only the plugins were pushed:** with the trigger workflow held off,
     nothing builds by itself. Start the build with
     `gh api repos/mariadb-corporation/mariadb-shell/dispatches -f event_type=updated-shell-plugins-rc`,
     then find the `repository_dispatch` run and check that its `headSha` is
     `shell.main_sha`.

   Watch `BUILD`.
6. **Verification.** Verify Packages starts by itself when the build
   finishes. Find its run with
   `find-run --workflow verify-packages.yml --title "Verify build <BUILD>" --timeout 900`.
   If no run appears, start it by hand with
   `gh workflow run verify-packages.yml -R mariadb-corporation/mariadb-shell -f run_id=<BUILD>`.
   Watch it. Success alone is not enough: confirm that the run carries the
   artifact publish-release gates on:

   ```
   gh api repos/mariadb-corporation/mariadb-shell/actions/runs/<VERIFY>/artifacts --jq '.artifacts[].name' | grep -x "verified-build-<BUILD>"
   ```

The report must show each release/candidate as `<old sha> → <new sha>` (or
`unchanged`), the build and verification runs with their workflow names, and
the marker artifact.

## Step 4: Publish

1. Check whether `gh release view v<v> -R mariadb-corporation/mariadb-shell`
   already finds a published release. publish-release would update it.
2. **Check that main has not moved past the build.** A build can take over
   an hour, and PRs keep merging meanwhile. Re-run `preflight` and compare:
   - **Shell:** `shell.main_sha` against the build's `headSha`
     (`gh run view <BUILD> -R mariadb-corporation/mariadb-shell --json headSha`).
   - **Plugins:** `plugins.main_sha` against the plugins commit the build
     bundled. That commit is the plugins release/candidate head when the
     build started: the SHA `push-rc` moved it to, or `plugins.rc_sha` when
     the build was reused or started without a push.

   For each repo that moved, list the commits the build does not have, with
   their PR numbers, from
   `gh api repos/<repo>/compare/<built sha>...<main sha> --jq '.commits[] | "\(.sha[0:9]) \(.commit.message | split("\n")[0])"'`.
   Also note whether the version on main changed. That is a warning, not a
   failure: the build is still valid, it just doesn't have those commits.
3. **A real publish needs the user's confirmation; a dry run does not.**
   Without `--dry-run`, first show a **pre-publish summary**. It is the
   final summary table, filled in from steps 1–3, with the Publish row
   holding what is about to be sent:
   - the exact `gh workflow run publish-release.yml …` inputs: `run_id`,
     `prerelease`, `dry_run=false`, and each `server_NN_tag`, or "none";
   - the release it creates: tag `v<v>`, title `MariaDB Shell <v>`,
     pre-release or official, at the MariaDB-Shell-Release run's commit;
   - the MCP server image it then pushes to Docker Hub,
     `mariadb/mariadb-shell-mcp-server:<v>` (plus `X.Y` and `latest` when
     `v<v>` is the newest release);
   - anything unusual seen along the way, each on its own line: a version
     note from step 1, an inconsistency and how it was resolved, a cancelled
     run, a release `v<v>` that already exists (it would be updated),
     artifacts that expire within 7 days, or **main moved past the build**
     (from 2), with the list of commits the release would not include.

   Then ask with AskUserQuestion. Give two options: **Publish v<v>**, naming
   pre-release or official, and **Stop**, which publishes nothing. When main
   moved past the build, add a third option between them: **Rebuild with the
   latest main**. It goes back to step 3: fast-forward release/candidate to
   the new main, build, verify, then show this summary again. If main's
   version changed, it goes back to step 1 instead. Do not dispatch anything
   until the answer is *Publish*. On *Stop*, end with the summary, state that
   nothing was published, and say that `/release` picks up here again
   because every earlier step is already done.

   With `--dry-run` there is no question, but a main that moved past the
   build is still reported, with its commits, in the step report and the
   final summary.
4. Note the time and dispatch:

   ```
   gh workflow run publish-release.yml -R mariadb-corporation/mariadb-shell --ref main \
       -f run_id=<BUILD> -f prerelease=<true unless --official> -f dry_run=<true if --dry-run> \
       [-f server_11_tag=<tag>] [-f server_12_tag=<tag>] [-f server_13_tag=<tag>]
   ```

   Pass a `server_NN_tag` only for the series in the tag set from 2.3. Leave
   the others out. Report the inputs given. For a real publish they must be
   exactly the ones the user confirmed. If anything changed since, for
   example main moved or a newer build appeared, show the pre-publish
   summary again and ask again.
5. Find the run. Its title is `Publish (version from build) from run <BUILD>`,
   or `Dry run (version from build) from run <BUILD>` for a dry run:

   ```
   find-run --workflow publish-release.yml --event workflow_dispatch --title "<title>" --created-after <time>
   ```

   Then watch it. Report every `::warning::` in its log, because a server line
   it could not resolve is only a warning there.
6. Report the result:
   - **Real publish:** `gh release view v<v> -R mariadb-corporation/mariadb-shell --json url,isPrerelease,assets`,
     giving the URL, the pre-release flag and the asset count. Then the
     run's `mcp-server-image` job: the Docker Hub tags from its summary
     (`X.Y.Z`, plus `X.Y` and `latest` when this is the newest release). The
     image is built from the published release, so when only that job
     failed, the release stands: report the failure, and after the fix
     rebuild the image alone with
     `gh workflow run mcp-server-image.yml -R mariadb-corporation/mariadb-shell -f version=<v>`.
   - **Dry run:** the `dry-run-assets-<BUILD>` artifact on the run. State
     plainly that nothing was published. Running `/release` again without
     `--dry-run` publishes the same build, because every earlier step is
     `skipped` or reused.

## Final summary

End with one table that holds everything the steps reported, so it can be
read on its own. Use one row per item, and `—` when a step had nothing to do:

| Item | What to put |
|---|---|
| Mode | pre-release or official; dry run or real publish |
| Last published version | version, pre-release or not, date |
| Version for this release | the version, and whether main had to be bumped |
| Shell version bump | the PR (number, link) and the MariaDB-Shell-CI-Publish run after its merge, or "already bumped" |
| MariaDB/server tags | the newest tag found for 11.x, 12.x and 13.x |
| Sandbox server builds | per tag: its `series_status`, then not needed (unchanged), reused or built, with its MariaDB-Sandbox-Server run and result; any inconsistency and the user's answer |
| Plugins version | before → after |
| Sandbox index | per series: packages updated (to this release) or kept (in which release) |
| Plugins PR | the PR (number, link) and its Shell Plugins CI result, or "no changes" |
| Trigger MariaDB Shell Release | state before, what was done, state after |
| release/candidate | shell and plugins: old → new SHA, or unchanged |
| Build | MariaDB-Shell-Release run, link, and whether it was reused or new |
| Verification | Verify Packages run, link, marker artifact |
| Publish | Publish Release run, link, the inputs given (prerelease, dry_run, server tags); for a real publish, the user's confirmation |
| Server tags shipped | the set from 2.3 and why, or none |
| Result | release URL, pre-release flag and asset count, or "dry run, nothing published" with the assets artifact |
| MCP server image | the Docker Hub tags pushed, or the failure; "—" for a dry run |
| Deadlines | the artifact-expiry date update-version reported, when relevant |
| Runs cancelled | any, with the reason |
