# Branch `wip/util_any_session`: util on any session, with a progress callback

Started 2026-10-10 from `main` (`8dfdd5ba3`) for mariadb-shell-plugins' MCP
`util` function group (branch `wip/mcp_dump_load` there), which runs dumps and
loads as background tasks with progress bars in the VS Code extension.

## What changed

Two options on `common::Common_options`, so every dump, load, copy, export and
import utility has them (`dumpInstance/Schemas/Tables`, `exportTable`,
`loadDump`, `importTable`, `copyInstance/Schemas/Tables`):

- `session`: a `ShellBaseSession` used in place of the global one
  (`Common_options::given_session()`, `Util::session_for(options)`). Copy
  takes it from its dump options; the target is still `connectionData`.
- `progressCallback`: a function called with one dictionary per event, in
  place of printing: `message` (level, text), `stageStarted`, `progress`
  (current, total, plus throughput/etaSeconds/items/totalIsApproximate or
  totalKnown), `stageFinished` (seconds). Returning `"cancel"` or `True` stops
  the utility as ^C does.

Both are `Option_scope::CLI_DISABLED` (objects, which a command line cannot
give).

## How it works

- `modules/util/common/progress_callback.{h,cc}`:
  - `Progress_callback_console`, an `IConsole` that turns prints into
    `message` events; prompts and the pager go to the console it replaced.
  - `current_progress_callback()` finds it through any `Console_with_progress`
    in front of it (new `Console_with_progress::console()`).
  - `Utility_scope`, opened at the top of every util function before the
    progress display and the interrupt handler exist.
- The console carries the callback to every thread: `spawn_scoped_thread`
  copies the caller's console, interrupts and options into the progress thread
  and the dump/load workers, including copy's.
- `Progress_thread` takes the callback when constructed, forces
  `showProgress` off, and gives it to every stage. `Stage::report()` sends
  stageStarted (in `start()`), progress (each 250 ms display tick) and, once
  the display loop ends, the final progress values followed by stageFinished,
  both from the display thread so that the order holds however the stage was
  finished (by the utility, by a counting stage reaching its total, or at
  shutdown). `Stage::progress()`: `Numeric_progress` keeps its last values
  under a mutex; `Throughput_progress` uses the new
  `Base_progress::snapshot()`.
- `Progress_callback_console` remembers the thread running the callback: what
  the callback itself prints (a Python `print()` lands on the current console,
  which is this one) is forwarded to the console it replaced instead of being
  handed back to it, which would deadlock on its mutex. A cancel that nothing
  took (`Interrupts::interrupt()` now returns whether a handler ran; it drops
  the request when busy or when no handler is registered yet) is asked again
  on the next event.
- Cancel: `Interrupts` only registers handlers pushed by the thread that
  created it (`in_creator_thread()`, now public). A utility called from
  another thread gets a fresh `Interrupts::create(nullptr)` pushed by
  `Utility_scope`; the callback's "cancel" calls its `interrupt()`. On the main
  thread the shell's own `Interrupts` is used, so ^C still works there.
- Threads a scripting language created have no mysys thread state, and mysys
  error paths segfault without it. `Mysys_thread_guard` (new, in
  `mysys_thread.h`) sets it up only where it is missing and ends only what it
  set up. `Utility_scope` holds one.

## Gotchas

- **An `Interrupts` object's helper thread keeps it alive.** The thread is
  spawned while that object is the current one, so `spawn_scoped_thread`
  hands it a reference. `~Utility_scope` pops it and calls
  `stop_background_thread()`, or my_end reports "N threads didn't exit".
- `mysqlsh::dump` has a nested `common` namespace: write
  `mysqlsh::common::` in `modules/util/dump`.
- **`util.dumpBinlogs()` / `util.loadBinlogs()` accept `session` and
  `progressCallback` but ignore them.** Their option packs include
  `Common_options`, yet they still run on `global_session()` with no
  `Utility_scope`, and their help does not list the options. They are
  MySQL-only (`HAVE_BINLOG_UTILS`), so neither this machine nor CI can build
  them; fixing it needs a MySQL build to compile and test against.
- Scripted tests run line by line: a blank line inside a `def` ends it.
- The help validations: `util_help_norecord.py` was merged from the shell's
  output (`?{...}` blocks of other builds kept). The js file was patched the
  same way by hand, because this build has no JS; its NAME briefs wrap at 79
  columns.
- A stale `build/share/.../plugins/debug/init.py` (from before `5c450ef3e`)
  makes `util_help_norecord` fail on the `util.debug` chunks until a full
  `ninja` copies the plugins again.

## Tests

- `unittest/scripts/auto/py_shell/scripts/util_session_and_progress_norecord.py`,
  on two sandboxes and with no global session:
  - every function on a given session from a Python thread;
  - the event types and stage order;
  - final values;
  - cancel from a thread and from the main thread, for a dump and a load;
  - a failing callback;
  - a closed session and wrong types are refused;
  - the CLI refuses `--session`.
- 45 tests passed on 2026-10-10 (about 25 min, the copy suites take 2 to 6 min
  each): help validations, the new test, `util_dump_and_load_mariadb`,
  `util_copy_*`, `util_dump_schemas/tables`, `util_export_table`,
  `util_import_table*`, `util_load_dump_trx`, `Dump_*`, `Load_*`,
  `Interrupt*`.

## Docs

`docs-ref`:
- `dump-utilities.md` § Running on Your Own Session: the events and a Python
  thread example.
- `session`/`progressCallback` rows and session wording in the load, copy and
  table export/import pages.
- `npm run build` is clean.
