#@<> INCLUDE dump_utils.inc

#@<> backup lock privilege
# The privilege which guards the backup lock, as the dumper names it: MySQL's
# LOCK INSTANCE FOR BACKUP needs BACKUP_ADMIN, MariaDB's BACKUP STAGE needs
# RELOAD (Dumper::backup_lock_privilege()).
backup_lock_privilege = "RELOAD" if __server_is_maria_db else ("BACKUP_ADMIN" if __version_num >= 80000 else "")


#@<> entry point

# imports
import json
import os
import os.path
import random
import re
import shutil
import stat
import string
import time

# constants
world_x_schema = "world_x_cities"
world_x_table = "cities"

# schema name can consist of 51 reserved characters max, server supports up to
# 64 chars but when it creates the directory for schema, it encodes non-letters
# ASCII using five bytes, meaning the directory name is going to be 255
# characters long (max on most # platforms)
# schema name consists of 49 reserved characters + UTF-8 character
test_schema = "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ó"
test_table_primary = "first"
test_table_unique = "second"
# mysql_data_home + schema + table name + path separators cannot exceed 512
# characters
# table name consists of 48 reserved characters + UTF-8 character
test_table_non_unique = "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ą"
test_table_no_index = "fóurth"
test_table_trigger = "sample_trigger"
test_schema_procedure = "sample_procedure"
test_schema_function = "sample_function"
test_schema_event = "sample_event"
test_schema_library = "sample_library"
test_schema_library_procedure = "sample_library_procedure"
test_schema_library_function = "sample_library_function"
test_view = "sample_view"

verification_schema = "wl13804_ver"

types_schema = "xtest"

incompatible_schema = "mysqlaas"
incompatible_table_wrong_engine = "wrong_engine"
incompatible_table_encryption = "has_encryption"
incompatible_table_data_directory = "has_data_dir"
incompatible_table_index_directory = "has_index_dir"
incompatible_tablespace = "sample_tablespace"
incompatible_table_tablespace = "use_tablespace"
incompatible_view = "has_definer"
incompatible_schema_tables = [incompatible_table_wrong_engine, incompatible_table_encryption, incompatible_table_data_directory, incompatible_table_index_directory, incompatible_table_tablespace]
incompatible_schema_views = [incompatible_view]

if __server_is_maria_db:
    # the encryption and tablespace tables are never created on MariaDB - see
    # the 'schema with MySQLaaS incompatibilities' chunk - so they cannot be
    # named in a dump either
    incompatible_schema_tables = [t for t in incompatible_schema_tables if t not in (incompatible_table_encryption, incompatible_table_tablespace)]

incompatible_table_directory = os.path.join(__tmp_dir, "incompatible")
table_data_directory = os.path.join(incompatible_table_directory, "data")
table_index_directory = os.path.join(incompatible_table_directory, "index")

shutil.rmtree(incompatible_table_directory, True)
os.makedirs(incompatible_table_directory)
os.mkdir(table_data_directory)
os.mkdir(table_index_directory)

if __os_type == "windows":
    # on windows server would have to be installed in the root folder to
    # handle long schema/table names
    if __version_num >= 80000:
        test_schema = "@ó"
        test_table_non_unique = "@ą"
    else:
        # when using latin1 encoding, UTF-8 characters sent by shell are converted by server
        # to latin1 representation, then upper case characters are converted to lower case,
        # which leads to invalid UTF-8 sequences when names are transfered back to shell
        # use ASCII in this case
        test_schema = "@o"
        test_table_non_unique = "@a"
        test_table_no_index = "fourth"

all_schemas = [world_x_schema, types_schema, test_schema, verification_schema, incompatible_schema]
test_schema_tables = [test_table_primary, test_table_unique, test_table_non_unique, test_table_no_index]
test_schema_views = [test_view]

uri = __sandbox_uri1
xuri = __sandbox_uri1.replace("mysql://", "mysqlx://") + "0"

test_output_relative = "dump_output"
test_output_absolute = os.path.abspath(test_output_relative)

# helpers
def setup_session(u = uri):
    shell.connect(u)
    session.run_sql("SET NAMES 'utf8mb4';")
    session.run_sql("SET GLOBAL local_infile = true;")

def drop_all_schemas(exclude=[]):
    for schema in session.run_sql("SELECT SCHEMA_NAME FROM information_schema.schemata;").fetch_all():
        if schema[0] not in ["information_schema", "mysql", "performance_schema", "sys"] + exclude:
            session.run_sql("DROP SCHEMA IF EXISTS !;", [ schema[0] ])

def create_all_schemas():
    for schema in all_schemas:
        session.run_sql("CREATE SCHEMA IF NOT EXISTS !;", [ schema ])

def create_user():
    session.run_sql(f"DROP USER IF EXISTS {test_user_account};")
    session.run_sql(f"CREATE USER IF NOT EXISTS {test_user_account} IDENTIFIED BY ?;", [test_user_pwd])

def recreate_verification_schema():
    session.run_sql("DROP SCHEMA IF EXISTS !;", [ verification_schema ])
    session.run_sql("CREATE SCHEMA !;", [ verification_schema ])

def EXPECT_SUCCESS(schema, tables, output_url, options = {}, views = [], check_number_of_tables = True):
    WIPE_STDOUT()
    shutil.rmtree(test_output_absolute, True)
    EXPECT_FALSE(os.path.isdir(test_output_absolute))
    util.dump_tables(schema, tables + views, output_url, options)
    if options.get("all", False):
        tables = get_all_tables(schema, False)
    if not "dryRun" in options:
        EXPECT_TRUE(os.path.isdir(test_output_absolute))
        if check_number_of_tables:
            EXPECT_STDOUT_CONTAINS("Tables dumped: {0}".format(len(tables)))

def EXPECT_FAIL(error, msg, schema, tables, output_url, options = {}, expect_dir_created = False):
    shutil.rmtree(test_output_absolute, True)
    is_re = is_re_instance(msg)
    full_msg = "{0}: {1}".format(re.escape(error) if is_re else error, msg.pattern if is_re else msg)
    if is_re:
        full_msg = re.compile("^" + full_msg)
    EXPECT_THROWS(lambda: util.dump_tables(schema, tables, output_url, options), full_msg)
    EXPECT_EQ(expect_dir_created, os.path.isdir(test_output_absolute))

def TEST_BOOL_OPTION(option):
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type Bool, but is Null".format(option), types_schema, types_schema_tables, test_output_relative, { option: None })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' Bool expected, but value is String".format(option), types_schema, types_schema_tables, test_output_relative, { option: "dummy" })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type Bool, but is Array".format(option), types_schema, types_schema_tables, test_output_relative, { option: [] })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type Bool, but is Map".format(option), types_schema, types_schema_tables, test_output_relative, { option: {} })

def TEST_STRING_OPTION(option):
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type String, but is Null".format(option), types_schema, types_schema_tables, test_output_relative, { option: None })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type String, but is Integer".format(option), types_schema, types_schema_tables, test_output_relative, { option: 5 })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type String, but is Integer".format(option), types_schema, types_schema_tables, test_output_relative, { option: -5 })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type String, but is Array".format(option), types_schema, types_schema_tables, test_output_relative, { option: [] })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type String, but is Map".format(option), types_schema, types_schema_tables, test_output_relative, { option: {} })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type String, but is Bool".format(option), types_schema, types_schema_tables, test_output_relative, { option: False })

def TEST_UINT_OPTION(option):
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type UInteger, but is Null".format(option), types_schema, types_schema_tables, test_output_relative, { option: None })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' UInteger expected, but Integer value is out of range".format(option), types_schema, types_schema_tables, test_output_relative, { option: -5 })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' UInteger expected, but value is String".format(option), types_schema, types_schema_tables, test_output_relative, { option: "dummy" })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type UInteger, but is Array".format(option), types_schema, types_schema_tables, test_output_relative, { option: [] })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type UInteger, but is Map".format(option), types_schema, types_schema_tables, test_output_relative, { option: {} })

def TEST_ARRAY_OF_STRINGS_OPTION(option):
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type Array, but is Integer".format(option), types_schema, types_schema_tables, test_output_relative, { option: 5 })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type Array, but is Integer".format(option), types_schema, types_schema_tables, test_output_relative, { option: -5 })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type Array, but is String".format(option), types_schema, types_schema_tables, test_output_relative, { option: "dummy" })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type Array, but is Map".format(option), types_schema, types_schema_tables, test_output_relative, { option: {} })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type Array, but is Bool".format(option), types_schema, types_schema_tables, test_output_relative, { option: False })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be an array of strings".format(option), types_schema, types_schema_tables, test_output_relative, { option: [ None ] })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be an array of strings".format(option), types_schema, types_schema_tables, test_output_relative, { option: [ 5 ] })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be an array of strings".format(option), types_schema, types_schema_tables, test_output_relative, { option: [ -5 ] })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be an array of strings".format(option), types_schema, types_schema_tables, test_output_relative, { option: [ {} ] })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be an array of strings".format(option), types_schema, types_schema_tables, test_output_relative, { option: [ False ] })

def TEST_MAP_OF_STRINGS_OPTION(option):
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type Map, but is Integer".format(option), types_schema, types_schema_tables, test_output_relative, { option: 5 })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type Map, but is Integer".format(option), types_schema, types_schema_tables, test_output_relative, { option: -5 })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type Map, but is String".format(option), types_schema, types_schema_tables, test_output_relative, { option: "dummy" })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type Map, but is Array".format(option), types_schema, types_schema_tables, test_output_relative, { option: [] })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type Map, but is Bool".format(option), types_schema, types_schema_tables, test_output_relative, { option: False })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be a map of strings".format(option), types_schema, types_schema_tables, test_output_relative, { option: { "key": None } })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be a map of strings".format(option), types_schema, types_schema_tables, test_output_relative, { option: { "key": 5 } })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be a map of strings".format(option), types_schema, types_schema_tables, test_output_relative, { option: { "key": -5 } })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be a map of strings".format(option), types_schema, types_schema_tables, test_output_relative, { option: { "key": {} } })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be a map of strings".format(option), types_schema, types_schema_tables, test_output_relative, { option: { "key": False } })

def TEST_MAP_OF_ARRAY_OF_STRINGS_OPTION(option):
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type Map, but is Integer".format(option), types_schema, types_schema_tables, test_output_relative, { option: 5 })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type Map, but is Integer".format(option), types_schema, types_schema_tables, test_output_relative, { option: -5 })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type Map, but is String".format(option), types_schema, types_schema_tables, test_output_relative, { option: "dummy" })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type Map, but is Array".format(option), types_schema, types_schema_tables, test_output_relative, { option: [] })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be of type Map, but is Bool".format(option), types_schema, types_schema_tables, test_output_relative, { option: False })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be a map of arrays of strings".format(option), types_schema, types_schema_tables, test_output_relative, { option: { "key": 5 } })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be a map of arrays of strings".format(option), types_schema, types_schema_tables, test_output_relative, { option: { "key": -5 } })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be a map of arrays of strings".format(option), types_schema, types_schema_tables, test_output_relative, { option: { "key": {} } })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be a map of arrays of strings".format(option), types_schema, types_schema_tables, test_output_relative, { option: { "key": False } })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be a map of arrays of strings".format(option), types_schema, types_schema_tables, test_output_relative, { option: { "key": [ 5 ] } })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be a map of arrays of strings".format(option), types_schema, types_schema_tables, test_output_relative, { option: { "key": [ -5 ] } })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be a map of arrays of strings".format(option), types_schema, types_schema_tables, test_output_relative, { option: { "key": [ {} ] } })
    EXPECT_FAIL("TypeError", "Argument #4: Option '{0}' is expected to be a map of arrays of strings".format(option), types_schema, types_schema_tables, test_output_relative, { option: { "key": [ False ] } })

def get_load_columns(options):
    decode = options["decodeColumns"] if "decodeColumns" in options else {}
    columns = []
    variables = []
    for c in options["columns"]:
        if c in decode:
            var = "@var{0}".format(len(variables))
            variables.append("{0} = {1}({2})".format(c, decode[c], var))
            columns.append(var)
        else:
            columns.append(c)
    statement = "(" + ",".join(columns) + ")"
    if variables:
        statement += " SET " + ",".join(variables)
    return statement

def get_all_columns(schema, table):
    columns = []
    for column in session.run_sql("SELECT COLUMN_NAME FROM information_schema.columns WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ? ORDER BY ORDINAL_POSITION;", [schema, table]).fetch_all():
        columns.append(column[0])
    return columns

def get_all_tables(schema, include_views = True):
    tables = []
    for table in session.run_sql("SELECT TABLE_NAME FROM information_schema.tables WHERE TABLE_SCHEMA = ?" + ("" if include_views else " and table_type = 'BASE TABLE'"), [schema]).fetch_all():
        tables.append(table[0])
    return tables

def compute_crc(schema, table, columns):
    session.run_sql("SET @crc = '';")
    session.run_sql("SELECT @crc := MD5(CONCAT_WS('#',@crc,{0})) FROM !.! ORDER BY {0};".format(("!," * len(columns))[:-1]), columns + [schema, table] + columns)
    return session.run_sql("SELECT @crc;").fetch_one()[0]

def compute_checksum(schema, table):
    return session.run_sql("CHECKSUM TABLE !.!", [ schema, table ]).fetch_one()[1]

def TEST_LOAD(schema, table, where = "", partitions = [], recreate_schema = True):
    print("---> testing: `{0}`.`{1}`".format(schema, table))
    # load data
    if recreate_schema:
        recreate_verification_schema()
    EXPECT_NO_THROWS(lambda: util.load_dump(test_output_absolute, { "showProgress": False, "loadUsers": False, "includeSchemas": [ schema ], "includeTables" : [ quote_identifier(schema, table) ], "schema": verification_schema, "resetProgress": True }), "loading should not throw")
    # compute CRC
    EXPECT_EQ(md5_table(session, schema, table, where, partitions), md5_table(session, verification_schema, table))

def TEST_DUMP_AND_LOAD(schema, tables, options = {}):
    EXPECT_SUCCESS(schema, tables, test_output_absolute, options)
    where = options.get("where", {})
    partitions = options.get("partitions", {})
    if options.get("all", False):
        tables = get_all_tables(schema, False)
    recreate_verification_schema()
    for table in tables:
        quoted = quote_identifier(schema, table)
        TEST_LOAD(schema, table, where.get(quoted, ""), partitions.get(quoted, []), False)

#@<> deploy sandbox
sandbox_options = {
    "loose_innodb_directories": filename_for_file(table_data_directory),
    "server_id": str(random.randint(1, 4294967295)),
    "log_bin": 1,
    "innodb_doublewrite": "OFF"
}
if not __server_is_maria_db:
    # MariaDB refuses to start with an unknown early_plugin_load and has no
    # keyring_file plugin, and it has neither gtid_mode nor
    # enforce_gtid_consistency - its GTIDs are always on
    sandbox_options.update({
        "early-plugin-load": "keyring_file." + ("dll" if __os_type == "windows" else "so"),
        "keyring_file_data": filename_for_file(os.path.join(incompatible_table_directory, "keyring")),
        "enforce_gtid_consistency": "ON",
        "gtid_mode": "ON",
    })
testutil.deploy_raw_sandbox(__mysql_sandbox_port1, "root", sandbox_options)

#@<> wait for server
testutil.wait_sandbox_alive(uri)
shell.connect(uri)
session.run_sql("/*!80021 alter instance disable innodb redo_log */")

#@<> Setup
setup_session()

drop_all_schemas()
create_all_schemas()

create_user()

session.run_sql("CREATE TABLE !.! (`ID` int(11) NOT NULL AUTO_INCREMENT, `Name` char(64) NOT NULL DEFAULT '', `CountryCode` char(3) NOT NULL DEFAULT '', `District` char(64) NOT NULL DEFAULT '', `Info` json DEFAULT NULL, PRIMARY KEY (`ID`)) ENGINE=InnoDB AUTO_INCREMENT=4080 DEFAULT CHARSET=utf8mb4;", [ world_x_schema, world_x_table ])
util.import_table(os.path.join(__import_data_path, "world_x_cities.dump"), { "schema": world_x_schema, "table": world_x_table, "characterSet": "utf8mb4", "showProgress": False })

rc = testutil.call_mysqlsh([uri, "--sql", "--file", os.path.join(__data_path, "sql", "fieldtypes_all.sql")])
EXPECT_EQ(0, rc)

# table with primary key, unique key, generated columns
session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT NOT NULL AUTO_INCREMENT PRIMARY KEY, `data` INT NOT NULL UNIQUE, `vdata` INT GENERATED ALWAYS AS (data + 1) VIRTUAL, `sdata` INT GENERATED ALWAYS AS (data + 2) STORED) ENGINE=InnoDB;", [ test_schema, test_table_primary ])
# table with unique key
session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT NOT NULL UNIQUE, `data` INT) ENGINE=InnoDB;", [ test_schema, test_table_unique ])
# table with non-unique key
session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT, `data` INT, KEY (`id`)) ENGINE=InnoDB;", [ test_schema, test_table_non_unique ])
# table without key
session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT, `data` INT, `tdata` INT) ENGINE=InnoDB;", [ test_schema, test_table_no_index ])

session.run_sql("CREATE VIEW !.! AS SELECT `tdata` FROM !.!;", [ test_schema, test_view, test_schema, test_table_no_index ])

session.run_sql("CREATE TRIGGER !.! BEFORE UPDATE ON ! FOR EACH ROW BEGIN SET NEW.tdata = NEW.data + 3; END;", [ test_schema, test_table_trigger, test_table_no_index ])

def create_test_schema_objects():
    session.run_sql("CREATE FUNCTION !.!(s CHAR(20)) RETURNS CHAR(50) DETERMINISTIC RETURN CONCAT('Hello, ',s,'!');", [ test_schema, test_schema_function ])
    session.run_sql("CREATE EVENT !.! ON SCHEDULE EVERY 1 HOUR STARTS CURRENT_TIMESTAMP + INTERVAL 1 WEEK DO DELETE FROM !.!;", [ test_schema, test_schema_event, test_schema, test_table_no_index ])
    session.run_sql("""
        CREATE PROCEDURE !.!()
        BEGIN
        DECLARE i INT DEFAULT 1;
        WHILE i < 10000 DO
            INSERT INTO !.! (`data`) VALUES (i);
            SET i = i + 1;
        END WHILE;
        END""", [ test_schema, test_schema_procedure, test_schema, test_table_primary ])
    if instance_supports_libraries:
        session.run_sql("""
            CREATE LIBRARY !.! LANGUAGE JAVASCRIPT
            AS $$
                export function squared(n) {
                    return n * n;
                }
            $$
            """, [ test_schema, test_schema_library ])
        session.run_sql("""
            CREATE FUNCTION !.!(n INTEGER) RETURNS INTEGER DETERMINISTIC LANGUAGE JAVASCRIPT
            USING (!.! AS mylib)
            AS $$
                return mylib.squared(n);
            $$
            """, [ test_schema, test_schema_library_function, test_schema, test_schema_library ])
        session.run_sql(f"""
            CREATE PROCEDURE !.!(n INTEGER) LANGUAGE JAVASCRIPT
            USING (!.!)
            AS $$
                {test_schema_library}.squared(n);
            $$
            """, [ test_schema, test_schema_library_procedure, test_schema, test_schema_library ])

create_test_schema_objects()

session.run_sql("""INSERT INTO !.! (`data`)
SELECT (tth * 10000 + th * 1000 + h * 100 + t * 10 + u + 1) x FROM
    (SELECT 0 tth UNION SELECT 1 UNION SELECT 2 UNION SELECT 3 UNION SELECT 4 UNION SELECT 5 UNION SELECT 6 UNION SELECT 7 UNION SELECT 8 UNION SELECT 9) E,
    (SELECT 0 th UNION SELECT 1 UNION SELECT 2 UNION SELECT 3 UNION SELECT 4 UNION SELECT 5 UNION SELECT 6 UNION SELECT 7 UNION SELECT 8 UNION SELECT 9) D,
    (SELECT 0 h UNION SELECT 1 UNION SELECT 2 UNION SELECT 3 UNION SELECT 4 UNION SELECT 5 UNION SELECT 6 UNION SELECT 7 UNION SELECT 8 UNION SELECT 9) C,
    (SELECT 0 t UNION SELECT 1 UNION SELECT 2 UNION SELECT 3 UNION SELECT 4 UNION SELECT 5 UNION SELECT 6 UNION SELECT 7 UNION SELECT 8 UNION SELECT 9) B,
    (SELECT 0 u UNION SELECT 1 UNION SELECT 2 UNION SELECT 3 UNION SELECT 4 UNION SELECT 5 UNION SELECT 6 UNION SELECT 7 UNION SELECT 8 UNION SELECT 9) A
ORDER BY x;""", [ test_schema, test_table_primary ])

session.run_sql("INSERT INTO !.! (`id`, `data`) SELECT `id`, `data` FROM !.!;", [ test_schema, test_table_unique, test_schema, test_table_primary ])
session.run_sql("INSERT INTO !.! (`id`, `data`) SELECT `id`, `data` FROM !.!;", [ test_schema, test_table_non_unique, test_schema, test_table_primary ])
# duplicate row, NULL row
session.run_sql("INSERT INTO !.! (`id`, `data`) VALUES (1, 1), (NULL, NULL);", [ test_schema, test_table_non_unique ])
session.run_sql("INSERT INTO !.! (`id`, `data`) SELECT `id`, `data` FROM !.!;", [ test_schema, test_table_no_index, test_schema, test_table_primary ])
# duplicate row, NULL row
session.run_sql("INSERT INTO !.! (`id`, `data`) VALUES (1, 1), (NULL, NULL);", [ test_schema, test_table_no_index ])

for table in [test_table_primary, test_table_unique, test_table_non_unique, test_table_no_index]:
    session.run_sql("ANALYZE TABLE !.!;", [ test_schema, table ])

#@<> schema with MySQLaaS incompatibilities
session.run_sql("ALTER DATABASE ! CHARACTER SET latin1;", [ incompatible_schema ])
session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT, `data` INT) ENGINE=MyISAM DEFAULT CHARSET=latin1;", [ incompatible_schema, incompatible_table_wrong_engine ])
session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT, `data` INT) ENGINE=InnoDB DATA DIRECTORY = '{0}' DEFAULT CHARSET=latin1;".format(filename_for_file(table_data_directory)), [ incompatible_schema, incompatible_table_data_directory ])
session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT, `data` INT) ENGINE=MyISAM INDEX DIRECTORY = '{0}' DEFAULT CHARSET=latin1;".format(filename_for_file(table_index_directory)), [ incompatible_schema, incompatible_table_index_directory ])
if not __server_is_maria_db:
    # MariaDB spells table encryption ENCRYPTED=YES (ENCRYPTION is error 1911)
    # and has no general tablespaces at all, so neither of these can be created
    # there - and neither is reachable, they exist for the ocimds checks
    session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT, `data` INT) ENGINE=InnoDB ENCRYPTION = 'Y' DEFAULT CHARSET=latin1;", [ incompatible_schema, incompatible_table_encryption ])
    session.run_sql("CREATE TABLESPACE ! ADD DATAFILE 't_s_1.ibd' ENGINE=INNODB;", [ incompatible_tablespace ])
    session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT, `data` INT) TABLESPACE ! DEFAULT CHARSET=latin1;", [ incompatible_schema, incompatible_table_tablespace, incompatible_tablespace ])
session.run_sql("CREATE VIEW !.! AS SELECT `data` FROM !.!;", [ incompatible_schema, incompatible_view, incompatible_schema, incompatible_table_wrong_engine ])

#@<> count types tables
types_schema_tables = []
types_schema_views = []

for table in session.run_sql("SELECT TABLE_NAME, TABLE_TYPE FROM information_schema.tables WHERE TABLE_SCHEMA = ?", [types_schema]).fetch_all():
    if table[1] == "BASE TABLE":
        types_schema_tables.append(table[0])
    else:
        types_schema_views.append(table[0])

#@<> BUG#31896448
tested_schema = "test_schema"
tested_table = "test"

session.run_sql("CREATE SCHEMA !;", [ tested_schema ])
session.run_sql("CREATE TABLE !.! (a BIGINT PRIMARY KEY, b VARCHAR(32));", [ tested_schema, tested_table ])
session.run_sql("""INSERT INTO !.! VALUES
(-9223372036854775808, 'a'),
(-6917529027641081856, 'b'),
(-4611686018427387904, 'c'),
(-2305843009213693952, 'd'),
(0, 'e'),
(2305843009213693952, 'f'),
(4611686018427387904, 'g'),
(6917529027641081856, 'h'),
(9223372036854775807, 'i');""", [ tested_schema, tested_table ])
session.run_sql("ANALYZE TABLE !.!;", [ tested_schema, tested_table ])

EXPECT_SUCCESS(tested_schema, [ tested_table ], test_output_absolute, { "bytesPerChunk": "128k", "compression": "none", "showProgress": False })
TEST_LOAD(tested_schema, tested_table)

session.run_sql("DROP SCHEMA !;", [ tested_schema ])

#@<> BUG#31766490 {__dbug}
tested_schema = "test_schema"
tested_table = "test"

session.run_sql("CREATE SCHEMA !;", [ tested_schema ])
session.run_sql("CREATE TABLE !.! (c INT PRIMARY KEY);", [ tested_schema, tested_table ])
session.run_sql("INSERT INTO !.! VALUES " + ','.join("(" + str(k) + ")" for k in range(1024)), [ tested_schema, tested_table ])

expected_msg = "NOTE: Table statistics not available for `{0}`.`{1}`, chunking operation may be not optimal. Please consider running 'ANALYZE TABLE `{0}`.`{1}`;' first.".format(tested_schema, tested_table)

testutil.dbug_set("+d,dumper_average_row_length_0")

EXPECT_SUCCESS(tested_schema, [ tested_table ], test_output_absolute, { "showProgress": False })
EXPECT_STDOUT_CONTAINS(expected_msg)

testutil.dbug_set("")

session.run_sql("ANALYZE TABLE !.!;", [ tested_schema, tested_table ])

WIPE_STDOUT()
EXPECT_SUCCESS(tested_schema, [ tested_table ], test_output_absolute, { "showProgress": False })
EXPECT_STDOUT_NOT_CONTAINS(expected_msg)

session.run_sql("DROP SCHEMA !;", [ tested_schema ])

#@<> BUG#32430402 metadata should contain information about binlog
EXPECT_SUCCESS(types_schema, [types_schema_tables[0]], test_output_absolute, { "ddlOnly": True, "showProgress": False })

with open(os.path.join(test_output_absolute, "@.json"), encoding="utf-8") as json_file:
    metadata = json.load(json_file)
    EXPECT_EQ(True, "binlogFile" in metadata, "'binlogFile' should be in metadata")
    EXPECT_EQ(True, "binlogPosition" in metadata, "'binlogPosition' should be in metadata")

#@<> BUG#32773468 setup
def validate_size(path, ext):
    size = 0
    for f in os.listdir(path):
        if f.endswith(ext):
            size += os.path.getsize(os.path.join(path, f))
    EXPECT_STDOUT_CONTAINS(f"Compressed data size: {size} bytes")

def test_bug_32773468(data):
    session.run_sql("TRUNCATE TABLE !.!;", [ tested_schema, tested_table ])
    session.run_sql(f"""INSERT INTO !.! VALUES {",".join([f"({v}, '{random.choices(string.ascii_letters + string.digits)[0]}')" for v in data])};""", [ tested_schema, tested_table ])
    session.run_sql("ANALYZE TABLE !.!;", [ tested_schema, tested_table ])
    EXPECT_SUCCESS(tested_schema, [ tested_table ], test_output_absolute, { "showProgress": False })
    validate_size(test_output_absolute, ".zst")

tested_schema = "test_schema"
session.run_sql("CREATE SCHEMA !;", [ tested_schema ])

#@<> BUG#32773468 setup table with signed integers
tested_table = "test_signed"
session.run_sql("CREATE TABLE !.! (a BIGINT PRIMARY KEY, b VARCHAR(32));", [ tested_schema, tested_table ])

#@<> BUG#32773468 table holds values close to signed minimum
test_bug_32773468(range(-9223372036854775808, -9223372036854775802))

#@<> BUG#32773468 table holds values close to signed minimum, data range overflows the max
test_bug_32773468(list(range(-9223372036854775808, -9223372036854775802)) + [ 1 ])

#@<> BUG#32773468 table holds values close to signed maximum
test_bug_32773468(range(9223372036854775802, 9223372036854775808))

#@<> BUG#32773468 table holds values close to signed maximum, data range overflows the max
test_bug_32773468([ -1 ] + list(range(9223372036854775802, 9223372036854775808)))

#@<> BUG#32773468 setup table with unsigned integers
tested_table = "test_unsigned"
session.run_sql("CREATE TABLE !.! (a BIGINT UNSIGNED PRIMARY KEY, b VARCHAR(32));", [ tested_schema, tested_table ])

#@<> BUG#32773468 table holds values close to unsigned minimum
test_bug_32773468(range(0, 6))

#@<> BUG#32773468 data range is maximum possible
test_bug_32773468(list(range(0, 6)) + [ 18446744073709551615 ])

#@<> BUG#32773468 table holds values close to unsigned maximum
test_bug_32773468(range(18446744073709551610, 18446744073709551616))

#@<> BUG#32773468 cleanup
session.run_sql("DROP SCHEMA !;", [ tested_schema ])

#@<> BUG#32602325 setup
tested_schema = "test_schema"
tested_table = "test_table"

session.run_sql("CREATE SCHEMA !;", [ tested_schema ])
session.run_sql("CREATE TABLE !.! (a BIGINT PRIMARY KEY, b text);", [ tested_schema, tested_table ])

gap_start = 1
gap_items = 10000
gap_step = 1000

def test_bug_32602325(step):
    def insert(r):
        session.run_sql(f"""INSERT INTO !.! VALUES {",".join([f"({v}, '')" for v in r])};""", [ tested_schema, tested_table ])
    def generate_gaps():
        value = gap_start
        while True:
            yield value
            value += next(step)
    gen = generate_gaps()
    # clear the data
    session.run_sql("TRUNCATE TABLE !.!;", [ tested_schema, tested_table ])
    # insert rows with gaps in the PK
    insert([next(gen) for i in range(gap_items)])
    # all the remaining data is continuous
    start = next(gen)
    insert(range(start, start + gap_items))
    # use some dummy data
    session.run_sql("UPDATE !.! SET b = repeat('x', 5000);", [ tested_schema, tested_table ])
    # analyze the table for optimum results
    session.run_sql("ANALYZE TABLE !.!;", [ tested_schema, tested_table ])
    # run the test
    EXPECT_SUCCESS(tested_schema, [ tested_table ], test_output_absolute, { "bytesPerChunk": "1M", "compression": "none", "showProgress": False })
    # expect at least 320 chunks, we're dealing with random data, allow for some chunks which are smaller
    CHECK_OUTPUT_SANITY(test_output_absolute, 180000, 320, 4)

#@<> BUG#32602325 - equal gaps
def equal_gaps():
    while True:
        yield gap_step

test_bug_32602325(equal_gaps())

#@<> BUG#32602325 - random gaps
def random_gaps():
    while True:
        yield random.randrange(1, gap_step)

test_bug_32602325(random_gaps())

#@<> BUG#32602325 - increasing gaps
def increasing_gaps():
    step = 1
    while True:
        yield step
        step += 1

test_bug_32602325(increasing_gaps())

#@<> BUG#32602325 - decreasing gaps
def decreasing_gaps():
    step = gap_step
    while True:
        yield step
        if step > 2:
            step -= 1

test_bug_32602325(decreasing_gaps())

#@<> BUG#32602325 cleanup
session.run_sql("DROP SCHEMA !;", [ tested_schema ])

#@<> composite non-integer key - setup
tested_schema = "test_schema"
tested_table = "char_hashes_4"
items = 10000

session.run_sql("CREATE SCHEMA !;", [ tested_schema ])
# MariaDB's grammar does not accept an explicit NOT NULL on a GENERATED ALWAYS
# AS column, MySQL's does; the columns are never actually NULL either way, so
# this does not change what the test covers
generated_not_null = "" if __server_is_maria_db else " NOT NULL"
session.run_sql(f"""CREATE TABLE !.! (
  `md5_1` varchar(8) NOT NULL,
  `md5_2` varchar(8) NOT NULL,
  `md5_3` varchar(8) GENERATED ALWAYS AS (substr(md5_full, 17, 8)) VIRTUAL{generated_not_null},
  `md5_4` varchar(8) GENERATED ALWAYS AS (substr(md5_full, 25, 8)) STORED{generated_not_null},
  `email` varchar(100) DEFAULT NULL,
  `md5_full` varchar(32) DEFAULT NULL,
  UNIQUE KEY `pk` (`md5_1`,`md5_2`,`md5_3`,`md5_4`)
);""", [ tested_schema, tested_table ])
session.run_sql(f"""INSERT INTO !.! (`md5_1`,`md5_2`, `email`, `md5_full`) VALUES {",".join([f"('{md5sum(email)[0:8]}', '{md5sum(email)[8:16]}', '{email}', '{md5sum(email)}')" for email in [random_email() for i in range(items)]])};""", [ tested_schema, tested_table ])
session.run_sql("ANALYZE TABLE !.!;", [ tested_schema, tested_table ])

#@<> composite non-integer key - test
WIPE_SHELL_LOG()
EXPECT_SUCCESS(tested_schema, [ tested_table ], test_output_absolute, { "bytesPerChunk": "128k", "compression": "none", "showProgress": False })
EXPECT_SHELL_LOG_CONTAINS(f"The data dump of table `{tested_schema}`.`{tested_table}` will be chunked using columns `md5_1`,`md5_2`,`md5_3`,`md5_4`")
CHECK_OUTPUT_SANITY(test_output_absolute, 55000, 10)
TEST_LOAD(tested_schema, tested_table)

#@<> composite non-integer key - cleanup
session.run_sql("DROP SCHEMA !;", [ tested_schema ])

#@<> BUG#32926856 setup
tested_schema = "test_schema"
tested_table = "test_table"

session.run_sql("CREATE SCHEMA !;", [ tested_schema ])
session.run_sql("SET @saved_sql_mode = @@SQL_MODE;")
session.run_sql("SET SQL_MODE='no_auto_value_on_zero';")
session.run_sql("CREATE TABLE !.! (a INT PRIMARY KEY AUTO_INCREMENT);", [ tested_schema, tested_table ])
session.run_sql("INSERT INTO !.! VALUES (0), (1);", [ tested_schema, tested_table ])
session.run_sql("SET @@SQL_MODE = @saved_sql_mode;")

checksum = compute_checksum(tested_schema, tested_table)

#@<> BUG#32926856 - test
EXPECT_SUCCESS(tested_schema, [ tested_table ], test_output_absolute, { "showProgress": False })

session.run_sql("DROP SCHEMA !;", [ tested_schema ])
EXPECT_NO_THROWS(lambda: util.load_dump(test_output_absolute), "dump should be loaded without problems")

EXPECT_EQ(checksum, compute_checksum(tested_schema, tested_table), "checksum mismatch")

#@<> BUG#32926856 cleanup
session.run_sql("DROP SCHEMA !;", [ tested_schema ])

#@<> BUG#33232480 setup
tested_schema = "test_schema"
tested_table = "test_table"

session.run_sql("CREATE SCHEMA !;", [ tested_schema ])
session.run_sql("CREATE TABLE !.! (`c (from, to)` varchar(16) NOT NULL, `c (to, from)` varchar(16) NOT NULL, `data` blob, PRIMARY KEY (`c (from, to)`, `c (to, from)`));", [ tested_schema, tested_table ])
session.run_sql("INSERT INTO !.! (`c (from, to)`, `c (to, from)`, `data`) VALUES (REPEAT('a',15), REPEAT('a',15), REPEAT('a',115));", [ tested_schema, tested_table ])
session.run_sql("INSERT INTO !.! (`c (from, to)`, `c (to, from)`, `data`) VALUES (REPEAT('z',15), REPEAT('z',15), REPEAT('z',115));", [ tested_schema, tested_table ])
session.run_sql("ANALYZE TABLE !.!;", [ tested_schema, tested_table ])

#@<> BUG#33232480 - test
EXPECT_SUCCESS(tested_schema, [ tested_table ], test_output_absolute, { "showProgress": False })

#@<> BUG#33232480 cleanup
session.run_sql("DROP SCHEMA !;", [ tested_schema ])

#@<> BUG#32954757 setup
tested_schema = "test_schema"
tested_table = "test_table"
constantly_update_table = Generate_transactions(uri, tested_schema, tested_table, 0)

#@<> BUG#32954757 - test
# constantly insert values to the table
constantly_update_table.generate_data()

# run a dump, expect a warning that gtid_executed has changed
EXPECT_SUCCESS(tested_schema, [ tested_table ], test_output_absolute, { "consistent": False, "threads": 2, "showProgress": False })
EXPECT_STDOUT_CONTAINS("WARNING: The value of the gtid_executed system variable has changed during the dump, from: ")

#@<> BUG#32954757 cleanup
constantly_update_table.stop()

#@<> BUG#33400387 setup
tested_schema = "test_schema"
tested_table = "test_table"

decimal_digits = 0

session.run_sql("CREATE SCHEMA !;", [ tested_schema ])
session.run_sql(f"CREATE TABLE !.! (a DECIMAL({20 + decimal_digits},{decimal_digits}) PRIMARY KEY, b text);", [ tested_schema, tested_table ])

gap_start = 18446744073709551615
gap_items = 10000
gap_step = 1000

def test_bug_33400387(step):
    def insert(r):
        session.run_sql(f"""INSERT INTO !.! VALUES {",".join([f"('{v}{'.' + str(random.randint(0, (10 ** decimal_digits) - 1)) if decimal_digits else ''}', '')" for v in r])};""", [ tested_schema, tested_table ])
    def generate_gaps():
        value = gap_start
        while True:
            yield value
            value += next(step)
    gen = generate_gaps()
    # clear the data
    session.run_sql("TRUNCATE TABLE !.!;", [ tested_schema, tested_table ])
    # insert rows with gaps in the PK
    insert([next(gen) for i in range(gap_items)])
    # all the remaining data is continuous
    start = next(gen)
    insert(range(start, start + gap_items))
    # use some dummy data
    session.run_sql("UPDATE !.! SET b = repeat('x', 5000);", [ tested_schema, tested_table ])
    # analyze the table for optimum results
    session.run_sql("ANALYZE TABLE !.!;", [ tested_schema, tested_table ])
    # run the test
    EXPECT_SUCCESS(tested_schema, [ tested_table ], test_output_absolute, { "bytesPerChunk": "1M", "compression": "none", "showProgress": False })
    # expect at least 320 chunks, we're dealing with random data, allow for some chunks which are smaller
    CHECK_OUTPUT_SANITY(test_output_absolute, 180000, 320, 4)
    # load the dump
    recreate_verification_schema()
    EXPECT_NO_THROWS(lambda: util.load_dump(test_output_absolute, { "schema": verification_schema, "showProgress": False }), "dump should be loaded without problems")
    # verify the checksums
    EXPECT_EQ(compute_checksum(tested_schema, tested_table), compute_checksum(verification_schema, tested_table), "checksum mismatch")

#@<> BUG#33400387 - equal gaps
test_bug_33400387(equal_gaps())

#@<> BUG#33400387 - random gaps
test_bug_33400387(random_gaps())

#@<> BUG#33400387 - increasing gaps
test_bug_33400387(increasing_gaps())

#@<> BUG#33400387 - decreasing gaps
test_bug_33400387(decreasing_gaps())

#@<> BUG#33400387 - create schema with some decimal digits
gap_start = -2 * 18446744073709551615
decimal_digits = 2
session.run_sql("DROP SCHEMA !;", [ tested_schema ])
session.run_sql("CREATE SCHEMA !;", [ tested_schema ])
session.run_sql(f"CREATE TABLE !.! (a DECIMAL({20 + decimal_digits},{decimal_digits}) PRIMARY KEY, b text);", [ tested_schema, tested_table ])

#@<> BUG#33400387 - equal gaps + decimal digits
test_bug_33400387(equal_gaps())

#@<> BUG#33400387 - random gaps + decimal digits
test_bug_33400387(random_gaps())

#@<> BUG#33400387 - increasing gaps + decimal digits
test_bug_33400387(increasing_gaps())

#@<> BUG#33400387 - decreasing gaps + decimal digits
test_bug_33400387(decreasing_gaps())

#@<> BUG#33400387 cleanup
session.run_sql("DROP SCHEMA !;", [ tested_schema ])

#@<> BUG#33744583
tested_schema = "virtual"
tested_table = "virtual"

session.run_sql(f"CREATE SCHEMA `{tested_schema}`")
session.run_sql(f"CREATE TABLE `{tested_schema}`.`{tested_table}` (a int)")

EXPECT_SUCCESS(tested_schema, [ tested_table ], test_output_absolute, { "showProgress": False })

session.run_sql("DROP SCHEMA !;", [ tested_schema ])

#@<> Cleanup
drop_all_schemas()
session.run_sql("SET GLOBAL local_infile = false;")
session.close()
testutil.destroy_sandbox(__mysql_sandbox_port1)
shutil.rmtree(incompatible_table_directory, True)
