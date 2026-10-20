#!/usr/bin/env python3
# Copyright (c) 2026, MariaDB plc.
#
# Moves the src/lsp-server submodule pin (`npm run lsp-server:update`).
#
# Fetches mariadb-corporation/lsp-server, shows the commit this tree pins and
# the latest commit on its main branch, and asks which commit to pin: Enter
# takes the latest, or type a commit, tag or branch. The submodule is checked
# out at that commit and the new pin is staged; committing it is left to you.
#
#   npm run lsp-server:update                  asks, offering origin/main
#   npm run lsp-server:update -- <commit>      pins <commit> without asking
#
# Before moving the pin it warns when the commit is not ahead of the current
# pin, and when its vcpkg.json does not match this tree's in the ways
# cmake/lsp_server.cmake requires.

import argparse
import json
import os
import subprocess
import sys

SUBMODULE = "src/lsp-server"
BRANCH = "main"
# The vcpkg ports mariadb-lsp links from the package; cmake/lsp_server.cmake
# requires both manifests to override them to the same versions.
SHARED_PORTS = ("openssl", "zlib")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SUB_DIR = os.path.join(ROOT, SUBMODULE)


class Error(Exception):
    pass


def git(*args, cwd=SUB_DIR, check=True):
    p = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True)
    if check and p.returncode != 0:
        raise Error(f"git {' '.join(args)} failed:\n{p.stderr.strip()}")
    return p.stdout.strip() if p.returncode == 0 else None


def describe(sha):
    return git("log", "-1", "--format=%h %cs %s", sha)


def resolve(ref):
    """The commit `ref` names, fetching it from origin when it is not known."""
    for candidate in (ref, f"origin/{ref}"):
        sha = git("rev-parse", "--verify", "--quiet", f"{candidate}^{{commit}}",
                  check=False)
        if sha:
            return sha
    if git("fetch", "--quiet", "origin", ref, check=False) is None:
        raise Error(f"'{ref}' is not a commit, tag or branch of {SUBMODULE}.")
    return git("rev-parse", "FETCH_HEAD^{commit}")


def ask(prompt):
    try:
        return input(prompt).strip()
    except EOFError:
        return ""


def confirm(prompt):
    return ask(f"{prompt} [y/N] ").lower() in ("y", "yes")


def override_versions(manifest):
    return {o["name"]: o.get("version")
            for o in manifest.get("overrides", [])
            if o.get("name") in SHARED_PORTS}


def vcpkg_mismatches(sha):
    with open(os.path.join(ROOT, "vcpkg.json"), encoding="utf-8") as f:
        ours = json.load(f)
    text = git("show", f"{sha}:vcpkg.json", check=False)
    if text is None:
        return []
    theirs = json.loads(text)
    problems = []
    if ours.get("builtin-baseline") != theirs.get("builtin-baseline"):
        problems.append(f"builtin-baseline: this tree {ours.get('builtin-baseline')}, "
                        f"lsp-server {theirs.get('builtin-baseline')}")
    a, b = override_versions(ours), override_versions(theirs)
    for port in SHARED_PORTS:
        if a.get(port) != b.get(port):
            problems.append(f"{port} override: this tree {a.get(port) or 'none'}, "
                            f"lsp-server {b.get(port) or 'none'}")
    return problems


def main():
    parser = argparse.ArgumentParser(
        description=f"Move the {SUBMODULE} pin to a commit of lsp-server "
                    f"(by default the latest on origin/{BRANCH}).")
    parser.add_argument("commit", nargs="?",
                        help="commit, tag or branch to pin, instead of asking")
    args = parser.parse_args()
    # Keep the context lines ahead of an error when the output is piped.
    sys.stdout.reconfigure(line_buffering=True)
    interactive = sys.stdin.isatty() and not args.commit

    if not os.path.exists(os.path.join(SUB_DIR, ".git")):
        print(f"Checking out {SUBMODULE}...")
        git("submodule", "update", "--init", SUBMODULE, cwd=ROOT)
    if git("status", "--porcelain"):
        raise Error(f"{SUBMODULE} has uncommitted changes; commit or stash them first.")

    print(f"Fetching origin/{BRANCH} of {SUBMODULE}...")
    fetch = ["fetch", "--quiet", "origin",
             f"+refs/heads/{BRANCH}:refs/remotes/origin/{BRANCH}"]
    if git("rev-parse", "--is-shallow-repository") == "true":
        fetch.insert(1, "--unshallow")
    git(*fetch)

    # The pin in the index: a move this script staged and nobody committed
    # yet is the starting point of the next one.
    committed = git("rev-parse", f"HEAD:{SUBMODULE}", cwd=ROOT)
    pinned = git("rev-parse", f":{SUBMODULE}", cwd=ROOT)
    staged = pinned != committed
    latest = git("rev-parse", f"origin/{BRANCH}")
    if staged:
        print(f"\n  Committed pin:     {describe(committed)}")
        print(f"  Staged pin:        {describe(pinned)}  (not committed yet)")
    else:
        print(f"\n  Pinned now:        {describe(pinned)}")
    print(f"  Latest on {BRANCH}:    {describe(latest)}")
    if pinned != latest and git("merge-base", "--is-ancestor", pinned, latest,
                                check=False) is not None:
        new = git("log", "--format=    %h %s", f"{pinned}..{latest}").splitlines()
        print(f"\n  {len(new)} new commit(s) on {BRANCH}:")
        print("\n".join(new[:20]))
        if len(new) > 20:
            print(f"    ... and {len(new) - 20} more")
    print()

    if not args.commit and pinned == latest:
        print(f"{SUBMODULE} already pins the latest on {BRANCH}"
              f"{', staged: commit it' if staged else ''}. To pin another "
              "commit: npm run lsp-server:update -- <commit>")
        return 0

    if args.commit:
        target = resolve(args.commit)
    elif interactive:
        answer = ask(f"Commit to pin, or Enter for the latest on {BRANCH} "
                     f"[{latest[:12]}]: ")
        target = resolve(answer) if answer else latest
    else:
        target = latest
    print(f"Using {describe(target)}")

    if target == pinned:
        if staged:
            print(f"{SUBMODULE} already pins that commit, staged: commit it.")
        else:
            print(f"{SUBMODULE} already pins that commit; nothing to do.")
        return 0

    if git("merge-base", "--is-ancestor", pinned, target, check=False) is None:
        if git("merge-base", "--is-ancestor", target, pinned, check=False) is not None:
            print(f"\nWarning: {target[:12]} is behind the current pin.")
        else:
            print(f"\nWarning: {target[:12]} and the current pin have diverged. "
                  "That is expected when the current pin was the head of an "
                  "lsp-server pull request that was since squash-merged.")
        if not interactive or not confirm("Pin it anyway?"):
            raise Error("Pin left unchanged.")

    problems = vcpkg_mismatches(target)
    if problems:
        print(f"\nWarning: {SUBMODULE}/vcpkg.json at {target[:12]} does not match "
              "this tree's vcpkg.json, so configure will fail until they agree:")
        for p in problems:
            print(f"  - {p}")
        if interactive and not confirm("Pin it anyway?"):
            raise Error("Pin left unchanged.")

    git("checkout", "--quiet", "--detach", target)
    git("add", SUBMODULE, cwd=ROOT)
    print(f"\n{SUBMODULE} now pins {target[:12]}; the change is staged. Commit it "
          "when ready. An existing build directory needs its lsp-server and "
          "lsp_server-prefix subdirectories deleted before it configures again.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Error as e:
        print(f"error: {e}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        sys.exit(130)
