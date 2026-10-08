# Slash Prefix for Shell Commands

Back to the index: [../PROJECT_CONTEXT.md](../PROJECT_CONTEXT.md). Docs side: [docs-ref.md](docs-ref.md).

Branch `wip/slash-commands`: shell commands also run with a `/` prefix (`/quit`, `/status`, `/connect`, `/sql`, `/?`), under the `slashCommands` option (bool, default `true`). Motivation: AI harnesses all use `/quit`, `/help`, so `/` instead of `\` is an easy slip.

## Behaviour (as implemented)

- A line is a `/command` only when the word after `/` is a shell command of the current mode **and** the line starts a new statement (`_input_buffer` empty, `Input_state::Ok`). Leading whitespace is allowed, as for `\`.
- Everything else starting with `/` goes to SQL/Python unchanged: `/* */`, `/*M! */`, a lone `/` (DELIMITER), continuation lines, unknown `/word` (no "Unknown command" message; the language reports the error).
- `\G`, `\g`, `\w`, `\W` inside a statement, and the `\` multi-line command, have no `/` form.
- History keeps what was typed (`/sql …`); filtering, syslog and completion treat it as the `\command`. `/help /quit` shows `\quit` help.
- The JSON front-end shell (`MARIADB_SHELL_JSON_SHELL`) also accepts `/commands`. Excluding it was tried and reverted: nothing downstream uses that mode, `\commands` already run there, and the risk (a sent line starting `/<command>`) is narrow.

## Design

- **One conversion point:** `Mysql_shell::process_line` calls `slash_command_as_backslash()` once; `do_shell_command` gets the `\` form. `Base_shell::notify_executed_statement(line, command)` puts it in the notification's `"command"` key; `Command_line_shell::handle_notification` uses it for the `\sql` / `sql_safe_for_logging` check and syslog. It does not convert again after the command ran, because mode and options may have changed by then.
- **One gate:** `Mysql_shell::slash_command_allowed()` = option on && `_input_buffer.empty()`. Used by the conversion, `Shell_command_provider` name completion (`complete_slash_command`), and `auto_complete_start_cb` (`/source`, `/.` path completion, which needs a space or end after the name).
- `Shell_command_handler::find_command()` is shared by `has_command()` (used by `Shell_core::is_shell_command`) and `process()`, so accepting and running a `/command` can't drift.
- `Shell_core::handle_shell_command` now returns true when the language's own handler ran the command (it returned false before). In practice only `Shell_sql` registers `\G`/`\g`, and with no function, so this did not change behaviour. It let both prefixes share one path in `do_shell_command`.
- `\sql` accepts any whitespace after its name, both in the command (`find_first_of(" \t\r\n\v\f")`) and in the history filter. Previously `\sql<TAB>SET PASSWORD…` was dropped and saved unfiltered.

## Files

- `src/mysqlsh/mysql_shell.{h,cc}`: `slash_command_as_backslash`, `slash_command_allowed`, `process_line`, `do_shell_command`, `cmd_print_shell_help`, `\sql` handler, `Shell_command_provider`
- `src/mysqlsh/cmdline_shell.cc`: `handle_notification`, `auto_complete`, `auto_complete_start_cb`
- `mysqlshdk/shellcore/shell_core.cc`: `first_word`, `find_command`, `has_command`, `is_shell_command`, `COMMANDS_DETAIL1` help
- `mysqlshdk/shellcore/base_shell.cc`: notification `"command"` key
- `mysqlshdk/shellcore/shell_options.cc` and `modules/mod_shell_options.cc`: the option and its help
- Tests: `interactive_shell_t.cc` (`slash_commands`), `completion_frontend_t.cc` (`builtin_slash`), `shell_history_t.cc` (`check_password_history_linenoise`), and the `*_help_norecord` / `options_help` / `shell_options_persists` validation files
- Docs: `README.md`; `shell-commands.md` § The Slash Prefix; `configuration-options.md`; `differences-from-mysql-shell.md`; `autocompletion.md`; `editing-and-history.md`

## State (2026-10-07)

- 4 commits rebased on `main` `0af379c0a`; unit suites `Interactive_shell_test`, `Completer_frontend`, `Json_shell`, `Shell_history` and `Shell_core_test` pass (MariaDB 12.3.2). Scripted help validations were not re-run after the rebase.
- `/simplify`, `/code-review` and `/security-review` were all run. The security review found nothing: `/` is a strict alias of `\` under the same statement-start conditions.
- **Not pushed.** `origin/wip/slash-commands` holds an older copy of commit 1 plus Mike Zinner's no-change merge of `main` (`b15014c29`). The rebased branch covers both, so the push needs `--force-with-lease`; tell Mike.

## Next steps

1. Push with `--force-with-lease`, then open the PR.
2. Done separately: `\connect` / `/connect` lines go to history without their password. `cmd_connect` hands history the arguments as its option parser left them (the parser removes URI passwords and stars password options in place), via `set_history_arguments()` / `keep_out_of_history()` and the `"history"` / `"temporary"` notification keys; a shell command line the shell rejects (`process_line`'s catch) is temporary whatever the command.

## Gotchas

- **Known, left as is:**
  - In SQL mode, syslog logs `/source f` as `\source f`, the command that ran.
  - Tab doesn't complete `  /co` or `  \co`; leading whitespace blocks completion for both prefixes.
  - `auto_complete`'s argument completion still splits on `' '` only.
- **Python heredoc edits of C++ code:** `\\\\` in a `<<'EOF'` Python string becomes `\\` in comments. Comments got doubled backslashes three times; check with grep after each edit.
