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

#@<> WL15947 - setup
schema_name = "wl15947"
test_table_unique_null = test_table_non_unique
test_table_partitioned = "part"
test_table_gipk = "gipk"
# generated invisible primary keys are MySQL 8.0.30+; MariaDB has neither them
# nor the show_gipk_in_create_table_and_information_schema variable
gipk_supported = not __server_is_maria_db and __version_num >= 80030

def setup_db():
    session.run_sql("DROP SCHEMA IF EXISTS !", [schema_name])
    session.run_sql("CREATE SCHEMA !", [schema_name])
    session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT NOT NULL PRIMARY KEY) ENGINE=InnoDB;", [ schema_name, test_table_primary ])
    session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT NOT NULL UNIQUE KEY) ENGINE=InnoDB;", [ schema_name, test_table_unique ])
    session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT UNIQUE KEY) ENGINE=InnoDB;", [ schema_name, test_table_unique_null ])
    session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT) ENGINE=InnoDB;", [ schema_name, test_table_no_index ])
    session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT NOT NULL PRIMARY KEY) ENGINE=InnoDB PARTITION BY RANGE (`id`) (PARTITION p0 VALUES LESS THAN (500), PARTITION p1 VALUES LESS THAN (1000), PARTITION p2 VALUES LESS THAN MAXVALUE);", [ schema_name, test_table_partitioned ])
    session.run_sql(f"INSERT INTO !.! (`id`) VALUES {','.join(f'({i})' for i in range(1500))}", [ schema_name, test_table_primary ])
    session.run_sql("INSERT INTO !.! SELECT * FROM !.!;", [ schema_name, test_table_unique, schema_name, test_table_primary ])
    session.run_sql("INSERT INTO !.! SELECT * FROM !.!;", [ schema_name, test_table_unique_null, schema_name, test_table_primary ])
    session.run_sql("INSERT INTO !.! SELECT * FROM !.!;", [ schema_name, test_table_no_index, schema_name, test_table_primary ])
    session.run_sql("INSERT INTO !.! SELECT * FROM !.!;", [ schema_name, test_table_partitioned, schema_name, test_table_primary ])
    session.run_sql("ANALYZE TABLE !.!;", [ schema_name, test_table_primary ])
    session.run_sql("ANALYZE TABLE !.!;", [ schema_name, test_table_unique ])
    session.run_sql("ANALYZE TABLE !.!;", [ schema_name, test_table_unique_null ])
    session.run_sql("ANALYZE TABLE !.!;", [ schema_name, test_table_no_index ])
    session.run_sql("ANALYZE TABLE !.!;", [ schema_name, test_table_partitioned ])
    if gipk_supported:
        session.run_sql("SET @@SESSION.sql_generate_invisible_primary_key=ON")
        session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT) ENGINE=InnoDB;", [ schema_name, test_table_gipk ])
        session.run_sql("INSERT INTO !.! SELECT * FROM !.!;", [ schema_name, test_table_gipk, schema_name, test_table_primary ])
        session.run_sql("ANALYZE TABLE !.!;", [ schema_name, test_table_gipk ])
        session.run_sql("SET @@SESSION.sql_generate_invisible_primary_key=OFF")

create_all_schemas()
setup_db()

checksum_file = checksum_file_path(test_output_absolute)

#@<> WL15947-TSFR_1_1 - help text
help_text = """
      - checksum: bool (default: false) - Compute and include checksum of the
        dumped data.
"""
EXPECT_TRUE(help_text in util.help("dump_tables"))

#@<> WL15947-TSFR_1_1_1 - no checksum option
EXPECT_SUCCESS(schema_name, [ test_table_primary ], test_output_absolute, { "showProgress": False })
EXPECT_STDOUT_NOT_CONTAINS("Checksum")
EXPECT_FALSE(os.path.isfile(checksum_file))

#@<> WL15947-TSFR_1_1_1 - checksum option set to false
EXPECT_SUCCESS(schema_name, [ test_table_primary ], test_output_absolute, { "checksum": False, "showProgress": False })
EXPECT_STDOUT_NOT_CONTAINS("Checksum")
EXPECT_FALSE(os.path.isfile(checksum_file))

#@<> WL15947-TSFR_1_1_2 - option type
TEST_BOOL_OPTION("checksum")

#@<> WL15947-TSFR_1_2_1_1 - checksum option set to true
EXPECT_SUCCESS(schema_name, [ test_table_primary ], test_output_absolute, { "checksum": True, "showProgress": False })
# checksums are generated
EXPECT_STDOUT_CONTAINS("Checksum")
# file is written
EXPECT_TRUE(os.path.isfile(checksum_file))

if __os_type != "windows":
    # permissions are the same as in case of other files
    actual_file = os.path.join(test_output_absolute, "@.done.json")
    EXPECT_EQ(stat.S_IMODE(os.stat(actual_file).st_mode), stat.S_IMODE(os.stat(checksum_file).st_mode))
    EXPECT_EQ(os.stat(actual_file).st_uid, os.stat(checksum_file).st_uid)

# checksum file is a valid json
EXPECT_NO_THROWS(lambda: read_json(checksum_file), "checksum file should be a valid json")

#@<> WL15947-TSFR_1_3_1 - "checksum": True, "ddlOnly": False, "chunking": True, not partitioned table
EXPECT_SUCCESS(schema_name, [ test_table_primary ], test_output_absolute, { "checksum": True, "ddlOnly": False, "chunking": True, "showProgress": False })
checksums = read_json(checksum_file)
# table is not partitioned - partition name is empty, chunking is enabled - valid chunk ID is used
EXPECT_TRUE("0" in checksums["data"][schema_name][test_table_primary]["partitions"][""])

#@<> WL15947-TSFR_1_3_2 - "checksum": True, "ddlOnly": False, "chunking": True, partitioned table
EXPECT_SUCCESS(schema_name, [ test_table_partitioned ], test_output_absolute, { "checksum": True, "ddlOnly": False, "chunking": True, "showProgress": False })
checksums = read_json(checksum_file)
# table is partitioned - partition name is used, chunking is enabled - valid chunk ID is used
EXPECT_TRUE("0" in checksums["data"][schema_name][test_table_partitioned]["partitions"]["p0"])

#@<> WL15947-TSFR_1_4_1 - "checksum": True, "ddlOnly": False, "chunking": False, not partitioned table
EXPECT_SUCCESS(schema_name, [ test_table_primary ], test_output_absolute, { "checksum": True, "ddlOnly": False, "chunking": False, "showProgress": False })
checksums = read_json(checksum_file)
# table is not partitioned - partition name is empty, chunking is disabled - chunk ID is not used
EXPECT_TRUE("-1" in checksums["data"][schema_name][test_table_primary]["partitions"][""])

#@<> WL15947-TSFR_1_4_2 - "checksum": True, "ddlOnly": False, "chunking": False, partitioned table
EXPECT_SUCCESS(schema_name, [ test_table_partitioned ], test_output_absolute, { "checksum": True, "ddlOnly": False, "chunking": False, "showProgress": False })
checksums = read_json(checksum_file)
# table is partitioned - partition name is used, chunking is disabled - chunk ID is no used
EXPECT_TRUE("-1" in checksums["data"][schema_name][test_table_partitioned]["partitions"]["p0"])

#@<> WL15947-TSFR_1_5_1 - "checksum": True, "ddlOnly": True, not partitioned table
for chunking in [ True, False ]:
    EXPECT_SUCCESS(schema_name, [ test_table_primary ], test_output_absolute, { "checksum": True, "ddlOnly": True, "chunking": chunking, "showProgress": False })
    checksums = read_json(checksum_file)
    # table is not partitioned - partition name is empty, data is not dumped - chunk ID is not used
    EXPECT_TRUE("-1" in checksums["data"][schema_name][test_table_primary]["partitions"][""])

#@<> WL15947-TSFR_1_5_2 - "checksum": True, "ddlOnly": True, partitioned table
for chunking in [ True, False ]:
    EXPECT_SUCCESS(schema_name, [ test_table_partitioned ], test_output_absolute, { "checksum": True, "ddlOnly": True, "chunking": chunking, "showProgress": False })
    checksums = read_json(checksum_file)
    # table is partitioned - partition name is used, data is not dumped - chunk ID is no used
    EXPECT_TRUE("-1" in checksums["data"][schema_name][test_table_partitioned]["partitions"]["p0"])

#@<> WL15947-TSFR_1_6_1 - "checksum": True, table without an index
EXPECT_SUCCESS(schema_name, [ test_table_no_index ], test_output_absolute, { "checksum": True, "showProgress": False })

checksums = read_json(checksum_file)
# checksum information present
EXPECT_TRUE(test_table_no_index in checksums["data"][schema_name])

#@<> WL15947-TSFR_1_6_2 - "checksum": True, table with a non-NULL unique index
EXPECT_SUCCESS(schema_name, [ test_table_unique ], test_output_absolute, { "checksum": True, "showProgress": False })

checksums = read_json(checksum_file)
# checksum information present
EXPECT_TRUE(test_table_unique in checksums["data"][schema_name])

#@<> WL15947-TSFR_1_6_3 - "checksum": True, GIPK {gipk_supported and not __server_is_maria_db}
session.run_sql("SET @@GLOBAL.show_gipk_in_create_table_and_information_schema = OFF")
EXPECT_SUCCESS(schema_name, [ test_table_gipk ], test_output_absolute, { "checksum": True, "showProgress": False })

checksums = read_json(checksum_file)
# checksum information present
EXPECT_TRUE(test_table_gipk in checksums["data"][schema_name])

#@<> WL15947-TSFR_1_6_4 - "checksum": True, GIPK {gipk_supported and not __server_is_maria_db}
session.run_sql("SET @@GLOBAL.show_gipk_in_create_table_and_information_schema = ON")
EXPECT_SUCCESS(schema_name, [ test_table_gipk ], test_output_absolute, { "checksum": True, "showProgress": False })

checksums = read_json(checksum_file)
# checksum information present
EXPECT_TRUE(test_table_gipk in checksums["data"][schema_name])

#@<> WL15947-TSFR_1_6_5 - "checksum": True, table without an index, with ignore_missing_pks {not __server_is_maria_db}
EXPECT_SUCCESS(schema_name, [ test_table_no_index ], test_output_absolute, { "checksum": True, "compatibility": [ "ignore_missing_pks" ], "showProgress": False })

checksums = read_json(checksum_file)
# checksum information present
EXPECT_TRUE(test_table_no_index in checksums["data"][schema_name])

#@<> WL15947-TSFR_1_6_6 - "checksum": True, table with a NULL unique index
EXPECT_SUCCESS(schema_name, [ test_table_unique_null ], test_output_absolute, { "checksum": True, "showProgress": False })

checksums = read_json(checksum_file)
# checksum information present
EXPECT_TRUE(test_table_unique_null in checksums["data"][schema_name])

#@<> WL15947 - dry run
EXPECT_SUCCESS(schema_name, [ test_table_unique_null ], test_output_absolute, { "dryRun": True, "checksum": True, "showProgress": False })
EXPECT_STDOUT_CONTAINS("Checksumming enabled.")

#@<> WL15947 - cleanup
session.run_sql("DROP SCHEMA IF EXISTS !;", [schema_name])

#@<> Cleanup
drop_all_schemas()
session.run_sql("SET GLOBAL local_infile = false;")
session.close()
testutil.destroy_sandbox(__mysql_sandbox_port1)
shutil.rmtree(incompatible_table_directory, True)
