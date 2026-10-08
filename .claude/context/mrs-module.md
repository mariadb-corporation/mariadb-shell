# MRS Module (C++ port of the mrs_plugin)

Back to the index: [../PROJECT_CONTEXT.md](../PROJECT_CONTEXT.md).

Branch `wip/mrs_module`: the Python `mrs_plugin` of mariadb-shell-plugins becomes a built-in C++ module in `modules/mrs/`. The MariaDB REST Service is managed only through REST SQL statements (`CREATE REST SERVICE ...`), which the shell intercepts with its SQL handler mechanism and runs against the `mysql_rest_service_metadata` schema instead of sending them to the server. The `mrs` global object keeps only file-system functions (today `runScript`; SDK generation is still to come). The `core/` part is written so it can later be reused in a MariaDB server plugin: the grammar is bison (as `sql_yacc.yy`), and nothing in `core/` includes shell headers.

## Layout

| Path | What it is |
| --- | --- |
| `modules/mrs/core/mrs_lexer.{h,cc}` | Hand-written lexer in the style of the server's `sql_lex.cc`: keywords (X-macro `MRS_KEYWORD_LIST`), identifiers, quoted strings honouring `ANSI_QUOTES`/`NO_BACKSLASH_ESCAPES`, numbers, comments, `/a/b` request paths, `@KEY`-style annotations |
| `modules/mrs/core/mrs_parser.yy` | The bison grammar (C++ skeleton, variants, `%require 3.6`), rule for rule the ANTLR `MRSParser.g4`; `START/END OF MERGE PART` marks the rules a server merge would take |
| `modules/mrs/core/mrs_ast.h` | The statement structs the grammar builds (`std::variant` of ~50 statement types, options as `std::optional`) |
| `modules/mrs/core/mrs_parser.{h,cc}` + `mrs_parser_driver.h` | `parse_script()` / `parse_statement()`, `Parse_error` ("Syntax Error: ... [Ln 1: Col 20]" like the Python plugin), `rest_sql_prefixes()`, the token glue (`yylex`) |
| `modules/mrs/core/mrs_db_session.h` | The abstract DB access of the core (`Db_session`, buffered `Db_result`, `Db_transaction`) |
| `modules/mrs/core/mrs_sql.{h,cc}` | SQL text building: quoting, hex ids (`Id` = 16 raw bytes), `Insert`/`Update` builders; no placeholders, complete statements go to the session |
| `modules/mrs/core/mrs_json.{h,cc}` | Minimal JSON model: parse, compact dump, pretty dump in Python's `json.dumps(indent=4)` layout (used by SHOW CREATE) |
| `modules/mrs/core/mrs_metadata*.{h,cc}` | Metadata access per area: common+services+schemas (`mrs_metadata`), `_schema` (deploy/status of the metadata schema itself), `_db_objects`, `_auth`, `_content` |
| `modules/mrs/core/mrs_ddl_executor*.{h,cc}` | The executor: `execute()` dispatches a statement to `do_execute(const ast::X&, Statement_result*)`; one source file per area (services, schemas, db_objects, auth, content) |
| `modules/mrs/core/metadata/mysql_rest_service_metadata_4.1.6.sql` | The metadata schema script, embedded into the binary by `cmake/embed_file.cmake` (byte array, not a string literal: MSVC limits) |
| `modules/mrs/mod_mrs.{h,cc}` | The `mrs` global object (`Extensible_object`), help, `runScript`, and the SQL handler callback (`run_rest_sql`): parse → execute → `Custom_result_set` |
| `modules/mrs/mrs_shell_session.{h,cc}` | `Db_session` on the shell's classic session; `execute_script` uses a cloned connection (the script does `USE`) and the shell's `split_sql_stream` for DELIMITER |
| `modules/mrs/CMakeLists.txt` | `mrs_core` convenience library (BISON_TARGET + embedding); `api_modules` links it and compiles the two shell files |
| `src/mysqlsh/mysql_shell.cc` | Creates the `mrs` object, registers it as global and CLI provider, registers the SQL handler (`register_sql_handler()`, prefixes = the old `MRS_PREFIXES`) |
| `unittest/modules/mrs/mrs_lexer_t.cc`, `mrs_parser_t.cc` | gtest suites `Mrs_lexer`, `Mrs_parser` (33 tests; parses every REST statement of `unittest/data/mrs/grammar_test.sql`) |
| `unittest/data/mrs/` | The former `grammar/test` files; `grammar_test.sql` has `{{MRS_DATA_DIR}}` placeholders for the content set paths |
| `unittest/scripts/auto/py_shell/scripts/mrs_*_norecord.py` | Scripted tests: `mrs_services` (metadata, services, schemas, USE, SHOW CREATE, DUMP/LOAD, CLI, `mrs.run_script`), `mrs_grammar_test` (the `run_grammar_test.sh` port), `mrs_db_objects`, `mrs_auth`, `mrs_content` |

## Design decisions

- **Bison, not ANTLR.** `%skeleton "lalr1.cc"`, `api.value.type variant`, `api.token.constructor`, `parse.error verbose`, `%expect 0`. The only ambiguity of the ANTLR grammar (`@NOCHECK` is both a field value option and a CRUD option) is resolved by precedence towards the value option, as ANTLR's greedy matching did. Bison ≥ 3.6 is needed (all CI legs have 3.7+; `cmake/bison.cmake` still says 3.0.4 for the MySQL grammars).
- **Keywords used as names.** Data mapping keys/values may be keywords (`name: name`, `type: type`); keyword tokens carry no value, so the driver remembers the text of the last 8 tokens by position (`Driver::token_text`). Elsewhere keywords must be quoted as names (`"mike"@"MRS"`, `'MySQL'`), exactly as in the ANTLR grammar.
- **SQL modes in the lexer, not the grammar.** `ANSI_QUOTES` changes how `"..."` is unquoted (identifier rules), `NO_BACKSLASH_ESCAPES` the escapes; the grammar accepts `DOUBLE_QUOTED_TEXT` where ANTLR had mode predicates, so it is slightly more lenient (a double quoted string is accepted as text under `ANSI_QUOTES`). JSON values keep their source text (`Quoted::raw`), so JSON escapes are not SQL-unescaped — the Python plugin did `json.loads(ctx.getText())`.
- **Executor results.** `Statement_result` = the dict the Python executor appended (`message`, `id` as `0x...`, `affectedItemsCount`, columns/rows, warnings). The shell converts it to the `Custom_result_set` the Python `get_shell_result` produced; `sqlstate` is deliberately left out of error dicts (the shell appends it to the message otherwise). The executor stops at the first failing statement.
- **Per-connection state.** `USE REST SERVICE/SCHEMA` lives in `Executor_state`, kept by the `mrs` object per connection id (the Python handler's `state` dict). It is re-validated on the first statement of each executor.
- **Metadata deployment.** `CONFIGURE REST METADATA` runs the embedded 4.1.6 release script on a cloned connection when the schema does not exist. Upgrading an older schema is not implemented (clear error); the MSM project's version-to-version scripts were not ported. `mysql_tasks` is never deployed (MariaDB only).
- **SHOW CREATE output** matches the Python formats, with two deliberate additions: `ITEMS PER PAGE` (when not 25) and `COMMENT` on REST SCHEMA statements, so that a dump round-trips.
- **Area ports (db_objects, auth, content) were done by three parallel agents** against fixed header contracts; their deliberate deviations from Python are listed below under State.
- **Password hashing** for MRS users is done in the core (own SHA-256/HMAC/PBKDF2/base64, no OpenSSL), bit-identical to the Python `cypher_auth_string` (`$A$005$salt$key`, 5000 iterations).
- **Content is stored as `0x...` hex literals**, so file bytes never pass through the connection charset. Script analysis (`LOAD [TYPESCRIPT] SCRIPTS`) only stores the flags; no TypeScript parsing.
- **`disabledModules` option** (`--disable-modules=mrs`, or `shell.options.set_persist('disabledModules', 'mrs')`): the shell then creates no `mrs` object, CLI provider or `MRS` SQL handler and removes the static `mrs` help topics (`Mrs::unregister_help()`), so the Python mrs_plugin in `~/.mariadb-shell/plugins` loads cleanly for comparison runs of its old tests. Values are validated against `k_disableable_modules` in `shell_options.cc`. An empty `--disable-modules=` is rejected by the command-line parser; undo a persisted value with `\option --unset-persist disabledModules`.
- **Service lookups** keep the Python semantics: a path without developers matches only a service that is not in development; developers are compared through the metadata's `sorted_developers` column (`GROUP_CONCAT ... ORDER BY item`, so sorted case-insensitively).

## Gotchas

- An installed Python `mrs_plugin` (in `~/.mariadb-shell/plugins`) clashes with the built-in module: "An SQL Handler named 'MRS' already exists" and "Could not register plugin object 'mrs'". Test with `MARIADB_SHELL_USER_CONFIG_HOME` pointing at an empty directory.
- The `NULL` keyword cannot be an enum member (`NULL` macro); the keyword enum uses the token names (`NULL_SYMBOL`).
- The generated parser sets `yynerrs_` without reading it: `-Wno-unused-but-set-variable` on the generated file (`-Werror` build).
- **GRANT/REVOKE commit implicitly**, so `Db_transaction` cannot undo a db_object insert when its grants fail. CREATE REST VIEW/PROCEDURE/FUNCTION delete the new object again on a grant failure (`run_grants_of_new_object`); ALTER re-grants and reports a missing routine/table (1305/1146) as a warning, because objects created with FORCE may name one.
- **The metadata procedures run with the `ONLY_FULL_GROUP_BY` mode they were created with.** On MariaDB `table_columns_with_references` then returns no rows for views that group loosely (sakila `film_list`); `get_table_columns_with_references` falls back to a plain INFORMATION_SCHEMA.COLUMNS query in the session's mode. The Python plugin had the same gap.
- **`Custom_result` drops the column names of an empty result** (it only builds metadata when `data` has rows). Tests check `get_column_names()` on non-empty results only.
- **`IRow::get_string` refuses DECIMAL** (e.g. `SUM()`); `Shell_db_session` uses `get_as_string` for all non-string types.
- **`--sql -e` output without a terminal is tab separated**, not a table; scripted tests that call `testutil.call_mysqlsh` expect `a\tb` lines.
- **Option JSON text differs** between the core's compact dump (`{"a":1}`) and the server's (`{"a": 1}`); tests compare with `json.loads`.
- `shell.help('mrs')` does not work in `-e` mode for any object; use `mrs.help()` or `\? mrs`.
- A `grep` with an empty file argument reads stdin and hangs a tool call (`f=$(...)` that found nothing).
- The scripted tests need a sandbox-capable `mariadbd` on `PATH` (`/opt/homebrew/bin/mariadbd` here) and `run_unit_tests` needs `MYSQL_PORT` of a running server plus `MYSQLSH_TEST_HOME=$PWD/unittest`. A sandbox for that: `build/bin/mariadb-shell --py -e "sandbox.deploy(3360, {'password': '', 'sandboxDir': '<scratch>', 'ssl': False})"`.

## State

As of 2026-10-08, nothing committed (the user commits on request).

- **All REST SQL statements of the grammar are implemented** except DUMP/LOAD REST PROJECT and `DUMP ... TO ZIP` (clear "not supported" errors). Metadata, services, schemas, views, procedures, functions, auth apps, users, roles, grants, content sets and files work end to end against MariaDB 12.3.2.
- **Tests, all passing (49 with the help/handler suites):** gtest `Mrs_lexer`/`Mrs_parser` (33); scripted `mrs_services`, `mrs_db_objects`, `mrs_auth`, `mrs_content`, `mrs_grammar_test` (the whole `grammar_test.sql`, no "Failed to"/"Cannot" output); `shell_register_sql_handler` (py+js) and `cmd_help_norecord.py` updated for the built-in `MRS` handler and the `mrs` global. Run: `MYSQL_PORT=3360 MYSQLSH_TEST_HOME=$PWD/unittest build/bin/run_unit_tests --gtest_filter='Mrs_*:*mrs_*:*shell_register_sql_handler*:*help_norecord*'`.
- **Deliberate deviations from Python** (fixes of Python bugs or added behaviour): ALTER REST AUTH APP implemented (Python had none); reference options stored per reference (Python stored the parent's); CREATE REST VIEW on a missing table fails; DROP/ALTER only act on their own object kind; ALTER VIEW CLASS without mapping keeps the fields; IDENTIFIED BY only for users with a password; ALTER REST USER honours MERGE OPTIONS; duplicate CONTENT FILE paths rejected (no unique index in the schema); routine PARAMETERS/RESULT always print braces (`{}` when empty) so dumps parse; LOAD REST SERVICE without AS names the dump's first service; copy-paste typos in Python messages fixed.
- **Not done:** SDK generation (`mrs` file-system functions beyond `runScript`), DUMP/LOAD REST PROJECT, ZIP dumps, metadata schema upgrades, MRS script (TypeScript) analysis, `${openApiUi}` content download, a JS-mode test, a review/`/simplify` pass of the agent-written area files.

## Next steps

1. Review pass over `mrs_metadata_{db_objects,auth,content}.cc` and their executors (written by agents); deduplicate `expand_user_path` (3 copies) and the base64 decoders (`mrs_sql.cc` and `mrs_metadata_content.cc`).
2. Commit on `wip/mrs_module` when the user asks; CI needs bison ≥ 3.6 on every leg (check Windows).
3. Port SDK generation as `mrs` file-system functions.
4. DUMP/LOAD REST PROJECT, then metadata upgrades from older schema versions.
5. Factor `core/` for the server plugin: a `Db_session` over the server's internal API.
