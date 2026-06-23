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

"""Tests for option-list parsing and my.cnf generation."""

import configparser
import os

import pytest


# --------------------------------------------------------------------------- #
# _parse_option_list
# --------------------------------------------------------------------------- #
def test_parse_option_list_none_and_empty(sandboxlib):
    assert sandboxlib._parse_option_list(None) == {}
    assert sandboxlib._parse_option_list([]) == {}


def test_parse_option_list_key_value_and_flags(sandboxlib):
    result = sandboxlib._parse_option_list(
        ["max_connections=50", "skip-name-resolve", " key = value "])
    assert result["max_connections"] == "50"
    assert result["skip-name-resolve"] is None
    # surrounding whitespace is trimmed from key and value
    assert result["key"] == "value"


def test_parse_option_list_accepts_single_string(sandboxlib):
    assert sandboxlib._parse_option_list("max_connections=50") == {
        "max_connections": "50"}


# --------------------------------------------------------------------------- #
# _write_option_file
# --------------------------------------------------------------------------- #
def test_write_option_file_roundtrip(sandboxlib, tmp_path):
    path = str(tmp_path / "my.cnf")
    sandboxlib._write_option_file(path, {
        "mysqld": {"port": 3310, "skip_name_resolve": None},
        "client": {"user": "root"},
    })

    parser = configparser.ConfigParser(allow_no_value=True)
    parser.read(path)
    assert parser["mysqld"]["port"] == "3310"
    assert parser["client"]["user"] == "root"
    # A None value must be written as a bare key (no '=').
    assert parser["mysqld"]["skip_name_resolve"] is None


def test_write_option_file_bare_key_has_no_equals(sandboxlib, tmp_path):
    path = str(tmp_path / "my.cnf")
    sandboxlib._write_option_file(path, {"mysqld": {"skip_name_resolve": None}})
    with open(path) as f:
        lines = [line.strip() for line in f]
    assert "skip_name_resolve" in lines
    assert "skip_name_resolve =" not in lines


# --------------------------------------------------------------------------- #
# _build_option_file
# --------------------------------------------------------------------------- #
def _innodb(sandboxlib):
    """A representative InnoDB option set as _innodb_opts() would produce it."""
    return dict(sandboxlib._INNODB_OPTS, innodb_log_file_size="4M")


def test_build_option_file_is_raw_no_replication(sandboxlib):
    sections = sandboxlib._build_option_file(
        3310, "/sb/3310", "/opt/mariadb", None, {}, _innodb(sandboxlib))
    mysqld = sections["mysqld"]
    for key in ("log_bin", "binlog_format", "gtid_strict_mode",
                "gtid_domain_id", "log_slave_updates", "report_host",
                "report_port"):
        assert key not in mysqld, "raw sandbox must not configure " + key


def test_build_option_file_core_and_innodb_keys(sandboxlib):
    innodb = _innodb(sandboxlib)
    sections = sandboxlib._build_option_file(
        3310, "/sb/3310", "/opt/mariadb", None, {}, innodb)
    mysqld = sections["mysqld"]
    assert mysqld["port"] == 3310
    assert mysqld["basedir"] == "/opt/mariadb"
    assert mysqld["datadir"].endswith("/sandboxdata")
    # The server writes its own pid-file only on Windows; on POSIX the start
    # script owns it, so the option is intentionally absent there.
    if os.name == "nt":
        assert mysqld["pid_file"].endswith("3310.pid")
    else:
        assert "pid_file" not in mysqld
    # InnoDB sizing must be present and match the sizing passed in.
    for key, value in innodb.items():
        assert mysqld[key] == value
    assert sections["client"]["port"] == 3310
    assert sections["client"]["user"] == "root"


def test_build_option_file_server_id_omitted_by_default(sandboxlib):
    mysqld = sandboxlib._build_option_file(
        3310, "/sb/3310", "/opt/mariadb", None, {}, _innodb(sandboxlib))[
            "mysqld"]
    assert "server_id" not in mysqld


def test_build_option_file_server_id_when_requested(sandboxlib):
    mysqld = sandboxlib._build_option_file(
        3310, "/sb/3310", "/opt/mariadb", 77, {}, _innodb(sandboxlib))["mysqld"]
    assert mysqld["server_id"] == 77


def test_build_option_file_applies_overrides(sandboxlib):
    mysqld = sandboxlib._build_option_file(
        3310, "/sb/3310", "/opt/mariadb", None,
        {"max_connections": "50"}, _innodb(sandboxlib))["mysqld"]
    assert mysqld["max_connections"] == "50"


def test_build_option_file_rejects_port_override(sandboxlib):
    with pytest.raises(sandboxlib.Error, match="port"):
        sandboxlib._build_option_file(
            3310, "/sb/3310", "/opt/mariadb", None, {"port": "9999"},
            _innodb(sandboxlib))


# --------------------------------------------------------------------------- #
# _build_option_file SSL wiring
# --------------------------------------------------------------------------- #
def _ssl_files():
    return {
        "ca_cert": "/sb/3310/ca-cert.pem",
        "server_cert": "/sb/3310/server-cert.pem",
        "server_key": "/sb/3310/server-key.pem",
        "client_cert": "/sb/3310/client-cert.pem",
        "client_key": "/sb/3310/client-key.pem",
    }


def test_build_option_file_no_ssl_by_default(sandboxlib):
    sections = sandboxlib._build_option_file(
        3310, "/sb/3310", "/opt/mariadb", None, {}, _innodb(sandboxlib))
    for key in ("ssl_ca", "ssl_cert", "ssl_key"):
        assert key not in sections["mysqld"]
        assert key not in sections["client"]


def test_build_option_file_enables_ssl_for_server_and_client(sandboxlib):
    ssl = _ssl_files()
    sections = sandboxlib._build_option_file(
        3310, "/sb/3310", "/opt/mariadb", None, {}, _innodb(sandboxlib), ssl)
    # The server gets the CA + server pair.
    assert sections["mysqld"]["ssl_ca"] == ssl["ca_cert"]
    assert sections["mysqld"]["ssl_cert"] == ssl["server_cert"]
    assert sections["mysqld"]["ssl_key"] == ssl["server_key"]
    # The bundled clients get the CA + client pair.
    assert sections["client"]["ssl_ca"] == ssl["ca_cert"]
    assert sections["client"]["ssl_cert"] == ssl["client_cert"]
    assert sections["client"]["ssl_key"] == ssl["client_key"]


def test_build_option_file_overrides_win_over_ssl(sandboxlib):
    # 'mariadbdOptions' are applied after the SSL defaults, so a user can point
    # the server at a different certificate.
    mysqld = sandboxlib._build_option_file(
        3310, "/sb/3310", "/opt/mariadb", None,
        {"ssl_cert": "/custom/cert.pem"}, _innodb(sandboxlib),
        _ssl_files())["mysqld"]
    assert mysqld["ssl_cert"] == "/custom/cert.pem"
