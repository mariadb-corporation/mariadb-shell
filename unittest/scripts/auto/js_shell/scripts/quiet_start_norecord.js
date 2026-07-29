//@ Normal shell call
testutil.callMysqlsh(["-i", "--js", "-e", "1", "--uri", __uripwd + "/mysql"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])

//@ Using --quiet-start
testutil.callMysqlsh(["-i", "--js", "-e", "1", "--quiet-start", "--uri", __uripwd + "/mysql"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])

//@ Using --quiet-start=1
testutil.callMysqlsh(["-i", "--js", "-e", "1", "--quiet-start=1", "--uri", __uripwd + "/mysql"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])

//@ Using --quiet-start=2
testutil.callMysqlsh(["-i", "--js", "-e", "1", "--quiet-start=2", "--uri", __uripwd + "/mysql"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])

//@ Normal shell call with error
testutil.callMysqlsh(["-i", "--js", "-e", "1", "--uri", __uripwd + "/unexisting"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])

//@ Using --quiet-start with error
testutil.callMysqlsh(["-i", "--js", "-e", "1", "--quiet-start", "--uri", __uripwd + "/unexisting"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])

//@ Using --quiet-start=1 with error
testutil.callMysqlsh(["-i", "--js", "-e", "1", "--quiet-start=1", "--uri", __uripwd + "/unexisting"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])

//@ Using --quiet-start=2 with error
testutil.callMysqlsh(["-i", "--js", "-e", "1", "--quiet-start=2", "--uri", __uripwd + "/unexisting"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
