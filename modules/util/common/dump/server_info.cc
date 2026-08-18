/*
 * Copyright (c) 2024, 2026, Oracle and/or its affiliates.
 * Copyright (c) 2026, MariaDB plc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is designed to work with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms,
 * as designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have either included with
 * the program or referenced in the documentation.
 *
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "modules/util/common/dump/server_info.h"

#include <mysqld_error.h>

#include <string>
#include <utility>

#include "mysqlshdk/include/shellcore/console.h"
#include "mysqlshdk/libs/mysql/instance.h"
#include "mysqlshdk/libs/mysql/replication.h"
#include "mysqlshdk/libs/mysql/utils.h"
#include "mysqlshdk/libs/utils/debug.h"
#include "mysqlshdk/libs/utils/logger.h"
#include "mysqlshdk/libs/utils/utils_general.h"
#include "mysqlshdk/libs/utils/utils_string.h"

#include "modules/util/common/dump/server_features.h"
#include "modules/util/dump/dump_errors.h"

namespace mysqlsh {
namespace dump {
namespace common {

namespace {

void initialize_sysvars(Server_variables *sysvars) {
  if (const auto hostname = sysvars->all.find("hostname");
      sysvars->all.end() != hostname) {
    sysvars->hostname = hostname->second;
  }

  if (const auto lower_case_table_names =
          sysvars->all.find("lower_case_table_names");
      sysvars->all.end() != lower_case_table_names) {
    sysvars->lower_case_table_names =
        shcore::lexical_cast<int8_t>(lower_case_table_names->second);
  }

  if (const auto partial_revokes = sysvars->all.find("partial_revokes");
      sysvars->all.end() != partial_revokes) {
    sysvars->partial_revokes = mysqlshdk::mysql::sysvar_to_bool(
        "partial_revokes", partial_revokes->second);
  }

  if (const auto port = sysvars->all.find("port"); sysvars->all.end() != port) {
    sysvars->port = shcore::lexical_cast<uint16_t>(port->second);
  }

  if (const auto server_uuid = sysvars->all.find("server_uuid");
      sysvars->all.end() != server_uuid) {
    sysvars->server_uuid = server_uuid->second;
  }

  if (const auto version = sysvars->all.find("version");
      sysvars->all.end() != version) {
    sysvars->version = version->second;
  }
}

const auto optional_string = [](const shcore::json::Value &o, const char *n) {
  return shcore::json::optional(o, n, true).value_or(std::string{});
};

const auto optional_uint = [](const shcore::json::Value &o, const char *n) {
  return shcore::json::optional_uint(o, n).value_or(0);
};

}  // namespace

std::string gtid_executed(
    const std::shared_ptr<mysqlshdk::db::ISession> &session,
    const Server_version &version) {
  // MariaDB has no gtid_executed. Its equivalent is gtid_current_pos, the union
  // of what this server wrote to its own binary log and what it replicated from
  // elsewhere - which is what mariabackup records as well, and the position a
  // server provisioned from this dump has to resume replication from.
  // See MARIADB_DUMP_LOAD.md section 4.4.
  const auto variable = is_maria_db_dialect(version)
                            ? "@@GLOBAL.gtid_current_pos"
                            : "@@GLOBAL.GTID_EXECUTED";

  try {
    const auto result = session->query(std::string{"SELECT "}.append(variable));

    if (const auto row = result->fetch_one()) {
      return row->get_string(0);
    }
  } catch (const mysqlshdk::db::Error &e) {
    log_error("Failed to fetch value of %s: %s.", variable, e.format().c_str());
  }

  return {};
}

const char *binlog_status_keyword(const Server_version &version) {
  return version.is_maria_db
             ? "MASTER"
             : mysqlshdk::mysql::get_binary_logs_keyword(version.number, true);
}

Binlog binlog(const std::shared_ptr<mysqlshdk::db::ISession> &session,
              const Server_version &version, bool quiet) {
  Binlog binlog;
  bool status_unavailable = false;

  try {
    auto keyword = binlog_status_keyword(version);

    DBUG_EXECUTE_IF("dumper_dump_mariadb", {
      // We need the binlog query to not be affected by this dbug flag, because
      // it has to work even when running in mysql.
      keyword = mysqlshdk::mysql::get_binary_logs_keyword(
          session->get_server_version(), true);
    });

    const auto result =
        session->query(shcore::str_format("SHOW %s STATUS", keyword));

    if (const auto row = result->fetch_one()) {
      binlog.file.name = row->get_string(0);              // File
      binlog.file.position = row->get_uint(1);            // Position
      binlog.startup_options.do_db = row->get_string(2);  // Binlog_Do_DB
      binlog.startup_options.ignore_db =
          row->get_string(3);  // Binlog_Ignore_DB

      if (result->get_metadata().size() > 4) {
        binlog.gtid_executed = row->get_string(4);  // Executed_Gtid_Set
      }
    }
  } catch (const mysqlshdk::db::Error &e) {
    if (e.code() == ER_SPECIFIC_ACCESS_DENIED_ERROR) {
      if (!quiet) {
        current_console()->print_warning(
            "Could not fetch the binary log information: " + e.format());
      }

      status_unavailable = true;
    } else {
      throw;
    }
  }

  // A current MariaDB does report a fifth column, but it is gtid_binlog_pos,
  // which misses everything a server replicated without log_slave_updates.
  // Ask for gtid_current_pos instead - always, so that servers old enough to
  // report four columns behave the same way.
  if (status_unavailable || is_maria_db_dialect(version)) {
    binlog.gtid_executed = gtid_executed(session, version);
  }

  return binlog;
}

Binlog binlog(const std::shared_ptr<mysqlshdk::db::ISession> &session,
              bool quiet) {
  return binlog(session, server_version(session), quiet);
}

void serialize(const Binlog &binlog, shcore::JSON_dumper *dumper) {
  dumper->start_object();

  dumper->append("file", binlog.file.name);
  dumper->append("position", binlog.file.position);
  dumper->append("gtidExecuted", binlog.gtid_executed);

  dumper->end_object();
}

Binlog binlog(const shcore::json::Value &object) {
  Binlog binlog;

  binlog.file.name = optional_string(object, "file");
  binlog.file.position = optional_uint(object, "position");
  binlog.gtid_executed = optional_string(object, "gtidExecuted");

  return binlog;
}

Server_version server_version(
    const std::shared_ptr<mysqlshdk::db::ISession> &session) {
  const auto result = session->query("SELECT @@GLOBAL.VERSION");

  if (const auto row = result->fetch_one()) {
    return server_version(row->get_string(0));
  } else {
    THROW_ERROR(SHERR_DUMP_IC_FAILED_TO_FETCH_VERSION);
  }

  return {};
}

Server_version server_version(const mysqlshdk::utils::Version &number,
                              bool is_maria_db) {
  using mysqlshdk::utils::Version;

  Server_version version;

  version.number = number;
  version.is_maria_db = is_maria_db;

  if (!is_maria_db) {
    if (number < Version(5, 7, 0)) {
      version.is_5_6 = true;
    } else if (number < Version(8, 0, 0)) {
      version.is_5_7 = true;
    } else {
      version.is_8_0 = true;
    }
  }

  return version;
}

Server_version server_version(std::string_view ver) {
  using mysqlshdk::utils::Version;

  Server_version version;

  version.number = Version(ver);

  DBUG_EXECUTE_IF("dumper_dump_mariadb", {
    version.number = Version("10.4.18-MariaDB-1:10.4.18+maria~focal");
  });

  if (std::string::npos !=
      shcore::str_lower(version.number.get_extra()).find("mariadb")) {
    version.is_maria_db = true;
#ifndef MARIADB_BUILD
    // A MySQL build supports dumping *from* MariaDB as a migration path, and
    // does so by pretending the server is an ancient MySQL: that turns off
    // every 8.0-and-later gate at once, which is what makes the produced dump
    // loadable into MySQL. Keep it, so that path behaves exactly as it always
    // has.
    //
    // A MariaDB build must not do this - it has to carry MariaDB's own
    // features, which the remap is precisely what suppresses. It takes the real
    // version instead and leaves all three MySQL flags false; every gate is
    // asked as a feature question in server_features.h.
    // See MARIADB_DUMP_LOAD.md sections 2 and 7.1.
    version.number = Version("5.6.0-" + version.number.get_full());
    version.is_5_6 = true;
#endif  // !MARIADB_BUILD
  } else if (version.number < Version(5, 7, 0)) {
    version.is_5_6 = true;
  } else if (version.number < Version(8, 0, 0)) {
    version.is_5_7 = true;
  } else {
    version.is_8_0 = true;
  }

  return version;
}

Server_variables server_variables(
    const std::shared_ptr<mysqlshdk::db::ISession> &session) {
  Server_variables sysvars;

  {
    const auto result = session->query("SHOW GLOBAL VARIABLES");

    while (const auto row = result->fetch_one()) {
      sysvars.all.emplace(row->get_string(0), row->get_string(1));
    }
  }

  initialize_sysvars(&sysvars);

  return sysvars;
}

Replication_topology replication_topology(
    const std::shared_ptr<mysqlshdk::db::ISession> &session) {
  mysqlshdk::mysql::Instance instance{session};

  Replication_topology topology;

  topology.canonical_address = instance.get_canonical_address();

  topology.group_name =
      instance.get_sysvar_string("group_replication_group_name")
          .value_or(std::string{});

  topology.view_change_uuid =
      instance.get_sysvar_string("group_replication_view_change_uuid")
          .value_or(std::string{});

  if (session->query("SHOW SCHEMAS LIKE 'mysql_innodb_cluster_metadata'")
          ->fetch_one()) {
    const auto query = [&session](std::string_view sql, const char *table,
                                  const char *column) {
      try {
        const auto result = session->query(sql);

        if (const auto row = result->fetch_one()) {
          return row->get_string(0);
        }
      } catch (const mysqlshdk::db::Error &e) {
        log_warning(
            "Failed to fetch '%s' from mysql_innodb_cluster_metadata.%s: %s",
            column, table, e.format().c_str());
      }

      return std::string{};
    };

    topology.cluster_id = query(
        "SELECT cluster_id FROM mysql_innodb_cluster_metadata.v2_this_instance",
        "v2_this_instance", "cluster_id");

    if (topology.cluster_id.empty()) {
      topology.cluster_id = query(
          "SELECT cluster_id FROM mysql_innodb_cluster_metadata.instances "
          "WHERE CAST(mysql_server_uuid AS binary)=CAST(@@server_uuid AS "
          "binary)",
          "instances", "cluster_id");
    }

    topology.cluster_set_id = query(
        "SELECT clusterset_id FROM mysql_innodb_cluster_metadata.v2_cs_members",
        "v2_cs_members", "clusterset_id");
  }

  return topology;
}

void serialize(const Server_info &info, shcore::JSON_dumper *dumper,
               bool binlog, bool sysvars) {
  const auto append_non_empty = [dumper](const char *name,
                                         const std::string &value) {
    if (!value.empty()) {
      dumper->append(name, value);
    }
  };

  dumper->start_object();

  // The vendor is recorded explicitly rather than left to be re-detected by
  // substring-matching the version string: a MariaDB build writes the server's
  // real version, so there is no longer a "-MariaDB" suffix to rely on in every
  // case. See MARIADB_DUMP_LOAD.md section 6.3.
  dumper->append("vendor", info.version.is_maria_db ? "mariadb" : "mysql");

  if (binlog) {
    dumper->append("binlog");
    serialize(info.binlog, dumper);
  }

  if (sysvars) {
    dumper->append("sysvars");
    dumper->start_object();

    for (const auto &var : info.sysvars.all) {
      dumper->append(var.first, var.second);
    }

    dumper->end_object();  // sysvars
  } else {
    // store just some basic information
    dumper->append("version", info.version.number.get_full());
    dumper->append("hostname", info.sysvars.hostname);
    dumper->append("port", info.sysvars.port);
    dumper->append("serverUuid", info.sysvars.server_uuid);
  }

  dumper->append("topology");
  dumper->start_object();

  dumper->append("canonicalAddress", info.topology.canonical_address);
  append_non_empty("groupName", info.topology.group_name);
  append_non_empty("viewChangeUuid", info.topology.view_change_uuid);
  append_non_empty("clusterId", info.topology.cluster_id);
  append_non_empty("clusterSetId", info.topology.cluster_set_id);

  dumper->end_object();  // topology

  dumper->end_object();
}

Server_info server_info(const shcore::json::Value &object) {
  Server_info info;

  if (const auto log = shcore::json::optional_object(object, "binlog");
      log.has_value()) {
    info.binlog = binlog(*log);
  }

  if (auto sysvars = shcore::json::optional_map(object, "sysvars");
      sysvars.has_value()) {
    info.sysvars.all = std::move(*sysvars);
    initialize_sysvars(&info.sysvars);
    info.version = server_version(info.sysvars.version);
  } else {
    if (const auto version = optional_string(object, "version");
        !version.empty()) {
      info.version = server_version(version);
    }

    info.sysvars.hostname = optional_string(object, "hostname");
    info.sysvars.server_uuid = optional_string(object, "serverUuid");
  }

  if (const auto vendor = optional_string(object, "vendor"); !vendor.empty()) {
    // an explicit vendor field wins over whatever the version string suggested
    if (const auto is_maria_db = shcore::str_caseeq(vendor, "mariadb");
        is_maria_db != info.version.is_maria_db) {
      info.version = server_version(info.version.number, is_maria_db);
    }
  }

  if (const auto topology = shcore::json::optional_object(object, "topology");
      topology.has_value()) {
    info.topology.canonical_address =
        optional_string(*topology, "canonicalAddress");
    info.topology.group_name = optional_string(*topology, "groupName");
    info.topology.view_change_uuid =
        optional_string(*topology, "viewChangeUuid");
    info.topology.cluster_id = optional_string(*topology, "clusterId");
    info.topology.cluster_set_id = optional_string(*topology, "clusterSetId");
  }

  return info;
}

}  // namespace common
}  // namespace dump
}  // namespace mysqlsh
