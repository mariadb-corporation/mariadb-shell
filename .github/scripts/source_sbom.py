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

"""Write a CycloneDX SBOM of the C/C++ libraries the shell is built from.

Syft identifies some libraries inside the release packages from their binaries
(OpenSSL, CPython and the bundled Python packages), but not all: a statically
linked or renamed library, or one with no version string in it, goes unseen.
This SBOM lists them from their sources instead -- every vcpkg port the given
manifests depend on, and the libraries copied into ext/ -- each with the NVD
CPE that Grype matches CVEs against.

A port's version is its override in the manifest, or else its entry in vcpkg's
versions/baseline.json at the manifest's builtin-baseline commit. That is the
version vcpkg picks unless a dependency asks for a newer one, so it is the
lower bound of what ships.

Usage:
  source_sbom.py --name NAME --version VER [--ext-listing FILE] \
                 -o OUT.cdx.json MANIFEST [MANIFEST...]
"""

import argparse
import json
import re
import sys
import urllib.request
import uuid

# vcpkg port -> (NVD vendor, NVD product). A port missing here is still listed,
# but Grype cannot match it, so the script warns until it is added.
PORT_CPES = {
    "antlr4": ("antlr", "antlr"),
    "bzip2": ("bzip", "bzip2"),
    "curl": ("haxx", "curl"),
    "libffi": ("libffi_project", "libffi"),
    "liblzma": ("tukaani", "xz"),
    "libssh": ("libssh", "libssh"),
    "openssl": ("openssl", "openssl"),
    "rapidjson": ("tencent", "rapidjson"),
    "sqlite3": ("sqlite", "sqlite"),
    "zlib": ("zlib", "zlib"),
    "zstd": ("facebook", "zstandard"),
}

# Ports that never reach a release package.
SKIP_PORTS = {"gtest"}

# Libraries copied into ext/, found by directory name in the --ext-listing
# file (one ext/ entry per line, as `git ls-tree --name-only <tag> ext/`
# prints them). The version comes from the directory name, so a bump of the
# copy is picked up without editing this table.
#   linenoise-ng: unmaintained upstream, no releases and no CPE; not listed.
#   polyglot: the GraalVM JavaScript bridge, not built for MariaDB releases.
EXT_LIBRARIES = {
    "tinyxml2": ("tinyxml2_project", "tinyxml2"),
}


def manifest_ports(manifest):
    names = []
    for dep in manifest.get("dependencies", []):
        name = dep if isinstance(dep, str) else dep["name"]
        if name not in SKIP_PORTS:
            names.append(name)
    return names


def override_version(override):
    for key in ("version", "version-semver", "version-date", "version-string"):
        if key in override:
            return override[key]
    return None


def fetch_baseline(sha, cache={}):
    if sha not in cache:
        url = f"https://raw.githubusercontent.com/microsoft/vcpkg/{sha}/versions/baseline.json"
        with urllib.request.urlopen(url, timeout=60) as resp:
            cache[sha] = json.load(resp)["default"]
    return cache[sha]


def component(name, version, cpe_pair, source):
    comp = {
        "type": "library",
        "bom-ref": f"{name}@{version}",
        "name": name,
        "version": version,
        "purl": f"pkg:generic/{name}@{version}",
        "properties": [{"name": "source", "value": source}],
    }
    if cpe_pair:
        vendor, product = cpe_pair
        comp["cpe"] = f"cpe:2.3:a:{vendor}:{product}:{version}:*:*:*:*:*:*:*"
    return comp


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("manifests", nargs="+", help="vcpkg.json files")
    ap.add_argument("--name", required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--ext-listing", help="file listing the ext/ entries")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()

    components = {}
    unmapped = set()

    for path in args.manifests:
        with open(path) as f:
            manifest = json.load(f)
        overrides = {o["name"]: override_version(o)
                     for o in manifest.get("overrides", [])}
        baseline = fetch_baseline(manifest["builtin-baseline"])
        for port in manifest_ports(manifest):
            version = overrides.get(port) or baseline.get(port, {}).get("baseline")
            if not version:
                print(f"::warning::{path}: no version found for vcpkg port '{port}'",
                      file=sys.stderr)
                continue
            if port not in PORT_CPES:
                unmapped.add(port)
            comp = component(port, version, PORT_CPES.get(port), f"vcpkg ({path})")
            components.setdefault(comp["bom-ref"], comp)

    if args.ext_listing:
        with open(args.ext_listing) as f:
            entries = [line.strip().rsplit("/", 1)[-1] for line in f if line.strip()]
        for lib, cpe_pair in EXT_LIBRARIES.items():
            for entry in entries:
                m = re.fullmatch(rf"{re.escape(lib)}-(\d[\w.]*)", entry)
                if m:
                    comp = component(lib, m.group(1), cpe_pair, f"ext/{entry}")
                    components.setdefault(comp["bom-ref"], comp)

    for port in sorted(unmapped):
        print(f"::warning::vcpkg port '{port}' has no CPE in .github/scripts/source_sbom.py "
              "PORT_CPES, so no CVE can be matched to it", file=sys.stderr)

    bom = {
        "bomFormat": "CycloneDX",
        "specVersion": "1.5",
        "serialNumber": f"urn:uuid:{uuid.uuid4()}",
        "version": 1,
        "metadata": {
            "component": {
                "type": "application",
                "bom-ref": args.name,
                "name": args.name,
                "version": args.version,
            }
        },
        "components": sorted(components.values(), key=lambda c: c["bom-ref"]),
    }
    with open(args.output, "w") as f:
        json.dump(bom, f, indent=2)
    print(f"{args.output}: {len(components)} components")


if __name__ == "__main__":
    main()
