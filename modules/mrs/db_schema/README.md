<!-- Copyright (c) 2024, 2025, Oracle and/or its affiliates.
Copyright (c) 2026, MariaDB plc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License, version 2.0,
as published by the Free Software Foundation.

This program is designed to work with certain software (including
but not limited to OpenSSL) that is licensed under separate terms, as
designated in a particular file or component or in included license
documentation.  The authors of MySQL hereby grant you an additional
permission to link the program and your derivative works with the
separately licensed software that they have either included with
the program or referenced in the documentation.

This program is distributed in the hope that it will be useful,  but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
the GNU General Public License, version 2.0, for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA -->

# MRS Metadata Schema

The MariaDB REST Service metadata schema is managed by the MariaDB Schema Management (`msm`) plugin of MariaDB Shell. Please make sure to install the MariaDB Shell for VS Code extension before working with the MRS metadata schema.

MySQL Workbench is used to design the MRS metadata schema and must be used to change the `./mariadb_rest_service.msm.project/development/wb/mariadb_rest_service.mwb` file and generate the core SQL CREATE script.

Please ensure to closely follow the process documented below to avoid manual changes to the SQL files being overwritten by the MySQL Workbench forward engineering function.

## Installing the Audit_Log_Triggers script in MySQL Workbench

MRS relies on a dedicated audit log table that tracks all changes to the MRS metadata. The audit log is implemented via triggers on each table that holds relevant data.

Instead of having to manually adjust the triggers for every change to the metadata schema, a MySQL Workbench script is used to generate the `Audit Log Triggers` SQL script instead.

Install the `./mariadb_rest_service.msm.project/development/wb/Audit_Log_Triggers_grt.py` script in `~/Library/Application Support/MySQL/Workbench/modules/Audit_Log_Triggers_grt.py` on MacOS. Use the corresponding paths on Linux and Windows.

## Generating the defaultStaticContent for config.data

In order to serve a welcome landing page when the root URI of a MariaDB REST Daemon configured for MRS is accessed (e.g. https://localhost:8440), a list of static files are defined in the `defaultStaticContent` section of the configuration JSON options as well as the `directoryIndexDirective` is set to `index.html`.

These static files are stored in the `./mariadb_rest_service.msm.project/development/default_static_content/` directory.

Please Note: Whenever changes are made to any of those files the following script needs the be run.

The script `./prepare_default_static_content.sh` and the corresponding NPM SCRIPTS entry can be used to generate a `./mariadb_rest_service.msm.project/development/sections/140-40_default_static_content.sql` file.

Please note, that the `./prepare_default_static_content.sh` script requires the installation of the following dependencies.

```sh
$ sudo npm install html-minifier-terser -g
$ brew install gettext
$ brew link --force gettext
```

## Process to update the MRS Metadata Schema

1. Open the `./mariadb_rest_service.msm.project/development/wb/mariadb_rest_service.mwb` file in MySQL Workbench, make the required changes to the model.
2. If a new id column or foreign key to one was added, run the `UUID_Columns` plugin (`Utilities > Use the UUID type for all BINARY(16) id columns`, from `Audit_Log_Triggers_grt.py`). Workbench's table editor rejects the `UUID` type, so new id columns are modelled as `BINARY(16)` and the plugin switches them to the model's `UUID` user datatype; single-column primary keys get the default `UUID_v7()`.
3. If there are changes to a table tracked in the audit log (check the existing `TRIGGER`s), select `Scripting > Run Script File > Audit_Log_Triggers_grt.py` to re-generate the embedded `Audit Log Triggers` SQL script.
4. Save the model.
5. Select `File > Export > Forward Engineer SQL CREATE Script...`, make sure only the two options "Omit Schema Qualifier in Object Names" and "Don't create view placeholder tables" are checked and store in `./mariadb_rest_service.msm.project/development/sections/140-10_tables.sql`.
6. If the triggers have been updated, go to the MySQL Model tab sheet and open the `SQL Scripts` section. Double-click on the `Audit Log Triggers` script, select and copy the contents of that script. Open the `mariadb_rest_service.msm.project/development/sections/150-40_audit_log_triggers.sql` file and replace its content with the one from the clipboard.
7. Update other files in the `./mariadb_rest_service.msm.project/development/sections/` directory as needed.
8. In MariaDB Shell with the msm plugin, run `msm.prepare_release(schema_project_path="./mariadb_rest_service.msm.project", version=..., next_version=...)` (`msm.prepareRelease()` in JavaScript, `msm prepare-release` on the command line). This will create the release file and in the `./mariadb_rest_service.msm.project/releases/versions/` folder, as well as an update script in the `./mariadb_rest_service.msm.project/releases/updates/` folder.
9. Update the content of the new update file in the `./mariadb_rest_service.msm.project/releases/updates/` folder.
10. Run `msm.generate_deployment_script(schema_project_path="./mariadb_rest_service.msm.project", version=...)`. This will create the final deployment script file and in the `./mariadb_rest_service.msm.project/releases/deployment/` folder.
11. The `k_schema_version` constant in `modules/mrs/core/mrs_metadata.h` needs to be updated to reflect the new version.

The scripts are schema-agnostic: the schema is named only in `CREATE SCHEMA` and `USE`, and the schema and role names carry the `/*<msm:schema_prefix>*/` and `/*<msm:schema_postfix>*/` placeholders (see `msm.project.json`). Do not qualify object names with the schema anywhere else, including routine and trigger bodies. After a new forward engineering of `140-10_tables.sql`, make sure its header comment names `mariadb_rest_service` and check the character offsets of its `SOURCE` statement in `development/mariadb_rest_service_next.sql`.
