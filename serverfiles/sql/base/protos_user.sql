/* change the password if used on remote servers */
CREATE USER `protos`@`%` IDENTIFIED BY 'protos';

GRANT Select ON TABLE `srv1_player`.`item_proto` TO `protos`@`%`;

GRANT Select ON TABLE `srv1_player`.`mob_proto` TO `protos`@`%`;
