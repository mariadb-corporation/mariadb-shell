#@ {__server_is_maria_db}
# -----------------------------------------------------------------------------
# Logging in to a gssapi (Kerberos) account through the bundled
# auth_gssapi_client plugin. The test brings up a throwaway Kerberos realm of
# its own: a KDC on a port of this worker, a service principal whose keytab the
# server is started with, and a user principal with a ticket in a temporary
# credential cache. It needs the MIT Kerberos tools (krb5kdc, kdb5_util,
# kadmin.local, kinit), and a server that has the auth_gssapi plugin; without
# either, every chunk is skipped.

import os
import shutil
import subprocess
import tempfile
import time

# -----------------------------------------------------------------------------
# helpers

__realm = "SHELL.TEST"
__service = "mariadb/localhost"
__user = "krbuser"
__user_password = "krb_pwd"

def find_tool(name):
    """The tool on the PATH or in the sbin dirs the packages put them in,
    which are often not on a user's PATH."""
    dirs = ["/usr/sbin", "/usr/local/sbin", "/opt/homebrew/opt/krb5/sbin",
            "/opt/homebrew/opt/krb5/bin", "/usr/local/opt/krb5/sbin",
            "/usr/local/opt/krb5/bin"]
    return shutil.which(name) or shutil.which(name, path=os.pathsep.join(dirs))

def server_has_gssapi():
    s = mysql.get_classic_session(__mysqluripwd)
    row = s.run_sql("SELECT COUNT(*) FROM information_schema.all_plugins WHERE plugin_name = 'gssapi'").fetch_one()
    s.close()
    return row[0] == 1

def run_tool(args, stdin=None):
    r = subprocess.run(args, input=stdin, env=__krb_env, capture_output=True, text=True)
    if r.returncode != 0:
        raise Exception(f"{' '.join(args)} failed: {r.stdout}{r.stderr}")

def gssapi_uri():
    # gssapi takes no password, but an empty one keeps the shell from asking
    return f"{__user}:@localhost:{__mysql_sandbox_port1}"

__tools = {t: find_tool(t) for t in ["krb5kdc", "kdb5_util", "kadmin.local", "kinit"]}
__have_gssapi = all(__tools.values()) and server_has_gssapi()

# -----------------------------------------------------------------------------
# setup

#@<> setup {__have_gssapi}
__krb_dir = tempfile.mkdtemp(prefix="krb-")
__kdc_port = __mysql_sandbox_port3

with open(os.path.join(__krb_dir, "krb5.conf"), "w") as f:
    f.write(f"""[libdefaults]
 default_realm = {__realm}
 dns_lookup_kdc = false
 dns_lookup_realm = false
 rdns = false
[realms]
 {__realm} = {{
  kdc = 127.0.0.1:{__kdc_port}
 }}
""")

with open(os.path.join(__krb_dir, "kdc.conf"), "w") as f:
    f.write(f"""[kdcdefaults]
 kdc_ports = {__kdc_port}
 kdc_tcp_ports = {__kdc_port}
[realms]
 {__realm} = {{
  database_name = {__krb_dir}/principal
  key_stash_file = {__krb_dir}/stash
  acl_file = {__krb_dir}/kadm5.acl
 }}
[logging]
 kdc = FILE:{__krb_dir}/kdc.log
""")

# Set in this process too: the in-process session's plugin reads them, and so
# do the mariadb-shell processes and the sandbox server started from here.
__krb_env = dict(os.environ)
for k, v in {"KRB5_CONFIG": f"{__krb_dir}/krb5.conf",
             "KRB5_KDC_PROFILE": f"{__krb_dir}/kdc.conf",
             "KRB5CCNAME": f"FILE:{__krb_dir}/ccache"}.items():
    __krb_env[k] = v
    os.environ[k] = v

__keytab = os.path.join(__krb_dir, "server.keytab")
run_tool([__tools["kdb5_util"], "create", "-s", "-r", __realm, "-P", "master_pwd"])
run_tool([__tools["kadmin.local"], "-r", __realm, "-q", f"addprinc -randkey {__service}"])
run_tool([__tools["kadmin.local"], "-r", __realm, "-q", f"ktadd -k {__keytab} {__service}"])
run_tool([__tools["kadmin.local"], "-r", __realm, "-q", f"addprinc -pw {__user_password} {__user}"])

__kdc = subprocess.Popen([__tools["krb5kdc"], "-n"], env=__krb_env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

# kinit fails until the KDC listens
for i in range(50):
    if subprocess.run([__tools["kinit"], __user], input=__user_password + "\n",
                      env=__krb_env, capture_output=True, text=True).returncode == 0:
        break
    time.sleep(0.2)
else:
    raise Exception("kinit never got a ticket from the test KDC")

testutil.deploy_sandbox(__mysql_sandbox_port1, "root", {
    "plugin-load-add": "auth_gssapi",
    "gssapi-keytab-path": __keytab,
    "gssapi-principal-name": f"{__service}@{__realm}"})
shell.connect(__sandbox_uri1)
session.run_sql(f"CREATE USER {__user}@'%' IDENTIFIED VIA gssapi AS '{__user}@{__realm}'")

#@<> login with a ticket {__have_gssapi}
s = mysql.get_classic_session(gssapi_uri())
EXPECT_EQ(f"{__user}@%", s.run_sql("SELECT current_user()").fetch_one()[0])
s.close()

testutil.call_mysqlsh([gssapi_uri(), "--sql", "-e", "SELECT concat('logged in as ', current_user())"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor"])
EXPECT_STDOUT_CONTAINS(f"logged in as {__user}@%")
EXPECT_STDOUT_NOT_CONTAINS("could not be loaded")
WIPE_OUTPUT()

#@<> login without a ticket {__have_gssapi}
# In a separate process only: libkrb5 may keep the credential cache it already
# opened in this one.
testutil.call_mysqlsh([gssapi_uri(), "--sql", "-e", "SELECT current_user()"], "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor", f"KRB5CCNAME=FILE:{__krb_dir}/no-such-ccache"])
EXPECT_STDOUT_CONTAINS("GSSAPI error")
EXPECT_STDOUT_NOT_CONTAINS("logged in as")
WIPE_OUTPUT()

#@<> cleanup {__have_gssapi}
session.close()
testutil.destroy_sandbox(__mysql_sandbox_port1)
__kdc.terminate()
__kdc.wait()
for k in ["KRB5_CONFIG", "KRB5_KDC_PROFILE", "KRB5CCNAME"]:
    del os.environ[k]
shutil.rmtree(__krb_dir, ignore_errors=True)
