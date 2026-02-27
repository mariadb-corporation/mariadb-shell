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

#ifndef MODULES_ADMINAPI_COMMON_RESET_REPLICATION_ACCOUNTS_PASSWORD_H_
#define MODULES_ADMINAPI_COMMON_RESET_REPLICATION_ACCOUNTS_PASSWORD_H_

#include <functional>
#include <variant>

#include "modules/adminapi/common/api_options.h"

namespace mysqlsh::dba {

class Base_cluster_impl;
class Cluster_impl;
class Cluster_set_impl;
class Replica_set_impl;

class Reset_replication_accounts_password {
 public:
  explicit Reset_replication_accounts_password(
      Base_cluster_impl &topo, const Force_options &options) noexcept;

  Reset_replication_accounts_password(
      const Reset_replication_accounts_password &) = delete;
  Reset_replication_accounts_password(Reset_replication_accounts_password &&) =
      delete;
  Reset_replication_accounts_password &operator=(
      const Reset_replication_accounts_password &) = delete;
  Reset_replication_accounts_password &operator=(
      Reset_replication_accounts_password &&) = delete;

  ~Reset_replication_accounts_password() = default;

 protected:
  void do_run();

  static constexpr bool supports_undo() noexcept { return false; }

 private:
  using Topology_ref = std::variant<std::reference_wrapper<Cluster_impl>,
                                    std::reference_wrapper<Cluster_set_impl>,
                                    std::reference_wrapper<Replica_set_impl>>;

  static Topology_ref make_topology_ref(Base_cluster_impl &topo);

  struct Instance_online {
    Scoped_instance instance;
    bool is_read_replica{false};
  };

  struct Prepared_targets {
    std::vector<Instance_online> online_instances;
    std::vector<std::string> skipped_instances;
  };

  struct Cluster_plan {
    std::shared_ptr<Cluster_impl> cluster;
    Prepared_targets targets;
  };

  using Clusterset_plan = std::vector<Cluster_plan>;

  /**
   * Auxiliary method to ask the user whether they want to continue with the
   * operation when one or more instances are not ONLINE (and would be skipped).
   *
   * @return true if the user answered 'yes' and wants to continue with the
   *         operation, otherwise false.
   */
  bool prompt_to_force_reset() const;

  bool should_prompt_force() const noexcept {
    return current_shell_options()->get().wizards &&
           !m_options.force.has_value();
  }

  // Generic helper (used by Cluster/ClusterSet/ReplicaSet)
  using Connect_fn = std::function<std::shared_ptr<mysqlsh::dba::Instance>(
      const std::string &endpoint)>;

  /**
   * Validate if the given instance is reachable.
   *
   * This function verifies whether it is possible to connect to the given
   * instance. If it is not reachable, an error may be issued depending on the
   * specified 'force' option value (or the user's response if prompted).
   *
   * The connection options from the topology (in particular authentication
   * options) will be used to connect to the instance, since it is assumed that
   * the same login credentials can be used to connect to all instances.
   *
   * NOTE: The provided target lists will be updated by this function.
   *
   * @param instance_address String with the address <host>:<port> of the
   *                         instance to check.
   * @param is_read_replica True if the instance is a read-replica.
   */
  void ensure_instance_reachable(const std::string &instance_address,
                                 bool is_read_replica,
                                 const Connect_fn &connect,
                                 Prepared_targets &targets) const;

  /**
   * Auxiliary function to handle instances that are not ONLINE.
   *
   * This function performs the necessary action depending on the specified
   * 'force' option value (or the user's response if prompted), issuing an error
   * or updating the provided list of instances to be skipped.
   */
  void handle_not_online_instances(const std::string &instance_address,
                                   const std::string &instance_state,
                                   const char *api_class,
                                   Prepared_targets &targets) const;

  void reset_clusterset_replication_channel(Cluster_set_impl &cs,
                                            Cluster_impl &cluster) const;

  bool prepare_cluster_targets(Cluster_impl &cluster,
                               Prepared_targets &targets);
  bool prepare_replicaset_targets(Replica_set_impl &rs,
                                  Prepared_targets &targets);
  bool prepare_clusterset_targets(Cluster_set_impl &cs, Clusterset_plan &plan);
  void reset_cluster_replication_accounts(Cluster_impl &cluster,
                                          const Prepared_targets &targets);
  void reset_replicaset_replication_accounts(Replica_set_impl &rs,
                                             const Prepared_targets &targets);
  void print_summary(const Prepared_targets &targets) const noexcept;

 private:
  Topology_ref m_topo;
  Cluster_type m_topo_type = Cluster_type::NONE;
  Force_options m_options;
};

}  // namespace mysqlsh::dba

#endif  // MODULES_ADMINAPI_COMMON_RESET_REPLICATION_ACCOUNTS_PASSWORD_H_
