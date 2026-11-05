#@<> Initialization
import os

user_plugins_path = os.path.join(testutil.get_user_config_path(), "plugins")
env_root = os.path.join(__tmp_dir, "plugins_env_vars")
dir_a = os.path.join(env_root, "a")
dir_b = os.path.join(env_root, "b")

plugin_template = '''
from mysqlsh.plugin_manager import plugin, plugin_function

@plugin
class {name}:
    """Plugin used to test the plugin environment variables."""

@plugin_function("{name}.name")
def name():
    """Returns the name of the plugin."""
    return "{name}"
'''

def create_plugin(parent, name):
    folder = os.path.join(parent, name)
    testutil.mkdir(folder, True)
    testutil.create_file(os.path.join(folder, "init.py"),
                         plugin_template.format(name=name))

create_plugin(user_plugins_path, "envpath_user")
create_plugin(dir_a, "envpath_a")
create_plugin(dir_b, "envpath_b")

list_code = "print('loaded:', sorted(n for n in dir() if n.startswith('envpath_')))"

def run(env, extra_args=[]):
    WIPE_OUTPUT()
    return testutil.call_mysqlsh(["--py"] + extra_args + ["-e", list_code], "", env)

#@<> Without MARIADB_SHELL_PLUGINS_PATH, the user plugins directory is used
EXPECT_EQ(0, run([]))
EXPECT_STDOUT_CONTAINS("loaded: ['envpath_user']")

#@<> MARIADB_SHELL_PLUGINS_PATH replaces the user plugins directory
EXPECT_EQ(0, run(["MARIADB_SHELL_PLUGINS_PATH=" + os.pathsep.join([dir_a, dir_b])]))
EXPECT_STDOUT_CONTAINS("loaded: ['envpath_a', 'envpath_b']")
EXPECT_STDOUT_NOT_CONTAINS("Found errors loading plugins")

#@<> The user plugins directory loads when it is listed
EXPECT_EQ(0, run(["MARIADB_SHELL_PLUGINS_PATH=" + os.pathsep.join([user_plugins_path, dir_b])]))
EXPECT_STDOUT_CONTAINS("loaded: ['envpath_b', 'envpath_user']")

#@<> The legacy MYSQLSH_PLUGINS_PATH name is honoured
EXPECT_EQ(0, run(["MYSQLSH_PLUGINS_PATH=" + dir_a]))
EXPECT_STDOUT_CONTAINS("loaded: ['envpath_a']")

#@<> A directory listed more than once is searched once
# a second load of the same plugin would fail registering its global object
EXPECT_EQ(0, run(["MARIADB_SHELL_PLUGINS_PATH=" + os.pathsep.join(
    [dir_a, os.path.join(dir_a, ""), os.path.join(dir_a, "."), dir_a])]))
EXPECT_STDOUT_CONTAINS("loaded: ['envpath_a']")
EXPECT_STDOUT_NOT_CONTAINS("Found errors loading plugins")

#@<> The directories in MARIADB_SHELL_PLUGINS_PATH are skipped with --disable-plugins
EXPECT_EQ(0, run(["MARIADB_SHELL_PLUGINS_PATH=" + dir_a], ["--disable-plugins"]))
EXPECT_STDOUT_CONTAINS("loaded: []")

#@<> MARIADB_SHELL_DISABLE_BUILTIN_PLUGINS disables the built-in plugins
builtin_code = "print('builtin:', 'plugins' in dir())"

def run_builtin(env, extra_args=[]):
    WIPE_OUTPUT()
    return testutil.call_mysqlsh(["--py"] + extra_args + ["-e", builtin_code], "", env)

for value in ["1", "true", "on"]:
    EXPECT_EQ(0, run_builtin(["MARIADB_SHELL_DISABLE_BUILTIN_PLUGINS=" + value]))
    EXPECT_STDOUT_CONTAINS("builtin: False")

for value in ["0", "false", "off"]:
    EXPECT_EQ(0, run_builtin(["MARIADB_SHELL_DISABLE_BUILTIN_PLUGINS=" + value]))
    EXPECT_STDOUT_CONTAINS("builtin: True")

#@<> The --disable-builtin-plugins option wins over MARIADB_SHELL_DISABLE_BUILTIN_PLUGINS=0
EXPECT_EQ(0, run_builtin(["MARIADB_SHELL_DISABLE_BUILTIN_PLUGINS=0"], ["--disable-builtin-plugins"]))
EXPECT_STDOUT_CONTAINS("builtin: False")

#@<> An invalid MARIADB_SHELL_DISABLE_BUILTIN_PLUGINS value is an error
EXPECT_EQ(1, run_builtin(["MARIADB_SHELL_DISABLE_BUILTIN_PLUGINS=maybe"]))
EXPECT_OUTPUT_CONTAINS("MARIADB_SHELL_DISABLE_BUILTIN_PLUGINS: Incorrect option value.")

#@<> Cleanup
testutil.rmdir(os.path.join(user_plugins_path, "envpath_user"), True)
testutil.rmdir(env_root, True)
