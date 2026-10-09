-- Copyright (c) 2025, Oracle and/or its affiliates.
-- Copyright (c) 2026, MariaDB plc.
-- -----------------------------------------------------
-- Create roles for the MariaDB REST Service

-- The mariadb_rest_service_admin ROLE allows to fully manage the REST services
-- The mariadb_rest_service_schema_admin ROLE allows to manage the database schemas assigned to REST services
-- The mariadb_rest_service_dev ROLE allows to develop new REST objects for given REST services and upload static files
-- The mariadb_rest_service_user ROLE can be assigned to MariaDB accounts that are granted access via MariaDB Internal authentication.
-- The mariadb_rest_service_meta_provider ROLE is used by the MariaDB REST Daemon to read the mrs metadata and make inserts into the auth_user table
-- The mariadb_rest_service_data_provider ROLE is used by the MariaDB REST Daemon to read the actual schema data that is exposed via REST

CREATE ROLE IF NOT EXISTS /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_user/*<msm:schema_postfix>*/,
    /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_data_provider/*<msm:schema_postfix>*/;

-- Allow the /*<msm:schema_prefix>*/mariadb_rest_service_user/*<msm:schema_postfix>*/ role to access the same data as /*<msm:schema_prefix>*/mariadb_rest_service_data_provider/*<msm:schema_postfix>*/
GRANT /*<msm:schema_prefix>*/mariadb_rest_service_data_provider/*<msm:schema_postfix>*/ TO /*<msm:schema_prefix>*/mariadb_rest_service_user/*<msm:schema_postfix>*/;

-- Allow the creation of temporary tables
GRANT CREATE TEMPORARY TABLES ON *
    TO /*<msm:schema_prefix>*/mariadb_rest_service_data_provider/*<msm:schema_postfix>*/;

-- `msm_schema_version`
GRANT SELECT ON `msm_schema_version`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- `audit_log`
GRANT SELECT ON `audit_log`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- -----------------------------------------------------
-- Config

-- `config`
GRANT SELECT, UPDATE
    ON `config`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/;
GRANT SELECT ON `config`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/;

-- `redirect`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `redirect`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT ON `redirect`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- -----------------------------------------------------
-- Service

-- `url_host`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `url_host`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT ON `url_host`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- `url_host_alias`
GRANT SELECT, INSERT, DELETE
    ON `url_host_alias`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT ON `url_host_alias`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- `service`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `service`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT ON `service`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- -----------------------------------------------------
-- Schema Objects

-- `db_schema`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `db_schema`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/;
GRANT SELECT ON `db_schema`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/;

-- `db_object`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `db_object`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/;
GRANT SELECT ON `db_object`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- `mrs_db_object_row_group_security`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `mrs_db_object_row_group_security`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/;
GRANT SELECT ON `mrs_db_object_row_group_security`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- `object`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `object`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/;
GRANT SELECT ON `object`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- `object_field`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `object_field`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/;
GRANT SELECT ON `object_field`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- `object_reference`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `object_reference`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/;
GRANT SELECT ON `object_reference`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- -----------------------------------------------------
-- Static Content

-- `content_set`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `content_set`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/;
GRANT SELECT ON `content_set`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- `content_file`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `content_file`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/;
GRANT SELECT ON `content_file`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;


-- `content_set_has_obj_def`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `content_set_has_obj_def`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/;
GRANT SELECT ON `content_set_has_obj_def`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- -----------------------------------------------------
-- User Authentication

-- `auth_app`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `auth_app`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT ON `auth_app`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/;

-- `service_has_auth_app`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `service_has_auth_app`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT ON `service_has_auth_app`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/;

-- `auth_vendor`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `auth_vendor`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT ON `auth_vendor`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/;

-- `mrs_user`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `mrs_user`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT, INSERT, UPDATE ON `mrs_user`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;
GRANT SELECT ON `mrs_user`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_data_provider/*<msm:schema_postfix>*/;

-- -----------------------------------------------------
-- User Hierarchy

-- `mrs_user_hierarchy`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `mrs_user_hierarchy`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT, INSERT ON `mrs_user_hierarchy`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;
GRANT SELECT ON `mrs_user_hierarchy`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_data_provider/*<msm:schema_postfix>*/;

-- `mrs_user_hierarchy_type`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `mrs_user_hierarchy_type`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT, INSERT ON `mrs_user_hierarchy_type`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- -----------------------------------------------------
-- User Roles

-- `mrs_user_has_role`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `mrs_user_has_role`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT, INSERT ON `mrs_user_has_role`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- `mrs_role`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `mrs_role`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT, INSERT ON `mrs_role`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- `mrs_privilege`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `mrs_privilege`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT, INSERT ON `mrs_privilege`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- -----------------------------------------------------
-- User Group Management

-- `mrs_user_has_group`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `mrs_user_has_group`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT, INSERT ON `mrs_user_has_group`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- `mrs_user_group`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `mrs_user_group`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT, INSERT ON `mrs_user_group`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- `mrs_user_group_has_role`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `mrs_user_group_has_role`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT, INSERT ON `mrs_user_group_has_role`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- `mrs_group_hierarchy_type`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `mrs_group_hierarchy_type`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT, INSERT ON `mrs_group_hierarchy_type`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- `mrs_user_group_hierarchy`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `mrs_user_group_hierarchy`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT, INSERT ON `mrs_user_group_hierarchy`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;
GRANT SELECT ON `mrs_user_group_hierarchy`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_data_provider/*<msm:schema_postfix>*/;

-- -----------------------------------------------------
-- MariaDB REST Daemon Management

-- `rest_daemon`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `rest_daemon`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT, INSERT, UPDATE ON `rest_daemon`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;
GRANT SELECT
    ON `rest_daemon`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/;

-- `rest_daemon_status`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `rest_daemon_status`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT, INSERT, UPDATE ON `rest_daemon_status`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;
GRANT SELECT ON `rest_daemon_status`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/;

-- `rest_daemon_general_log`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `rest_daemon_general_log`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT INSERT ON `rest_daemon_general_log`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;
GRANT SELECT ON `rest_daemon_general_log`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/;

-- `rest_daemon_session`
GRANT SELECT, INSERT, UPDATE, DELETE
    ON `rest_daemon_session`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;
GRANT SELECT, INSERT ON `rest_daemon_session`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;
GRANT SELECT ON `rest_daemon_session`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/;

-- `rest_daemon_services`
GRANT SELECT ON `rest_daemon_services`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- -----------------------------------------------------
-- Procedures and Functions

-- `get_sequence_id`

GRANT EXECUTE ON FUNCTION `get_sequence_id`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_data_provider/*<msm:schema_postfix>*/;

-- `table_columns_with_references`
GRANT EXECUTE ON PROCEDURE `table_columns_with_references`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- `sdk_service_data`
GRANT EXECUTE ON PROCEDURE `sdk_service_data`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- `restore_roles`
GRANT EXECUTE ON PROCEDURE `restore_roles`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/;


-- -----------------------------------------------------
-- Views

-- `mrs_user_schema_version`
GRANT SELECT
    ON `mrs_user_schema_version`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;

-- `object_fields_with_references`
GRANT SELECT
    ON `object_fields_with_references`
    TO /*<msm:schema_prefix>*/mariadb_rest_service_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_schema_admin/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_dev/*<msm:schema_postfix>*/, /*<msm:schema_prefix>*/mariadb_rest_service_meta_provider/*<msm:schema_postfix>*/;
