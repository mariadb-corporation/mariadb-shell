#@ {has_oci_environment('MDS')}

#@<> Execute upgrade checker vs MHS instance
util.check_for_server_upgrade(MDS_URI, {"excludeSchemas":["mysql_autopilot"]})

