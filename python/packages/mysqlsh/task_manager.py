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

"""Background tasks: work that outlives the call that started it.

A dump, a load or a cloud deployment can run for minutes or hours, far longer
than a caller - a shell user, the VS Code extension, an MCP client - wants to
wait for one call to return. Such work runs as a task instead: starting it
returns the task's id at once, and the caller follows it by that id, reading
its state, the progress of its current stage and the messages it reported
since the last read, and can cancel it.

Every plugin that runs such work uses this one task manager, so that one
``task.list`` shows all of them, each with its ``task_type`` - ``util`` for a
dump or load, ``dba`` for a cloud deployment.

* **The work** runs on a thread of its own: a function taking the task. It
  reports through the task - :meth:`Task.message`, :meth:`Task.stage_started`,
  :meth:`Task.progress`, :meth:`Task.stage_finished`, or :meth:`Task.on_event`
  for a ready-made event - and its return value is the task's result.
* **Cancelling** sets a flag the work checks (:meth:`Task.check_cancelled`, or
  :meth:`Task.sleep`, which wakes on a cancel), and calls what the work set
  with :meth:`Task.set_canceller` - interrupting a process it runs, say.
  Work that ends after a cancel was asked for is reported cancelled.
* **An owner** keeps the tasks of different callers apart: an MCP server
  passes the client that started a task, and another client is told it does
  not exist. The shell's own tasks have no owner.

Tasks live in the process that started them, in memory: a task started in one
shell process is not seen by another, and a restart forgets them. Finished
tasks are kept for :data:`FINISHED_TASK_LIFETIME` seconds and at most
:data:`MAX_FINISHED_TASKS` of them, the oldest dropped first, so that a caller
coming back late can still read how its task ended.

The process must not end under the shell's own tasks, so:

* **A command-line call** (``mariadb-shell -- dba deploy vendor object ...``)
  follows the task it starts to its end: the function returns through
  :func:`answer`, which the shell tells which function the call runs.
* **A script** (``-f``, ``-e``) that ends with tasks still running waits for
  them before the shell exits; Ctrl+C stops the waiting.
* **An interactive session** that ends with tasks still running says which
  stop with it, and does not wait.

Tasks with an owner - an MCP server's - are the server's business, and are
never waited for.
"""

import atexit
import collections
import datetime
import itertools
import os
import sys
import threading
import time
import uuid

PENDING = "pending"
RUNNING = "running"
COMPLETED = "completed"
FAILED = "failed"
CANCELLED = "cancelled"
FINISHED_STATES = (COMPLETED, FAILED, CANCELLED)

# How long a finished task is kept, and how many are, at most.
FINISHED_TASK_LIFETIME = 3600
MAX_FINISHED_TASKS = 100
# How many tasks of one type an owner may have running at once, unless the
# plugin starting them says otherwise.
MAX_RUNNING_TASKS = 4
# The messages kept per task; older ones are dropped, and the snapshot says how
# many were.
MAX_MESSAGES = 2000
# The longest a caller may wait for a change.
MAX_WAIT_MS = 30000

_tasks = {}
_tasks_lock = threading.Lock()

# The function a command-line call runs, as "object.function", e.g.
# "dba.deploy.vendor.object": set by the shell right before it calls it.
cli_function = None
# Whether the shell's own tasks were started in an interactive session, which
# does not wait for them when it ends; None until one is started.
_interactive = None


class TaskError(Exception):
    """A task that does not exist, or one too many to start.

    Plugins turn it into their own kind of error: ``mysqlsh.Error`` in the
    shell, a tool error in an MCP server.
    """


class TaskCancelled(Exception):
    """Raised in the work by :meth:`Task.check_cancelled` once a cancel was asked for."""


def utc_timestamp() -> str:
    """The current time as tasks report it: ISO 8601, UTC, seconds, with a Z.

    The Z, not +00:00, as JavaScript's toISOString and the MCP plugin's other
    answers write it, so a client reads every time the same way.
    """
    return (
        datetime.datetime.now(datetime.timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


class Task:
    """One background task and everything a caller can learn about it.

    Args:
        task_type (str): The kind of plugin work, e.g. "util" or "dba".
        kind (str): What the task does, e.g. "dump_schemas".
        title (str): A line describing it, for a list of tasks.
        owner: Who started it, compared with ``==``; None for the shell's own.
        attributes (dict): More to report with the task, e.g. the connection
            it works on.
        max_running (int): How many tasks of the type the owner may have
            running at once, this one included.
    """

    def __init__(
        self,
        task_type: str,
        kind: str,
        title: str,
        owner=None,
        attributes=None,
        max_running: int = MAX_RUNNING_TASKS,
    ):
        self.id = str(uuid.uuid4())
        self.task_type = task_type
        self.kind = kind
        self.title = title
        self.owner = owner
        self.attributes = dict(attributes or {})
        self.max_running = max_running
        self.status = PENDING
        self.created_at = utc_timestamp()
        self.started_at = None
        self.finished_at = None
        self.finished_monotonic = None
        # the stages in the order they started, each with its state
        self.stages = []
        self.stage = None
        self.progress_info = None
        self.messages = collections.deque(maxlen=MAX_MESSAGES)
        self.message_count = 0
        self.result = None
        self.error = None
        self.cancel_requested = False
        # stops the running work; set by it while it can be stopped
        self._canceller = None
        # bumped on every change, so a waiting caller can tell one happened
        self.version = 0
        self.changed = threading.Condition(threading.RLock())

    # --- reported by the running work ------------------------------------

    def on_event(self, event) -> None:
        """Records one event of the running work.

        Args:
            event (dict): The event: "message" (level, text), "stageStarted"
                (stage), "progress" (stage, current, total, and what else the
                stage measures) or "stageFinished" (stage, seconds).
        """
        kind = event.get("type")

        with self.changed:
            if kind == "message":
                self._add_message(event.get("level", "info"), event.get("text", ""))
            elif kind == "stageStarted":
                self.stage = event.get("stage")
                self.stages.append({"name": self.stage, "status": RUNNING, "seconds": None})
                self.progress_info = None
            elif kind == "progress":
                self.stage = event.get("stage", self.stage)
                self.progress_info = _progress_of(event)
            elif kind == "stageFinished":
                for stage in reversed(self.stages):
                    if stage["name"] == event.get("stage") and stage["status"] == RUNNING:
                        stage["status"] = COMPLETED
                        stage["seconds"] = event.get("seconds")
                        break

            self._touch()

    def message(self, text: str, level: str = "info") -> None:
        """Reports a message: "info", "warning" or "error"."""
        self.on_event({"type": "message", "level": level, "text": text})

    def stage_started(self, stage: str) -> None:
        """Reports that a stage of the work started."""
        self.on_event({"type": "stageStarted", "stage": stage})

    def progress(self, current, total, stage=None, **measures) -> None:
        """Reports the progress of the current stage.

        Args:
            current: How far it got.
            total: Where it ends.
            stage (str): The stage; the current one when None.
            **measures: What else it measures: throughput, etaSeconds, items,
                totalIsApproximate, totalKnown.
        """
        event = {"type": "progress", "current": current, "total": total, **measures}
        if stage is not None:
            event["stage"] = stage
        self.on_event(event)

    def stage_finished(self, stage: str, seconds=None) -> None:
        """Reports that a stage of the work finished."""
        self.on_event({"type": "stageFinished", "stage": stage, "seconds": seconds})

    def _add_message(self, level: str, text: str) -> None:
        self.message_count += 1
        self.messages.append(
            {"seq": self.message_count, "time": utc_timestamp(), "level": level, "text": text}
        )

    def _touch(self) -> None:
        self.version += 1
        self.changed.notify_all()

    # --- lifecycle -------------------------------------------------------

    def start(self, work) -> None:
        """Runs the work on a thread of its own.

        From here on the task is listed, and holds one of its owner's
        ``max_running`` slots until it ends. A task created and never started
        is forgotten with its object instead, and holds nothing.

        Args:
            work: A function taking the task and returning its result, or
                raising to fail it.

        Raises:
            TaskError: The owner already runs ``max_running`` tasks of the
                type, and the task was not started; or no thread could be
                started, and the task ended without running. Either way it is
                not left running.
        """
        _register(self)
        thread = threading.Thread(
            target=self._run, args=(work,), name=f"task-{self.task_type}-{self.id}", daemon=True
        )
        try:
            thread.start()
        except BaseException as error:
            # Ctrl+C can land after the thread started: _run then finds the
            # task ended, and returns without running the work
            reason = str(error).strip() or error.__class__.__name__
            with self.changed:
                if self.status == PENDING:
                    self._finish(
                        CANCELLED if self.cancel_requested else FAILED,
                        error=f"The task could not start: {reason}",
                    )
            if isinstance(error, Exception):
                raise TaskError(f"The task could not start: {reason}") from error
            raise
        # the thread marks the task running; a caller reads it at once
        with self.changed:
            self.changed.wait_for(lambda: self.status != PENDING)

    def _run(self, work) -> None:
        with self.changed:
            if self.status != PENDING:
                # start() gave up on this task: see there
                return
            self.status = RUNNING
            self.started_at = utc_timestamp()
            self._touch()
        try:
            result = work(self)
        except Exception as error:  # noqa: BLE001 - reported to the caller
            # a cancelled task keeps what its work said on stopping, e.g. a
            # utility's "Interrupted by user"
            message = str(error).strip() or error.__class__.__name__
            self._finish(CANCELLED if self.cancel_requested else FAILED, error=message)
        else:
            self._finish(CANCELLED if self.cancel_requested else COMPLETED, result=result)

    def _finish(self, status: str, result=None, error=None) -> None:
        with self.changed:
            self.status = status
            self.result = result
            self.error = error
            self.finished_at = utc_timestamp()
            self.finished_monotonic = time.monotonic()
            # nothing is left to stop; let go of what the canceller holds
            self._canceller = None
            # a stage still running ends as the task did
            for stage in self.stages:
                if stage["status"] == RUNNING:
                    stage["status"] = status
            if status == CANCELLED:
                self._add_message("info", "The task was cancelled.")
            elif status == FAILED:
                self._add_message("error", error)
            self._touch()

    def fail_running_stages(self) -> None:
        """Ends the stages still running as failed: their process crashed."""
        with self.changed:
            for stage in self.stages:
                if stage["status"] == RUNNING:
                    stage["status"] = FAILED
            self._touch()

    # --- cancelling ------------------------------------------------------

    def set_canceller(self, canceller) -> None:
        """Sets what stops the running work, or None where nothing can.

        A cancel asked for before it was set reaches it at once.
        """
        with self.changed:
            self._canceller = canceller
            cancelled = self.cancel_requested

        if canceller is not None and cancelled:
            canceller()

    def cancel(self) -> None:
        """Asks the running work to stop."""
        with self.changed:
            if self.status in FINISHED_STATES:
                return
            self.cancel_requested = True
            canceller = self._canceller
            self._touch()

        if canceller is not None:
            canceller()

    def check_cancelled(self) -> None:
        """Raises :class:`TaskCancelled` once a cancel was asked for."""
        if self.cancel_requested:
            raise TaskCancelled("The task was cancelled.")

    def sleep(self, seconds: float) -> None:
        """Waits, waking early to raise :class:`TaskCancelled` on a cancel.

        For work that polls: a cancel ends the wait at once instead of after
        the poll interval.
        """
        with self.changed:
            self.changed.wait_for(lambda: self.cancel_requested, seconds)
        self.check_cancelled()

    # --- read by callers -------------------------------------------------

    def snapshot(self, since: int = 0) -> dict:
        """The task's state, with the messages after ``since``.

        Args:
            since (int): The ``seq`` of the last message the caller has.

        Returns:
            dict: The snapshot.
        """
        since = max(0, since)
        with self.changed:
            first_kept = self.messages[0]["seq"] if self.messages else self.message_count + 1
            # the kept messages have consecutive seqs: skip to the first one wanted
            messages = list(itertools.islice(self.messages, max(0, since - first_kept + 1), None))

            return {
                "task_id": self.id,
                "task_type": self.task_type,
                "kind": self.kind,
                "title": self.title,
                **self.attributes,
                "status": self.status,
                "cancel_requested": self.cancel_requested,
                "created_at": self.created_at,
                "started_at": self.started_at,
                "finished_at": self.finished_at,
                "stage": self.stage,
                "progress": dict(self.progress_info) if self.progress_info else None,
                "stages": [dict(stage) for stage in self.stages],
                "messages": messages,
                # messages that were dropped before the caller could read them
                "messages_dropped": max(0, first_kept - since - 1),
                "next_since": self.message_count,
                "result": self.result,
                "error": self.error,
            }

    def summary(self) -> dict:
        """The task's state without its messages, for a list of tasks."""
        snapshot = self.snapshot(self.message_count)
        snapshot.pop("messages")
        snapshot.pop("messages_dropped")
        return snapshot

    def wait_for_news(self, since: int, timeout: float) -> dict:
        """The task's state, once there is news for a caller at ``since``.

        Answers at once where there are messages after ``since`` or the task
        finished, and otherwise after its next change, or ``timeout``. The
        snapshot and the version waited on are taken under one hold of the
        lock: a change between the two would otherwise be waited past.

        Args:
            since (int): The ``seq`` of the last message the caller has.
            timeout (float): The longest to wait, in seconds; capped at
                :data:`MAX_WAIT_MS`.

        Returns:
            dict: The snapshot, as :meth:`snapshot` returns it.
        """
        with self.changed:
            snapshot = self.snapshot(since)
            if snapshot["messages"] or snapshot["status"] in FINISHED_STATES:
                return snapshot
            # the lock is reentrant, and the wait lets go of it whole
            self.wait_for_change(self.version, min(timeout, MAX_WAIT_MS / 1000.0))
            return self.snapshot(since)

    def wait_for_change(self, version: int, timeout: float) -> None:
        """Waits until the task changed after ``version``, or it finished.

        Args:
            version (int): The version the caller has seen.
            timeout (float): The longest to wait, in seconds.
        """
        with self.changed:
            self.changed.wait_for(
                lambda: self.version != version or self.status in FINISHED_STATES, timeout
            )


def _progress_of(event: dict) -> dict:
    """The progress of a stage as callers get it, with a percentage."""
    current = event.get("current", 0)
    total = event.get("total", 0)
    progress = {"current": current, "total": total}

    for key, name in (
        ("throughput", "throughput"),
        ("etaSeconds", "eta_seconds"),
        ("items", "items"),
        ("totalIsApproximate", "total_is_approximate"),
        ("totalKnown", "total_known"),
    ):
        if key in event:
            progress[name] = event[key]

    known = event.get("totalKnown", True)
    progress["percent"] = (
        min(100.0, round(current * 100.0 / total, 1)) if known and total else None
    )
    return progress


def _prune() -> None:
    """Drops finished tasks past their lifetime, and the oldest beyond the cap.

    Called with :data:`_tasks_lock` held.
    """
    now = time.monotonic()
    finished = sorted(
        (task for task in _tasks.values() if task.finished_monotonic is not None),
        key=lambda task: task.finished_monotonic,
    )
    for index, task in enumerate(finished):
        expired = now - task.finished_monotonic > FINISHED_TASK_LIFETIME
        too_many = len(finished) - index > MAX_FINISHED_TASKS
        if expired or too_many:
            _tasks.pop(task.id, None)


def _refuse_one_too_many(task: Task) -> None:
    """Raises :class:`TaskError` where the task's owner already runs
    ``max_running`` tasks of its type. Called with :data:`_tasks_lock` held."""
    running = sum(
        1
        for other in _tasks.values()
        if other.owner == task.owner
        and other.task_type == task.task_type
        and other.status not in FINISHED_STATES
    )
    if running >= task.max_running:
        raise TaskError(
            f"Already {running} {task.task_type} tasks are running; wait for one to "
            "finish or cancel one with task.cancel before starting another."
        )


def _register(task: Task) -> None:
    """Lists the task, as :meth:`Task.start` does, refusing one too many.

    A task is listed only once it runs: one created and never started -
    its plugin failing in between - would otherwise hold a running slot and
    show as pending for as long as the process lives.
    """
    with _tasks_lock:
        _prune()
        _refuse_one_too_many(task)
        _tasks[task.id] = task


def create(
    task_type: str,
    kind: str,
    title: str,
    owner=None,
    max_running: int = MAX_RUNNING_TASKS,
    attributes=None,
) -> Task:
    """Makes a new task, refusing one too many.

    Args:
        task_type (str): The kind of plugin work, e.g. "util" or "dba".
        kind (str): What the task does, e.g. "dump_schemas".
        title (str): A line describing it, for a list of tasks.
        owner: Who starts it; None for the shell's own.
        max_running (int): How many tasks of this type the owner may have
            running at once, this one included.
        attributes (dict): More to report with the task.

    Returns:
        Task: The task, not started yet. It is listed, and holds one of the
        owner's ``max_running`` slots, from :meth:`Task.start` on; one never
        started is forgotten with its object.

    Raises:
        TaskError: The owner already runs ``max_running`` tasks of the type.
    """
    task = Task(task_type, kind, title, owner, attributes, max_running)
    if owner is None:
        _note_session()

    with _tasks_lock:
        _prune()
        _refuse_one_too_many(task)

    return task


def get(task_id: str, owner=None) -> Task:
    """Looks a task up for an owner.

    A task of another owner is reported exactly like one that does not exist.

    Args:
        task_id (str): The id the task was started with.
        owner: Who asks; None for the shell's own tasks.

    Returns:
        Task: The task.

    Raises:
        TaskError: There is no such task for the owner.
    """
    with _tasks_lock:
        _prune()
        task = _tasks.get(task_id)

    if task is None or task.owner != owner:
        raise TaskError(
            f"There is no task with the id '{task_id}'. Use task.list to see the tasks."
        )

    return task


def list_tasks(owner=None, task_type=None) -> list:
    """The tasks of an owner, oldest first.

    Args:
        owner: Who asks; None for the shell's own tasks.
        task_type (str): Only the tasks of this type; None for all.

    Returns:
        list: The tasks.
    """
    with _tasks_lock:
        _prune()
        return [
            task
            for task in _tasks.values()
            if task.owner == owner and (task_type is None or task.task_type == task_type)
        ]


def clear() -> None:
    """Forgets every task. For tests only: running ones keep running."""
    with _tasks_lock:
        _tasks.clear()


# --- The shell's own tasks, and the end of the process ------------------------


def _note_session() -> None:
    """Notes whether the shell runs an interactive session, while it still can tell."""
    global _interactive
    if _interactive is None:
        try:
            from mysqlsh.globals import shell

            _interactive = bool(shell.options["interactive"])
        except Exception:  # noqa: BLE001 - no shell around, as in the tests
            _interactive = False


def _normalized(name) -> str:
    """A function name as both the command line and the API spell it."""
    return str(name).replace("-", "").replace("_", "").lower()


def _write_stderr(line: str) -> None:
    sys.stderr.write(line + "\n")
    sys.stderr.flush()


def _write_fd(line: str) -> None:
    """Writes to the standard error file itself: at the end of the process the
    shell's console, which sys.stderr may lead to, can be gone already."""
    os.write(2, (line + "\n").encode("utf-8", "replace"))


def _follow(task, write, position=None) -> dict:
    """Writes a task's messages as they come, until it ends; returns its last snapshot.

    Args:
        task (Task): The task.
        write: Called with each line to write.
        position (dict): Where the writing got to, as ``{"since": seq}``, kept
            up to date, so that following again goes on from there.
    """
    position = {"since": 0} if position is None else position
    while True:
        snapshot = task.wait_for_news(position["since"], 1.0)
        for message in snapshot["messages"]:
            if snapshot["status"] in (FAILED, CANCELLED) and message["text"] == snapshot["error"]:
                continue  # the error is reported as such
            level = message["level"]
            write(message["text"] if level in ("info", "status") else f"{level.upper()}: {message['text']}")
        position["since"] = snapshot["next_since"]
        if snapshot["status"] in FINISHED_STATES:
            return snapshot


def follow(task, write=None):
    """Follows a task to its end and returns its result.

    The task's messages are written as they come, to stderr unless ``write``
    is given. Ctrl+C cancels the task and waits for it to stop.

    Args:
        task (Task): The task.
        write: Called with each line to write.

    Returns:
        The task's result.

    Raises:
        TaskError: The task failed or was cancelled.
    """
    write = write or _write_stderr
    position = {"since": 0}
    try:
        snapshot = _follow(task, write, position)
    except KeyboardInterrupt:
        write("Cancelling the task...")
        task.cancel()
        snapshot = _follow(task, write, position)

    if snapshot["status"] == COMPLETED:
        return snapshot["result"]
    raise TaskError(snapshot["error"] or f"The task was {snapshot['status']}.")


def answer(task, function: str):
    """What a function that started a task returns to its caller.

    The task's id and status, at once - unless the function is the one a
    command-line call runs (``mariadb-shell -- dba deploy vendor object ...``),
    whose process ends right after it returns. That call follows the task to
    its end instead (see :func:`follow`): its messages go to stderr as they
    come, and it returns the task's result, which the shell prints, or raises
    its error, which makes the exit code. A function another one calls, as an
    MCP server's tools are called from ``mcp start``, is no command-line call.

    Args:
        task (Task): The task the function started.
        function (str): The function, as "object.function", e.g.
            "dba.deploy.vendor.object".

    Returns:
        dict: ``task_id`` and ``status``; or, in a command-line call, the
        task's result.

    Raises:
        TaskError: In a command-line call, the task failed or was cancelled.
    """
    if cli_function is None or _normalized(cli_function) != _normalized(function):
        return {"task_id": task.id, "status": task.status}
    return follow(task)


# Whether at_exit() ran: the shell calls it once its script or session ended,
# and Python's atexit once more, for a process ending without the shell's say.
_exited = False


def at_exit() -> None:
    """Keeps the process from ending under the shell's own running tasks.

    A script waits for them, writing their messages; Ctrl+C stops the
    waiting. An interactive session only says which tasks stop with it: who
    quits the shell expects it to go.

    The shell calls this once its script or session ended, while it is still
    whole: the tasks' work uses the shell - its sessions, its globals - and
    the client library, which the shell tears down right after, and the Ctrl+C
    handler that makes a KeyboardInterrupt of the key is the shell's. It is
    registered with atexit as well, for a process that ends without the
    shell's say - a script's sys.exit() skips the shell's end - and does
    nothing the second time it is called.
    """
    global _exited
    if _exited:
        return
    _exited = True

    try:
        running = [task for task in list_tasks() if task.status not in FINISHED_STATES]
        if not running:
            return

        titles = "; ".join(task.title for task in running)
        if _interactive:
            _write_fd(
                f"{len(running)} background task(s) still running stop with the shell: "
                f"{titles}. What a cloud vendor already started goes on there."
            )
            return

        _write_fd(
            f"Waiting for {len(running)} background task(s) to end: {titles}. "
            "Press Ctrl+C to stop waiting."
        )
        for task in running:
            _follow(task, _write_fd)
    except KeyboardInterrupt:
        _write_fd("Stopped waiting: the tasks still running stop with the shell.")


atexit.register(at_exit)
