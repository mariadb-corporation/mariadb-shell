# Copyright (c) 2026, MariaDB Corporation.
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335 USA

"""Shell plugin to deploy and manage local MariaDB and MySQL server sandboxes.

This plugin is used to provide sandbox management operations
(deploy/start/stop/kill/delete). It works with both MariaDB and MySQL
servers: the server found on the PATH (or pointed to by 'mariadbdPath')
is identified and initialized with the right tool-chain for its vendor.
"""

from mysqlsh.plugin_manager import plugin, plugin_function

from sandbox import sandboxlib


@plugin
class sandbox:
    """Deploy and manage local MariaDB and MySQL server sandbox instances.

    A sandbox is a self-contained server deployment created under
    <sandboxDir>/<port>, intended only for local testing and development.

    Sandbox instances are only suitable for running on the local machine and
    are not meant to be accessible from external networks.

    Both MariaDB and MySQL servers are supported. The server binary (mariadbd
    or mysqld) is located on the PATH by default, or under the installation
    directory provided through the 'mariadbdPath' option, and its vendor is
    detected automatically. MariaDB data directories are initialized with
    mariadb-install-db; MySQL 8.0+ has no such tool, so its data directory is
    initialized with 'mysqld --initialize-insecure'.
    """


@plugin_function("sandbox.deploy", cli=True)
def deploy(port, options=None):
    """Deploys a new MariaDB or MySQL sandbox instance on localhost.

    This deploys a plain (raw) standalone instance using the server found on the
    PATH (or pointed to by 'mariadbdPath'), be it MariaDB or MySQL: no
    replication or GTID configuration is applied. The server is started, the
    root password is set and, by default, a root account reachable from any host
    is created. The instance is left running.

    SSL/TLS is enabled by default: the server and the bundled clients are
    configured with a private certificate authority plus server and client
    certificates. For MariaDB these are generated into the sandbox directory,
    which requires the openssl command-line tool; MySQL generates its own while
    initializing the data directory, so no external tool is needed there. Set the
    'ssl' option to false to deploy without TLS.

    The one exception is MariaDB on Windows, where TLS defaults to off: those
    builds bundle wolfSSL, whose server-side TLS cannot complete a handshake with
    the shell's client, making a TLS sandbox undeployable. Pass ssl:true to force
    it (only useful with a MariaDB built against OpenSSL).

    To avoid repeating the expensive data directory initialization, the first
    deployment for a given server version creates a boilerplate under the
    sandbox directory; later deployments of the same version are created by
    copying that initialized data directory, so only separate data directories
    are produced.

    A start and a stop script are written into the sandbox directory. They
    capture the absolute path to the server binary used for the deployment and
    reference the instance option file, so the sandbox can be started and
    stopped from a terminal even when the server binary is not on the PATH.

    Args:
        port (int): The port where the new instance will listen for connections.
        options (dict): Optional dictionary with deployment options.

    Allowed options for options:
        password (str): Password for the MariaDB root user on the new instance.
        sandboxDir (str): Path where the new instance will be deployed.
        allowRootFrom (str): Host pattern for a remote root account to create.
            Defaults to '%'. Set to an empty string to skip creating it.
        serverId (int): server_id value for the instance. Omitted by default;
            a plain instance does not require one.
        ssl (bool): Whether to generate SSL certificates and enable TLS on the
            instance. Defaults to true, except for MariaDB on Windows (bundled
            wolfSSL) where it defaults to false.
        opensslPath (str): Path to the openssl executable, or to a directory
            containing it, used to generate the SSL certificates. Located in
            the MariaDB installation and then on the PATH by default. Ignored
            for MySQL, which generates its own certificates.
        mariadbdPath (str): Path to the mariadbd/mysqld binary, or to the
            server installation directory. Located on the PATH by default.
        mariadbdOptions (list): Additional server configuration options to
            write to the [mysqld] section, as 'option=value' strings. These are
            applied after the SSL options, so they can override them.
        timeout (int): Seconds to wait for the instance to start listening for
            connections. Defaults to 60.

    Sandbox instances are only suitable for deploying and running on the local
    machine for testing purposes and are not accessible from external networks.
    """
    sandboxlib.create_sandbox(port, options or {})


@plugin_function("sandbox.start", cli=True)
def start(port, options=None):
    """Starts an existing MariaDB sandbox instance on localhost.

    Args:
        port (int): The port of the instance to be started.
        options (dict): Optional dictionary with options affecting the result.

    Allowed options for options:
        sandboxDir (str): Path where the instance is located.
        mariadbdPath (str): Path to the mariadbd/mysqld binary, or to the
            MariaDB installation directory. By default the binary captured in
            the sandbox start script at deployment time is used, so this is
            only needed to point the instance at a different server binary
            (which also regenerates the management scripts).
        timeout (int): Seconds to wait for the instance to start listening for
            connections. Defaults to 60.

    The sandboxDir must be the one where the instance was deployed. If not
    specified the default sandbox directory is used.
    """
    sandboxlib.start_sandbox(port, options or {})


@plugin_function("sandbox.stop", cli=True)
def stop(port, options=None):
    """Gracefully stops a running MariaDB sandbox instance on localhost.

    Args:
        port (int): The port of the instance to be stopped.
        options (dict): Optional dictionary with options affecting the result.

    Allowed options for options:
        sandboxDir (str): Path where the instance is located.
        password (str): Root password, used on Windows to request the shutdown
            via mariadb-admin. Ignored on other platforms, where a clean
            shutdown signal is sent to the server process.
        timeout (int): Seconds to wait for the instance to stop. Defaults to 60.

    The sandboxDir must be the one where the instance was deployed. If not
    specified the default sandbox directory is used.
    """
    sandboxlib.stop_sandbox(port, options or {})


@plugin_function("sandbox.kill", cli=True)
def kill(port, options=None):
    """Kills the process of a running MariaDB sandbox instance on localhost.

    This forcefully terminates the server process. Use stop() for a graceful
    shutdown.

    Args:
        port (int): The port of the instance to be killed.
        options (dict): Optional dictionary with options affecting the result.

    Allowed options for options:
        sandboxDir (str): Path where the instance is located.

    The sandboxDir must be the one where the instance was deployed. If not
    specified the default sandbox directory is used.
    """
    sandboxlib.kill_sandbox(port, options or {})


@plugin_function("sandbox.delete", cli=True)
def delete(port, options=None):
    """Deletes an existing MariaDB sandbox instance on localhost.

    The instance must be stopped before it can be deleted.

    Args:
        port (int): The port of the instance to be deleted.
        options (dict): Optional dictionary with options affecting the result.

    Allowed options for options:
        sandboxDir (str): Path where the instance is located.

    The sandboxDir must be the one where the instance was deployed. If not
    specified the default sandbox directory is used.
    """
    sandboxlib.delete_sandbox(port, options or {})


@plugin_function("sandbox.vendor", cli=True)
def vendor(port=None, options=None):
    """Reports the server vendor ('MariaDB' or 'MySQL') for sandbox operations.

    Without a port, it returns the vendor a new deployment would use: the
    server found on the PATH, or the one at 'mariadbdPath' when provided. With
    a port, it returns the vendor recorded for the existing sandbox at that
    port, which is the vendor its operations report.

    Args:
        port (int): The port of an existing sandbox to report the vendor of. If
            omitted, the vendor a new deployment would use is reported instead.
        options (dict): Optional dictionary with options affecting the result.

    Allowed options for options:
        sandboxDir (str): Path where the instance is located.
        mariadbdPath (str): Path to the mariadbd/mysqld binary, or to the
            server installation directory. Used to identify the vendor when no
            port is given (or when an existing sandbox predates vendor
            recording). Located on the PATH by default.

    Returns:
        str: The server vendor, either "MariaDB" or "MySQL", or null when no
            server binary can be found and the vendor cannot be determined.
    """
    return sandboxlib.sandbox_vendor(port, options or {})


@plugin_function("sandbox.version", cli=True)
def version(port=None, options=None):
    """Reports the server version to be used by sandbox operations.

    Without a port, it returns the version a new deployment would use: the
    server found on the PATH, or the one at 'mariadbdPath' when provided. With
    a port, it returns the version recorded for the existing sandbox at that
    port.

    Args:
        port (int): The port of an existing sandbox to report the version of.
            If omitted, the version a new deployment would use is reported
            instead.
        options (dict): Optional dictionary with options affecting the result.

    Allowed options for options:
        sandboxDir (str): Path where the instance is located.
        mariadbdPath (str): Path to the mariadbd/mysqld binary, or to the
            server installation directory. Used to identify the version when no
            port is given (or when an existing sandbox predates version
            recording). Located on the PATH by default.

    Returns:
        str: The server version as major.minor.patch (for example "9.7.1" or
            "11.4.2"); the vendor is available through sandbox.vendor(). Returns
            null when no server binary can be found and the version cannot be
            determined.
    """
    return sandboxlib.sandbox_version(port, options or {})
