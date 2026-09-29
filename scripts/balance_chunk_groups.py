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

"""Balances the chunk groups of a scripted test split by run_unit_tests.py.

A scripted test listed in run_unit_tests.py's _SPLIT_TEST_GROUPS runs as N
tasks, each with GROUP_ID set to 1..N; a chunk whose header ends in "(G)" only
runs in the task whose GROUP_ID is G, and a chunk with no group suffix runs in
every task (setup, sandbox deployment, cleanup, ...).

This script assigns the groups so the N tasks take about the same time:

  - the grouped chunks are split, in file order, into N contiguous ranges
    (group 1 first, ..., group N last), minimizing the slowest group; the
    ungrouped chunks count towards every group,
  - a range is never cut between chunks that depend on each other: a
    setup/.../cleanup sequence, consecutive chunks sharing a bug prefix
    ("BUG#32773468 ..."), and a chunk using a variable or function whose last
    definition is in an earlier grouped chunk,
  - dependencies the heuristics can't see (typically on-disk state, such as a
    dump written by one chunk and checked by the next) are declared with a
    marker comment in the dependent chunk's code:

        # balance: keep-with-previous

    a longer run that must stay in one group (a chunk switching the session
    to another user, the chunks running as that user, and the one switching
    back) is marked in its first and last chunks with:

        # balance: begin-block
        # balance: end-block

    and a heuristic dependency that isn't real can be dropped with:

        # balance: allow-cut-before

The chunk durations come from the chunk timing file run_unit_tests.py keeps
beside its -t timing file (test-chunk-times.txt): the shell_script_tester
records every executed chunk when CHUNK_TIMINGS_FILE is set, and
run_unit_tests.py sets it for scripted tests and merges the results. Timings
measured with N parallel workers are inflated compared to a serial run, but
only their relative sizes matter, as long as the runs use the same parallelism.

Usage:
    balance_chunk_groups.py <script> <N> [--write] [--explain] [--times FILE]
        Balance the script's grouped chunks into N groups.
    balance_chunk_groups.py <script> <N> --estimate [--write]
        Same, but with each chunk's non-blank line count standing in for its
        duration: a provisional split for a script with no timings yet, which
        is far quicker to run split (and time) than unsplit.
    balance_chunk_groups.py <script> --init [--common ID]...
        First split of a script that has no groups yet: gives every chunk
        except the common ones (INCLUDEs, "entry point", "Setup", "Cleanup"
        and any --common) a "(1)" placeholder, for the balancing to renumber.
    balance_chunk_groups.py --merge FILE... [--skip PREFIX]... [--times FILE]
        Merge chunk timing files into the timing file, skipping the chunks whose
        id starts with a --skip prefix. run_unit_tests.py only merges passing
        tasks; a failed task keeps its chunk-timings.txt in its log folder, and
        this merges it once the failing chunks are left out.
"""

import argparse
import ast
import difflib
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Set, Tuple

CHUNK_TIMES_FILE_NAME = "test-chunk-times.txt"
DEFAULT_TIMES_FILE = Path(__file__).resolve().parent.parent / "unittest" / CHUNK_TIMES_FILE_NAME

KEEP_WITH_PREVIOUS_MARKER = "balance: keep-with-previous"
BEGIN_BLOCK_MARKER = "balance: begin-block"
END_BLOCK_MARKER = "balance: end-block"
ALLOW_CUT_BEFORE_MARKER = "balance: allow-cut-before"

# Chunk header token per script language (shell_script_tester's get_chunk_token)
_CHUNK_TOKENS = {".py": "#@", ".js": "//@"}

_GROUP_SUFFIX = re.compile(r"\(\s*(\d*)\s*\)\s*$")


# ----------------------------------------------------------------------------
# Chunk timing files
# ----------------------------------------------------------------------------

@dataclass
class ChunkTiming:
    """One chunk timing: a line of a chunk timing file.

    Lines read "<script> <group id> <duration ms> <chunk id>", the chunk id
    last since it contains spaces; the script is the test script's file name.
    """
    script: str
    group_id: int
    duration_ms: float
    chunk_id: str

    def to_line(self) -> str:
        return f"{self.script} {self.group_id} {self.duration_ms:.2f} {self.chunk_id}\n"

    @staticmethod
    def from_line(line: str) -> Optional["ChunkTiming"]:
        parts = line.rstrip("\n").split(" ", 3)
        if len(parts) != 4 or not parts[3]:
            return None
        try:
            return ChunkTiming(parts[0], int(parts[1]), float(parts[2]), parts[3])
        except ValueError:
            return None


def read_chunk_timings(path: Path) -> List[ChunkTiming]:
    """Reads a chunk timing file, skipping malformed lines."""
    if not path.exists():
        return []
    with open(path, "r", encoding="utf-8") as f:
        return [t for t in map(ChunkTiming.from_line, f) if t]


def load_chunk_times(path: Path) -> Dict[Tuple[str, str], ChunkTiming]:
    """Loads a chunk timing file keyed by (script, chunk id)."""
    return {(t.script, t.chunk_id): t for t in read_chunk_timings(path)}


def merge_chunk_times(path: Path, samples: Iterable[ChunkTiming]) -> None:
    """Merges one run's chunk timings into the persistent chunk timing file.

    The latest run replaces the recorded value of every chunk it timed. An
    ungrouped chunk runs in every group of a split test, so the run holds
    several samples of it: those are averaged into a single value.
    """
    by_key: Dict[Tuple[str, str], List[ChunkTiming]] = {}
    for sample in samples:
        by_key.setdefault((sample.script, sample.chunk_id), []).append(sample)
    if not by_key:
        return

    times = load_chunk_times(path)
    for key, key_samples in by_key.items():
        times[key] = ChunkTiming(
            key[0], key_samples[-1].group_id,
            sum(s.duration_ms for s in key_samples) / len(key_samples), key[1])

    path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = path.with_name(path.name + ".tmp")
    with open(tmp_path, "w", encoding="utf-8") as f:
        for key in sorted(times):
            f.write(times[key].to_line())
    os.replace(tmp_path, path)


# ----------------------------------------------------------------------------
# Script parsing
# ----------------------------------------------------------------------------

@dataclass
class Chunk:
    """A chunk of a test script.

    Attributes:
        header_index: Index of the header line within the script's lines.
        chunk_id: The id the tester knows the chunk by (and records its timing
            under): the header without the group suffix, context, etc.
        group: The chunk's group, 0 when the suffix is "()", None when
            the chunk has no group suffix (and so runs in every group).
        code: The chunk's code lines, up to the next header.
    """
    header_index: int
    chunk_id: str
    group: Optional[int]
    code: List[str] = field(default_factory=list)

    @property
    def grouped(self) -> bool:
        return self.group is not None

    def has_marker(self, marker: str) -> bool:
        return any(marker in line for line in self.code)


def _parse_header(text: str) -> Tuple[str, Optional[int]]:
    """Parses a chunk header, minus its token, the way
    Shell_script_tester::load_chunk_definition does.

    Returns the chunk id and group; unlike the tester (which ignores it), an
    empty "()" suffix yields group 0, as it's a chunk meant to be grouped.
    """
    if text.startswith("#"):
        text = text[2:] if text[1:2] == " " else text[1:]

    group = None
    m = _GROUP_SUFFIX.search(text)
    if m:
        if m.group(1):
            group = int(m.group(1))
            text = text[:m.start()].strip()
        else:
            # The tester keeps "()" in the chunk id
            group = 0

    start, end = text.find("{"), text.rfind("}")
    if start != -1 and end != -1 and start < end:
        text = text[:start]

    for prefix in ("<OUT>", "<ERR>", "<PROTOCOL>", "<>"):
        if text.startswith(prefix):
            text = text[len(prefix):].strip()
            break

    if len(text) > 1 and text[0] in "+-":
        text = text[1:]

    use = text.find("[USE:")
    if use != -1 and text.find("]", use) != -1:
        text = text[:use]

    return text.strip(), group


def parse_chunks(lines: List[str], token: str) -> List[Chunk]:
    chunks: List[Chunk] = []
    for index, line in enumerate(lines):
        if line.startswith(token):
            chunk_id, group = _parse_header(line[len(token):].rstrip("\n"))
            chunks.append(Chunk(index, chunk_id, group))
        elif chunks:
            chunks[-1].code.append(line)
    return chunks


# ----------------------------------------------------------------------------
# Dependencies between chunks
# ----------------------------------------------------------------------------

class _NameCollector(ast.NodeVisitor):
    """Collects the global names a chunk binds and the ones it reads.

    Names local to a function, lambda or comprehension (parameters and names
    assigned inside it) are neither: reading them doesn't depend on another
    chunk.
    """

    def __init__(self):
        self.defined: Dict[str, int] = {}  # name -> first line binding it
        self.used: Dict[str, int] = {}     # name -> first line reading it
        self.deferred_used: Set[str] = set()  # names read inside functions
        self._locals: List[Set[str]] = []
        self._in_function = 0

    def _bind(self, name: str, lineno: int) -> None:
        if self._locals:
            self._locals[-1].add(name)
        else:
            self.defined.setdefault(name, lineno)

    def _read(self, name: str, lineno: int) -> None:
        if any(name in scope for scope in self._locals):
            return
        # A function body reads globals when it's called, not where it's
        # defined, so any definition in the chunk may be the one it sees
        if self._in_function:
            self.deferred_used.add(name)
        else:
            self.used.setdefault(name, lineno)

    def _visit_scope(self, node, params: Iterable[str], body) -> None:
        scope = set(params)
        # Names assigned anywhere in the scope are local to all of it
        for child in ast.walk(node):
            if isinstance(child, ast.Name) and isinstance(child.ctx, ast.Store):
                scope.add(child.id)
            elif isinstance(child, ast.Global):
                scope.difference_update(child.names)
        self._locals.append(scope)
        for child in body:
            self.visit(child)
        self._locals.pop()

    @staticmethod
    def _params(args: ast.arguments) -> List[str]:
        all_args = args.posonlyargs + args.args + args.kwonlyargs
        names = [a.arg for a in all_args]
        if args.vararg:
            names.append(args.vararg.arg)
        if args.kwarg:
            names.append(args.kwarg.arg)
        return names

    def visit_FunctionDef(self, node):
        self._bind(node.name, node.lineno)
        for default in node.args.defaults + node.args.kw_defaults:
            if default:
                self.visit(default)
        for child in ast.walk(node):
            if isinstance(child, ast.Global):
                for name in child.names:
                    self.defined.setdefault(name, node.lineno)
        self._in_function += 1
        self._visit_scope(node, self._params(node.args), node.body)
        self._in_function -= 1

    visit_AsyncFunctionDef = visit_FunctionDef

    def visit_ClassDef(self, node):
        self._bind(node.name, node.lineno)
        self.generic_visit(node)

    def visit_Lambda(self, node):
        self._visit_scope(node, self._params(node.args), [node.body])

    def _visit_comprehension(self, node, elements):
        targets = [n.id for gen in node.generators for n in ast.walk(gen.target)
                   if isinstance(n, ast.Name)]
        self.visit(node.generators[0].iter)
        self._locals.append(set(targets))
        for gen in node.generators:
            for cond in gen.ifs:
                self.visit(cond)
        for gen in node.generators[1:]:
            self.visit(gen.iter)
        for element in elements:
            self.visit(element)
        self._locals.pop()

    def visit_ListComp(self, node):
        self._visit_comprehension(node, [node.elt])

    visit_SetComp = visit_GeneratorExp = visit_ListComp

    def visit_DictComp(self, node):
        self._visit_comprehension(node, [node.key, node.value])

    def visit_Import(self, node):
        for alias in node.names:
            self._bind((alias.asname or alias.name).split(".")[0], node.lineno)

    visit_ImportFrom = visit_Import

    def visit_ExceptHandler(self, node):
        if node.name:
            self._bind(node.name, node.lineno)
        self.generic_visit(node)

    def visit_Name(self, node):
        if isinstance(node.ctx, ast.Store):
            self._bind(node.id, node.lineno)
        elif isinstance(node.ctx, ast.Load):
            self._read(node.id, node.lineno)


_JS_DEFINITION = re.compile(r"^\s*(?:(?:var|let|const)\s+(\w+)|function\s+(\w+)|(\w+)\s*=[^=])")
_IDENTIFIER = re.compile(r"[A-Za-z_]\w*")


def _chunk_names(chunk: Chunk, python: bool) -> Tuple[Set[str], Set[str]]:
    """Returns the (defined, used) global names of a chunk.

    A name the chunk binds before reading it isn't a use: its value doesn't
    come from an earlier chunk.
    """
    code = "".join(chunk.code)
    if python:
        try:
            collector = _NameCollector()
            collector.visit(ast.parse(code))
            used = {name for name, line in collector.used.items()
                    if collector.defined.get(name, line) >= line}
            used.update(collector.deferred_used - set(collector.defined))
            return set(collector.defined), used
        except SyntaxError:
            pass

    # Rough fallback for JS and Python that doesn't parse on its own
    defined, used = set(), set()
    for line in chunk.code:
        stripped = line.split("#", 1)[0] if python else line.split("//", 1)[0]
        m = _JS_DEFINITION.match(stripped)
        name = next((g for g in m.groups() if g), None) if m else None
        if name:
            defined.add(name)
        used.update(n for n in _IDENTIFIER.findall(stripped) if n not in defined)
    return defined, used


def _chunk_tag(chunk_id: str) -> str:
    """The prefix naming what a chunk belongs to: "BUG#123" / "WL456" when it
    starts with one, otherwise the text before " - ", otherwise the id minus
    any setup/cleanup word."""
    m = re.match(r"(BUG#\d+|WL\d+)", chunk_id)
    if m:
        return m.group(1)
    if " - " in chunk_id:
        return chunk_id.split(" - ", 1)[0].strip()
    return re.sub(r"\b(setup|cleanup)\b", "", chunk_id, flags=re.I).strip()


@dataclass
class Dependency:
    """Chunk `last` must run in the same group as chunk `first` (indices
    into the script's chunk list), and so must every chunk between them."""
    first: int
    last: int
    reason: str


def find_dependencies(chunks: List[Chunk], python: bool) -> List[Dependency]:
    deps: List[Dependency] = []
    grouped = [i for i, c in enumerate(chunks) if c.grouped]

    # Variables and functions defined in a grouped chunk only exist in the
    # group that runs it
    last_definer: Dict[str, int] = {}
    for j, chunk in enumerate(chunks):
        defined, used = _chunk_names(chunk, python)
        if chunk.grouped:
            for name in sorted(used):
                i = last_definer.get(name)
                if i is not None and chunks[i].grouped:
                    deps.append(Dependency(i, j, f"uses '{name}'"))
        for name in defined:
            last_definer[name] = j

    for pos, i in enumerate(grouped):
        chunk = chunks[i]
        tag = _chunk_tag(chunk.chunk_id)

        # setup ... cleanup sequences, up to the matching cleanup or else the
        # last chunk of the run sharing the setup's tag
        if re.search(r"\bsetup\b", chunk.chunk_id, re.I):
            end = None
            for j in grouped[pos + 1:]:
                if (_chunk_tag(chunks[j].chunk_id) == tag
                        and re.search(r"\bcleanup\b", chunks[j].chunk_id, re.I)):
                    end = j
                    break
            if end is None:
                for j in grouped[pos + 1:]:
                    if _chunk_tag(chunks[j].chunk_id) != tag:
                        break
                    end = j
            if end is not None:
                deps.append(Dependency(i, end, f"{tag} setup..cleanup"))

        # Consecutive chunks of the same bug
        if pos > 0 and tag.startswith("BUG#"):
            prev = grouped[pos - 1]
            if _chunk_tag(chunks[prev].chunk_id) == tag:
                deps.append(Dependency(prev, i, f"same bug {tag}"))

        if pos > 0 and chunk.has_marker(KEEP_WITH_PREVIOUS_MARKER):
            deps.append(Dependency(grouped[pos - 1], i, "keep-with-previous marker"))

        if chunk.has_marker(BEGIN_BLOCK_MARKER):
            end = next((j for j in grouped[pos:] if chunks[j].has_marker(END_BLOCK_MARKER)), None)
            if end is None:
                raise ValueError(f"'{BEGIN_BLOCK_MARKER}' in chunk {chunk.chunk_id!r} "
                                 f"has no matching '{END_BLOCK_MARKER}'")
            deps.append(Dependency(i, end, "begin-block..end-block markers"))

    return deps


# ----------------------------------------------------------------------------
# Partitioning
# ----------------------------------------------------------------------------

def linear_partition(costs: List[float], parts: int) -> List[int]:
    """Splits `costs` into `parts` contiguous, non-empty ranges minimizing the
    largest range sum, and among those the sum of squared range sums (so the
    other ranges come out even too). Returns each range's start index."""
    n = len(costs)
    prefix = [0.0]
    for c in costs:
        prefix.append(prefix[-1] + c)

    def span(a, b):
        return prefix[b] - prefix[a]

    inf = float("inf")
    # best_max[k][i]: min over splits of costs[:i] into k ranges of the max sum
    best_max = [[inf] * (n + 1) for _ in range(parts + 1)]
    best_max[0][0] = 0.0
    for k in range(1, parts + 1):
        for i in range(k, n + 1):
            best_max[k][i] = min(max(best_max[k - 1][j], span(j, i)) for j in range(k - 1, i))
    bound = best_max[parts][n] + 1e-6

    # Second pass: min sum of squares, keeping every range within the bound
    sq = [[inf] * (n + 1) for _ in range(parts + 1)]
    choice = [[0] * (n + 1) for _ in range(parts + 1)]
    sq[0][0] = 0.0
    for k in range(1, parts + 1):
        for i in range(k, n + 1):
            for j in range(k - 1, i):
                s = span(j, i)
                if s <= bound and sq[k - 1][j] + s * s < sq[k][i]:
                    sq[k][i] = sq[k - 1][j] + s * s
                    choice[k][i] = j

    starts, i = [], n
    for k in range(parts, 0, -1):
        i = choice[k][i]
        starts.append(i)
    return starts[::-1]


# ----------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------

def _format_s(ms: float) -> str:
    return f"{ms / 1000:7.1f}s"


_DEFAULT_COMMON = ("entry point", "Setup", "Cleanup")


def init_groups(script: Path, token: str, common: List[str]) -> int:
    """Tags every chunk except the common ones with a "(1)" group suffix."""
    lines = script.read_text(encoding="utf-8").splitlines(keepends=True)
    chunks = parse_chunks(lines, token)
    if any(c.grouped for c in chunks):
        print(f"Error: {script} already has grouped chunks.", file=sys.stderr)
        return 1
    common_ids = set(_DEFAULT_COMMON) | set(common)
    left = []
    for chunk in chunks:
        if chunk.chunk_id in common_ids or chunk.chunk_id.startswith("INCLUDE "):
            left.append(chunk.chunk_id)
            continue
        lines[chunk.header_index] = lines[chunk.header_index].rstrip("\n") + " (1)\n"
    script.write_text("".join(lines), encoding="utf-8")
    print(f"Tagged {len(chunks) - len(left)} of {len(chunks)} chunks with (1); "
          f"left common: {', '.join(left)}")
    return 0


def merge_files(files: List[str], skip: List[str], times_file: Path) -> int:
    samples = [t for f in files for t in read_chunk_timings(Path(f))
               if not any(t.chunk_id.startswith(prefix) for prefix in skip)]
    if not samples:
        print("Error: nothing to merge.", file=sys.stderr)
        return 1
    merge_chunk_times(times_file, samples)
    print(f"Merged {len(samples)} chunk timings into {times_file}.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Balances the (N) chunk groups of a split scripted test.")
    parser.add_argument("script", nargs="?", help="Test script to balance.")
    parser.add_argument("groups", type=int, nargs="?",
                        help="Number of groups (N in _SPLIT_TEST_GROUPS).")
    parser.add_argument("--init", action="store_true",
                        help="Tag every non-common chunk of an unsplit script with (1).")
    parser.add_argument("--common", action="append", default=[], metavar="ID",
                        help="With --init: another chunk id to leave ungrouped.")
    parser.add_argument("--merge", nargs="+", metavar="FILE",
                        help="Merge these chunk timing files into the timing file.")
    parser.add_argument("--skip", action="append", default=[], metavar="PREFIX",
                        help="With --merge: skip the chunks whose id starts with this.")
    parser.add_argument("--times", default=str(DEFAULT_TIMES_FILE),
                        help=f"Chunk timing file. Default: {DEFAULT_TIMES_FILE}")
    parser.add_argument("--write", action="store_true",
                        help="Rewrite the script's group suffixes in place; "
                             "otherwise the change is printed as a diff.")
    parser.add_argument("--explain", action="store_true",
                        help="List every dependency found between chunks.")
    parser.add_argument("--estimate", action="store_true",
                        help="Use the chunks' line counts instead of their timings.")
    args = parser.parse_args()

    if args.merge:
        return merge_files(args.merge, args.skip, Path(args.times).expanduser())
    if not args.script:
        parser.error("a script is required")

    script = Path(args.script)
    token = _CHUNK_TOKENS.get(script.suffix)
    if not token:
        print(f"Error: unsupported script type '{script.suffix}'.", file=sys.stderr)
        return 1

    if args.init:
        return init_groups(script, token, args.common)
    if args.groups is None:
        parser.error("the number of groups is required")

    lines = script.read_text(encoding="utf-8").splitlines(keepends=True)
    chunks = parse_chunks(lines, token)
    grouped = [i for i, c in enumerate(chunks) if c.grouped]
    if len(grouped) < args.groups:
        print(f"Error: only {len(grouped)} grouped chunks, can't make {args.groups} groups.",
              file=sys.stderr)
        return 1

    if args.estimate:
        times = {(script.name, c.chunk_id): ChunkTiming(
                     script.name, 0, 1000.0 * max(1, sum(1 for l in c.code if l.strip())), c.chunk_id)
                 for c in chunks}
    else:
        times = load_chunk_times(Path(args.times).expanduser())
    cost = []
    untimed = []
    for chunk in chunks:
        timing = times.get((script.name, chunk.chunk_id))
        cost.append(timing.duration_ms if timing else 0.0)
        if not timing:
            untimed.append(chunk)
    if len(untimed) == len(chunks):
        print(f"Error: no timings for '{script.name}' in {args.times}.", file=sys.stderr)
        return 1
    shared_ms = sum(cost[i] for i, c in enumerate(chunks) if not c.grouped)

    # A cut before grouped position p (between grouped[p-1] and grouped[p]) is
    # forbidden when a dependency spans it
    position = {chunk_index: p for p, chunk_index in enumerate(grouped)}
    deps = find_dependencies(chunks, script.suffix == ".py")
    forbidden: Dict[int, Dependency] = {}
    for dep in deps:
        for p in range(position[dep.first] + 1, position[dep.last] + 1):
            forbidden.setdefault(p, dep)
    for p, i in enumerate(grouped):
        if chunks[i].has_marker(ALLOW_CUT_BEFORE_MARKER):
            forbidden.pop(p, None)

    # Blocks: runs of grouped chunks that can't be cut apart
    block_starts = [p for p in range(len(grouped)) if p == 0 or p not in forbidden]
    if len(block_starts) < args.groups:
        print(f"Error: dependencies leave only {len(block_starts)} separable blocks, "
              f"can't make {args.groups} groups.", file=sys.stderr)
        return 1
    block_ends = block_starts[1:] + [len(grouped)]
    block_costs = [sum(cost[grouped[p]] for p in range(s, e))
                   for s, e in zip(block_starts, block_ends)]

    group_starts = [block_starts[b] for b in linear_partition(block_costs, args.groups)]
    group_ends = group_starts[1:] + [len(grouped)]

    if args.explain:
        print("Dependencies:")
        for dep in deps:
            print(f"  {chunks[dep.first].chunk_id[:60]!r} .. {chunks[dep.last].chunk_id[:60]!r}: "
                  f"{dep.reason}")
        print()

    # Current assignment, for comparison
    current: Dict[int, float] = {}
    for i in grouped:
        current[chunks[i].group] = current.get(chunks[i].group, 0.0) + cost[i]

    print(f"{script.name}: {len(chunks)} chunks, {len(grouped)} grouped, "
          f"{len(block_starts)} separable blocks; shared chunks cost {_format_s(shared_ms)} "
          f"in every group.")
    if untimed:
        print(f"{len(untimed)} chunk(s) have no timing and count as 0 "
              f"(skipped by their context, or never run).")
    print()
    print(f"{'group':>5}  {'chunks':>6}  {'current':>8}  {'balanced':>8}  first chunk")
    new_group: Dict[int, int] = {}
    for g, (s, e) in enumerate(zip(group_starts, group_ends), start=1):
        for p in range(s, e):
            new_group[grouped[p]] = g
        group_ms = sum(cost[grouped[p]] for p in range(s, e))
        cur = current.get(g)
        print(f"{g:>5}  {e - s:>6}  {_format_s(shared_ms + cur) if cur is not None else '       -'}"
              f"  {_format_s(shared_ms + group_ms)}  {chunks[grouped[s]].chunk_id[:70]}")
    if 0 in current:
        print(f"Chunks with an empty '()' group run in every group today, costing "
              f"{_format_s(current[0])} in each; they're given a group now.")

    # The cuts: worth a look for dependencies the heuristics can't see
    print("\nCuts (check the chunk after each one doesn't need the one before it):")
    for g, s in enumerate(group_starts[1:], start=2):
        print(f"  {g - 1}|{g}: {chunks[grouped[s - 1]].chunk_id[:60]!r}")
        print(f"       {chunks[grouped[s]].chunk_id[:60]!r}")

    new_lines = list(lines)
    for i, g in new_group.items():
        header = new_lines[chunks[i].header_index]
        new_lines[chunks[i].header_index] = _GROUP_SUFFIX.sub(f"({g})", header.rstrip("\n")) + "\n"

    if new_lines == lines:
        print("\nThe script already has this assignment.")
    elif args.write:
        script.write_text("".join(new_lines), encoding="utf-8")
        print(f"\nRewrote the group suffixes in {script}.")
    else:
        print()
        sys.stdout.writelines(difflib.unified_diff(lines, new_lines, str(script), str(script), n=0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
