/* choose user n password generated from https://passwordsgenerator.net/ without special symbols possibly; length 16 is ok */
CREATE USER 'web_WUeJ526zu8mz'@'%' IDENTIFIED BY 'XBMyq6wubfXqhKck';

GRANT SELECT, INSERT, UPDATE, DELETE ON `srv1_player`.* TO 'web_WUeJ526zu8mz'@'%';

GRANT SELECT, INSERT, UPDATE, DELETE ON `srv1_log`.* TO 'web_WUeJ526zu8mz'@'%';

GRANT SELECT, INSERT, UPDATE, DELETE ON `srv1_hotbackup`.* TO 'web_WUeJ526zu8mz'@'%';

GRANT SELECT, INSERT, UPDATE, DELETE ON `srv1_common`.* TO 'web_WUeJ526zu8mz'@'%';

GRANT SELECT, INSERT, UPDATE, DELETE ON `srv1_account`.* TO 'web_WUeJ526zu8mz'@'%';

