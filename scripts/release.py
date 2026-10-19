#!/usr/bin/env python3
# Copyright (c) 2026, MariaDB plc.
#
# The deterministic half of the /release command (.claude/commands/release.md):
# every check the release procedure makes against GitHub, and the waiting it
# does on workflow runs. The procedure itself -- what to do with each answer --
# lives in the command.
#
# Subcommands:
#   preflight            Versions on both repos, the last release, the
#                        release/candidate branches and the sandbox server
#                        builds, with the decision each one leads to.
#   shell-bump-pr V      Opens the PR that sets MYSQL_VERSION on main to V (or
#                        finds the one already open).
#   push-rc REPO SHA     Fast-forwards release/candidate; for the plugins repo,
#                        with its shell-build trigger workflow disabled around
#                        the push.
#   find-run             Waits for a workflow run matching some filters to
#                        appear, and prints its id.
#   watch RUN_ID         Polls a run until it finishes, reporting every job as
#                        it starts and ends, and stops at the first failed job.
#
# Needs git and the GitHub CLI (gh), logged in with access to both repos.

import argparse
import base64
import datetime
import json
import re
import subprocess
import sys
import time

SHELL_REPO = "mariadb-corporation/mariadb-shell"
PLUGINS_REPO = "mariadb-corporation/mariadb-shell-plugins"
SERVER_SOURCE_REPO = "https://github.com/MariaDB/server"
RC_BRANCH = "release/candidate"

# Kept in step with SERVER_MAJORS, SERVER_TAG_PATTERN, RUN_SEARCH_LIMIT,
# EXPECTED_PLATFORMS and classify_series() in the plugins repo's
# tools/update_version/update_version.py: this decides which sandbox servers to
# build, that script decides what the index says about them, and the two have
# to agree on the tags, on each series' status and on the runs.
SERVER_MAJORS = (11, 12, 13)
SERVER_TAG_PATTERN = re.compile(r"^mariadb-(\d+)\.(\d+)\.(\d+)$")
RUN_SEARCH_LIMIT = 50
EXPECTED_PLATFORMS = ("darwin-arm64", "darwin-x64", "linux-arm64", "linux-x64", "win32-arm64", "win32-x64")
SERVER_INDEX_FILE = "mcp_plugin/lib/sandbox_server_versions.json"
SERVER_CHECKSUMS_ASSET = "SERVER_SHA256SUMS"

# Workflow display names, for reporting: a run number is always printed with
# the name of the workflow it belongs to.
SANDBOX_WORKFLOW_NAME = "MariaDB-Sandbox-Server"

PLUGINS_TRIGGER_WORKFLOW = "trigger-shell-release.yml"
PLUGINS_TRIGGER_WORKFLOW_NAME = "Trigger MariaDB Shell Release"

SHELL_VERSION_PATTERN = re.compile(r"^(\d{2})\.(\d{1,2})\.(\d+)$")


def run(command, check=True, stdin=None):
    try:
        result = subprocess.run(command, capture_output=True, text=True, input=stdin)
    except FileNotFoundError:
        raise SystemExit(f"'{command[0]}' is needed but was not found.")
    if check and result.returncode != 0:
        raise SystemExit(f"{' '.join(command)} failed:\n{result.stderr.strip()}")
    return result


def gh(*args, check=True):
    return run(["gh", *args], check=check).stdout


def gh_json(*args):
    return json.loads(gh(*args))


def now_utc():
    return datetime.datetime.now(datetime.timezone.utc)


def stamp():
    return now_utc().strftime("%H:%M:%S")


def parse_time(value):
    return datetime.datetime.fromisoformat(value.replace("Z", "+00:00"))


# ---------------------------------------------------------------------------
# Versions
# ---------------------------------------------------------------------------

def version_tuple(version):
    match = SHELL_VERSION_PATTERN.match(version)
    return tuple(int(part) for part in match.groups()) if match else None


def raw_file(repo, path, ref):
    return gh("api", f"repos/{repo}/contents/{path}?ref={ref}", "-H", "Accept: application/vnd.github.raw")


def shell_version(ref):
    """The shell version on a ref, derived the way publish-release derives it."""
    raw = raw_file(SHELL_REPO, "MYSQL_VERSION", ref)
    fields = {}
    for key in ("MAJOR", "MINOR", "PATCH", "EXTRA"):
        match = re.search(rf"^MYSQL_VERSION_{key}=(.*)$", raw, re.MULTILINE)
        fields[key] = match.group(1).strip() if match else ""
    return f"{fields['MAJOR']}.{fields['MINOR']}.{fields['PATCH']}{fields['EXTRA']}"


def plugins_version(ref):
    match = re.search(r"^VERSION\s*=\s*(\S+)", raw_file(PLUGINS_REPO, "VERSION", ref), re.MULTILINE)
    return match.group(1) if match else None


def last_release():
    """The newest published release (prereleases count, drafts do not)."""
    releases = gh_json(
        "release", "list", "-R", SHELL_REPO, "--exclude-drafts", "--limit", "50",
        "--json", "tagName,isPrerelease,publishedAt",
    )
    best = None
    for release in releases:
        version = version_tuple(release["tagName"].lstrip("v"))
        if version and (best is None or version > best[0]):
            best = (version, release)
    if best is None:
        return None
    release = best[1]
    return {
        "tag": release["tagName"],
        "version": release["tagName"].lstrip("v"),
        "prerelease": release["isPrerelease"],
        "published_at": release["publishedAt"],
    }


def expected_next_version(last, today):
    """yy.mm.p: p counts the releases of the month, from 0.

    The month is written the way MYSQL_VERSION_MINOR and every release tag so
    far write it, without a leading zero (26.9.5, 26.10.0).
    """
    yy, mm = today.year % 100, today.month
    if last is not None:
        last_yy, last_mm, last_p = version_tuple(last["version"])
        if (last_yy, last_mm) == (yy, mm):
            return f"{yy}.{mm}.{last_p + 1}"
    return f"{yy}.{mm}.0"


def check_shell_version(main_version, last, today):
    """Decides whether main's version can be this release's version.

    Being newer than the last release is not enough: the version also has to
    be in the current month. 26.10.3 on main is newer than a released 26.10.2,
    but on 2026-11-01 the release is 26.11.0 and main still needs a bump.

    Returns release_version, the version this release goes out as: main's when
    it is bumped, expected_next when it is not.
    """
    notes = []
    expected = expected_next_version(last, today)
    main = version_tuple(main_version)
    if main is None:
        return {"bumped": False, "expected_next": expected, "release_version": expected,
                "notes": [f"main's version '{main_version}' is not yy.mm.p"]}

    last_version = version_tuple(last["version"]) if last else None
    current_month = (today.year % 100, today.month)
    if last_version is not None and main <= last_version:
        bumped = False
        notes.append(f"main's {main_version} is not newer than the last release {last['version']}")
    elif main[:2] < current_month:
        bumped = False
        notes.append(f"main's {main_version} is newer than the last release, but from an earlier month "
                     f"than today's {today.isoformat()}")
    else:
        bumped = True
        if main_version != expected:
            # Newer and not from a past month, but not the version this
            # procedure would have picked: a skipped patch number, or a month
            # still to come.
            notes.append(f"newer than the last release, but the next version by yy.mm.p would be {expected}")
    return {"bumped": bumped, "expected_next": expected,
            "release_version": main_version if bumped else expected, "notes": notes}


# ---------------------------------------------------------------------------
# Branches
# ---------------------------------------------------------------------------

def branch_sha(repo, branch):
    return gh("api", f"repos/{repo}/git/ref/heads/{branch}", "--jq", ".object.sha").strip()


def rc_state(repo):
    """Where release/candidate stands against main: identical, behind (can be
    fast-forwarded to main) or diverged/ahead (cannot)."""
    main_sha = branch_sha(repo, "main")
    rc_sha = branch_sha(repo, RC_BRANCH)
    # compare/BASE...HEAD: status is HEAD relative to BASE, so "ahead" means
    # main is ahead of release/candidate -- a fast-forward.
    status = gh("api", f"repos/{repo}/compare/{rc_sha}...{main_sha}", "--jq", ".status").strip()
    meaning = {"identical": "identical", "ahead": "behind main (fast-forward)"}.get(status, status)
    return {"main_sha": main_sha, "rc_sha": rc_sha, "rc_vs_main": meaning,
            "can_fast_forward": status in ("identical", "ahead")}


# ---------------------------------------------------------------------------
# Sandbox servers
# ---------------------------------------------------------------------------

def latest_server_tags():
    output = run(["git", "ls-remote", "--tags", "--refs", SERVER_SOURCE_REPO, "mariadb-*"]).stdout
    latest = {}
    for line in output.splitlines():
        match = SERVER_TAG_PATTERN.match(line.rpartition("refs/tags/")[2])
        if match is None:
            continue
        version = tuple(int(part) for part in match.groups())
        if version[0] in SERVER_MAJORS and version > latest.get(version[0], ()):
            latest[version[0]] = version
    return {major: "mariadb-{}.{}.{}".format(*version) for major, version in latest.items()}


def index_series(index):
    """{major: (version tuple, {platform key: package})}, the newest patch of
    each supported series in the sandbox server index."""
    series = {}
    for entry in index.get("serverVersions", []):
        if entry.get("major") not in SERVER_MAJORS:
            continue
        for patch_group in entry.get("patches", []):
            for patch, packages in patch_group.items():
                version = (entry["major"], entry["minor"], int(patch))
                if entry["major"] not in series or version > series[entry["major"]][0]:
                    series[entry["major"]] = (version, {package["os"]: package for package in packages})
    return series


def package_release(url):
    """(release tag, package name) from an index entry's URL."""
    tag, _, name = url.partition("/releases/download/")[2].partition("/")
    return tag, name


_release_cache = {}


def release_checksums(tag):
    """None when the release is not published; otherwise {package: sha256}
    from its SERVER_SHA256SUMS, empty when it has none."""
    if tag not in _release_cache:
        view = run(["gh", "release", "view", tag, "-R", SHELL_REPO, "--json", "isDraft", "--jq", ".isDraft"],
                   check=False)
        if view.returncode != 0 or view.stdout.strip() != "false":
            _release_cache[tag] = None
        else:
            sums = run(["gh", "release", "download", tag, "-R", SHELL_REPO, "-p", SERVER_CHECKSUMS_ASSET,
                        "-O", "-"], check=False)
            checksums = {}
            for line in sums.stdout.splitlines() if sums.returncode == 0 else []:
                sha256sum, _, name = line.strip().partition("  ")
                if name:
                    checksums[name.lstrip("*")] = sha256sum
            _release_cache[tag] = checksums
    return _release_cache[tag]


def classify_series(tag_version, indexed):
    """What a series needs, from its newest tag and its index entry, as
    update_version.py decides it: unchanged, new, pending, inconsistent or
    problem, with the reason."""
    if indexed is None:
        return "new", "not in the index yet"
    version, packages = indexed
    indexed_text = "{}.{}.{}".format(*version)
    if tag_version > version:
        return "new", f"newer than the index's {indexed_text}"
    if tag_version < version:
        return "problem", f"older than the index's {indexed_text}"

    missing = [key for key in EXPECTED_PLATFORMS if key not in packages]
    if missing:
        return "inconsistent", f"the index has {indexed_text} without {', '.join(missing)}"

    unpublished, wrong = set(), []
    for key, package in sorted(packages.items()):
        tag, name = package_release(package.get("url", ""))
        published = release_checksums(tag)
        if published is None:
            unpublished.add(tag)
        elif not published:
            wrong.append(f"{key}: {tag} has no {SERVER_CHECKSUMS_ASSET}")
        elif name not in published:
            wrong.append(f"{key}: {tag} does not carry {name}")
        elif published[name] != package.get("sha256sum"):
            wrong.append(f"{key}: {tag} carries {name} under another checksum")
    if wrong:
        return "inconsistent", f"the index has {indexed_text}, but " + "; ".join(wrong)
    if unpublished:
        return "pending", f"the index has {indexed_text} in {', '.join(sorted(unpublished))}, not published yet"
    releases = sorted({package_release(p["url"])[0] for p in packages.values()})
    return "unchanged", f"the index has {indexed_text} from {', '.join(releases)} ({SERVER_CHECKSUMS_ASSET} matches)"


def sandbox_status(tags, index):
    """For each series: its status against the index, and for a series that
    needs packages from a build, the run publish-release would attach or why
    there is none.

    Only new and pending series need a build. An unchanged series is left
    alone -- no build, no artifacts, no index change -- however old its build
    is: the published release carries it, and release assets do not expire.
    """
    fields = "databaseId,displayTitle,createdAt,conclusion,status,url"
    successful = recent = None
    indexed = index_series(index)

    status = {}
    for major in SERVER_MAJORS:
        tag = tags.get(major)
        entry = {"tag": tag, "needs_build": False, "blocked": False}
        status[major] = entry
        if tag is None:
            entry.update(series_status="problem", blocked=True,
                         decision=f"MariaDB/server has no mariadb-{major}.x.y release tag")
            continue

        tag_version = tuple(int(part) for part in tag.split("-")[1].split("."))
        series_status, reason = classify_series(tag_version, indexed.get(major))
        entry.update(series_status=series_status, reason=reason)
        if series_status == "unchanged":
            entry["decision"] = f"unchanged: {reason} -- no build, no index change"
            continue
        if series_status == "problem":
            entry.update(blocked=True, decision=f"problem: {reason}")
            continue
        if series_status == "inconsistent":
            entry.update(blocked=True, decision=f"INCONSISTENT: {reason} -- ask the user")
            continue

        # new or pending: the packages come from a build.
        if successful is None:
            successful = gh_json("run", "list", "-R", SHELL_REPO, "--workflow", "sandbox-server.yml",
                                 "--status", "success", "--limit", str(RUN_SEARCH_LIMIT), "--json", fields)
            recent = gh_json("run", "list", "-R", SHELL_REPO, "--workflow", "sandbox-server.yml",
                             "--limit", "100", "--json", fields)
        prefix = f"{series_status}: {reason}"
        title = f"Sandbox Server {tag}"
        pending = [r for r in recent if r["displayTitle"] == title and r["status"] != "completed"]
        success = next((r for r in successful if r["displayTitle"] == title), None)
        failed = next((r for r in recent if r["displayTitle"] == title and r["status"] == "completed"
                       and r["conclusion"] != "success"), None)

        if pending:
            entry.update(pending_run=pending[0]["databaseId"], pending_url=pending[0]["url"],
                         decision=f"{prefix} -- {SANDBOX_WORKFLOW_NAME} run {pending[0]['databaseId']} "
                                  "is still running, wait for it")
            continue

        if success:
            artifacts = gh_json("api", f"repos/{SHELL_REPO}/actions/runs/{success['databaseId']}/artifacts?per_page=100")
            expired = [a["name"] for a in artifacts["artifacts"] if a["expired"]]
            entry.update(success_run=success["databaseId"], success_created=success["createdAt"])
            if not artifacts["artifacts"] or expired:
                entry.update(needs_build=True,
                             decision=f"{prefix} -- {SANDBOX_WORKFLOW_NAME} run {success['databaseId']} succeeded "
                                      "but its artifacts expired, build it again")
            else:
                entry["decision"] = f"{prefix} -- built by {SANDBOX_WORKFLOW_NAME} run {success['databaseId']}"
            continue

        entry["needs_build"] = True
        if failed:
            entry.update(last_failed_run=failed["databaseId"],
                         decision=f"{prefix} -- no successful build ({SANDBOX_WORKFLOW_NAME} run "
                                  f"{failed['databaseId']} concluded {failed['conclusion']}), build it")
        else:
            entry["decision"] = f"{prefix} -- never built, build it"
    return status


# ---------------------------------------------------------------------------
# preflight
# ---------------------------------------------------------------------------

def cmd_preflight(args):
    today = now_utc().date()
    shell = rc_state(SHELL_REPO)
    plugins = rc_state(PLUGINS_REPO)
    shell["main_version"] = shell_version(shell["main_sha"])
    shell["rc_version"] = shell_version(shell["rc_sha"])
    plugins["main_version"] = plugins_version(plugins["main_sha"])
    plugins["rc_version"] = plugins_version(plugins["rc_sha"])
    last = last_release()
    version_check = check_shell_version(shell["main_version"], last, today)
    index = json.loads(raw_file(PLUGINS_REPO, SERVER_INDEX_FILE, plugins["main_sha"]))
    servers = sandbox_status(latest_server_tags(), index)

    report = {
        "today": today.isoformat(),
        "last_release": last,
        "shell": shell,
        "plugins": plugins,
        "step1_shell_version": version_check,
        "step2_plugins_in_sync": plugins["main_version"] == shell["main_version"],
        "step2_servers": servers,
    }

    if args.json:
        print(json.dumps(report, indent=2))
        return 0

    def short(sha):
        return sha[:9]

    print("Step 1 - shell version")
    print(f"  last published version:   "
          f"{last['version'] + (' (pre-release)' if last['prerelease'] else '') if last else 'none'}")
    print(f"  main MYSQL_VERSION:       {shell['main_version']}  -> "
          f"{'bumped' if version_check['bumped'] else 'NOT bumped'} (today {today.isoformat()})")
    print(f"  version for this release: {version_check['release_version']}"
          f"{'' if version_check['bumped'] else '  (main needs a bump PR to it)'}")
    for note in version_check["notes"]:
        print(f"  note: {note}")
    print()
    print("Step 2 - plugins version and sandbox servers")
    print(f"  plugins main VERSION: {plugins['main_version']}  -> "
          f"{'in sync with' if report['step2_plugins_in_sync'] else 'differs from'} "
          f"the shell's {shell['main_version']}")
    print("  newest MariaDB/server tag of each series, against the plugins main sandbox index:")
    for major, entry in servers.items():
        print(f"  {major}.x  {entry['tag'] or '-':<16} {entry['decision']}")
    print()
    print("Step 3 - release/candidate")
    for name, repo in (("shell", shell), ("plugins", plugins)):
        print(f"  {name:<8} main {short(repo['main_sha'])} ({repo['main_version']}), "
              f"rc {short(repo['rc_sha'])} ({repo['rc_version']}): rc is {repo['rc_vs_main']}")
    return 0


# ---------------------------------------------------------------------------
# shell-bump-pr
# ---------------------------------------------------------------------------

def cmd_shell_bump_pr(args):
    """Opens the PR that sets MYSQL_VERSION on main to a version, or finds the
    one a previous attempt opened. Done through the API, so no checkout of the
    shell repo is touched."""
    match = SHELL_VERSION_PATTERN.match(args.version)
    if match is None:
        raise SystemExit(f"'{args.version}' is not yy.mm.p")
    major, minor, patch = match.groups()
    branch = f"wip/version-{args.version}"

    existing = gh_json("pr", "list", "-R", SHELL_REPO, "--head", branch, "--state", "open",
                       "--json", "number,url")
    if existing:
        print(f"{existing[0]['number']}\t{existing[0]['url']}\t(already open)")
        return 0

    main_sha = branch_sha(SHELL_REPO, "main")
    old = raw_file(SHELL_REPO, "MYSQL_VERSION", main_sha)
    new = old
    for key, value in (("MAJOR", major), ("MINOR", minor), ("PATCH", patch)):
        new = re.sub(rf"^MYSQL_VERSION_{key}=.*$", f"MYSQL_VERSION_{key}={value}", new, count=1,
                     flags=re.MULTILINE)
    if new == old:
        raise SystemExit(f"MYSQL_VERSION on main already says {args.version}")

    blob = gh("api", f"repos/{SHELL_REPO}/contents/MYSQL_VERSION?ref={main_sha}", "--jq", ".sha").strip()
    gh("api", f"repos/{SHELL_REPO}/git/refs", "-f", f"ref=refs/heads/{branch}", "-f", f"sha={main_sha}")
    payload = {
        "message": f"Updated version to {args.version}",
        "content": base64.b64encode(new.encode()).decode(),
        "sha": blob,
        "branch": branch,
    }
    run(["gh", "api", "-X", "PUT", f"repos/{SHELL_REPO}/contents/MYSQL_VERSION", "--input", "-"],
        stdin=json.dumps(payload))

    # No test depends on MYSQL_VERSION, so the PR skips CI. The push to main
    # it merges into still runs MariaDB-Shell-CI-Publish, which /release waits
    # for.
    url = gh("pr", "create", "-R", SHELL_REPO, "--base", "main", "--head", branch,
             "--title", f"[bypass-ci] Updated version to {args.version}",
             "--body", f"Sets MYSQL_VERSION to {args.version} for the next release "
                       f"(last release: {args.last or 'none'}).").strip()
    print(f"{url.rstrip('/').rpartition('/')[2]}\t{url}\t(created)")
    return 0


# ---------------------------------------------------------------------------
# push-rc
# ---------------------------------------------------------------------------

def workflow_state(repo, workflow):
    return gh("api", f"repos/{repo}/actions/workflows/{workflow}", "--jq", ".state").strip()


def cmd_push_rc(args):
    """Fast-forwards a repo's release/candidate to a SHA.

    For the plugins repo, its trigger-shell-release.yml would answer the push
    by starting a second shell build. The shell's own push builds the same
    commits, so that workflow is disabled for the push and put back the way it
    was afterwards.
    """
    repo = SHELL_REPO if args.repo == "shell" else PLUGINS_REPO
    start = now_utc()
    was_active = False
    if args.repo == "plugins":
        state = workflow_state(PLUGINS_REPO, PLUGINS_TRIGGER_WORKFLOW)
        was_active = state == "active"
        print(f"{PLUGINS_TRIGGER_WORKFLOW_NAME} ({PLUGINS_TRIGGER_WORKFLOW}): {state}")
        if was_active:
            gh("workflow", "disable", PLUGINS_TRIGGER_WORKFLOW, "-R", PLUGINS_REPO)
            print("  disabled it for the push")

    try:
        gh("api", "-X", "PATCH", f"repos/{repo}/git/refs/heads/{RC_BRANCH}",
           "-f", f"sha={args.sha}", "-F", "force=false")
        print(f"{repo} {RC_BRANCH} -> {args.sha}")

        if was_active:
            # The push event is handled asynchronously: keep the workflow
            # disabled until GitHub has had time to evaluate it, and check
            # that it really started nothing.
            time.sleep(args.settle)
            runs = gh_json("run", "list", "-R", PLUGINS_REPO, "--workflow", PLUGINS_TRIGGER_WORKFLOW,
                           "--limit", "5", "--json", "databaseId,createdAt")
            started = [r["databaseId"] for r in runs if parse_time(r["createdAt"]) >= start]
            if started:
                print(f"  WARNING: {PLUGINS_TRIGGER_WORKFLOW_NAME} run(s) {', '.join(map(str, started))} "
                      "started anyway")
            else:
                print(f"  no {PLUGINS_TRIGGER_WORKFLOW_NAME} run was started")
    finally:
        if was_active:
            gh("workflow", "enable", PLUGINS_TRIGGER_WORKFLOW, "-R", PLUGINS_REPO)
            print(f"{PLUGINS_TRIGGER_WORKFLOW_NAME}: enabled again "
                  f"({workflow_state(PLUGINS_REPO, PLUGINS_TRIGGER_WORKFLOW)})")
    return 0


# ---------------------------------------------------------------------------
# find-run
# ---------------------------------------------------------------------------

def cmd_find_run(args):
    """Waits for a run to appear. A run is only listed once GitHub has queued
    it, which for workflow_run- and dispatch-triggered runs can take a while."""
    after = parse_time(args.created_after) if args.created_after else None
    deadline = time.monotonic() + args.timeout
    while True:
        runs = gh_json("run", "list", "-R", args.repo, "--workflow", args.workflow, "--limit", "30",
                       "--json", "databaseId,displayTitle,event,headSha,createdAt,status,conclusion,url,workflowName")
        matches = [
            r for r in runs
            if (args.event is None or r["event"] == args.event)
            and (args.sha is None or r["headSha"] == args.sha)
            and (args.title is None or r["displayTitle"] == args.title)
            and (after is None or parse_time(r["createdAt"]) >= after)
        ]
        if matches:
            # gh lists newest first; with several matches, report them all so
            # duplicates (a second trigger of the same build) are visible.
            for r in matches:
                print(f"{r['workflowName']} run {r['databaseId']}\t{r['event']}\t{r['status']}/{r['conclusion'] or '-'}\t"
                      f"{r['createdAt']}\t{r['url']}")
            return 0
        if time.monotonic() >= deadline:
            print(f"No {args.workflow} run matched within {args.timeout}s.", file=sys.stderr)
            return 1
        time.sleep(args.interval)


# ---------------------------------------------------------------------------
# watch
# ---------------------------------------------------------------------------

def cmd_watch(args):
    seen = {}
    name = None
    print(f"[{stamp()}] watching run {args.run_id}", flush=True)
    while True:
        result = run(["gh", "run", "view", str(args.run_id), "-R", args.repo,
                      "--json", "status,conclusion,jobs,url,displayTitle,workflowName"], check=False)
        if result.returncode != 0:
            # A transient API error must not end the watch: report and retry.
            print(f"[{stamp()}] gh run view failed, retrying: {result.stderr.strip()}", flush=True)
            time.sleep(args.interval)
            continue
        run_info = json.loads(result.stdout)
        if name is None:
            name = f"{run_info['workflowName']} run {args.run_id}"
            print(f"[{stamp()}] {name}: {run_info['displayTitle']}\n           {run_info['url']}", flush=True)

        failed = []
        for job in run_info["jobs"]:
            state = job["conclusion"] or job["status"]
            if seen.get(job["name"]) != state:
                seen[job["name"]] = state
                print(f"[{stamp()}] {job['name']}: {state}", flush=True)
            if job["conclusion"] in ("failure", "timed_out", "startup_failure"):
                failed.append(job)

        if failed and not args.keep_going and run_info["status"] != "completed":
            report_failures(args, failed)
            print(f"[{stamp()}] RESULT: {name} failure (still {run_info['status']}; "
                  "not cancelled)", flush=True)
            return 1

        if run_info["status"] == "completed":
            conclusion = run_info["conclusion"]
            if failed:
                report_failures(args, failed)
            print(f"[{stamp()}] RESULT: {name} {conclusion}", flush=True)
            return 0 if conclusion == "success" else 1
        time.sleep(args.interval)


def report_failures(args, failed):
    for job in failed:
        print(f"\n=== FAILED: {job['name']} ({job['conclusion']})\n    {job['url']}", flush=True)
        failed_steps = [s["name"] for s in job.get("steps", []) if s.get("conclusion") == "failure"]
        if failed_steps:
            print(f"    failed step: {', '.join(failed_steps)}", flush=True)
        log = run(["gh", "run", "view", "-R", args.repo, "--job", str(job["databaseId"]), "--log-failed"],
                  check=False)
        # Each line is "<job>\t<step>\t<timestamp> <text>": keep only the text.
        lines = [line.split("\t", 2)[-1].split(" ", 1)[-1] for line in log.stdout.splitlines()]
        # The end of a failed step's log is the runner's post-job cleanup, so
        # show the lines leading up to the first error instead, when there is one.
        first_error = next((i for i, line in enumerate(lines) if line.startswith("##[error]")), None)
        if first_error is not None:
            excerpt = lines[max(0, first_error - args.log_lines + 1):first_error + 1]
        else:
            excerpt = lines[-args.log_lines:]
        if excerpt:
            print("    --- failed step's log, up to its first error ---", flush=True)
            for line in excerpt:
                print(f"    {line}", flush=True)


def main():
    parser = argparse.ArgumentParser(description="Release checks and run watching for /release.")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("preflight", help="Report the state every release step depends on.")
    p.add_argument("--json", action="store_true", help="Print the report as JSON.")
    p.set_defaults(func=cmd_preflight)

    p = sub.add_parser("shell-bump-pr", help="Open (or find) the PR setting MYSQL_VERSION on main.")
    p.add_argument("version", help="The new version, yy.mm.p.")
    p.add_argument("--last", help="The last released version, for the PR description.")
    p.set_defaults(func=cmd_shell_bump_pr)

    p = sub.add_parser("push-rc", help="Fast-forward release/candidate (plugins: with its shell trigger off).")
    p.add_argument("repo", choices=("shell", "plugins"))
    p.add_argument("sha", help="The exact main commit preflight reported.")
    p.add_argument("--settle", type=int, default=60,
                   help="Plugins only: seconds to keep the trigger workflow disabled after the push (default 60).")
    p.set_defaults(func=cmd_push_rc)

    p = sub.add_parser("find-run", help="Wait for a matching workflow run and print it.")
    p.add_argument("--repo", default=SHELL_REPO)
    p.add_argument("--workflow", required=True, help="Workflow file name, e.g. shell-release.yml.")
    p.add_argument("--event", help="push, repository_dispatch, workflow_run, workflow_dispatch...")
    p.add_argument("--sha", help="Head commit of the run.")
    p.add_argument("--title", help="Exact run display title.")
    p.add_argument("--created-after", help="ISO-8601 UTC time; older runs are ignored.")
    p.add_argument("--timeout", type=int, default=900, help="Seconds to wait (default 900).")
    p.add_argument("--interval", type=int, default=20)
    p.set_defaults(func=cmd_find_run)

    p = sub.add_parser("watch", help="Poll a run until it finishes or a job fails.")
    p.add_argument("run_id", type=int)
    p.add_argument("--repo", default=SHELL_REPO)
    p.add_argument("--interval", type=int, default=60, help="Seconds between polls (default 60).")
    p.add_argument("--keep-going", action="store_true",
                   help="Do not stop at the first failed job; wait for the whole run.")
    p.add_argument("--log-lines", type=int, default=40)
    p.set_defaults(func=cmd_watch)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
