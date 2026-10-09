/*
 * Copyright (c) 2026, MariaDB plc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is designed to work with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms, as
 * designated in a particular file or component or in included license
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

#ifndef MODULES_MRS_MRS_SCHEMA_DEPLOYERS_H_
#define MODULES_MRS_MRS_SCHEMA_DEPLOYERS_H_

// How the shell deploys the MRS metadata schema: through the msm plugin's
// msm.deploySchema() when the plugin is loaded, otherwise with the core's
// metadata::Script_deployer, which writes msm's update log and backs the
// schema up with the shell's schema dumper and loader, like msm does with
// util.dumpSchemas() and util.loadDump().

#include <memory>
#include <string>

#include "modules/mrs/core/mrs_schema_deployment.h"
#include "mysqlshdk/include/shellcore/base_session.h"
#include "mysqlshdk/include/shellcore/ishell_core.h"

namespace mysqlsh {
namespace mrs {

// The bundled MSM project of the metadata schema,
// <share>/mrs/mariadb_rest_service.msm.project.
std::string msm_project_path();

// The deployer for CONFIGURE REST METADATA on the given session.
std::unique_ptr<::mrs::metadata::Schema_deployer> make_schema_deployer(
    shcore::IShell_core *shell_core,
    const std::shared_ptr<ShellBaseSession> &session);

}  // namespace mrs
}  // namespace mysqlsh

#endif  // MODULES_MRS_MRS_SCHEMA_DEPLOYERS_H_
