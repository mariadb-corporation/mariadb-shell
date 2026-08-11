# Copyright (c) 2017, 2024, Oracle and/or its affiliates.
# Copyright (c) 2026, MariaDB plc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# This program is designed to work with certain software (including
# but not limited to OpenSSL) that is licensed under separate terms,
# as designated in a particular file or component or in included license
# documentation.  The authors of MySQL hereby grant you an additional
# permission to link the program and your derivative works with the
# separately licensed software that they have either included with
# the program or referenced in the documentation.
#
# This program is distributed in the hope that it will be useful,  but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
# the GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

set(mysql_SOURCE
    instance.cc
    mycnf.cc
    sandbox.cc
    script.cc
    utils.cc
    plugin.cc
    undo.cc
    version_compatibility.cc
)

# User_privileges is needed by dump/load, which is built for both vendors; the
# class already handles MariaDB's grant output (see the SET DEFAULT ROLE case in
# user_privileges.cc). mariadb_gtid is the domain-based counterpart of
# gtid_utils below and is likewise needed by dump/load.
list(APPEND mysql_SOURCE
  user_privileges.cc
  mariadb_gtid.cc
  )

# gtid_utils/binlog_utils implement MySQL's uuid:N-M GTID model and binlog
# listing; MariaDB uses domain-based d-s-seq positions, handled by
# mariadb_gtid.cc above (MARIADB_DUMP_LOAD.md section 4.4), and its binary log
# needs mariadb_rpl_*, so these stay MySQL-only. lock_service is the MySQL
# locking-service plugin.
IF(NOT MARIADB_BUILD)
  list(APPEND mysql_SOURCE
    lock_service.cc
    gtid_utils.cc
    binlog_utils.cc
    )
ENDIF()


IF(HAVE_ADMIN_API)
  list(APPEND mysql_SOURCE
      repl_config.cc
      replication.cc
      async_replication.cc
      clone.cc
      repl_config.cc
      group_replication.cc)
ENDIF()


add_convenience_library(mysql ${mysql_SOURCE})
