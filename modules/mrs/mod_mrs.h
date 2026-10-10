/*
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

#ifndef MODULES_MRS_MOD_MRS_H_
#define MODULES_MRS_MOD_MRS_H_

#include <map>
#include <memory>
#include <string>

#include "modules/mod_extensible_object.h"
#include "modules/mrs/core/mrs_ddl_executor.h"
#include "mysqlshdk/include/scripting/types.h"

namespace shcore {
class IShell_core;
}

namespace mysqlsh {

class ShellBaseSession;

namespace mrs {

/**
 * \defgroup mrs mrs
 * \ingroup ShellAPI
 * $(MRS_BRIEF)
 */
class SHCORE_PUBLIC Mrs : public Extensible_object {
 public:
  explicit Mrs(shcore::IShell_core *owner);

  std::string class_name() const override { return "Mrs"; }

  // Registers the SQL handler that runs the REST SQL statements. Does
  // nothing when no SQL handler registry is active.
  void register_sql_handler();

  // Runs a REST SQL script against a session and returns the shell result
  // of its statements. The current service and schema are kept per
  // session object.
  shcore::Value run_rest_sql(const std::shared_ptr<ShellBaseSession> &session,
                             const std::string &sql);

 private:
  shcore::IShell_core &m_shell_core;
  // The USE REST SERVICE / SCHEMA state and the metadata check cache of
  // each session. Keyed by the session object, not its connection id
  // (ids repeat across servers); entries of closed sessions are dropped.
  std::map<std::weak_ptr<ShellBaseSession>, ::mrs::Executor_state,
           std::owner_less<>>
      m_states;
};

}  // namespace mrs
}  // namespace mysqlsh

#endif  // MODULES_MRS_MOD_MRS_H_
