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

"""Renders an HTML chart from a test execution plan and timing file.

Draws one horizontal stacked bar per worker (from run_unit_tests.py's
--list-groups / test-execution-plan.txt), sized by the summed execution time
of the suites assigned to it. Each bar segment is one suite, so a single large
segment stands out immediately: it's a suite that behaves like an indivisible
block of work and is a good candidate to split into per-test tasks (see
run_unit_tests.py's -s/--split-suites) so the LPT scheduler can spread it
across workers instead of loading a single one.

Usage:
    python3 plot_execution_plan.py \\
        --execution-plan-file test-execution-plan.txt \\
        --timing-file test-execution-times.txt \\
        -o execution-plan.html
"""

import argparse
import html
import re
import sys
import webbrowser
import zlib
from pathlib import Path
from typing import Dict, List, NamedTuple, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_unit_tests import TestTaskFactory  # noqa: E402  (path tweak above)

_PLAN_LINE_RE = re.compile(r"^worker(\d+):\s*(.*)$")

_PALETTE = [
    "#4e79a7", "#f28e2b", "#e15759", "#76b7b2", "#59a14f",
    "#edc948", "#b07aa1", "#ff9da7", "#9c755f", "#bab0ac",
]
_UNKNOWN_COLOR = "#d9d9d9"

_BAR_HEIGHT = 32
_BAR_GAP = 18
_CHART_WIDTH = 900
_LABEL_WIDTH = 190
_RIGHT_MARGIN = 130
_TOP_MARGIN = 40
_BOTTOM_MARGIN = 20
_FONT_SIZE = 12


class Segment(NamedTuple):
    """One suite's contribution to a worker's bar."""
    suite: str
    duration_ms: float
    known: bool  # False if the suite had no entry in the timing file


def load_plan(filepath: Path) -> Dict[int, List[str]]:
    """Parses a test-execution-plan.txt file into {worker_id: [suite, ...]}."""
    plan: Dict[int, List[str]] = {}
    with open(filepath, "r", encoding="utf-8") as f:
        for lineno, raw_line in enumerate(f, start=1):
            line = raw_line.strip()
            if not line:
                continue
            match = _PLAN_LINE_RE.match(line)
            if not match:
                raise ValueError(f"{filepath}:{lineno}: could not parse line: {raw_line!r}")
            worker_id = int(match.group(1))
            suites_part = match.group(2).strip()
            suites = [s.strip() for s in suites_part.split(",") if s.strip()]
            plan[worker_id] = suites
    return plan


def build_worker_segments(
    plan: Dict[int, List[str]],
    times: Dict[str, float]
) -> Dict[int, List[Segment]]:
    """Builds each worker's segment list, sorted with the largest suite first."""
    worker_segments: Dict[int, List[Segment]] = {}
    for worker_id, suites in plan.items():
        segments = [
            Segment(suite, times.get(suite, 0.0), suite in times)
            for suite in suites
        ]
        segments.sort(key=lambda s: s.duration_ms, reverse=True)
        worker_segments[worker_id] = segments
    return worker_segments


def _suite_color(suite: str) -> str:
    """Deterministic (across runs) color pick, independent of PYTHONHASHSEED."""
    idx = zlib.crc32(suite.encode("utf-8")) % len(_PALETTE)
    return _PALETTE[idx]


def _estimated_text_width(text: str) -> float:
    return len(text) * (_FONT_SIZE * 0.6)


def render_chart_svg(
    worker_segments: Dict[int, List[Segment]],
    split_share_threshold: float
) -> Tuple[str, List[Tuple[int, Segment, float]]]:
    """Renders the stacked bar chart as an SVG fragment.

    Returns:
        A (svg_markup, flagged) tuple, where flagged lists (worker_id, segment,
        share) for every segment whose share of its worker's total time is at
        or above split_share_threshold.
    """
    worker_ids = sorted(worker_segments)
    totals = {w: sum(s.duration_ms for s in worker_segments[w]) for w in worker_ids}
    max_total = max(totals.values(), default=0.0) or 1.0
    scale = _CHART_WIDTH / max_total

    chart_height = _TOP_MARGIN + len(worker_ids) * (_BAR_HEIGHT + _BAR_GAP) + _BOTTOM_MARGIN
    svg_width = _LABEL_WIDTH + _CHART_WIDTH + _RIGHT_MARGIN

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{svg_width}" '
        f'height="{chart_height}" font-family="system-ui, sans-serif" '
        f'font-size="{_FONT_SIZE}">'
    ]

    # Vertical gridlines with a time axis, every quarter of the widest bar.
    for i in range(5):
        gx = _LABEL_WIDTH + _CHART_WIDTH * i / 4
        gt = max_total * i / 4
        parts.append(
            f'<line x1="{gx:.1f}" y1="{_TOP_MARGIN - 8}" x2="{gx:.1f}" '
            f'y2="{chart_height - _BOTTOM_MARGIN}" stroke="currentColor" '
            f'stroke-opacity="0.15"/>'
        )
        parts.append(
            f'<text x="{gx:.1f}" y="{_TOP_MARGIN - 14}" text-anchor="middle" '
            f'opacity="0.6">{gt / 1000.0:.1f}s</text>'
        )

    flagged: List[Tuple[int, Segment, float]] = []

    for row, worker_id in enumerate(worker_ids):
        segments = worker_segments[worker_id]
        total = totals[worker_id]
        y = _TOP_MARGIN + row * (_BAR_HEIGHT + _BAR_GAP)

        parts.append(
            f'<text x="{_LABEL_WIDTH - 12}" y="{y + _BAR_HEIGHT / 2 + 4:.1f}" '
            f'text-anchor="end">worker{worker_id} '
            f'<tspan opacity="0.6">({len(segments)} suites)</tspan></text>'
        )

        x = _LABEL_WIDTH
        for segment in segments:
            width = max(segment.duration_ms * scale, 1.5)
            color = _UNKNOWN_COLOR if not segment.known else _suite_color(segment.suite)
            share = (segment.duration_ms / total) if total > 0 else 0.0
            is_split_candidate = segment.known and share >= split_share_threshold
            if is_split_candidate:
                flagged.append((worker_id, segment, share))

            stroke = ' stroke="#c0392b" stroke-width="2"' if is_split_candidate else ' stroke="white" stroke-width="1"'
            title = html.escape(
                f"{segment.suite}: {segment.duration_ms:,.1f} ms "
                f"({share * 100:.1f}% of worker{worker_id})"
                + ("" if segment.known else " [no timing data yet]")
            )
            parts.append(
                f'<rect x="{x:.1f}" y="{y}" width="{width:.1f}" height="{_BAR_HEIGHT}" '
                f'fill="{color}"{stroke}><title>{title}</title></rect>'
            )

            label = segment.suite if segment.known else f"{segment.suite} (?)"
            if _estimated_text_width(label) < width - 6:
                parts.append(
                    f'<text x="{x + width / 2:.1f}" y="{y + _BAR_HEIGHT / 2 + 4:.1f}" '
                    f'text-anchor="middle" fill="white">'
                    f'{html.escape(label)}</text>'
                )
            x += width

        parts.append(
            f'<text x="{x + 8:.1f}" y="{y + _BAR_HEIGHT / 2 + 4:.1f}" opacity="0.8">'
            f'{total / 1000.0:,.1f}s</text>'
        )

    parts.append("</svg>")
    return "\n".join(parts), flagged


def render_flagged_table(flagged: List[Tuple[int, Segment, float]], split_share_threshold: float) -> str:
    """Renders the HTML table listing every suite worth splitting."""
    if not flagged:
        return (
            f'<p>No suite takes up {split_share_threshold * 100:.0f}% or more of its '
            f"worker's total time — nothing obviously worth splitting.</p>"
        )

    flagged = sorted(flagged, key=lambda f: f[1].duration_ms, reverse=True)
    rows = []
    for worker_id, segment, share in flagged:
        rows.append(
            "<tr>"
            f"<td>{html.escape(segment.suite)}</td>"
            f"<td>worker{worker_id}</td>"
            f"<td>{segment.duration_ms:,.1f}</td>"
            f"<td>{share * 100:.1f}%</td>"
            "</tr>"
        )

    return (
        "<table>"
        "<thead><tr><th>Suite</th><th>Worker</th><th>Duration (ms)</th>"
        "<th>Share of worker's total</th></tr></thead>"
        f"<tbody>{''.join(rows)}</tbody>"
        "</table>"
    )


def render_html(
    worker_segments: Dict[int, List[Segment]],
    split_share_threshold: float,
    plan_path: Path,
    timing_path: Path
) -> str:
    chart_svg, flagged = render_chart_svg(worker_segments, split_share_threshold)
    table_html = render_flagged_table(flagged, split_share_threshold)

    return f"""<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>Test execution plan</title>
<style>
  body {{ margin: 24px; color: #1a1a1a; }}
  h1 {{ font-size: 18px; }}
  p.source {{ opacity: 0.6; font-size: 12px; }}
  table {{ border-collapse: collapse; margin-top: 12px; }}
  th, td {{ padding: 4px 12px; text-align: left; border-bottom: 1px solid #ddd; }}
  th {{ opacity: 0.7; font-weight: 600; }}
</style>
</head>
<body>
<h1>Per-worker execution time, by suite</h1>
<p class="source">plan: {html.escape(str(plan_path))} &mdash; timing: {html.escape(str(timing_path))}</p>
{chart_svg}
<h1>Suites recommended for splitting
  (≥ {split_share_threshold * 100:.0f}% of their worker's total time)</h1>
{table_html}
</body>
</html>
"""


def parse_args():
    parser = argparse.ArgumentParser(
        description="Render an HTML chart of the per-worker test execution plan, "
                     "highlighting suites that deserve to be split."
    )
    parser.add_argument(
        "-p", "--execution-plan-file",
        default="test-execution-plan.txt", type=str,
        help="Path to the execution plan file. Default: 'test-execution-plan.txt'."
    )
    parser.add_argument(
        "-t", "--timing-file",
        default="test-execution-times.txt", type=str,
        help="Path to the execution timing file. Default: 'test-execution-times.txt'."
    )
    parser.add_argument(
        "-o", "--output", default="test-execution-plan.html", type=str,
        help="Path to write the HTML chart to. Default: 'test-execution-plan.html'."
    )
    parser.add_argument(
        "--split-threshold", default=0.30, type=float,
        help="Flag a suite as a split candidate once it takes up at least this "
             "fraction of its worker's total time. Default: 0.30."
    )
    parser.add_argument(
        "--open", action="store_true",
        help="Open the generated HTML file in the default browser."
    )
    return parser.parse_args()


def main():
    args = parse_args()

    plan_path = Path(args.execution_plan_file)
    timing_path = Path(args.timing_file)

    if not plan_path.exists():
        print(f"Error: plan file '{plan_path}' does not exist.", file=sys.stderr)
        sys.exit(1)

    plan = load_plan(plan_path)
    times = TestTaskFactory.load_execution_times(timing_path)
    if not times:
        print(f"Warning: '{timing_path}' has no timing data; all bars will be "
              f"drawn as unknown-duration placeholders.", file=sys.stderr)

    worker_segments = build_worker_segments(plan, times)
    output_html = render_html(worker_segments, args.split_threshold, plan_path, timing_path)

    output_path = Path(args.output)
    output_path.write_text(output_html, encoding="utf-8")
    print(f"Chart written to '{output_path}'.")

    if args.open:
        webbrowser.open(output_path.resolve().as_uri())


if __name__ == "__main__":
    main()
