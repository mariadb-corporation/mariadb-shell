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

"""Tests for mysqlsh.task_manager, the background tasks every plugin shares."""

import threading
import time

import pytest

from mysqlsh import task_manager as tm


@pytest.fixture(autouse=True)
def no_tasks():
    tm.clear()
    yield
    tm.clear()


def _finished(task, timeout=5.0):
    """The task's snapshot once it ended."""
    deadline = time.monotonic() + timeout
    while task.status not in tm.FINISHED_STATES:
        assert time.monotonic() < deadline, f"the task is still {task.status}"
        task.wait_for_change(task.version, 0.1)
    return task.snapshot()


def test_a_task_reports_its_stages_progress_and_result():
    def work(task):
        task.message("starting")
        task.stage_started("copy")
        task.progress(50, 200, items="rows")
        task.stage_finished("copy", seconds=1.5)
        return {"rows": 200}

    task = tm.create("util", "copy_schemas", "Copy sakila", attributes={"connection_id": "c1"})
    assert task.status == tm.PENDING
    task.start(work)
    snapshot = _finished(task)

    assert snapshot["task_type"] == "util"
    assert snapshot["kind"] == "copy_schemas"
    # times end in Z, as the MCP plugin's other answers and JavaScript write them
    assert snapshot["created_at"].endswith("Z") and snapshot["finished_at"].endswith("Z")
    assert snapshot["messages"][0]["time"].endswith("Z")
    assert snapshot["connection_id"] == "c1"
    assert snapshot["status"] == tm.COMPLETED
    assert snapshot["result"] == {"rows": 200}
    assert snapshot["stage"] == "copy"
    assert snapshot["progress"] == {"current": 50, "total": 200, "items": "rows", "percent": 25.0}
    assert snapshot["stages"] == [{"name": "copy", "status": tm.COMPLETED, "seconds": 1.5}]
    assert [m["text"] for m in snapshot["messages"]] == ["starting"]
    assert snapshot["next_since"] == 1
    assert task.snapshot(1)["messages"] == []


def test_a_failing_task_reports_its_error_and_ends_its_running_stage():
    def work(task):
        task.stage_started("deploy")
        raise RuntimeError("quota exceeded")

    task = tm.create("dba", "deploy_object", "Deploy db1")
    task.start(work)
    snapshot = _finished(task)

    assert snapshot["status"] == tm.FAILED
    assert snapshot["error"] == "quota exceeded"
    assert snapshot["stages"][0]["status"] == tm.FAILED
    assert snapshot["messages"][-1] == {**snapshot["messages"][-1], "level": "error", "text": "quota exceeded"}


def test_a_cancel_wakes_a_sleeping_task():
    started = threading.Event()

    def work(task):
        started.set()
        task.sleep(60)
        return "never"

    task = tm.create("dba", "deploy_object", "Deploy db1")
    task.start(work)
    started.wait(5)
    began = time.monotonic()
    task.cancel()
    snapshot = _finished(task)

    assert time.monotonic() - began < 5
    assert snapshot["status"] == tm.CANCELLED
    assert snapshot["cancel_requested"] is True
    assert snapshot["result"] is None
    assert snapshot["messages"][-1]["text"] == "The task was cancelled."


def test_a_cancel_reaches_the_canceller_set_before_or_after_it():
    stopped = []
    release = threading.Event()

    def work(task):
        task.set_canceller(lambda: (stopped.append(True), release.set()))
        release.wait(5)
        task.check_cancelled()

    task = tm.create("util", "dump_schemas", "Dump")
    task.cancel()  # before it started: nothing to call yet
    task.start(work)
    assert _finished(task)["status"] == tm.CANCELLED
    assert stopped == [True]


def test_cancelling_a_finished_task_changes_nothing():
    task = tm.create("dba", "delete_object", "Delete db1")
    task.start(lambda t: "done")
    _finished(task)
    task.cancel()
    assert task.status == tm.COMPLETED
    assert task.cancel_requested is False


def test_waiting_for_news_answers_on_a_message_or_after_the_timeout():
    go = threading.Event()

    def work(task):
        go.wait(5)
        task.message("half way")
        go.clear()
        go.wait(5)

    task = tm.create("util", "load_dump", "Load")
    task.start(work)

    began = time.monotonic()
    assert task.wait_for_news(0, 0.2)["messages"] == []
    assert time.monotonic() - began >= 0.15

    threading.Timer(0.1, go.set).start()
    news = task.wait_for_news(0, 5)
    assert [m["text"] for m in news["messages"]] == ["half way"]
    go.set()
    _finished(task)


def test_tasks_are_kept_apart_by_owner_and_listed_by_type():
    mine = tm.create("dba", "deploy_object", "Deploy db1", owner="client-a")
    tm.create("util", "dump_schemas", "Dump", owner="client-a")
    tm.create("dba", "deploy_object", "Deploy db2", owner="client-b")
    shells = tm.create("dba", "deploy_object", "Deploy db3")

    assert [t.title for t in tm.list_tasks("client-a")] == ["Deploy db1", "Dump"]
    assert [t.title for t in tm.list_tasks("client-a", "dba")] == ["Deploy db1"]
    assert tm.list_tasks() == [shells]
    assert tm.get(mine.id, "client-a") is mine

    # another owner's task is reported as one that does not exist
    with pytest.raises(tm.TaskError, match=f"There is no task with the id '{mine.id}'"):
        tm.get(mine.id, "client-b")
    with pytest.raises(tm.TaskError):
        tm.get(mine.id)


def test_an_owner_may_run_only_so_many_tasks_of_a_type():
    release = threading.Event()
    running = []
    for number in range(2):
        task = tm.create("dba", "deploy_object", f"Deploy db{number}", max_running=2)
        task.start(lambda t: release.wait(5))
        running.append(task)

    with pytest.raises(tm.TaskError, match="Already 2 dba tasks are running"):
        tm.create("dba", "deploy_object", "Deploy db3", max_running=2)
    # other types, and other owners, are counted on their own
    tm.create("util", "dump_schemas", "Dump", max_running=2)
    tm.create("dba", "deploy_object", "Deploy db3", owner="client-b", max_running=2)

    release.set()
    for task in running:
        _finished(task)
    tm.create("dba", "deploy_object", "Deploy db3", max_running=2)


def test_finished_tasks_expire_and_are_capped(monkeypatch):
    tasks = []
    for number in range(3):
        task = tm.create("dba", "delete_object", f"Delete db{number}")
        task.start(lambda t: None)
        _finished(task)
        tasks.append(task)

    monkeypatch.setattr(tm, "MAX_FINISHED_TASKS", 2)
    assert [t.title for t in tm.list_tasks()] == ["Delete db1", "Delete db2"]

    monkeypatch.setattr(tm, "FINISHED_TASK_LIFETIME", 0)
    time.sleep(0.01)
    assert tm.list_tasks() == []


def test_a_summary_leaves_the_messages_out():
    task = tm.create("dba", "deploy_object", "Deploy db1")
    task.message("one")
    summary = task.summary()
    assert "messages" not in summary and "messages_dropped" not in summary
    assert summary["next_since"] == 1


def test_progress_carries_what_the_stage_measures():
    task = tm.create("util", "dump_schemas", "Dump")
    task.on_event({
        "type": "progress", "stage": "Dumping data", "current": 50, "total": 200,
        "throughput": 1000, "etaSeconds": 3, "items": "rows", "totalIsApproximate": True,
    })
    assert task.snapshot()["progress"] == {
        "current": 50, "total": 200, "throughput": 1000, "eta_seconds": 3,
        "items": "rows", "total_is_approximate": True, "percent": 25.0,
    }


def test_a_percentage_only_where_the_total_is_known():
    assert tm._progress_of({"current": 5, "total": 10, "totalKnown": False})["percent"] is None
    assert tm._progress_of({"current": 5, "total": 0})["percent"] is None
    assert tm._progress_of({"current": 15, "total": 10})["percent"] == 100.0


def test_messages_after_since_and_those_dropped(monkeypatch):
    monkeypatch.setattr(tm, "MAX_MESSAGES", 3)
    task = tm.create("util", "load_dump", "Load")
    for index in range(5):
        task.message(f"line {index}")

    snapshot = task.snapshot(1)
    assert [m["text"] for m in snapshot["messages"]] == ["line 2", "line 3", "line 4"]
    # line 1 was dropped before the caller could read it
    assert snapshot["messages_dropped"] == 1
    assert task.snapshot(4)["messages"][0]["seq"] == 5
    assert task.snapshot(5)["messages"] == []


def test_a_waiting_caller_wakes_on_a_change():
    task = tm.create("util", "load_dump", "Load")
    threading.Timer(0.2, lambda: task.message("hello")).start()
    began = time.monotonic()
    task.wait_for_change(task.version, 5)
    assert time.monotonic() - began < 2
    assert task.snapshot()["messages"][0]["text"] == "hello"


def test_a_cancelled_task_keeps_what_its_work_said_on_stopping():
    stop = threading.Event()

    def work(task):
        task.set_canceller(stop.set)
        stop.wait(5)
        raise RuntimeError("Interrupted by user\n")

    task = tm.create("util", "dump_instance", "Dump")
    task.start(work)
    task.cancel()
    snapshot = _finished(task)
    assert snapshot["status"] == tm.CANCELLED
    assert snapshot["error"] == "Interrupted by user"


def test_a_task_that_cannot_start_ends_failed(monkeypatch):
    def no_thread(self):
        raise RuntimeError("can't start new thread")

    monkeypatch.setattr(threading.Thread, "start", no_thread)
    task = tm.create("dba", "deploy_object", "Deploy db1", max_running=1)
    with pytest.raises(tm.TaskError, match="The task could not start: can't start new thread"):
        task.start(lambda t: None)

    snapshot = task.snapshot()
    assert snapshot["status"] == tm.FAILED
    assert snapshot["error"] == "The task could not start: can't start new thread"
    assert snapshot["messages"][-1]["text"] == snapshot["error"]
    # it never ran, and does not hold the one running slot
    assert snapshot["started_at"] is None and snapshot["finished_at"] is not None
    tm.create("dba", "deploy_object", "Deploy db2", max_running=1)


def test_a_task_cancelled_while_it_could_not_start_ends_cancelled(monkeypatch):
    task = tm.create("dba", "deploy_object", "Deploy db1")

    def cancel_then_fail(self):
        task.cancel()
        raise RuntimeError("can't start new thread")

    monkeypatch.setattr(threading.Thread, "start", cancel_then_fail)
    with pytest.raises(tm.TaskError):
        task.start(lambda t: None)
    assert task.status == tm.CANCELLED


def test_an_interrupted_start_does_not_run_the_work_later(monkeypatch):
    # Ctrl+C lands in Thread.start once the thread exists: the caller gets the
    # KeyboardInterrupt, the task ends, and the work is never run
    ran = []
    real_start = threading.Thread.start
    started = []

    def start_then_interrupt(self):
        with task.changed:
            # the lock held keeps _run from marking the task running first
            real_start(self)
            started.append(self)
            raise KeyboardInterrupt

    task = tm.create("dba", "deploy_object", "Deploy db1")
    monkeypatch.setattr(threading.Thread, "start", start_then_interrupt)
    with pytest.raises(KeyboardInterrupt):
        task.start(lambda t: ran.append(True))
    started[0].join(5)

    assert task.status == tm.FAILED
    assert task.error == "The task could not start: KeyboardInterrupt"
    assert ran == []


# --- the end of the process ----------------------------------------------------


@pytest.fixture
def cli_call(monkeypatch):
    """Runs a test as the shell does a command-line call of dba.deploy.vendor.object."""
    monkeypatch.setattr(tm, "cli_function", "dba.deploy.vendor.object")


def test_a_function_returns_the_task_id_unless_it_is_the_command_line_call(monkeypatch):
    task = tm.create("dba", "deploy_object", "Deploy db1")
    assert tm.answer(task, "dba.deploy.vendor.object") == {"task_id": task.id, "status": tm.PENDING}
    # a function another one calls, as an MCP server's tools are from mcp start
    monkeypatch.setattr(tm, "cli_function", "mcp.start")
    assert tm.answer(task, "dba.deploy.vendor.object")["task_id"] == task.id


def test_the_command_line_call_follows_its_task_to_the_result(cli_call, capsys):
    def work(task):
        task.message("is starting")
        task.message("quota nearly used", "warning")
        return {"name": "db1"}

    task = tm.create("dba", "deploy_object", "Deploy db1")
    task.start(work)
    # the shell's spelling of the name, from the command line
    assert tm.answer(task, "dba.deploy.vendor.object") == {"name": "db1"}
    assert capsys.readouterr().err == "is starting\nWARNING: quota nearly used\n"


def test_the_command_line_call_raises_the_error_of_its_task(cli_call, capsys):
    def work(task):
        task.message("is starting")
        raise RuntimeError("quota exceeded")

    task = tm.create("dba", "deploy_object", "Deploy db1")
    task.start(work)
    with pytest.raises(tm.TaskError, match="^quota exceeded$"):
        tm.answer(task, "dba.deploy.vendor.object")
    # the error is reported once, as the error
    assert capsys.readouterr().err == "is starting\n"


def test_ctrl_c_cancels_the_task_a_command_line_call_follows(monkeypatch):
    task = tm.create("dba", "deploy_object", "Deploy db1")
    task.start(lambda t: t.sleep(60))
    real_follow = tm._follow
    calls = []

    def interrupted_once(task, write, position):
        calls.append(True)
        if len(calls) == 1:
            raise KeyboardInterrupt
        return real_follow(task, write, position)

    monkeypatch.setattr(tm, "_follow", interrupted_once)
    written = []
    with pytest.raises(tm.TaskError, match="cancelled"):
        tm.follow(task, written.append)
    assert task.status == tm.CANCELLED
    # the cancel is reported once, as the error
    assert written == ["Cancelling the task..."]


def test_a_script_waits_at_exit_for_the_shells_own_tasks(monkeypatch):
    written = []
    monkeypatch.setattr(tm, "_write_fd", written.append)
    monkeypatch.setattr(tm, "_interactive", False)
    release = threading.Event()

    def work(task):
        release.wait(5)
        task.message("is running")

    mine = tm.create("dba", "deploy_object", "Deploy db1")
    mine.start(work)
    # an MCP client's task is the server's business
    tm.create("dba", "deploy_object", "Deploy db2", owner="client-a").start(lambda t: t.sleep(60))
    threading.Timer(0.2, release.set).start()

    tm._at_exit()
    assert mine.status == tm.COMPLETED
    assert written == [
        "Waiting for 1 background task(s) to end: Deploy db1. Press Ctrl+C to stop waiting.",
        "is running",
    ]


def test_an_interactive_session_does_not_wait_at_exit(monkeypatch):
    written = []
    monkeypatch.setattr(tm, "_write_fd", written.append)
    monkeypatch.setattr(tm, "_interactive", True)
    task = tm.create("dba", "deploy_object", "Deploy db1")
    task.start(lambda t: t.sleep(60))

    tm._at_exit()
    assert task.status == tm.RUNNING
    assert written == [
        "1 background task(s) still running stop with the shell: Deploy db1. "
        "What a cloud vendor already started goes on there."
    ]
    task.cancel()
