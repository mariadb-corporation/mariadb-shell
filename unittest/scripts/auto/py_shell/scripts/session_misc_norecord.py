#@<> setup
shell.connect(__mysqluripwd)

def com_select(sess):
    return int(sess.run_sql("show session status like 'Com_select'").fetch_one()[1])

#@<> get_sql_mode without tracking
session2 = shell.open_session(__mysqluripwd)

real_sql_mode = session2.run_sql("select @@sql_mode").fetch_one()[0]
EXPECT_EQ(real_sql_mode, session2.get_sql_mode())

#@<> sql_mode and tracking in classic session {VER(>=8.0)}

# session tracking is enabled by default on global session
session.run_sql("set sql_mode='ansi_quotes'")
EXPECT_EQ("ANSI_QUOTES", session.get_sql_mode())

#@<> sql_mode tracking in a new session {VER(>=8.0) and sandbox.vendor() == "MySQL"}
EXPECT_EQ(real_sql_mode, session2.get_sql_mode())

# session tracking disabled by default in non-global sessions - in that case, it should always query sql_mode
num_selects = com_select(session2)
session2.run_sql("set sql_mode='ansi_quotes'")
EXPECT_EQ("ANSI_QUOTES", session2.get_sql_mode())
EXPECT_EQ("ANSI_QUOTES", session2.get_sql_mode())
EXPECT_EQ(2, com_select(session2) - num_selects)

tracked_vars = session2.track_system_variable("sql_mode")
EXPECT_TRUE("sql_mode" in tracked_vars)

# session tracking now enabled - no queries on sql_mode at all
num_selects = com_select(session2)
session2.run_sql("set sql_mode='ansi_quotes'")
EXPECT_EQ("ANSI_QUOTES", session2.get_sql_mode())
EXPECT_EQ("ANSI_QUOTES", session2.get_sql_mode())

session2.run_sql("set sql_mode=''")
EXPECT_EQ("", session2.get_sql_mode())
EXPECT_EQ("", session2.get_sql_mode())
EXPECT_EQ(0, com_select(session2) - num_selects)

#@<> client data
deleted = False


class Data:
    def __del__(self):
        global deleted
        deleted = True


session.set_client_data("mydata", {"HELLO": "WORLD"})
session.set_client_data("null", None)
session.set_client_data("binary", b"binarydata")
data = Data()
session.set_client_data("opaque", data)

EXPECT_EQ({"HELLO": "WORLD"}, session.get_client_data("mydata"))
EXPECT_EQ(None, session.get_client_data("null"))
EXPECT_EQ(data, session.get_client_data("opaque"))
EXPECT_EQ(b"binarydata", session.get_client_data("binary"))

EXPECT_EQ(None, session.get_client_data("invalid"))

del data

import gc

gc.collect()
EXPECT_FALSE(deleted)

session.set_client_data("opaque", None)
EXPECT_EQ(None, session.get_client_data("opaque"))

gc.collect()
EXPECT_TRUE(deleted)

#@<> clone creates an independent session to the same server
clone = session.clone()

EXPECT_TRUE(clone.is_open())
EXPECT_EQ(session.uri, clone.uri)
EXPECT_NE(session.connection_id, clone.connection_id)

# closing the clone does not affect the session it was created from
clone.close()
EXPECT_FALSE(clone.is_open())
EXPECT_TRUE(session.is_open())
EXPECT_EQ(1, session.run_sql("select 1").fetch_one()[0])

# the connection options are still available on a closed session
EXPECT_EQ(session.uri, clone.uri)

#@<> clone requires an open session
EXPECT_THROWS(lambda: clone.clone(), "Not connected.")

#@<> in_transaction follows the transaction state
session3 = shell.open_session(__mysqluripwd)
session3.run_sql("create schema if not exists in_trx_test")
session3.run_sql("create temporary table in_trx_test.t (id int primary key) engine=InnoDB")

EXPECT_FALSE(session3.in_transaction())

session3.run_sql("start transaction")
EXPECT_TRUE(session3.in_transaction())

session3.run_sql("insert into in_trx_test.t values (1)")
EXPECT_THROWS(lambda: session3.run_sql("insert into in_trx_test.t values (1)"), "Duplicate entry")
# a failed statement leaves the transaction open
EXPECT_TRUE(session3.in_transaction())

session3.run_sql("rollback")
EXPECT_FALSE(session3.in_transaction())

session3.run_sql("start transaction")
EXPECT_TRUE(session3.in_transaction())
session3.run_sql("commit")
EXPECT_FALSE(session3.in_transaction())

#@<> in_transaction with autocommit disabled
session3.run_sql("set autocommit=0")
EXPECT_FALSE(session3.in_transaction())
session3.run_sql("select * from in_trx_test.t")
EXPECT_TRUE(session3.in_transaction())
session3.run_sql("rollback")
EXPECT_FALSE(session3.in_transaction())
session3.run_sql("set autocommit=1")

#@<> in_transaction sends no query
# a query would reset the diagnostics area, so the warning of the previous
# statement must still be there
session3.run_sql("select cast('x' as signed)")
EXPECT_FALSE(session3.in_transaction())
EXPECT_FALSE(session3.in_transaction())
EXPECT_EQ(1, len(session3.run_sql("show warnings").fetch_all()))

#@<> in_transaction requires an open session
session3.run_sql("drop schema in_trx_test")
session3.close()
EXPECT_THROWS(lambda: session3.in_transaction(), "Not connected.")

#@<> cleanup
session2.close()
shell.disconnect()
