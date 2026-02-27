/*
 * Copyright (c) 2026, Oracle and/or its affiliates.
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

#include "modules/adminapi/common/reset_replication_accounts_password.h"

#include "modules/adminapi/cluster_set/cluster_set_impl.h"
#include "modules/adminapi/common/async_topology.h"
#include "modules/adminapi/common/dba_errors.h"
#include "modules/adminapi/replica_set/replica_set_impl.h"

namespace mysqlsh::dba {

Reset_replication_accounts_password::Topology_ref
Reset_replication_accounts_password::make_topology_ref(
    Base_cluster_impl &topo) {
  switch (topo.get_type()) {
    case Cluster_type::GROUP_REPLICATION:
      return std::ref(static_cast<Cluster_impl &>(topo));

    case Cluster_type::REPLICATED_CLUSTER:
      return std::ref(static_cast<Cluster_set_impl &>(topo));

    case Cluster_type::ASYNC_REPLICATION:
      return std::ref(static_cast<Replica_set_impl &>(topo));

    default:
      // Preconditions should prevent this
      throw std::logic_error(
          "Unsupported topology type for resetReplicationAccountsPassword");
  }
}

Reset_replication_accounts_password::Reset_replication_accounts_password(
    Base_cluster_impl &topo, const Force_options &options) noexcept
    : m_topo{make_topology_ref(topo)},
      m_topo_type{topo.get_type()},
      m_options{options} {}

bool Reset_replication_accounts_password::prompt_to_force_reset() const {
  auto console = mysqlsh::current_console();
  console->print_info();

  const bool result =
      console->confirm(
          "Do you want to continue anyway (the replication account passwords "
          "for the instance will not be reset)?",
          Prompt_answer::NO) == Prompt_answer::YES;

  console->print_info();
  return result;
}

void Reset_replication_accounts_password::ensure_instance_reachable(
    const std::string &instance_address, bool is_read_replica,
    const Connect_fn &connect, Prepared_targets &targets) const {
  try {
    auto inst = connect(instance_address);
    targets.online_instances.push_back(
        {Scoped_instance{std::move(inst)}, is_read_replica});
  } catch (const std::exception &err) {
    auto console = mysqlsh::current_console();

    if (!m_options.get_force()) {
      console->print_error(shcore::str_format(
          "Unable to connect to instance '%s'. Please verify connection "
          "credentials and make sure the instance is available.",
          instance_address.c_str()));

      const bool continue_reset =
          should_prompt_force() && prompt_to_force_reset();

      if (!continue_reset) {
        throw shcore::Exception(err.what(), SHERR_DBA_UNREACHABLE_INSTANCES);
      }

      targets.skipped_instances.push_back(instance_address);

      return;
    }

    // force = true
    targets.skipped_instances.push_back(instance_address);

    console->print_note(shcore::str_format(
        "The replication account passwords for instance '%s' will not be reset "
        "because the instance is not reachable.",
        instance_address.c_str()));
    console->print_info();
  }
}

void Reset_replication_accounts_password::handle_not_online_instances(
    const std::string &instance_address, const std::string &instance_state,
    const char *api_class, Prepared_targets &targets) const {
  auto console = mysqlsh::current_console();
  const bool prompt_force = should_prompt_force();

  if (!m_options.get_force()) {
    auto message = shcore::str_format(
        "The replication account passwords for instance '%s' cannot be reset "
        "because it is in a '%s' state. Replication account passwords can only "
        "be rotated for ONLINE instances. "
        "Ensure the instance is reachable and use <%s>.<<<rejoinInstance>>>() "
        "to rejoin it and refresh its internal replication account "
        "credentials.",
        instance_address.c_str(), instance_state.c_str(), api_class);

    if (prompt_force) {
      message +=
          " You can choose to proceed with the operation and skip resetting "
          "this instance's replication account passwords.";
    } else {
      message += shcore::str_format(
          " You can also run <%s>.<<<resetReplicationAccountsPassword>>>() "
          "with the force option enabled to skip instances that are not "
          "ONLINE.",
          api_class);
    }

    console->print_error(message);

    const bool continue_reset = prompt_force && prompt_to_force_reset();

    if (!continue_reset) {
      const auto err_msg =
          shcore::str_format("The instance '%s' is '%s' (it must be ONLINE).",
                             instance_address.c_str(), instance_state.c_str());
      const auto error_code = (m_topo_type == Cluster_type::ASYNC_REPLICATION)
                                  ? SHERR_DBA_ASYNC_MEMBER_INVALID_STATUS
                                  : SHERR_DBA_GROUP_MEMBER_NOT_ONLINE;
      throw shcore::Exception(err_msg, error_code);
    }

    targets.skipped_instances.push_back(instance_address);
    return;
  }

  // force = true
  targets.skipped_instances.push_back(instance_address);

  console->print_note(shcore::str_format(
      "Skipping reset of the replication account passwords for instance '%s' "
      "because it is '%s'.",
      instance_address.c_str(), instance_state.c_str()));
  console->print_info();
}

void Reset_replication_accounts_password::reset_clusterset_replication_channel(
    Cluster_set_impl &cs, Cluster_impl &cluster) const {
  // Get current replication channel options for this cluster
  auto ar_options =
      cs.get_clusterset_replication_options(cluster.get_id(), nullptr);

  // Rotate the password for the ClusterSet replication account associated with
  // this cluster
  Replication_account repl_account{cs};
  auto account = repl_account.refresh_replication_user(*cs.get_primary_master(),
                                                       cluster.get_id(), false);

  ar_options.repl_credentials = std::move(account.auth);

  // If this is the PRIMARY cluster then there are no inbound ClusterSet
  // channels to reconfigure
  if (cluster.is_primary_cluster()) return;

  // Ensure the replica cluster is caught up before rotating the
  // ClusterSet replication channel credentials
  mysqlsh::current_console()->print_info(
      "* Waiting for the Cluster to synchronize with the PRIMARY Cluster...");
  cs.sync_transactions(*cluster.get_cluster_server(),
                       {k_clusterset_async_channel_name},
                       current_shell_options()->get().dba_gtid_wait_timeout);

  // Reset the ClusterSet replication channel
  log_info(
      "Resetting ClusterSet replication channel credentials for "
      "Cluster '%s' (channel: %s)",
      cluster.get_name().c_str(), k_clusterset_async_channel_name);

  // Apply the updated credentials on all members of the replica cluster
  cluster.execute_in_members(
      {}, cluster.get_primary_master()->get_connection_options(), {},
      [&](const std::shared_ptr<Instance> &target,
          const mysqlshdk::gr::Member &) {
        mysqlsh::current_console()->print_info(shcore::str_format(
            "* Updating ClusterSet replication credentials on '%s' "
            "(channel: %s). The replication channel will be temporarily "
            "stopped and restarted to apply the new credentials.",
            target->descr().c_str(), k_clusterset_async_channel_name));

        async_update_replica_credentials(
            target.get(), k_clusterset_async_channel_name, ar_options, false);
        return true;
      });
}

bool Reset_replication_accounts_password::prepare_cluster_targets(
    Cluster_impl &cluster, Prepared_targets &targets) {
  switch (auto auth_type = cluster.query_cluster_auth_type(); auth_type) {
    case Replication_auth_type::CERT_ISSUER:
    case Replication_auth_type::CERT_SUBJECT:
      mysqlsh::current_console()->print_note(shcore::str_format(
          "The Cluster's authentication type is '%s', which "
          "doesn't use passwords for replication accounts.\n",
          to_string(auth_type).c_str()));
      return true;
    default:
      break;
  }

  mysqlsh::current_console()->print_info(shcore::str_format(
      "* Verifying Cluster '%s' status", cluster.get_name().c_str()));

  auto connect = [&](const std::string &ep) {
    return cluster.get_session_to_cluster_instance(ep);
  };

  const auto instance_defs = cluster.get_instances_with_state();
  for (const auto &instance_def : instance_defs) {
    const auto state = instance_def.second.state;
    const auto &endpoint = instance_def.first.endpoint;

    if (state == mysqlshdk::gr::Member_state::ONLINE) {
      ensure_instance_reachable(endpoint, false, connect, targets);
    } else {
      handle_not_online_instances(endpoint, mysqlshdk::gr::to_string(state),
                                  "Cluster", targets);
    }
  }

  cluster.iterate_read_replicas(
      [&](const Instance_metadata &md,
          const mysqlshdk::mysql::Replication_channel &) {
        ensure_instance_reachable(md.endpoint, true, connect, targets);
        return true;
      });

  return false;
}

bool Reset_replication_accounts_password::prepare_clusterset_targets(
    Cluster_set_impl &cs, Clusterset_plan &plan) {
  auto console = mysqlsh::current_console();

  switch (auto auth_type = cs.query_cluster_auth_type(); auth_type) {
    case Replication_auth_type::CERT_ISSUER:
    case Replication_auth_type::CERT_SUBJECT:
      console->print_note(shcore::str_format(
          "The ClusterSet's authentication type is '%s', which "
          "doesn't use passwords for replication accounts.\n",
          to_string(auth_type).c_str()));
      return true;
    default:
      break;
  }

  console->print_info("* Verifying ClusterSet status");

  std::vector<Cluster_set_member_metadata> all_clusters;
  cs.get_metadata_storage()->get_cluster_set(cs.get_id(), true, nullptr,
                                             &all_clusters);

  // Gather eligible clusters (and fail-fast if any is unhealthy)
  std::vector<std::shared_ptr<Cluster_impl>> eligible_clusters;
  eligible_clusters.reserve(all_clusters.size());

  for (const auto &cluster_md : all_clusters) {
    auto cluster = cs.get_cluster_object(cluster_md, true);
    const auto cl_status = cs.get_cluster_global_status(cluster.get());

    // INVALIDATED Clusters are to be skipped
    if (cl_status == Cluster_global_status::INVALIDATED) {
      console->print_note(
          shcore::str_format("Cluster '%s' is INVALIDATED and will be skipped.",
                             cluster->get_name().c_str()));
      continue;
    }

    if (cl_status != Cluster_global_status::OK) {
      console->print_error(shcore::str_format(
          "The Cluster '%s' has a global status of '%s' (it must be OK). "
          "Ensure the Cluster is healthy and run "
          "<ClusterSet>.<<<resetReplicationAccountsPassword>>>() again.",
          cluster->get_name().c_str(), to_string(cl_status).c_str()));

      throw shcore::Exception(
          shcore::str_format(
              "Cluster '%s' has global status '%s' (it must be OK).",
              cluster->get_name().c_str(), to_string(cl_status).c_str()),
          SHERR_DBA_CLUSTER_STATUS_INVALID);
    }

    eligible_clusters.emplace_back(std::move(cluster));
  }

  // Prepare per-cluster targets
  plan.clear();
  plan.reserve(eligible_clusters.size());

  for (const auto &cluster : eligible_clusters) {
    Cluster_plan entry;
    entry.cluster = cluster;

    // Check the cluster targets
    const bool no_op = prepare_cluster_targets(*entry.cluster, entry.targets);

    if (no_op) {
      // cert-based auth used in this Cluster, no-op
      continue;
    }

    plan.emplace_back(std::move(entry));
  }

  return false;
}

bool Reset_replication_accounts_password::prepare_replicaset_targets(
    Replica_set_impl &rs, Prepared_targets &targets) {
  switch (const auto auth_type = rs.query_cluster_auth_type(); auth_type) {
    case Replication_auth_type::CERT_ISSUER:
    case Replication_auth_type::CERT_SUBJECT:
      mysqlsh::current_console()->print_note(shcore::str_format(
          "The ReplicaSet's authentication type is '%s', which doesn't use "
          "passwords for replication accounts.\n",
          to_string(auth_type).c_str()));
      return true;
    default:
      break;
  }

  mysqlsh::current_console()->print_info("* Verifying ReplicaSet status");

  auto connect = [&](const std::string &ep) {
    return rs.get_session_to_cluster_instance(ep);
  };

  auto topology_mng = rs.get_topology_manager();
  for (const auto &node : topology_mng->topology()->nodes()) {
    const auto node_status = node->status();
    const auto instance_address = node->get_primary_member()->endpoint;

    if (node_status == mysqlsh::dba::topology::Node_status::ONLINE) {
      ensure_instance_reachable(instance_address, false, connect, targets);
    } else {
      handle_not_online_instances(instance_address, to_string(node_status),
                                  "ReplicaSet", targets);
    }
  }

  return false;
}

void Reset_replication_accounts_password::reset_cluster_replication_accounts(
    Cluster_impl &cluster, const Prepared_targets &targets) {
  const auto primary = cluster.get_cluster_server();
  const std::string primary_repr = primary->descr();

  auto md_server = cluster.get_metadata_storage()->get_md_server();
  Replication_account repl{cluster};

  mysqlsh::current_console()->print_info(shcore::str_format(
      "* Resetting replication account passwords of Cluster '%s'...",
      cluster.get_name().c_str()));

  for (const auto &t : targets.online_instances) {
    const auto &inst = t.instance;
    const std::string inst_repr = inst->descr();

    Replication_account::User_hosts user_hosts;
    log_debug("Getting replication account for instance '%s'",
              inst_repr.c_str());

    try {
      user_hosts = repl.get_replication_user(*inst, t.is_read_replica);
    } catch (const shcore::Exception &err) {
      log_error("Failed to get replication account for instance '%s': %s",
                inst_repr.c_str(), err.what());
      if (!err.is_metadata()) {
        mysqlsh::current_console()->print_error(shcore::str_format(
            "The replication account name for instance '%s' does not match the "
            "expected format for accounts created automatically by InnoDB "
            "Cluster. Please use <Cluster>.<<<rejoinInstance>>>() to ensure a "
            "supported replication account is used. Aborting password reset "
            "operation.",
            inst_repr.c_str()));
      }
      throw;
    }

    // Generate a new password and set it on the primary for all account hosts
    std::string password = mysqlshdk::mysql::generate_password();

    for (const auto &host : user_hosts.hosts) {
      log_info("Changing password for replication account '%s'@'%s' on '%s'",
               user_hosts.user.c_str(), host.c_str(), primary_repr.c_str());
      md_server->set_user_password(user_hosts.user, host, password);
    }

    if (cluster.is_cluster_set_member() && !cluster.is_primary_cluster()) {
      // Ensure the new password is replicated to the replica cluster before
      // restarting channels that will use it.
      mysqlsh::current_console()->print_info(
          "* Waiting for the Cluster to synchronize with the PRIMARY "
          "Cluster...");
      cluster.sync_transactions(
          *primary, k_clusterset_async_channel_name,
          current_shell_options()->get().dba_gtid_wait_timeout);
    }

    mysqlshdk::mysql::Replication_credentials_options cred_opts;
    cred_opts.password = std::move(password);

    const char *channel = nullptr;

    if (!t.is_read_replica) {
      channel = mysqlshdk::gr::k_gr_recovery_channel;
      log_info("Updating replication credentials on '%s' (channel: %s)",
               inst_repr.c_str(), channel);

      mysqlshdk::mysql::change_replication_credentials(
          *inst, channel, user_hosts.user, cred_opts);
      continue;
    }

    channel = mysqlsh::dba::k_read_replica_async_channel_name;
    log_info("Updating replication credentials on '%s' (channel: %s)",
             inst_repr.c_str(), channel);

    mysqlsh::current_console()->print_info(shcore::str_format(
        "* Updating replication credentials on '%s' (channel: %s). The "
        "replication receiver will be temporarily stopped and restarted.",
        inst_repr.c_str(), channel));

    mysqlshdk::mysql::stop_replication_receiver(*inst, channel);

    // Always attempt to restart the channel
    auto inst_copy = inst;  // copy Scoped_instance to keep session alive
    shcore::Scoped_callback restart([inst_copy, channel]() {
      mysqlshdk::mysql::start_replication_receiver(*inst_copy, channel);
    });

    mysqlshdk::mysql::change_replication_credentials(
        *inst, channel, user_hosts.user, cred_opts);
  }
}

void Reset_replication_accounts_password::reset_replicaset_replication_accounts(
    Replica_set_impl &rs, const Prepared_targets &targets) {
  const auto primary = rs.get_primary_master();
  const auto primary_uuid = primary->get_uuid();

  Replication_account repl{rs};

  mysqlsh::current_console()->print_info(shcore::str_format(
      "* Resetting replication account passwords of ReplicaSet '%s'...",
      rs.get_name().c_str()));

  for (const auto &t : targets.online_instances) {
    const auto &inst = t.instance;
    const auto inst_uuid = inst->get_uuid();

    auto account = repl.refresh_replication_user(*inst, false);

    // Primary has no inbound channel, just rotate its own account
    if (inst_uuid == primary_uuid) {
      continue;
    }

    // Get current replication channel options for instance
    Async_replication_options ar_options;
    rs.read_replication_options(inst_uuid, &ar_options, nullptr);
    // Add the new auth/password
    ar_options.repl_credentials = std::move(account.auth);

    log_info("Updating replication credentials on '%s' (channel: %s)",
             inst->descr().c_str(), k_replicaset_channel_name);

    mysqlsh::current_console()->print_info(shcore::str_format(
        "* Updating replication credentials on '%s' (channel: %s). The "
        "replication channel will be temporarily stopped and restarted to "
        "apply the new credentials.",
        inst->descr().c_str(), k_replicaset_channel_name));

    async_update_replica_credentials(inst.get(), k_replicaset_channel_name,
                                     ar_options, false);
  }
}

void Reset_replication_accounts_password::print_summary(
    const Prepared_targets &targets) const noexcept {
  auto console = mysqlsh::current_console();

  const std::string api_class_name =
      to_display_string(m_topo_type, Display_form::API_CLASS);

  if (!targets.skipped_instances.empty()) {
    std::string msg =
        "Not all replication account passwords were successfully reset, the "
        "following instance";

    msg.append(targets.skipped_instances.size() > 1 ? "s were " : " was ");
    msg.append("skipped: '");
    msg.append(shcore::str_join(targets.skipped_instances, "', '"));
    msg.append("'. Ensure ");
    msg.append(targets.skipped_instances.size() > 1 ? "these instances are "
                                                    : "this instance is ");
    msg.append("reachable and use <");
    msg.append(api_class_name);
    msg.append(">.<<<rejoinInstance>>>() to rejoin ");
    msg.append(targets.skipped_instances.size() > 1 ? "them" : "it");
    msg.append(" and refresh ");
    msg.append(targets.skipped_instances.size() > 1
                   ? "their internal replication account credentials."
                   : "its internal replication account credentials.");

    console->print_info();
    console->print_warning(msg);
  } else {
    console->print_info();
    console->print_info(shcore::str_format(
        "The replication account passwords of all the %s instances were "
        "successfully reset.",
        api_class_name.c_str()));
    console->print_info();
  }
}

void Reset_replication_accounts_password::do_run() {
  std::visit(
      [&](auto &&topo_ref) {
        auto &topo = topo_ref.get();
        using Topo = std::decay_t<decltype(topo)>;

        // Cluster topology handling
        if constexpr (std::is_same_v<Topo, Cluster_impl>) {
          Cluster_impl &cluster = topo;
          Prepared_targets targets;

          if (prepare_cluster_targets(cluster, targets)) return;

          reset_cluster_replication_accounts(cluster, targets);
          print_summary(targets);
        } else if constexpr (std::is_same_v<Topo, Cluster_set_impl>) {
          // ClusterSet topology handling
          Cluster_set_impl &cs = topo;
          Clusterset_plan plan;

          if (prepare_clusterset_targets(cs, plan)) return;

          Prepared_targets overall_targets;

          for (auto &cluster_plan : plan) {
            // Reset all internal replication accounts
            reset_cluster_replication_accounts(*cluster_plan.cluster,
                                               cluster_plan.targets);

            reset_clusterset_replication_channel(cs, *cluster_plan.cluster);

            // Aggregate skipped instances for the final summary
            overall_targets.skipped_instances.insert(
                overall_targets.skipped_instances.end(),
                cluster_plan.targets.skipped_instances.begin(),
                cluster_plan.targets.skipped_instances.end());
          }

          print_summary(overall_targets);
        } else if constexpr (std::is_same_v<Topo, Replica_set_impl>) {
          // ReplicaSet topology handling
          Replica_set_impl &rs = topo;
          Prepared_targets targets;

          if (prepare_replicaset_targets(rs, targets)) return;

          reset_replicaset_replication_accounts(rs, targets);
          print_summary(targets);
        }
      },
      m_topo);
}

}  // namespace mysqlsh::dba
