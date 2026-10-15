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

def where_tids(condition):
    """The tid column of all the threads which match the condition."""
    return column(threads({'all': True, 'format': 'tid', 'where': condition}))

# -----------------------------------------------------------------------------
# setup

#@<> setup
shell.connect(__mysqluripwd)

session.run_sql("CREATE USER IF NOT EXISTS 'threads_test'@'%' IDENTIFIED BY 'pwd'")
session.run_sql("GRANT ALL ON *.* TO 'threads_test'@'%'")

__test_session = mysql.get_classic_session("threads_test:pwd@{0}:{1}".format(__host, __mysql_port))
__test_ids = get_ids(__test_session)
__session_ids = get_ids(session)

# the order and condition checks need thread IDs of one and of two digits,
# which compare differently as numbers and as text: thread 1 is the server's
# main thread, and this keeps opening sessions until one of them has an ID of
# two digits or more
__extra_sessions = []
__big_tid = max(__test_ids['tid'], __session_ids['tid'])
while __big_tid < 10:
    __extra_sessions.append(mysql.get_classic_session(__mysqluripwd))
    __big_tid = get_ids(__extra_sessions[-1])['tid']

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

#@<> where - comparison operators
# every spelling of the operators is in the unit test, these check the SQL;
# background threads come and go, so each check looks at the rows it gets and
# at threads which stay: thread 1 and the sessions of this test
def check_where(condition, matches, included, excluded=()):
    tids = where_tids(condition)
    for t in tids:
        EXPECT_TRUE(matches(t), "{0}: {1}".format(condition, t))
    for t in included:
        EXPECT_IN(t, tids, condition)
    for t in excluded:
        EXPECT_TRUE(t not in tids, "{0}: {1} in {2}".format(condition, t, tids))

tid = __session_ids['tid']
EXPECT_EQ([tid], where_tids('tid = {0}'.format(tid)))
check_where('tid <> {0}'.format(tid), lambda t: t != tid, [1], [tid])
# numbers compare as numbers, not as text
check_where('tid > 9', lambda t: t > 9, [__big_tid], [1])
check_where('tid >= 9', lambda t: t >= 9, [__big_tid], [1])
check_where('tid < 10', lambda t: t < 10, [1], [__big_tid])
check_where('tid <= 10', lambda t: t <= 10, [1], [__big_tid] if __big_tid > 10 else [])
check_where('tid > -1', lambda t: t > -1, [1, tid, __big_tid])

#@<> where - LIKE and strings
names = column(threads({'all': True, 'format': 'name', 'where': "name LIKE '%main%'"}))
EXPECT_TRUE(len(names) > 0)
for name in names:
    EXPECT_TRUE('main' in name, name)
for name in column(threads({'all': True, 'format': 'name', 'where': "name NOT LIKE '%main%'"})):
    EXPECT_TRUE('main' not in name, name)

#@<> where - logical operators and grouping
EXPECT_EQ([__test_ids['tid']], where_tids('tid = {0} and cid = {1}'.format(__test_ids['tid'], __test_ids['cid'])))
EXPECT_EQ(sorted([__test_ids['tid'], tid]), where_tids('tid = {0} Or tid = {1}'.format(__test_ids['tid'], tid)))
# with parentheses, as the X protocol parser applies a NOT prefix to the
# operand that follows it, and the parser of the other builds to the comparison
check_where('NOT (tid = {0})'.format(tid), lambda t: t != tid, [1], [tid])
EXPECT_EQ([__test_ids['tid']], where_tids('(tid = {0} OR cid = {1}) AND NOT (tid = {2} OR cid = {3})'.format(__test_ids['tid'], __test_ids['cid'], tid, __session_ids['cid'])))

#@<> where - errors reach the caller
# one of each kind, the unit test has the rest
EXPECT_THROWS(lambda: where_tids('invalidcolumn = 1'), "Failed to parse 'where' parameter: Unknown column name: invalidcolumn")
EXPECT_THROWS(lambda: where_tids('tid | 1'), "Failed to parse 'where' parameter: Disallowed operator: |")
EXPECT_THROWS(lambda: where_tids('one two'), "Failed to parse 'where' parameter: Expected end of expression, at position 4,\nin: one two\n        ^^^")
EXPECT_THROWS(lambda: where_tids(''), "The 'where' parameter cannot be empty.")

#@<> a variable name cannot change the query
EXPECT_THROWS(lambda: threads({'format': "status.x'"}), "Unknown column name: status.x'")
EXPECT_THROWS(lambda: threads({'format': 'tid', 'order_by': "status.x' OR 1=1 -- "}), "Unknown column name: status.x' OR 1=1 -- ")
EXPECT_THROWS(lambda: where_tids("status.`a'b` = 1"), "Failed to parse 'where' parameter: Unknown column name: status.a'b")
EXPECT_EQ(1, len(threads({'format': 'tid,status.bytes_sent', 'where': 'tid = {0}'.format(__session_ids['tid'])})))

#@<> where - values cannot change the condition
EXPECT_EQ([], where_tids("name = 'x'' OR ''1''=''1'"))
EXPECT_EQ([], where_tids("name = 'x\\' OR 1=1 -- '"))
EXPECT_EQ([], where_tids('name = "x"" OR ""1""=""1"'))

# -----------------------------------------------------------------------------
# cleanup

#@<> cleanup
for s in __extra_sessions:
    s.close()

__test_session.close()
session.run_sql("DROP USER IF EXISTS 'threads_test'@'%'")
session.close()
