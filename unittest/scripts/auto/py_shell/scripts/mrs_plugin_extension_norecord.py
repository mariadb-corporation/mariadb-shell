#@ {__server_is_maria_db}
#@<> Initialization
# The mrs global object is extensible: a Python plugin adds its functions to
# it with the usual decorators, as the mrs_plugin does for the MRS SDK and
# the dumping and loading of MRS projects. This plugin mimics that.
import os

user_path = testutil.get_user_config_path()
plugin_folder_path = os.path.join(user_path, "plugins", "mrs_extension_tester")
testutil.mkdir(plugin_folder_path, True)

testutil.create_file(os.path.join(plugin_folder_path, "init.py"), '''
from mysqlsh.plugin_manager import plugin, plugin_function

@plugin
class mrs:
    """Plugin to manage the MariaDB REST Data Service (MRS).

    The built-in mrs object exists already, so this docstring is not used.
    """

    class project:
        """Used to dump and load MRS projects.

        A collection of functions to dump and load MRS projects.
        """

@plugin_function("mrs.dumpSdkServiceFiles", shell=True, cli=True, web=True)
def dump_sdk_service_files(**kwargs):
    """Dumps the SDK files of a REST service

    Args:
        **kwargs: Additional options.

    Keyword Args:
        directory (str): The directory to write the files to.

    Returns:
        The directory
    """
    return "SDK files written to " + str(kwargs.get("directory"))

@plugin_function("mrs.project.dump", shell=True, cli=True, web=True)
def dump_project(name, **kwargs):
    """Dumps an MRS project

    Args:
        name (str): The name of the project.
        **kwargs: Additional options.

    Returns:
        The name
    """
    return "Project " + name + " dumped"
''')

def call_mysqlsh(args):
    testutil.call_mysqlsh(["--disable-builtin-plugins"] + args, "", ["MARIADB_SHELL_TERM_COLOR_MODE=nocolor", "MARIADB_SHELL_USER_CONFIG_HOME=" + user_path])

#@<> The plugin functions extend the built-in object
call_mysqlsh(["--py", "-e", "print(sorted(dir(mrs))); print(mrs.dump_sdk_service_files(directory='sdk')); print(mrs.project.dump('p1'))"])
EXPECT_STDOUT_CONTAINS("['dump_sdk_service_files', 'help', 'project']")
EXPECT_STDOUT_CONTAINS("SDK files written to sdk")
EXPECT_STDOUT_CONTAINS("Project p1 dumped")
EXPECT_STDOUT_NOT_CONTAINS("Could not register")
WIPE_OUTPUT()

#@<> The built-in help describes the object and lists the plugin's members
call_mysqlsh(["--py", "-i", "-e", "mrs.help()"])
EXPECT_STDOUT_CONTAINS("mrs - Global object for the MariaDB REST Service (MRS).")
EXPECT_STDOUT_CONTAINS("Plugins can add functions to this object")
EXPECT_STDOUT_CONTAINS("dump_sdk_service_files([kwargs])")
EXPECT_STDOUT_CONTAINS("Used to dump and load MRS projects.")
WIPE_OUTPUT()

#@<> The plugin functions are available on the command line
call_mysqlsh(["--", "mrs", "dump-sdk-service-files", "--directory=cli_sdk"])
EXPECT_STDOUT_CONTAINS("SDK files written to cli_sdk")
WIPE_OUTPUT()
call_mysqlsh(["--", "mrs", "project", "dump", "p2"])
EXPECT_STDOUT_CONTAINS("Project p2 dumped")
WIPE_OUTPUT()

#@<> Cleanup
testutil.rmdir(plugin_folder_path, True)
