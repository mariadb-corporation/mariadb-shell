#@ {VER(>=8.0.27)}

# Concurrent AdminAPI calls against independent topologies must not share
# Instance_pool state or default admin credentials.

#@<> utils
import threading
import traceback

import mysqlsh

iterations = 20

def admin_uri(user, password, port):
    return f"mysql://{user}:{password}@localhost:{port}"

def run_admin_call(cluster, iteration):
    operation = iteration % 4
    if operation == 0:
        cluster.status({"extended": 1})
    elif operation == 1:
        cluster.describe()
    elif operation == 2:
        cluster.options()
    else:
        cluster.execute("SELECT 1;", "all")

class AdminCallWorker(threading.Thread):
    def __init__(self, name, uri, barrier):
        super().__init__(name=name)
        self.uri = uri
        self.barrier = barrier
        self.errors = []
        self.successes = 0
    def run(self):
        mysqlsh.thread_init()
        try:
            for iteration in range(iterations):
                try:
                    self.barrier.wait()
                except threading.BrokenBarrierError:
                    break
                dba_handle = None
                cluster = None
                try:
                    dba_handle = mysqlsh.connect_dba(self.uri)
                    cluster = dba_handle.get_cluster()
                    run_admin_call(cluster, iteration)
                    self.successes += 1
                except Exception as exc:
                    self.errors.append(
                        f"{self.name} iteration {iteration}: {exc}\n"
                        f"{traceback.format_exc()}"
                    )
                    try:
                        self.barrier.abort()
                    except Exception:
                        pass
                    break
                finally:
                    if cluster is not None:
                        try:
                            cluster.disconnect()
                        except Exception:
                            pass
                    if dba_handle is not None:
                        try:
                            dba_handle.session.close()
                        except Exception:
                            pass
        except Exception as exc:
            self.errors.append(
                f"{self.name} startup failure: {exc}\n{traceback.format_exc()}"
            )
            try:
                self.barrier.abort()
            except Exception:
                pass
        finally:
            mysqlsh.thread_end()

#@<> Setup
testutil.deploy_sandbox(__mysql_sandbox_port1, "root", {"report_host": hostname})
testutil.deploy_sandbox(__mysql_sandbox_port2, "root", {"report_host": hostname})
testutil.deploy_sandbox(__mysql_sandbox_port3, "root", {"report_host": hostname})

shell.connect(__sandbox_uri1)
c1 = dba.create_cluster("cluster1", {"gtidSetIsComplete": 1})
cs1 = c1.create_cluster_set("domain1")
cs1.setup_admin_account("mysqladmin_cs1@%", {"password": "adminpass_cs1"})
cs1.disconnect()
c1.disconnect()
session.close()

shell.connect(__sandbox_uri2)
c2 = dba.create_cluster("cluster2", {"gtidSetIsComplete": 1})
cs2 = c2.create_cluster_set("domain2")
cs2.setup_admin_account("mysqladmin_cs2@%", {"password": "adminpass_cs2"})
cs2.disconnect()
c2.disconnect()
session.close()

shell.connect(__sandbox_uri3)
c3 = dba.create_cluster("cluster3", {"gtidSetIsComplete": 1})
cs3 = c3.create_cluster_set("domain3")
cs3.setup_admin_account("mysqladmin_cs3@%", {"password": "adminpass_cs3"})
cs3.disconnect()
c3.disconnect()
session.close()

admin_uri1 = admin_uri("mysqladmin_cs1", "adminpass_cs1", __mysql_sandbox_port1)
admin_uri2 = admin_uri("mysqladmin_cs2", "adminpass_cs2", __mysql_sandbox_port2)
admin_uri3 = admin_uri("mysqladmin_cs3", "adminpass_cs3", __mysql_sandbox_port3)

#@<> Run concurrent AdminAPI calls in a ClusterSet
barrier = threading.Barrier(3)

workers = [
    AdminCallWorker("cluster1", admin_uri1, barrier),
    AdminCallWorker("cluster2", admin_uri2, barrier),
    AdminCallWorker("cluster3", admin_uri3, barrier),
]

for worker in workers:
    worker.start()

for worker in workers:
    worker.join()

#@<> Verify concurrent calls did not fail
errors = []
successes = 0
for worker in workers:
    errors.extend(worker.errors)
    successes += worker.successes

if errors:
    for error in errors[:5]:
        print(error)

EXPECT_EQ([], errors)
EXPECT_EQ(3 * iterations, successes)

#@<> Destroy
testutil.destroy_sandbox(__mysql_sandbox_port1)
testutil.destroy_sandbox(__mysql_sandbox_port2)
testutil.destroy_sandbox(__mysql_sandbox_port3)
