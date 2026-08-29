#@<> Setup
def call_mysqlsh(command_line_args, stdin=""):
    testutil.call_mysqlsh(["--quiet-start=2"] + command_line_args, stdin, [
                          "MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])

shell.connect(__mysqluripwd)

session.run_sql("DROP SCHEMA IF EXISTS inline_sql_formatting")
session.run_sql("CREATE SCHEMA inline_sql_formatting")
session.run_sql("""CREATE TABLE inline_sql_formatting.employees (
  id INT PRIMARY KEY,
  name VARCHAR(40),
  role VARCHAR(20),
  salary DECIMAL(10,2),
  hired DATE
)""")

for employee in [
    [1, "Ada Lovelace", "engineer", "120000.00", "2021-03-01"],
    [2, "Alan Turing", "engineer", "135000.00", "2019-07-15"],
    [3, "Grace Hopper", "manager", "150000.00", "2018-01-09"],
    [4, "Edsger Dijkstra", "engineer", "128000.50", "2022-11-30"],
    [5, "Barbara Liskov", "architect", "145000.25", "2020-05-20"],
]:
    session.run_sql("INSERT INTO inline_sql_formatting.employees VALUES (?, ?, ?, ?, ?)", employee)

query = "SELECT * FROM inline_sql_formatting.employees"

#@<OUT> Default output
call_mysqlsh([__mysqluripwd, "-i", "-e", query])

#@<OUT> Vertical output
call_mysqlsh([__mysqluripwd, "-i", "-e", f"{query}\\G"])

#@<OUT> Tabbed output
call_mysqlsh([__mysqluripwd, "-i", "-e", f"{query}\\GT"])

#@<OUT> Tabbed output without headers
call_mysqlsh([__mysqluripwd, "-i", "-e", f"{query}\\Gt"])

#@<OUT> Full JSON Output
call_mysqlsh([__mysqluripwd, "-i", "-e", f"{query}\\GJ"])

#@<OUT> Record List JSON Output
call_mysqlsh([__mysqluripwd, "-i", "-e", f"{query}\\Gj"])

#@<OUT> Full JSON Output honors --json=pretty [USE:Full JSON Output]
call_mysqlsh([__mysqluripwd, "--json=pretty", "-i", "-e", f"{query}\\GJ"])

#@<OUT> Record List JSON Output honors --json=pretty [USE:Record List JSON Output]
call_mysqlsh([__mysqluripwd, "--json=pretty", "-i", "-e", f"{query}\\Gj"])

#@<OUT> Full JSON Output honors --json=raw
call_mysqlsh([__mysqluripwd, "--json=raw", "-i", "-e", f"{query}\\GJ"])

#@<OUT> Record List JSON Output honors --json=raw
call_mysqlsh([__mysqluripwd, "--json=raw", "-i", "-e", f"{query}\\Gj"])

# The tabbed formats are not JSON, so they can not override the JSON wrapping,
# the --json option wins and the format letter is ignored
#@<OUT> Tabbed output is ignored while --json is enabled
call_mysqlsh([__mysqluripwd, "--json=raw", "-i", "-e", f"{query}\\GT"])

#@<OUT> Tabbed output without headers is ignored while --json is enabled [USE:Tabbed output is ignored while --json is enabled]
call_mysqlsh([__mysqluripwd, "--json=raw", "-i", "-e", f"{query}\\Gt"])

# The statement delimiters are also handled when the input is a script rather
# than an interactive session
#@<OUT> Batch execution honors the format
call_mysqlsh([__mysqluripwd, "--sql", "-e", f"{query}\\GT"])

#@<OUT> Script read from stdin honors the format
call_mysqlsh([__mysqluripwd, "--sql"], f"{query}\\GT\n")

#@<OUT> A \sql one liner honors the format
call_mysqlsh([__mysqluripwd, "--py", "-i", "-e", f"\\sql {query}\\Gt"])

# The format letter belongs to the delimiter, so it is consumed with it
#@<OUT> The format letter is not left behind for the next statement
call_mysqlsh([__mysqluripwd, "--sql", "-i", "-e",
              "SELECT 1 AS first\\GTSELECT 2 AS second;"])

#@<OUT> A letter which is not a format starts the next statement
call_mysqlsh([__mysqluripwd, "--sql", "-i", "-e",
              "SELECT 1 AS first\\GSELECT 2 AS second;"])

#@<> The delimiter does not modify the configured options
call_mysqlsh([__mysqluripwd, "--py", "-i", "-e",
              f"\\sql {query}\\Gt\nprint(shell.options.resultFormat)"])
EXPECT_STDOUT_CONTAINS("table")

#@<> TearDown
session.run_sql("DROP SCHEMA IF EXISTS inline_sql_formatting")
