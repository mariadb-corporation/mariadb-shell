-- Copyright (c) 2025, Oracle and/or its affiliates.
-- Copyright (c) 2026, MariaDB plc.
-- -----------------------------------------------------
-- Config

DELETE FROM `config`;
INSERT INTO `config` (`id`, `service_enabled`, `data`) VALUES (1, 1, '{ "defaultStaticContent": { "index.html": "${indexHtmlB64}", "favicon.ico": "${faviconIcoB64}", "favicon.svg": "${faviconSvgB64}", "mariadb-seal.svg": "${mariadbSealSvgB64}", "standalone-preact.js": "${standalonePreactJsB64}" }, "directoryIndexDirective": [ "index.html" ] }');
