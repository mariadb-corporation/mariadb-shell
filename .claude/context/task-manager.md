# Background tasks: `mysqlsh.task_manager` and the `task` global object

Part of [../PROJECT_CONTEXT.md](../PROJECT_CONTEXT.md).

## What it is

- `python/packages/mysqlsh/task_manager.py` is the one task library for plugin work that outlives the call that starts it (a dump or load run by the MCP server, a cloud deployment by the dba plugin). Starting work returns a task id at once; the caller follows the task by id (state, stage progress, messages since a `since` cursor) or cancels it. It is in `mysqlsh` because bundled plugins are not on `sys.path`, so a plugin cannot import another plugin's code.
- `python/plugins/task_plugin/init.py` is the `task` global object for the shell's own tasks: `task.list([taskType])`, `task.get(taskId[, since][, waitMs])`, `task.cancel(taskId)`. Not on the CLI: a task lives in the process that started it.
- The MCP plugin in the mariadb-shell-plugins repository is the other consumer: `mcp_plugin/lib/tasks.py` wraps the library (owner = the MCP client, `TaskError` -> tool error), `lib/util_process.py` feeds a utility subprocess's events into a task, `lib/task_functions.py` serves `task.get`-style tools. Its unit tests (`mcp_plugin/tests/unit/test_util.py`) call the library directly.

## Design decisions

- **Owner** keeps callers apart: an MCP client for the server's tasks, `None` for the shell's own. `get()` reports another owner's task as nonexistent. `at_exit()` only ever waits for ownerless tasks.
- **A task is listed, and holds one of its owner's `max_running` slots per type, from `Task.start()` on**, not from `create()`; `create()` still refuses one too many early. A task created and never started (its plugin failing in between) is forgotten with its object. Code that creates a task and expects it in `list_tasks()`/`get()` before starting it is wrong by design.
- **Camel-case events** (`stageStarted`, `progress` with `etaSeconds`/`totalIsApproximate`/`totalKnown`, `stageFinished`) are the format the shell's own progress reporting emits (`mysqlshdk/libs/textui/text_progress.cc`) and the MCP util runner forwards through `Task.on_event()`. `message()`/`stage_started()`/`progress()`/`stage_finished()` are the typed way to the same state. Keep both.
- **Finished tasks** are kept `FINISHED_TASK_LIFETIME` seconds and at most `MAX_FINISHED_TASKS`; messages per task `MAX_MESSAGES` (the snapshot says how many were dropped). A finished task drops its canceller so it pins nothing (a process, a session) for that hour.
- **`wait_for_news()` wakes on any change**, progress included, not only on a message or the end. That is deliberate for now; a client polling with `waitMs` during a chatty dump returns at the progress rate.
- **`TaskError` is a plain `Exception`**, not `mysqlsh.Error`: `str(e)` must stay the bare message for the MCP tool errors. Each front end translates it (`task_plugin/init.py` `_call`, `mcp_plugin/lib/tasks.py`). A failed `Task.start()` (no thread) raises `TaskError` too; the task ends FAILED (CANCELLED if a cancel came in), without a `started_at`.
- **Timestamps** end in `Z`, as the MCP plugin's other answers and JavaScript write them.

## The command line and the end of the process

- `main.cc` stores the function a command-line call runs (`mariadb-shell -- dba deploy vendor object ...`) in `mysqlsh.task_manager.cli_function`, told by a `Shell_cli_operation` observer. `task_manager.answer(task, "object.function")` returns `{task_id, status}` at once, unless the function is that very call: then it follows the task to its end (`follow()`: messages to stderr, result returned and printed by the shell, error raised -> exit code). A function another one calls (an MCP tool under `mcp start-server`) is no command-line call.
- `main.cc` calls `mysqlsh.task_manager.at_exit()` right after the script, `-e` stream, stdin stream or interactive session ends, **while the shell is whole**: a script waits for the shell's own running tasks, writing their messages (Ctrl+C stops the waiting); an interactive session only says which tasks stop with it. `at_exit()` is also registered with Python's `atexit`, for a `sys.exit()` that skips main's cleanup, and does nothing the second time.
- Ctrl+C in Python code the main thread runs is one trigger, `Python_context::keyboard_interrupt()` (`PyErr_SetInterrupt()` plus the Windows `SetEvent(_PyOS_SigintEvent())`), used by `Shell_python::interrupt()` for scripts and by `main.cc`'s `Interrupt_handler` around a CLI call and the exit wait. Python's SIGINT callback is armed once at init (`Python_init_singleton`); the shell's own C handler stays installed.
- The interrupt helper thread is stopped by an `atexit` handler of `interrupt_handler.cc`, because a script's `sys.exit()` ends the process inside `PyErr_Print()` and never reaches `global_end()`; without it `my_end()` waited five seconds and printed "Error in my_thread_global_end(): 1 threads didn't exit".

## Tests

- `python/plugins/task_plugin/runtests.py` runs the pytest tests of the library: `python3 runtests.py [-k name]` (needs pytest) or `mariadb-shell --py -f runtests.py`. Inside the shell, `test/conftest.py` must drop the copy the shell already imported (its `task_plugin` loads the library at startup) from `sys.modules` and from the `mysqlsh` package, or the tests run the build's copy; keep that when touching the conftest.
- `unittest/shell_cmdline_regressions_t.cc`: `Command_line_test.background_tasks_are_waited_for_at_exit` covers the exit wait end to end. `run_unit_tests` needs a server at startup; run it through `scripts/run_unit_tests.py` (what `npm run rut` does).
- Changing anything the MCP plugin uses (`on_event`, `set_canceller`, `fail_running_stages`, `snapshot`/`summary`/`wait_for_news`, `version`/`wait_for_change`, `MAX_WAIT_MS`, `_progress_of`, the module constants) needs its tests run against the edited library: put `python/packages` first on `mysqlsh.__path__`, drop the cached `mysqlsh.task_manager` as the conftest does, then `pytest mcp_plugin/tests/unit/test_util.py tests/unit/test_dba.py`.

## Next steps

- mariadb-shell-plugins: `test_a_task_belongs_to_its_client` and `test_running_tasks_are_limited_per_client` in `mcp_plugin/tests/unit/test_util.py` create tasks without starting them and expect them listed and counted; they need a `.start(...)`. The production code (`util_functions.py` creates and starts in one go) is unaffected.
- The dba plugin (cloud deployments) is the next consumer, through `answer()` for its CLI calls.
- Document the `task` global object and `mysqlsh.task_manager` for plugin authors; see [docs-ref.md](docs-ref.md).
- `python/plugins/sandbox/runtests.py` takes an option's value (`-k name`) for a test path, the flaw fixed in `task_plugin/runtests.py`.
