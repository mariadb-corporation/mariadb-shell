#@<> Warnins on unknown loose options
testutil.call_mysqlsh(["--loose-whatever", "--py", "-e", "print('done')"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("WARNING: unknown option: --loose-whatever")
WIPE_OUTPUT()

#@<> Warnins on unknown loose options with --quiet-start=1
testutil.call_mysqlsh(["--quiet-start=1", "--loose-whatever", "--py", "-e", "print('done')"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("WARNING: unknown option: --loose-whatever")
WIPE_OUTPUT()

#@<> No warnins on unknown loose options with --quiet-start=2
testutil.call_mysqlsh(["--quiet-start=2", "--loose-whatever", "--py", "-e", "print('done')"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_NOT_CONTAINS("WARNING: unknown option: --loose-whatever")
WIPE_OUTPUT()

#@<> Existing options can be prefixed with --loose
testutil.call_mysqlsh(["--loose-quiet-start=1", "--loose-whatever", "--py", "-e", "print('done')"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_NOT_CONTAINS("WARNING: unknown option: --loose-quiet-start")
EXPECT_STDOUT_CONTAINS("WARNING: unknown option: --loose-whatever")
WIPE_OUTPUT()

#@<> Unknown loose option with URI value as --loose-option=URI ignores value
testutil.call_mysqlsh([f"--loose-whatever={__mysqluripwd}", "--py", "-e", "print(session)"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS(f"WARNING: unknown option: --loose-whatever={__mysqluripwd}")
EXPECT_STDOUT_NOT_CONTAINS(f"<ClassicSession:{__mysql_uri}>")
EXPECT_STDOUT_CONTAINS("None")
WIPE_OUTPUT()

#@<> Unknown loose option with URI value as --loose-option URI uses value
testutil.call_mysqlsh([f"--loose-whatever", __mysqluripwd, "--py", "-e", "print(session)"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("WARNING: unknown option: --loose-whatever")
EXPECT_STDOUT_CONTAINS(f"<ClassicSession:{__mysql_uri}>")
EXPECT_STDOUT_NOT_CONTAINS("None")
WIPE_OUTPUT()

#@<> Option after a loose one is not loose itself
testutil.call_mysqlsh(["--loose-whatever", "--whatever-else", "--py", "-e", "print('done')"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS("unknown option --whatever-else")
EXPECT_STDOUT_NOT_CONTAINS("done")
WIPE_OUTPUT()

#@<> prep options-file helper
def run_with_cnf(cnf, code="print('done')", args=None):
    testutil.create_file("my_options.cnf", cnf)
    testutil.call_mysqlsh(["--defaults-file=my_options.cnf"] + (args or []) + ["--py", "-e", code], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
    testutil.rmfile("my_options.cnf")

def connect_with_cnf(cnf, args=None):
    run_with_cnf(cnf, "print('connected:', session.is_open())", (args or []) + [__mysqluripwd])

client_log_path = os.path.join(__tmp_dir, "client_options.log")

def run_with_cnf_and_log(cnf, args=None):
    """Runs the shell with its own log file and returns that log's content."""
    run_with_cnf(cnf, args=[f"--log-file={client_log_path}"] + (args or []))
    with open(client_log_path, encoding="utf-8") as f:
        log = f.read()
    os.remove(client_log_path)
    return log

#@<> Warns unsupported loose options from the shell's groups of the options file
for group in ["mariadb-shell", "mysqlsh"]:
    run_with_cnf(f"[{group}]\nloose-unknown=whatever\n")
    EXPECT_STDOUT_CONTAINS("WARNING: unknown option: --loose-unknown")
    EXPECT_STDOUT_CONTAINS("done")
    WIPE_OUTPUT()

#@<> Unsupported options in [client] of the options file are skipped with a warning
# [client] is read by every client program; e.g. Gentoo's stock config sets
# character-sets-dir and loose-default-character-set there. The console points
# to the log, which lists them; a loose- one is skipped without a mention.
client_cnf = """[client]
character-sets-dir=/usr/share/mariadb/charsets
loose-default-character-set=utf8
plugin_dir=/usr/lib/mariadb/plugin
skip-unknown-flag
character-sets-dir=/usr/share/mariadb/charsets
"""
ignored_client_options = "These options from the [client] group of the option files are not supported by MariaDB Shell and were ignored: --character-sets-dir, --plugin-dir, --skip-unknown-flag. To avoid this warning, prefix them with loose- in the option file (e.g. loose-character-sets-dir)."
log = run_with_cnf_and_log(client_cnf)
EXPECT_STDOUT_NOT_CONTAINS("unknown option")
EXPECT_STDOUT_CONTAINS(f"WARNING: Some options from the [client] group of the option files are not supported by MariaDB Shell and were ignored. See the log file for details: {client_log_path}")
EXPECT_STDOUT_NOT_CONTAINS("character-sets-dir")
EXPECT_STDOUT_CONTAINS("done")
# Listed once, although character-sets-dir is set twice.
EXPECT_EQ(1, log.count(f"Warning: {ignored_client_options}"), log)
EXPECT_FALSE("default-character-set" in log, log)
WIPE_OUTPUT()

#@<> With logging off, the console warning lists the [client] options itself
run_with_cnf(client_cnf, args=["--log-level=none"])
EXPECT_STDOUT_CONTAINS(f"WARNING: {ignored_client_options}")
EXPECT_STDOUT_CONTAINS("done")
WIPE_OUTPUT()

#@<> No console warning on unsupported [client] options with --quiet-start=2, the log still lists them
log = run_with_cnf_and_log(client_cnf, args=["--quiet-start=2"])
EXPECT_STDOUT_NOT_CONTAINS("WARNING")
EXPECT_STDOUT_CONTAINS("done")
EXPECT_EQ(1, log.count(f"Warning: {ignored_client_options}"), log)
EXPECT_FALSE("See the log file for details" in log, log)
WIPE_OUTPUT()

#@<> Unsupported loose- options in [client] are skipped quietly, also in the log
log = run_with_cnf_and_log("""[client]
loose-character-sets-dir=/usr/share/mariadb/charsets
loose-default-character-set=utf8
""")
EXPECT_STDOUT_NOT_CONTAINS("WARNING")
EXPECT_STDOUT_CONTAINS("done")
EXPECT_FALSE("[client] group" in log, log)
EXPECT_FALSE("character-set" in log, log)
WIPE_OUTPUT()

#@<> Unsupported options in the shell's groups of the options file still fail
for group in ["mariadb-shell", "mysqlsh"]:
    run_with_cnf(f"[{group}]\ncharacter-sets-dir=/usr/share/mariadb/charsets\n")
    EXPECT_STDOUT_CONTAINS("unknown option --character-sets-dir")
    EXPECT_STDOUT_NOT_CONTAINS("done")
    WIPE_OUTPUT()

#@<> Unsupported option in [client] and in the shell's group still fails
run_with_cnf("""[client]
character-sets-dir=/usr/share/mariadb/charsets
[mariadb-shell]
character_sets_dir=/usr/share/mariadb/charsets
""")
EXPECT_STDOUT_CONTAINS("unknown option --character-sets-dir")
EXPECT_STDOUT_NOT_CONTAINS("done")
WIPE_OUTPUT()

#@<> Supported options in [client] are still applied
run_with_cnf("""[client]
character-sets-dir=/usr/share/mariadb/charsets
column-type-info
loose-unknown=whatever
""", "print('column type info:', shell.options['showColumnTypeInfo'])")
EXPECT_STDOUT_NOT_CONTAINS("unknown option")
EXPECT_STDOUT_CONTAINS("column type info: True")
WIPE_OUTPUT()

#@<> ssl-verify-server-cert verifies the server's certificate
# It is --ssl-mode=VERIFY_IDENTITY, which the sandbox's self-signed certificate
# fails.
for cnf in ["[client]\nssl-verify-server-cert\n", "[client]\nssl_verify_server_cert=ON\n", "[client]\nloose-ssl-verify-server-cert=1\n", "[mariadb-shell]\nenable-ssl-verify-server-cert\n"]:
    connect_with_cnf(cnf)
    EXPECT_STDOUT_NOT_CONTAINS("unknown option")
    EXPECT_STDOUT_NOT_CONTAINS("connected: True")
    EXPECT_STDOUT_CONTAINS("Error 2026 (HY000)")  # CR_SSL_CONNECTION_ERROR
    WIPE_OUTPUT()

connect_with_cnf("", ["--ssl-verify-server-cert"])
EXPECT_STDOUT_CONTAINS("Error 2026 (HY000)")
WIPE_OUTPUT()

#@<> A later --ssl-mode wins over ssl-verify-server-cert
connect_with_cnf("[client]\nssl-verify-server-cert\n", ["--ssl-mode=DISABLED"])
EXPECT_STDOUT_CONTAINS("connected: True")
WIPE_OUTPUT()

#@<> ssl-verify-server-cert turned off leaves the SSL mode alone
for entry in ["skip-ssl-verify-server-cert", "disable-ssl-verify-server-cert", "ssl-verify-server-cert=0", "ssl-verify-server-cert=off", "ssl-verify-server-cert\nskip-ssl-verify-server-cert"]:
    connect_with_cnf(f"[client]\n{entry}\n")
    EXPECT_STDOUT_NOT_CONTAINS("unknown option")
    EXPECT_STDOUT_CONTAINS("connected: True")
    WIPE_OUTPUT()

#@<> ssl-verify-server-cert turned off restores the SSL mode it replaced
# VERIFY_CA fails on the sandbox's self-signed certificate; cleared, it would
# connect.
connect_with_cnf("[client]\nssl-mode=VERIFY_CA\nssl-verify-server-cert\nskip-ssl-verify-server-cert\n")
EXPECT_STDOUT_CONTAINS("Error 2026 (HY000)")
WIPE_OUTPUT()
