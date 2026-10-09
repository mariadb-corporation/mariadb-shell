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

#ifndef MODULES_MRS_MRS_SHELL_SESSION_H_
#define MODULES_MRS_MRS_SHELL_SESSION_H_

// The MRS core's database session, implemented on the shell's classic
// session.

#include <memory>
#include <string>

#include "modules/mrs/core/mrs_db_session.h"
#include "mysqlshdk/libs/db/session.h"

namespace mysqlsh {
namespace mrs {

class Shell_db_session : public ::mrs::Db_session {
 public:
  explicit Shell_db_session(std::shared_ptr<mysqlshdk::db::ISession> session);

  void execute_script(const std::string &script) override;
  std::string sql_mode() override;

 protected:
  ::mrs::Db_result do_query(const std::string &sql) override;
  uint64_t do_execute(const std::string &sql) override;

 private:
  std::shared_ptr<mysqlshdk::db::ISession> m_session;
};

}  // namespace mrs
}  // namespace mysqlsh

#endif  // MODULES_MRS_MRS_SHELL_SESSION_H_
