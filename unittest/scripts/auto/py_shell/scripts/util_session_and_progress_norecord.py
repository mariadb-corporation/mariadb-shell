# The 'session' and 'progressCallback' options of the dump, load, import,
# export and copy utilities: run on a session other than the global one, from
# a thread other than the main one, report to a callback in place of printing,
# and stop when the callback asks for it.

#@<> INCLUDE dump_utils.inc

#@<> Setup
import os
import os.path
import threading

outdir = os.path.join(__tmp_dir, "ldtest_session_progress")
wipe_dir(outdir)
testutil.mkdir(outdir)

for port in [__mysql_sandbox_port1, __mysql_sandbox_port2]:
    testutil.deploy_sandbox(port, "root", {"local_infile": "1"})

# neither is the global session
session1 = shell.open_session(__sandbox_uri1)
session2 = shell.open_session(__sandbox_uri2)

session1.run_sql("CREATE SCHEMA src")
session1.run_sql("CREATE TABLE src.t (id INT PRIMARY KEY AUTO_INCREMENT, v VARCHAR(100))")
session1.run_sql("INSERT INTO src.t (v) SELECT REPEAT('x', 100) FROM src.seq_1_to_50000")

def dump_dir_for(name):
    path = os.path.join(outdir, name)
    wipe_dir(path)
    return path

def count(session, table):
    return session.run_sql(f"SELECT COUNT(*) FROM {table}").fetch_one()[0]

def run_in_thread(fn, *args, answer=None):
    """Runs a utility in a thread of its own, with the last argument as its
    options plus session1 and a callback, and returns the events and the error,
    if any."""
    events = []
    error = []
    def callback(event):
        events.append(dict(event))
        return answer(event) if answer else None
    def work():
        try:
            options = dict(args[-1], session=session1, progressCallback=callback)
            fn(*args[:-1], options)
        except Exception as e:
            error.append(str(e))
    thread = threading.Thread(target=work)
    thread.start()
    thread.join()
    return events, error[0] if error else None

def types(events):
    return set(event["type"] for event in events)

def stages(events):
    return [event["stage"] for event in events if event["type"] == "stageStarted"]

#@<> a dump on a given session, without a global one
EXPECT_EQ(None, shell.get_session())
dump_dir = dump_dir_for("main_thread")
EXPECT_NO_THROWS(lambda: util.dump_schemas(["src"], dump_dir, {"session": session1, "showProgress": False}), "dump")
EXPECT_TRUE(os.path.isfile(os.path.join(dump_dir, "@.done.json")))
EXPECT_EQ(None, shell.get_session())

#@<> the callback gets the events in place of the output
events = []
util.dump_schemas(["src"], dump_dir_for("callback"), {"session": session1, "progressCallback": lambda e: events.append(dict(e))})
EXPECT_EQ({"message", "stageStarted", "progress", "stageFinished"}, types(events))
EXPECT_EQ("Dumping data", stages(events)[-1])
EXPECT_STDOUT_NOT_CONTAINS("Dumping data")
EXPECT_STDOUT_NOT_CONTAINS("Schemas dumped")
messages = [event for event in events if event["type"] == "message"]
EXPECT_TRUE(any(m["level"] == "status" and m["text"] == "Dumping data..." for m in messages), "status lines are messages")
for m in messages:
    EXPECT_FALSE(m["text"].endswith("\n"), "messages have no line break at the end")
last = [event for event in events if event["type"] == "progress" and event["stage"] == "Dumping data"][-1]
EXPECT_EQ(50000, last["current"])
EXPECT_EQ("rows", last["items"])
EXPECT_TRUE("throughput" in last and "etaSeconds" in last and "totalIsApproximate" in last)
finished = [event for event in events if event["type"] == "stageFinished"]
# every stage that started finished, though not always in the same order
EXPECT_EQ(sorted(stages(events)), sorted(event["stage"] for event in finished))
EXPECT_TRUE(all(event["seconds"] >= 0 for event in finished))

#@<> a counting stage reports what it counts
counting = [event for event in events if event["type"] == "progress" and "totalKnown" in event]
EXPECT_TRUE(len(counting) > 0, "counting stages")
EXPECT_TRUE(all(event["current"] <= event["total"] for event in counting if event["totalKnown"]))

#@<> dumpSchemas and loadDump from a thread
dump_dir = dump_dir_for("thread")
events, error = run_in_thread(util.dump_schemas, ["src"], dump_dir, {"threads": 2})
EXPECT_EQ(None, error)
EXPECT_TRUE(os.path.isfile(os.path.join(dump_dir, "@.done.json")))
events, error = run_in_thread(util.load_dump, dump_dir, {"schema": "loaded", "threads": 2})
EXPECT_EQ(None, error)
EXPECT_EQ(50000, count(session1, "loaded.t"))
EXPECT_TRUE("Loading data" in stages(events))
session1.run_sql("DROP SCHEMA loaded")

#@<> dumpInstance and dumpTables from a thread
events, error = run_in_thread(util.dump_instance, dump_dir_for("instance"), {"users": False})
EXPECT_EQ(None, error)
events, error = run_in_thread(util.dump_tables, "src", ["t"], dump_dir_for("tables"), {})
EXPECT_EQ(None, error)
EXPECT_TRUE(os.path.isfile(os.path.join(outdir, "tables", "@.done.json")))

#@<> exportTable and importTable from a thread
tsv = os.path.join(outdir, "t.tsv")
events, error = run_in_thread(util.export_table, "src.t", tsv, {})
EXPECT_EQ(None, error)
EXPECT_TRUE(os.path.isfile(tsv))
session1.run_sql("CREATE SCHEMA imported")
session1.run_sql("CREATE TABLE imported.t LIKE src.t")
events, error = run_in_thread(util.import_table, tsv, {"schema": "imported", "table": "t"})
EXPECT_EQ(None, error)
EXPECT_EQ(50000, count(session1, "imported.t"))
EXPECT_EQ({"message", "stageStarted", "progress", "stageFinished"}, types(events))
session1.run_sql("DROP SCHEMA imported")

#@<> copySchemas, copyTables and copyInstance from a thread
events, error = run_in_thread(util.copy_schemas, ["src"], __sandbox_uri2, {})
EXPECT_EQ(None, error)
EXPECT_EQ(50000, count(session2, "src.t"))
# the dump's stages and the load's
EXPECT_TRUE("Dumping data" in stages(events) and "Loading data" in stages(events))
session2.run_sql("DROP SCHEMA src")
events, error = run_in_thread(util.copy_tables, "src", ["t"], __sandbox_uri2, {"schema": "copied"})
EXPECT_EQ(None, error)
EXPECT_EQ(50000, count(session2, "copied.t"))
session2.run_sql("DROP SCHEMA copied")
events, error = run_in_thread(util.copy_instance, __sandbox_uri2, {"users": False})
EXPECT_EQ(None, error)
EXPECT_EQ(50000, count(session2, "src.t"))
wipeout_server(session2)

#@<> the callback stops a utility running in a thread
events, error = run_in_thread(util.dump_schemas, ["src"], dump_dir_for("cancelled"), {"threads": 1, "maxRate": "100k"}, answer=lambda e: "cancel" if e["type"] == "progress" else None)
EXPECT_EQ("Interrupted by user", error.strip())
EXPECT_FALSE(os.path.isfile(os.path.join(outdir, "cancelled", "@.done.json")))

#@<> returning true stops it as well, on the main thread too
EXPECT_THROWS(lambda: util.dump_schemas(["src"], dump_dir_for("cancelled_main"), {"session": session1, "threads": 1, "maxRate": "100k", "progressCallback": lambda e: e["type"] == "progress"}), "Interrupted by user")

#@<> a load stops too
dump_dir = os.path.join(outdir, "thread")
events, error = run_in_thread(util.load_dump, dump_dir, {"schema": "stopped", "resetProgress": True, "threads": 1}, answer=lambda e: "cancel" if e["type"] == "stageStarted" and e["stage"] == "Loading data" else None)
EXPECT_TRUE(error is not None, "load interrupted")
session1.run_sql("DROP SCHEMA IF EXISTS stopped")

#@<> a callback that fails does not stop the utility
def failing(event):
    raise RuntimeError("callback failed")

dump_dir = dump_dir_for("failing_callback")
EXPECT_NO_THROWS(lambda: util.dump_schemas(["src"], dump_dir, {"session": session1, "progressCallback": failing}), "dump")
EXPECT_TRUE(os.path.isfile(os.path.join(dump_dir, "@.done.json")))

#@<> a session that is closed, or not a session, is refused
closed = shell.open_session(__sandbox_uri1)
closed.close()
EXPECT_THROWS(lambda: util.dump_schemas(["src"], dump_dir_for("closed"), {"session": closed}), "The session given in the 'session' option is not open.")
EXPECT_THROWS(lambda: util.dump_schemas(["src"], dump_dir_for("not_a_session"), {"session": "root@localhost"}), "Option 'session' is expected to be of type Object, but is String")
EXPECT_THROWS(lambda: util.dump_schemas(["src"], dump_dir_for("not_a_function"), {"session": session1, "progressCallback": 1}), "Option 'progressCallback' is expected to be of type Function, but is Integer")

#@<> without a given session the global one is still required
EXPECT_THROWS(lambda: util.dump_schemas(["src"], dump_dir_for("no_session"), {}), "An open session is required to perform this operation.")

#@<> neither option exists on the command line
rc = testutil.call_mysqlsh([__sandbox_uri1, "--", "util", "dump-schemas", "src", "--outputUrl", dump_dir_for("cli"), "--session", "x"])
EXPECT_NE(0, rc)
EXPECT_STDOUT_CONTAINS("The following option is invalid: --session")

#@<> Cleanup
session1.close()
session2.close()
testutil.destroy_sandbox(__mysql_sandbox_port1)
testutil.destroy_sandbox(__mysql_sandbox_port2)
wipe_dir(outdir)
