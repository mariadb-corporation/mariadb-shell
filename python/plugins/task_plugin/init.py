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

"""Plugin registration

This file is automatically loaded by the MariaDB Shell at startup time.

It registers the task global object, which follows the background tasks
plugins start through mysqlsh.task_manager.
"""

from mysqlsh import Error
from mysqlsh import task_manager
from mysqlsh.plugin_manager import plugin, plugin_function


@plugin
class task:
    """Follow and cancel background tasks.

    Long-running plugin functions, such as deploying a database to a cloud
    vendor, start a background task and return its id at once instead of
    blocking until the work is done. This global object follows those tasks.
    It lists them, reads the state, progress and messages of one, and
    cancels one.

    Each task has a type naming the plugin work it does, such as dba, so the
    list can be narrowed to one type. Tasks live in the shell process that
    started them and are forgotten when it ends; a finished task is kept for
    an hour.
    """


def _call(function, *args):
    """Runs a task manager call, turning its refusals into shell errors."""
    try:
        return function(*args)
    except task_manager.TaskError as e:
        raise Error(str(e)) from e


@plugin_function("task.list")
def list_tasks(task_type=None):
    """Lists the background tasks, oldest first.

    Each task is listed with its id, type, kind, title, status, current stage
    and progress, and, once it ended, its result or error.

    Args:
        task_type (str): Only the tasks of this type, such as dba. By
            default, every type.

    Returns:
        A list of tasks.
    """
    return [t.summary() for t in task_manager.list_tasks(task_type=task_type or None)]


@plugin_function("task.get")
def get_task(task_id, since=0, wait_ms=0):
    """Returns the state of a background task.

    Returns the task's status, its stages and their progress, the messages it
    reported after since, next_since to pass as since on the next call, and,
    once it ended, its result or error.

    Args:
        task_id (str): The id the function that started the task returned.
        since (int): The next_since of the previous call, to get only the
            messages reported after it. By default 0, every message.
        wait_ms (int): Wait up to this many milliseconds, at most 30000, for
            news - a message or the end of the task - before returning. By
            default 0, return at once.

    Returns:
        The state of the task.
    """
    found = _call(task_manager.get, task_id, None)
    if wait_ms and wait_ms > 0:
        return found.wait_for_news(since or 0, wait_ms / 1000.0)
    return found.snapshot(since or 0)


@plugin_function("task.cancel")
def cancel_task(task_id):
    """Asks a background task to stop.

    The task stops as soon as its work can: what a cloud vendor already
    started on its side, such as building a server, goes on there and has to
    be deleted on its own. Cancelling a task that already ended does nothing.

    Args:
        task_id (str): The id the function that started the task returned.

    Returns:
        The state of the task.
    """
    found = _call(task_manager.get, task_id, None)
    found.cancel()
    return found.summary()
