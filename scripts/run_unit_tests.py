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

"""Parallel Google Test Orchestrator.

This module parses Google Test outputs, balances workload across N worker queues
based on historical execution durations, and runs tests in parallel, isolated
inside Firejail sandboxes when Firejail is available (Linux only).
"""

import argparse
import html
import shutil
import socket
import subprocess
import time
import os
import sys
from pathlib import Path
from dataclasses import dataclass
from typing import List, Dict, Optional, Tuple
from concurrent.futures import ThreadPoolExecutor

_ANSI_GREEN = "\033[32m"
_ANSI_RED = "\033[31m"
_ANSI_RESET = "\033[0m"

# Firejail (Linux-only process isolation) is used when available, otherwise the
# test binary is invoked directly. Resolved once at import time to avoid a PATH
# lookup on every task.
_FIREJAIL_PATH = shutil.which("firejail")


@dataclass
class TestTask:
    """Represents a discrete unit of test execution work.

    Attributes:
        filter_spec: GTest filter expression (e.g., 'Suite.*' or 'Suite.Test').
        last_execution_time_ms: Measured execution duration from prior run.
    """
    filter_spec: str
    last_execution_time_ms: float = 0.0


@dataclass
class FailureRecord:
    """A failed task, kept for the end-of-run summary and HTML report.

    Attributes:
        worker_id: Id of the worker that ran the task.
        sequence: Position of the task within its worker's execution plan line.
        filter_spec: GTest filter expression of the failed task.
        log_path: Path to that task's mariadb-shell.log.
        output_path: Path to that task's captured stdout/stderr.
        duration_ms: Measured execution duration.
        sandbox_error_log_path: Path to the copy of the sandbox's error.log
            taken at failure time, or None if it couldn't be copied (or wasn't
            attempted, as when the sandbox itself never came up).
    """
    worker_id: int
    sequence: int
    filter_spec: str
    log_path: Path
    output_path: Path
    duration_ms: float
    sandbox_error_log_path: Optional[Path] = None


class TestTaskFactory:
    """Handles loading runtime statistics, parsing test lists, and instantiating tasks."""

    @staticmethod
    def load_execution_times(filepath: Path) -> Dict[str, float]:
        """Reads historical test execution times from a simple space-delimited text file.

        Args:
            filepath: Path to the timing file.

        Returns:
            Dict mapping filter expressions to their recorded duration in milliseconds.
        """
        times = {}
        if not filepath.exists():
            return times

        with open(filepath, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                # rsplit from the right: filter_spec itself may contain spaces
                # (some suite names do), but the trailing duration never does.
                parts = line.rsplit(maxsplit=1)
                if len(parts) >= 2:
                    filter_spec, duration = parts
                    times[filter_spec] = float(duration)
        return times

    @staticmethod
    def save_execution_times(filepath: Path, tasks: List[TestTask]) -> None:
        """Persists updated execution times for all processed tasks back to disk.

        Args:
            filepath: Path to the timing file.
            tasks: List of TestTasks containing updated timing metadata.
        """
        filepath.parent.mkdir(parents=True, exist_ok=True)
        with open(filepath, "w", encoding="utf-8") as f:
            for task in tasks:
                f.write(f"{task.filter_spec} {task.last_execution_time_ms:.2f}\n")

    @staticmethod
    def create_tasks(
        raw_gtest_output: str,
        split_suites: List[str],
        exec_times: Dict[str, float]
    ) -> List[TestTask]:
        """Parses '--gtest_list_tests' string output into granular TestTask units.

        Args:
            raw_gtest_output: Raw stdout from running the binary with --gtest_list_tests.
            split_suites: List of suite names that should be split into individual per-test tasks.
            exec_times: Map of historical runtimes used to seed task duration properties.

        Returns:
            List of initialized TestTask objects.
        """
        tasks: List[TestTask] = []
        current_suite = ""
        current_tests: List[str] = []

        def flush_suite():
            """Creates tasks for the accumulated suite based on split criteria."""
            nonlocal current_suite, current_tests
            if not current_suite or not current_tests:
                # No accumulated test names means this "suite" was just a line
                # of stdout noise (e.g. a startup banner) ending in a period,
                # not an actual GTest suite header.
                return

            if current_suite in split_suites:
                # Create individual tasks for every test inside specified split suites
                for test_name in current_tests:
                    filter_spec = f"{current_suite}.{test_name}"
                    duration = exec_times.get(filter_spec, 0.0)
                    tasks.append(TestTask(filter_spec=filter_spec, last_execution_time_ms=duration))
            else:
                # Create a single suite-level wildcard task by default
                filter_spec = f"{current_suite}.*"
                duration = exec_times.get(filter_spec, 0.0)
                tasks.append(TestTask(filter_spec=filter_spec, last_execution_time_ms=duration))

            current_tests = []

        # Parse indented hierarchy from GTest standard output
        for line in raw_gtest_output.splitlines():
            if not line.strip():
                continue

            if line.endswith("."):
                # Header lines ending in a dot indicate a Test Suite name
                flush_suite()
                current_suite = line.strip()[:-1]
            elif line[:1].isspace():
                # Indented lines under a header represent individual Test names.
                # Non-indented lines are ignored: the binary under test may write
                # its own startup/diagnostic noise to stdout alongside the actual
                # '--gtest_list_tests' listing (e.g. "Session replay not enabled."),
                # and such noise must not be mistaken for a test name.
                test_name = line.strip()
                # Strip out type or value parameter comments (e.g., "# TypeParam = ...")
                if "#" in test_name:
                    test_name = test_name.split("#")[0].strip()
                if test_name:
                    current_tests.append(test_name)

        flush_suite()
        return tasks


class TestWorker:
    """Wrapper that executes individual GTest tasks, isolated via Firejail when available."""

    OUTPUT_FILE_NAME = "test-output.log"

    @staticmethod
    def execute_task(binary_path: str, task: TestTask, env: Optional[Dict[str, str]] = None) -> Tuple[float, bool]:
        """Invokes the binary and measures elapsed execution time.

        Wrapped with Firejail for process isolation when it's on the PATH
        (Linux only); otherwise the binary is invoked directly, so this also
        works on Linux hosts without Firejail installed, as well as other
        platforms such as macOS.

        The process's combined stdout/stderr is captured and saved as
        'test-output.log' under env['MARIADB_SHELL_USER_CONFIG_HOME'] (the same
        folder the mariadb-shell.log for this task lands in), so a failure can
        be diagnosed from the test's own output as well as the shell's log.

        The process is also run with its cwd set to that same folder. Some
        GTest suites (e.g. ShellRunScript/ShellExeRunScript) create scratch
        fixture files using relative paths (good.py, bad.py, ...); without a
        per-task cwd, every task across every worker shares the parent
        process's working directory, so two suites using the same fixture
        names can race (one's TearDownTestCase/SetUpTestCase deleting or
        recreating files while another is mid-test), causing intermittent
        "No such file or directory" failures.

        Args:
            binary_path: Path to the target GTest executable.
            task: The TestTask to execute.
            env: Environment variables to pass to the test process. Defaults to
                the current process environment.

        Returns:
            A (elapsed_time_ms, success) tuple.
        """
        gtest_arg = f"--gtest_filter={task.filter_spec}"
        if _FIREJAIL_PATH:
            cmd = [_FIREJAIL_PATH, "--quiet", "--noprofile", "--deterministic-exit-code", binary_path, gtest_arg]
        else:
            cmd = [binary_path, gtest_arg]

        config_home = (env or {}).get("MARIADB_SHELL_USER_CONFIG_HOME")

        success = True
        stdout_data = b""
        stderr_data = b""
        start_time = time.perf_counter()
        try:
            result = subprocess.run(cmd, check=True, capture_output=True, env=env, cwd=config_home)
            stdout_data, stderr_data = result.stdout, result.stderr
        except subprocess.CalledProcessError as e:
            success = False
            stdout_data, stderr_data = e.stdout, e.stderr
            print(f"[FAIL] Task failed: {task.filter_spec}\nError: {e.stderr.decode(errors='replace')}",
                  file=sys.stderr)
        finally:
            end_time = time.perf_counter()

        if config_home:
            output_path = Path(config_home) / TestWorker.OUTPUT_FILE_NAME
            try:
                with open(output_path, "wb") as f:
                    f.write(stdout_data)
                    if stderr_data:
                        f.write(b"\n--- stderr ---\n")
                        f.write(stderr_data)
            except OSError as e:
                print(f"[WARN] Could not write '{output_path}': {e}", file=sys.stderr)

        elapsed_ms = (end_time - start_time) * 1000.0
        task.last_execution_time_ms = elapsed_ms
        return elapsed_ms, success


class SandboxManager:
    """Deploys and tears down the per-worker MariaDB sandbox used as the test target server."""

    def __init__(self, shell_binary: str, env: Optional[Dict[str, str]] = None):
        """Initializes the manager with the mariadb-shell binary used to drive the sandbox plugin.

        Args:
            shell_binary: Path to the mariadb-shell executable.
            env: Environment variables passed to every 'mariadb-shell' invocation.
                Defaults to the current process environment.
        """
        self.shell_binary = shell_binary
        self.env = env

    @staticmethod
    def find_free_ports(count: int) -> List[int]:
        """Returns `count` currently unused TCP ports on localhost.

        The sockets are all kept bound simultaneously until every one of them
        has been claimed, so the same port can't be handed back twice (which
        binding and releasing them one at a time could otherwise race into).
        """
        sockets = []
        try:
            for _ in range(count):
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                s.bind(("", 0))
                sockets.append(s)
            return [s.getsockname()[1] for s in sockets]
        finally:
            for s in sockets:
                s.close()

    def _run_cli(self, *args: str) -> None:
        """Invokes 'mariadb-shell -- <args>' and raises on failure."""
        cmd = [self.shell_binary, "--", *args]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=self.env)

    def deploy(self, port: int) -> None:
        """Deploys a base sandbox server instance listening on the given port."""
        self._run_cli("sandbox", "deploy", str(port), "--password=")

    def start(self, port: int) -> None:
        """(Re)starts a previously deployed, currently stopped sandbox instance."""
        self._run_cli("sandbox", "start", str(port))

    @staticmethod
    def is_reachable(port: int, timeout: float = 2.0) -> bool:
        """Returns whether a TCP connection to localhost:port can be established.

        Used as a health check between suites: the sandbox process may have
        died (crash, OOM kill, ...) without this script's own teardown ever
        running, in which case the port simply refuses connections.
        """
        try:
            with socket.create_connection(("localhost", port), timeout=timeout):
                return True
        except OSError:
            return False

    def clear_error_log(self, port: int) -> None:
        """Deletes the sandbox's error.log, if any, so a restart starts with a
        clean one instead of appending to the log from before it went down.
        """
        try:
            error_log = self.get_path(port, "error")
            if error_log.exists():
                error_log.unlink()
        except (subprocess.CalledProcessError, OSError) as e:
            print(f"[WARN] Could not clear sandbox error log for port {port}: {e}", file=sys.stderr)

    def get_path(self, port: int, path_id: str = None) -> Path:
        """Returns the path to a file inside the sandbox's data directory.

        Args:
            port: Port number of the sandbox instance.
            path_id: Identifier for the path to retrieve.

        Returns:
            Path to the requested file.
        """
        args = ["sandbox", "get-path", str(port)]
        if path_id is not None:
            args.append(path_id)
        result = subprocess.run(
            [self.shell_binary, "--", *args],
            check=True,
            capture_output=True,
            text=True,
            env=self.env
        )
        return Path(result.stdout.strip())

    def copy_error_log(self, port: int, dest: Path) -> Optional[Path]:
        """Copies the sandbox's error.log to 'dest', returning the copy's path.

        Returns None (and reports a warning) instead of raising if the log
        can't be located or copied, since this is only ever best-effort
        diagnostics collected after a test has already failed.
        """
        try:
            src = self.get_path(port, "error")
            shutil.copyfile(src, dest)
            return dest
        except (subprocess.CalledProcessError, OSError) as e:
            print(f"[WARN] Could not copy sandbox error log for port {port}: {e}", file=sys.stderr)
            return None

    def teardown(self, port: int) -> None:
        """Stops and removes the sandbox instance on the given port.

        Both steps are attempted independently (and errors reported rather than
        raised) so a failure stopping the server does not leave its sandbox
        directory behind.
        """
        try:
            self._run_cli("sandbox", "stop", str(port))
        except subprocess.CalledProcessError as e:
            print(f"[WARN] Failed to stop sandbox on port {port}: {e.stderr.decode()}", file=sys.stderr)

        try:
            self._run_cli("sandbox", "delete", str(port))
        except subprocess.CalledProcessError as e:
            print(f"[WARN] Failed to delete sandbox on port {port}: {e.stderr.decode()}", file=sys.stderr)


class Orchestrator:
    """Manages task extraction, workload partitioning, and parallel worker execution."""

    # 1 main sandbox deployed by this script + up to 6 extra servers a test may
    # deploy itself via MYSQL_SANDBOX_PORT1..6 (unittest/test_utils/sandboxes.h
    # k_num_ports).
    NUM_SANDBOX_PORTS = 7

    def __init__(
        self,
        binary_path: str,
        shell_binary: str,
        timing_file: str,
        num_workers: int = 0,
        gtest_filter: str = None,
        execution_plan_file: str = "test-execution-plan.txt",
        logs_dir: str = "test-execution-logs",
        report_file: str = "test-execution-report.html"
    ):
        """Initializes the orchestrator with target binary configuration.

        Args:
            binary_path: Path to the GTest executable.
            shell_binary: Path to the mariadb-shell executable, used to deploy a
                per-worker sandbox server for the tests to run against.
            timing_file: Path to load/store timing metrics.
            num_workers: Worker thread count (defaults to system CPU count).
            gtest_filter: Optional GTest filter pattern passed during discovery and execution.
            execution_plan_file: Path to write the per-worker execution plan to.
            logs_dir: Directory to keep every worker/suite's mariadb-shell.log under.
                Not cleared between runs; same-numbered suite folders are overwritten.
            report_file: Path to write the HTML execution report to.
        """
        self.binary_path = binary_path
        self.shell_binary = shell_binary
        self.timing_file = Path(timing_file)
        self.num_workers = num_workers or (os.cpu_count() or 4)
        self.gtest_filter = gtest_filter
        self.execution_plan_file = Path(execution_plan_file)
        self.logs_dir = Path(logs_dir)
        self.report_file = Path(report_file)

    def _get_gtest_list(self) -> str:
        """Queries the test binary to list available tests, applying --gtest_filter if provided."""
        cmd = [self.binary_path, "--gtest_list_tests"]
        if self.gtest_filter:
            cmd.append(f"--gtest_filter={self.gtest_filter}")

        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        return result.stdout

    @staticmethod
    def _report_link(path: Optional[Path], report_dir: Path) -> str:
        """Renders an HTML link to path, relative to the report's own directory.

        Falls back to plain (non-linked) text if the file isn't there to link
        to, and to "n/a" if there's no path at all (e.g. the sandbox error log
        couldn't be copied).
        """
        if path is None:
            return "n/a"
        if not path.exists():
            return f"{html.escape(str(path))} (missing)"
        href = html.escape(os.path.relpath(path, start=report_dir))
        return f'<a href="{href}">{html.escape(str(path))}</a>'

    @staticmethod
    def _write_report(report_path: Path, total_tasks: int, failures: List[FailureRecord]) -> None:
        """Writes an HTML summary of the run, linking every failed suite to its
        mariadb-shell.log and captured test output.

        Args:
            report_path: File to write the HTML report to.
            total_tasks: Total number of tasks that were executed.
            failures: Failed tasks, in whatever order they were collected.
        """
        failures = sorted(failures, key=lambda f: (f.worker_id, f.sequence))

        if failures:
            rows = "\n".join(
                "<tr>"
                f"<td>worker{f.worker_id}</td>"
                f"<td>{html.escape(f.filter_spec)}</td>"
                f"<td>{f.duration_ms:.1f}</td>"
                f"<td>{Orchestrator._report_link(f.output_path, report_path.parent)}</td>"
                f"<td>{Orchestrator._report_link(f.log_path, report_path.parent)}</td>"
                f"<td>{Orchestrator._report_link(f.sandbox_error_log_path, report_path.parent)}</td>"
                "</tr>"
                for f in failures
            )
            body = (
                "<table>"
                "<thead><tr><th>Worker</th><th>Suite</th><th>Duration (ms)</th>"
                "<th>Test output</th><th>Shell log</th><th>Sandbox log</th></tr></thead>"
                f"<tbody>{rows}</tbody>"
                "</table>"
            )
        else:
            body = "<p>All suites passed.</p>"

        report_path.write_text(f"""<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>Test execution report</title>
<style>
  body {{ margin: 24px; font-family: system-ui, sans-serif; color: #1a1a1a; }}
  h1 {{ font-size: 18px; }}
  table {{ border-collapse: collapse; margin-top: 12px; }}
  th, td {{ padding: 4px 12px; text-align: left; border-bottom: 1px solid #ddd; }}
  th {{ opacity: 0.7; font-weight: 600; }}
</style>
</head>
<body>
<h1>{len(failures)} of {total_tasks} suite(s) failed</h1>
{body}
</body>
</html>
""", encoding="utf-8")

    @staticmethod
    def _write_execution_plan(plan_path: Path, queues: List[List[TestTask]]) -> None:
        """Writes the per-worker task assignment, one worker per line.

        Args:
            plan_path: File to write the plan to.
            queues: Per-worker task queues, indexed by worker id.
        """
        with open(plan_path, "w", encoding="utf-8") as f:
            for worker_id, queue in enumerate(queues):
                suites = ", ".join(task.filter_spec for task in queue)
                f.write(f"worker{worker_id}: {suites}\n")

    def _partition_tasks(self, tasks: List[TestTask]) -> List[List[TestTask]]:
        """Balances tasks across worker queues using Longest Processing Time First (LPT).

        1. Tasks with prior timing (>0ms) are sorted descending and assigned greedy
           to the queue with the current smallest total cumulative execution time.
        2. Untimed tasks (0ms) are distributed starting at the queue with the lowest total.
        """
        queues: List[List[TestTask]] = [[] for _ in range(self.num_workers)]
        queue_totals = [0.0] * self.num_workers

        timed_tasks = [t for t in tasks if t.last_execution_time_ms > 0.0]
        untimed_tasks = [t for t in tasks if t.last_execution_time_ms == 0.0]

        # 1. Greedy LPT distribution for known long-running tasks
        timed_tasks.sort(key=lambda x: x.last_execution_time_ms, reverse=True)
        for task in timed_tasks:
            min_q_idx = queue_totals.index(min(queue_totals))
            queues[min_q_idx].append(task)
            queue_totals[min_q_idx] += task.last_execution_time_ms

        # 2. Even round-robin style distribution for unprofiled tasks, starting at
        # the queue with the current lowest total. Untimed tasks don't add to
        # queue_totals, so a plain "smallest total" pick would keep resolving to
        # the same queue instead of rotating through all of them.
        if untimed_tasks:
            start_idx = queue_totals.index(min(queue_totals))
            for offset, task in enumerate(untimed_tasks):
                q_idx = (start_idx + offset) % self.num_workers
                queues[q_idx].append(task)

        return queues

    def run(self, split_suites: List[str] = None, list_groups: bool = False) -> None:
        """Executes the complete orchestration workflow.

        Args:
            split_suites: Suite names to split into per-test tasks.
            list_groups: If true, print the balanced per-worker task groups and
                return without deploying any sandbox or running any test.
        """
        split_suites = split_suites or []

        # Step 1: Obtain available test structure and historical timing metrics
        raw_list = self._get_gtest_list()
        exec_times = TestTaskFactory.load_execution_times(self.timing_file)

        # Step 2: Formulate test tasks
        all_tasks = TestTaskFactory.create_tasks(raw_list, split_suites, exec_times)
        if not all_tasks:
            print("No test tasks discovered matching the criteria.")
            return

        # Never spin up more workers than there are tasks to run.
        self.num_workers = min(self.num_workers, len(all_tasks))

        # Step 3: Partition tasks into balanced queues
        queues = self._partition_tasks(all_tasks)

        self._write_execution_plan(self.execution_plan_file, queues)
        print(f"Execution plan written to '{self.execution_plan_file}'.")

        if list_groups:
            for worker_id, queue in enumerate(queues):
                total_ms = sum(task.last_execution_time_ms for task in queue)
                print(f"Worker {worker_id}: {len(queue)} task(s), {total_ms:.1f} ms total (prior runs)")
                for task in queue:
                    print(f"  {task.filter_spec} ({task.last_execution_time_ms:.1f} ms)")
            return

        # Persistent, well-known logs folder (not cleared between runs). Each
        # worker gets its own subfolder, and each suite it runs gets a further
        # sequential subfolder (0, 1, 2, ...) matching its position in that
        # worker's line of the execution plan, set as MARIADB_SHELL_USER_CONFIG_HOME
        # for that suite's run so its mariadb-shell.log is addressable afterwards.
        # The worker-level folder itself is only used as the config home for that
        # worker's own sandbox deploy/stop/delete calls. Only failures are worth
        # keeping around, so a passing suite's folder is removed right away, and a
        # worker whose suites all passed has its own folder removed too.
        self.logs_dir.mkdir(parents=True, exist_ok=True)
        print(f"Execution logs will be kept under '{self.logs_dir}'.")

        # Worker thread task execution handler
        def worker_loop(worker_id: int, queue: List[TestTask], worker_ports: List[int]) -> List[FailureRecord]:
            failures: List[FailureRecord] = []
            if not queue:
                return failures

            worker_dir = self.logs_dir / f"worker{worker_id}"
            worker_dir.mkdir(parents=True, exist_ok=True)

            sandbox_env = os.environ.copy()
            sandbox_env["MARIADB_SHELL_USER_CONFIG_HOME"] = str(worker_dir.resolve())

            sandbox = SandboxManager(self.shell_binary, env=sandbox_env)
            # A test may deploy up to NUM_SANDBOX_PORTS-1 extra servers of its own
            # (unittest/test_utils/sandboxes.h k_num_ports), on top of the one main
            # sandbox this worker deploys below. worker_ports were reserved for
            # this worker alone in one shared batch before any worker started, so
            # no two workers can be handed the same port.
            port, *extra_ports = worker_ports
            sandbox_log_path = worker_dir / "mariadb-shell.log"

            print(f"[Worker {worker_id}] Deploying sandbox on port {port}...")
            try:
                sandbox.deploy(port)
            except subprocess.CalledProcessError as e:
                print(f"[Worker {worker_id}] {_ANSI_RED}FAILED{_ANSI_RESET} to deploy sandbox on port "
                      f"{port}: {e.stderr.decode()}", file=sys.stderr)
                # No server ever came up, so every task queued for this worker
                # would fail anyway: report them all as failed up front instead
                # of trying (and failing) each one, pointing at the base
                # sandbox's own log since that's the only diagnostic available.
                sandbox.teardown(port)
                return [
                    FailureRecord(worker_id, i, task.filter_spec, sandbox_log_path, sandbox_log_path, 0.0)
                    for i, task in enumerate(queue)
                ]

            try:
                for i, task in enumerate(queue):
                    if not sandbox.is_reachable(port):
                        print(f"[Worker {worker_id}] Sandbox on port {port} is not reachable; "
                              f"restarting before running {task.filter_spec}...", file=sys.stderr)
                        sandbox.clear_error_log(port)
                        try:
                            sandbox.start(port)
                        except subprocess.CalledProcessError as e:
                            print(f"[Worker {worker_id}] {_ANSI_RED}FAILED{_ANSI_RESET} to restart "
                                  f"sandbox on port {port}: {e.stderr.decode()}", file=sys.stderr)
                            # No server to run the remaining tasks against: report them
                            # all as failed instead of trying (and failing) each one,
                            # pointing at the base sandbox's own log since that's the
                            # only diagnostic available.
                            failures.extend(
                                FailureRecord(worker_id, j, t.filter_spec, sandbox_log_path,
                                              sandbox_log_path, 0.0)
                                for j, t in enumerate(queue[i:], start=i)
                            )
                            break

                    task_dir = worker_dir / str(i)
                    task_dir.mkdir(parents=True, exist_ok=True)
                    log_path = task_dir / "mariadb-shell.log"
                    output_path = task_dir / TestWorker.OUTPUT_FILE_NAME

                    env = os.environ.copy()
                    env["MARIADB_SHELL_USER_CONFIG_HOME"] = str(task_dir.resolve())
                    env["MYSQL_PORT"] = str(port)
                    env["MYSQL_SANDBOX_PORT0"] = str(port)
                    for j, extra_port in enumerate(extra_ports, start=1):
                        env[f"MYSQL_SANDBOX_PORT{j}"] = str(extra_port)

                    print(f"[Worker {worker_id}] Starting: {task.filter_spec} (log: {task_dir})")
                    duration, success = TestWorker.execute_task(self.binary_path, task, env)
                    status = (f"{_ANSI_GREEN}OK{_ANSI_RESET}" if success
                              else f"{_ANSI_RED}FAILED{_ANSI_RESET}")
                    print(f"[Worker {worker_id}] {status}: {task.filter_spec} ({duration:.1f} ms)")
                    if success:
                        shutil.rmtree(task_dir, ignore_errors=True)
                    else:
                        sandbox_error_log_path = sandbox.copy_error_log(
                            port, task_dir / "sandbox-error.log")
                        failures.append(FailureRecord(
                            worker_id, i, task.filter_spec, log_path, output_path, duration,
                            sandbox_error_log_path))
            finally:
                print(f"[Worker {worker_id}] Tearing down sandbox on port {port}...")
                sandbox.teardown(port)

            if not failures:
                shutil.rmtree(worker_dir, ignore_errors=True)
            return failures

        # Step 4: Launch parallel worker threads consuming assigned queues
        print(f"Executing {len(all_tasks)} test tasks using {self.num_workers} parallel firejail workers...")

        # Reserve every worker's ports in a single batch, up front, so the
        # underlying find_free_ports() guarantee (no port handed out twice
        # while its sockets are still open) covers all workers at once instead
        # of each worker racing the others via its own separate call.
        all_ports = SandboxManager.find_free_ports(self.num_workers * self.NUM_SANDBOX_PORTS)

        all_failures: List[FailureRecord] = []
        with ThreadPoolExecutor(max_workers=self.num_workers) as executor:
            futures = [
                executor.submit(
                    worker_loop, i, queues[i],
                    all_ports[i * self.NUM_SANDBOX_PORTS:(i + 1) * self.NUM_SANDBOX_PORTS],
                )
                for i in range(self.num_workers)
            ]
            for future in futures:
                all_failures.extend(future.result())

        # Step 5: Save updated metrics back to disk
        TestTaskFactory.save_execution_times(self.timing_file, all_tasks)
        print(f"Completed execution. Updated metrics saved to '{self.timing_file}'.")

        # Step 6: Summarize failures and write the HTML report
        self._write_report(self.report_file, len(all_tasks), all_failures)
        print(f"Execution report written to '{self.report_file}'.")

        if all_failures:
            print(f"\n{len(all_failures)} of {len(all_tasks)} suite(s) failed:", file=sys.stderr)
            for failure in sorted(all_failures, key=lambda f: (f.worker_id, f.filter_spec)):
                print(f"  worker{failure.worker_id}: {failure.filter_spec} "
                      f"(log: {failure.log_path}, output: {failure.output_path}, "
                      f"sandbox error log: {failure.sandbox_error_log_path or 'n/a'})",
                      file=sys.stderr)
        else:
            print("All suites passed.")


def parse_args():
    """Parses command-line arguments for the parallel test runner CLI."""
    parser = argparse.ArgumentParser(
        description="Parallel Google Test Orchestrator using Firejail isolation and LPT scheduling."
    )

    # Required target test binary parameter
    parser.add_argument(
        "-b", "--binary",
        required=True,
        type=str,
        help="Path to the Google Test executable binary."
    )

    # Required mariadb-shell binary parameter, used to deploy per-worker sandboxes.
    # Not needed when only listing the balanced worker groups (--list-groups).
    parser.add_argument(
        "--shell-binary",
        default=None,
        type=str,
        help="Path to the mariadb-shell executable, used to deploy a base "
             "server sandbox for each worker to test against. Required unless "
             "--list-groups is given."
    )

    # Optional Google Test filter string
    parser.add_argument(
        "--gtest_filter",
        type=str,
        default=None,
        help="Filter pattern passed to --gtest_list_tests and used to select specific tasks."
    )

    # Optional execution flags
    parser.add_argument(
        "-t", "--timing-file",
        default="test-execution-times.txt",
        type=str,
        help="Path to the execution timing profile. Default: 'test-execution-times.txt'."
    )
    parser.add_argument(
        "-p", "--execution-plan-file",
        default="test-execution-plan.txt",
        type=str,
        help="Path to write the per-worker execution plan to (one line per worker, "
             "listing the suites assigned to it). Default: 'test-execution-plan.txt'."
    )
    parser.add_argument(
        "--logs-dir",
        default="test-execution-logs",
        type=str,
        help="Directory to keep every worker/suite's mariadb-shell.log under. Not "
             "cleared between runs. Default: 'test-execution-logs'."
    )
    parser.add_argument(
        "--report-file",
        default="test-execution-report.html",
        type=str,
        help="Path to write the HTML execution report to, listing failed suites "
             "with a link to their log. Default: 'test-execution-report.html'."
    )
    parser.add_argument(
        "-j", "--jobs",
        default=0,
        type=int,
        help="Number of parallel worker threads. Default: CPU core count."
    )
    parser.add_argument(
        "-s", "--split-suites",
        nargs="*",
        default=[],
        help="Space-separated list of suite names to split into per-test tasks (e.g. -s SuiteA SuiteB)."
    )
    parser.add_argument(
        "-l", "--list-groups",
        action="store_true",
        help="List the balanced per-worker task groups and exit, without deploying "
             "any sandbox or running any test."
    )

    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()

    # Validate that binary exists prior to starting orchestrator
    binary_path = Path(args.binary).resolve()
    if not binary_path.exists():
        print(f"Error: Binary path '{binary_path}' does not exist.", file=sys.stderr)
        sys.exit(1)

    shell_binary_path = None
    if not args.list_groups and not args.shell_binary:
        print("Error: --shell-binary is required unless --list-groups is given.", file=sys.stderr)
        sys.exit(1)

    if args.shell_binary:
        shell_binary_path = Path(args.shell_binary).resolve()
        if not shell_binary_path.exists():
            print(f"Error: Shell binary path '{shell_binary_path}' does not exist.", file=sys.stderr)
            sys.exit(1)

    orchestrator = Orchestrator(
        binary_path=str(binary_path),
        shell_binary=str(shell_binary_path) if shell_binary_path else None,
        timing_file=args.timing_file,
        num_workers=args.jobs,
        gtest_filter=args.gtest_filter,
        execution_plan_file=args.execution_plan_file,
        logs_dir=args.logs_dir,
        report_file=args.report_file
    )

    orchestrator.run(split_suites=args.split_suites, list_groups=args.list_groups)