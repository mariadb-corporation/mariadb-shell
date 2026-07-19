#@ util help
util.help()

#@ util help, \? [USE:util help]
\? util

#@ util check_for_server_upgrade help {__have_upgrade_checker}
util.help('check_for_server_upgrade')

#@ util check_for_server_upgrade help, \? [USE:util check_for_server_upgrade help] {__have_upgrade_checker}
\? check_for_server_upgrade

#@ util copy_instance help {__have_dump_and_load}
# WL15298_TSFR_1_1
# WL15298_TSFR_4_4_1
# WL15298_TSFR_5_1_1
util.help('copy_instance')

#@ util copy_instance help, \? [USE:util copy_instance help] {__have_dump_and_load}
# WL15298_TSFR_1_1
# WL15298_TSFR_4_4_1
# WL15298_TSFR_5_1_1
\? copy_instance

#@ util copy_schemas help {__have_dump_and_load}
# WL15298_TSFR_2_1
# WL15298_TSFR_4_4_1
# WL15298_TSFR_5_1_1
util.help('copy_schemas')

#@ util copy_schemas help, \? [USE:util copy_schemas help] {__have_dump_and_load}
# WL15298_TSFR_2_1
# WL15298_TSFR_4_4_1
# WL15298_TSFR_5_1_1
\? copy_schemas

#@ util copy_tables help {__have_dump_and_load}
# WL15298_TSFR_4_4_1
util.help('copy_tables')

#@ util copy_tables help, \? [USE:util copy_tables help] {__have_dump_and_load}
# WL15298_TSFR_4_4_1
\? copy_tables

#@ util dump_binlogs help {__have_dump_and_load}
util.help('dump_binlogs')

#@ util dump_binlogs help, \? [USE:util dump_binlogs help] {__have_dump_and_load}
\? dump_binlogs

# WL13807-TSFR_1_1
#@ util dump_instance help {__have_dump_and_load}
util.help('dump_instance')

#@ util dump_instance help, \? [USE:util dump_instance help] {__have_dump_and_load}
\? dump_instance

# WL13807-TSFR_2_1
#@ util dump_schemas help {__have_dump_and_load}
util.help('dump_schemas')

#@ util dump_schemas help, \? [USE:util dump_schemas help] {__have_dump_and_load}
\? dump_schemas

# WL13804-TSFR_6_1
#@ util dump_tables help {__have_dump_and_load}
util.help('dump_tables')

#@ util dump_tables help, \? [USE:util dump_tables help] {__have_dump_and_load}
\? dump_tables

# WL13804-TSFR_1_4
#@ util export_table help {__have_dump_and_load}
util.help('export_table')

#@ util export_table help, \? [USE:util export_table help] {__have_dump_and_load}
\? export_table

#@ util import_json help {__have_x_protocol}
util.help('import_json')

#@ util import_json help, \? [USE:util import_json help]  {__have_x_protocol}
\? import_json

#@ util import_table help {__have_dump_and_load}
util.help('import_table')

#@ util import_table help, \? [USE:util import_table help] {__have_dump_and_load}
\? import_table

#@ util load_binlogs help {__have_dump_and_load}
util.help('load_binlogs')

#@ util load_binlogs help, \? [USE:util load_binlogs help] {__have_dump_and_load}
\? load_binlogs

#@ util load_dump help {__have_dump_and_load}
util.help('load_dump')

#@ util load_dump help, \? [USE:util load_dump help] {__have_dump_and_load}
\? load_dump

#@ util debug collect_diagnostics (full path) {__have_dump_and_load}
\? util.debug.collect_diagnostics

#@ util debug collect_diagnostics with util.debug.help (partial path) {__have_dump_and_load}
util.debug.help("collect_diagnostics")

#@ util debug collect_high_load_diagnostics {__have_dump_and_load}
\? collect_high_load_diagnostics

#@ util debug collect_slow_query_diagnostics {__have_dump_and_load}
\? collect_slow_query_diagnostics

#@ Help on remote storage {__have_dump_and_load}
\? remote storage

#@ Help on mysql_native_password {not __mariadb_build}
\? mysql_native_password