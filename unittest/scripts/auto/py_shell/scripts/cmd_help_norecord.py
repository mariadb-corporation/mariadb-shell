#@ Generic Help
# call shell with disabled builtin plugins to prevent them from interfering with the output
testutil.call_mysqlsh(["--disable-builtin-plugins", "--interactive=full", "--py", "--execute", "\\?"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])

#@ Generic Help - plugins plugin
\?

#@ Help Contents
\? contents

#@ Generic Help in SQL mode
\sql
\?
\py

#@ Help Contents in SQL mode
\sql
\? contents
\py

#@ Help on Admin API Category {__have_admin_api}
\? adminapi

#@ Help on shell commands
\? shell commands

#@ Help on ShellAPI Category
\? shellapi

#@ Help on X DevAPI Category {__have_x_protocol}
\? x devapi

#@ Help on unknown topic
\? unknown

#@ Help on topic with several matches {__have_x_protocol}
\? session

#@ Help on topic with several matches {__mariadb_build}
\? delete*

#@ Help for sandbox operations
\? *sandbox*

#@ Help for SQL, with classic session, multiple matches {not __mariadb_build}
shell.connect(__mysqluripwd)
\? select

#@ Help for SQL, with classic session, multiple matches {__mariadb_build}
shell.connect(__mysqluripwd)
\? delete


#@ Switching to SQL mode, same test gives results mysql {sandbox.vendor() == "MySQL"}
\sql
\? select
\py

#@ Switching to SQL mode, same test gives results mariadb {sandbox.vendor() == "MariaDB"}
\sql
\? delete
\py

#@ Switching back to Python, help for SQL Syntax
\py
\? SQL Syntax
session.close()

#@ Help for SQL Syntax, with x session {__have_x_protocol}
shell.connect(__uripwd)
\? SQL Syntax
session.close()

#@ Help for SQL Syntax, no connection
\? SQL Syntax

#@ Help for API Command Line Integration
\? cmdline

#@ Help for API Command Line Integration (full name) [USE: Help for API Command Line Integration]
\? command line
