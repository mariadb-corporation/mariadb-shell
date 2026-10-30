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
