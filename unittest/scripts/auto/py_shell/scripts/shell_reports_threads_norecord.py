# -----------------------------------------------------------------------------
# test 'threads' report: the parts every build and server have, the JavaScript
# test covers the rest

# -----------------------------------------------------------------------------
# helpers

def get_ids(s):
    ids = s.run_sql("SELECT THREAD_ID, PROCESSLIST_ID FROM performance_schema.threads WHERE PROCESSLIST_ID = connection_id()").fetch_one()
    return {'tid': ids[0], 'cid': ids[1]}

def threads(options):
    """The rows of the report, without the header."""
    return shell.reports.threads(session, [], options)['report'][1:]

def column(rows, index=0):
    return [row[index] for row in rows]

# -----------------------------------------------------------------------------
# setup

#@<> setup
shell.connect(__mysqluripwd)

session.run_sql("CREATE USER IF NOT EXISTS 'threads_test'@'%' IDENTIFIED BY 'pwd'")
session.run_sql("GRANT ALL ON *.* TO 'threads_test'@'%'")

__test_session = mysql.get_classic_session("threads_test:pwd@{0}:{1}".format(__host, __mysql_port))
__test_ids = get_ids(__test_session)
__session_ids = get_ids(session)

# the order checks need thread IDs of one and of two digits, which compare
# differently as numbers and as text
__extra_sessions = []
while session.run_sql("SELECT MAX(THREAD_ID) FROM performance_schema.threads").fetch_one()[0] < 10:
    __extra_sessions.append(mysql.get_classic_session(__mysqluripwd))

# -----------------------------------------------------------------------------
# tests

#@<> the report runs without any options, and lists the threads of this account only
cids = column(threads({'format': 'cid'}))
EXPECT_IN(__session_ids['cid'], cids)
EXPECT_TRUE(__test_ids['cid'] not in cids, str(cids))

#@<> the report runs from the command
WIPE_OUTPUT()
\show threads
EXPECT_STDOUT_CONTAINS("| tid ")
EXPECT_STDOUT_NOT_CONTAINS("not supported")

#@<> foreground threads include the other account
EXPECT_IN(__test_ids['cid'], column(threads({'foreground': True, 'format': 'cid'})))

#@<> background threads only
EXPECT_EQ(['BACKGROUND'], sorted(set(column(threads({'background': True, 'format': 'type'})))))

#@<> rows are ordered by the numeric value of tid, not its text
tids = column(threads({'all': True, 'format': 'tid'}))
EXPECT_TRUE(min(tids) < 10 <= max(tids), str(tids))
EXPECT_EQ(sorted(tids), tids)

#@<> descending order reverses every column of the order
tids = column(threads({'all': True, 'format': 'tid', 'desc': True}))
EXPECT_EQ(sorted(tids, reverse=True), tids)

#@<> a text column is ordered as text
names = column(threads({'all': True, 'format': 'name', 'order_by': 'name'}))
EXPECT_EQ(sorted(names, key=lambda n: n.encode()), names)

#@<> a column with nulls orders them first, and last with --desc
cids = column(threads({'all': True, 'format': 'cid', 'order_by': 'cid'}))
EXPECT_EQ(None, cids[0])
non_null = [c for c in cids if c is not None]
EXPECT_EQ(sorted(non_null), non_null)
EXPECT_EQ(None, column(threads({'all': True, 'format': 'cid', 'order_by': 'cid', 'desc': True}))[-1])

#@<> several columns, each of them ordered
rows = threads({'all': True, 'format': 'type,tid', 'order_by': 'type,tid'})
EXPECT_EQ(sorted(rows), rows)

#@<> limit the number of rows
EXPECT_EQ(3, len(threads({'all': True, 'format': 'tid', 'limit': 3})))

#@<> a variable name cannot change the query
EXPECT_THROWS(lambda: threads({'format': "status.x'"}), "Unknown column name: status.x'")
EXPECT_THROWS(lambda: threads({'format': 'tid', 'order_by': "status.x' OR 1=1 -- "}), "Unknown column name: status.x' OR 1=1 -- ")
EXPECT_TRUE(len(threads({'format': 'tid,status.bytes_sent'})) > 0)

#@<> an explicit condition {__have_x_protocol}
EXPECT_EQ([__test_ids['tid']], column(threads({'foreground': True, 'format': 'tid', 'where': 'tid = {0}'.format(__test_ids['tid'])})))

#@<> an explicit condition is refused without the condition parser {not __have_x_protocol}
EXPECT_THROWS(lambda: threads({'where': 'tid > 1'}), "The 'where' parameter is not supported in this build.")

# -----------------------------------------------------------------------------
# cleanup

#@<> cleanup
for s in __extra_sessions:
    s.close()

__test_session.close()
session.run_sql("DROP USER IF EXISTS 'threads_test'@'%'")
session.close()
