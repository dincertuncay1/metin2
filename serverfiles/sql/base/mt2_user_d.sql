INSERT IGNORE INTO mysql.user (user, host, password) VALUES ('mt2', 'localhost', PASSWORD('mt2'));
FLUSH PRIVILEGES;

GRANT SELECT, INSERT, UPDATE, DELETE ON `srv1_player`.* TO 'mt2'@'localhost';

GRANT SELECT, INSERT, UPDATE, DELETE ON `srv1_log`.* TO 'mt2'@'localhost';

GRANT SELECT, INSERT, UPDATE, DELETE ON `srv1_hotbackup`.* TO 'mt2'@'localhost';

GRANT SELECT, INSERT, UPDATE, DELETE ON `srv1_common`.* TO 'mt2'@'localhost';

GRANT SELECT, INSERT, UPDATE, DELETE ON `srv1_account`.* TO 'mt2'@'localhost';

