#!/usr/bin/env python3
# Copyright (c) 2026, MariaDB plc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335 USA

"""Turn Grype JSON reports into the Vulnerability-Scan issue body.

Every report is one scanned target (a release package, or the source SBOM).
The same vulnerability in the same package version is one row, whatever the
number of targets it was found in. Rows below --min-severity are left out;
"Unknown" is always kept, because a CVE NVD has not scored yet is the newest
kind there is.

Writes:
  <out-dir>/report.md   the issue body; ends with a hidden list of the row keys
  <out-dir>/changes.md  what differs from --previous-body (new/gone rows)
and prints one word: "none" (no rows), "unchanged" (same rows as
--previous-body) or "changed".
"""

import argparse
import json
import os
import re
import sys

SEVERITIES = ["Critical", "High", "Medium", "Low", "Negligible", "Unknown"]
KEYS_RE = re.compile(r"<!-- vuln-keys: (.*?) -->", re.S)
MAX_BODY = 60000  # GitHub caps an issue body at 65536 characters


def load_rows(paths, min_severity):
    keep = set(SEVERITIES[: SEVERITIES.index(min_severity) + 1]) | {"Unknown"}
    rows = {}
    for path in paths:
        target = os.path.basename(path).removesuffix(".json")
        with open(path) as f:
            report = json.load(f)
        for match in report.get("matches", []):
            vuln = match["vulnerability"]
            art = match["artifact"]
            severity = vuln.get("severity") or "Unknown"
            if severity not in keep:
                continue
            key = f"{vuln['id']}|{art['name']}|{art['version']}"
            row = rows.setdefault(key, {
                "id": vuln["id"],
                "url": vuln.get("dataSource") or "",
                "severity": severity,
                "package": art["name"],
                "version": art["version"],
                "type": art.get("type", ""),
                "fixed": ", ".join(vuln.get("fix", {}).get("versions", [])) or "-",
                "targets": set(),
            })
            row["targets"].add(target)
    return rows


def row_line(row):
    vid = f"[{row['id']}]({row['url']})" if row["url"] else row["id"]
    targets = "<br>".join(sorted(row["targets"]))
    return (f"| {row['severity']} | {vid} | {row['package']} | {row['version']} "
            f"| {row['fixed']} | {targets} |")


def sort_key(row):
    return (SEVERITIES.index(row["severity"]), row["package"], row["id"])


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("reports", nargs="+")
    ap.add_argument("--tag", required=True)
    ap.add_argument("--run-url", required=True)
    ap.add_argument("--min-severity", default="Medium", choices=SEVERITIES[:5])
    ap.add_argument("--previous-body", help="file holding the open issue's body")
    ap.add_argument("--out-dir", default=".")
    args = ap.parse_args()

    rows = load_rows(args.reports, args.min_severity)
    ordered = sorted(rows.values(), key=sort_key)
    keys = sorted(rows)

    counts = {s: sum(1 for r in ordered if r["severity"] == s) for s in SEVERITIES}
    summary = ", ".join(f"{n} {s}" for s, n in counts.items() if n)

    header = [
        f"Vulnerabilities found in the **{args.tag}** release packages "
        f"and the libraries the shell is built from.",
        "",
        f"Severity {args.min_severity} and above, plus Unknown (not yet scored). "
        f"Scan run: {args.run_url} (the SBOMs and the full Grype reports, "
        "every severity, are that run's artifacts).",
        "",
        f"**{len(ordered)} findings:** {summary or 'none'}",
        "",
        "| Severity | Vulnerability | Package | Version | Fixed in | Found in |",
        "|---|---|---|---|---|---|",
    ]
    footer = f"\n<!-- vuln-keys: {' '.join(keys)} -->\n"

    body = "\n".join(header)
    shown = 0
    for row in ordered:
        line = "\n" + row_line(row)
        if len(body) + len(line) + len(footer) + 200 > MAX_BODY:
            break
        body += line
        shown += 1
    if shown < len(ordered):
        body += (f"\n\n_{len(ordered) - shown} more rows do not fit in an issue; "
                 "see the run's artifacts._")
    body += "\n" + footer

    os.makedirs(args.out_dir, exist_ok=True)
    with open(os.path.join(args.out_dir, "report.md"), "w") as f:
        f.write(body)

    previous = set()
    if args.previous_body and os.path.exists(args.previous_body):
        with open(args.previous_body) as f:
            m = KEYS_RE.search(f.read())
        if m:
            previous = set(m.group(1).split())

    new = [rows[k] for k in keys if k not in previous]
    gone = sorted(previous - set(keys))
    lines = [f"Scan of **{args.tag}**: {args.run_url}", ""]
    if new:
        lines += [f"**New ({len(new)}):**", "",
                  "| Severity | Vulnerability | Package | Version | Fixed in | Found in |",
                  "|---|---|---|---|---|---|"]
        lines += [row_line(r) for r in sorted(new, key=sort_key)][:200]
        lines.append("")
    if gone:
        lines += [f"**No longer found ({len(gone)}):**", ""]
        lines += [f"- {k.replace('|', ' in ', 1).replace('|', ' ')}" for k in gone][:200]
    with open(os.path.join(args.out_dir, "changes.md"), "w") as f:
        f.write("\n".join(lines) + "\n")

    if not rows:
        print("none")
    elif set(keys) == previous:
        print("unchanged")
    else:
        print("changed")


if __name__ == "__main__":
    sys.exit(main())
