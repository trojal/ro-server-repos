--
-- eAthena Database Converter ( InnoDB -> MyISAM )
--

ALTER TABLE `cart_inventory` TYPE = MYISAM;
ALTER TABLE `char` TYPE = MYISAM;
ALTER TABLE `charlog` TYPE = MYISAM;
ALTER TABLE `friends` TYPE = MYISAM;
ALTER TABLE `global_reg_value` TYPE = MYISAM;
ALTER TABLE `guild` TYPE = MYISAM;
ALTER TABLE `guild_alliance` TYPE = MYISAM;
ALTER TABLE `guild_castle` TYPE = MYISAM;
ALTER TABLE `guild_expulsion` TYPE = MYISAM;
ALTER TABLE `guild_member` TYPE = MYISAM;
ALTER TABLE `guild_position` TYPE = MYISAM;
ALTER TABLE `guild_skill` TYPE = MYISAM;
ALTER TABLE `guild_storage` TYPE = MYISAM;
ALTER TABLE `homunculus` TYPE = MYISAM;
ALTER TABLE `interlog` TYPE = MYISAM;
ALTER TABLE `inventory` TYPE = MYISAM;
ALTER TABLE `ipbanlist` TYPE = MYISAM;
ALTER TABLE `item_db` TYPE = MYISAM;
ALTER TABLE `item_db2` TYPE = MYISAM;
ALTER TABLE `login` TYPE = MYISAM;
#ALTER TABLE `loginlog` TYPE = MYISAM;
#ALTER TABLE `mail` TYPE = MYISAM;
ALTER TABLE `mapreg` TYPE = MYISAM;
ALTER TABLE `memo` TYPE = MYISAM;
ALTER TABLE `mob_db` TYPE = MYISAM;
ALTER TABLE `mob_db2` TYPE = MYISAM;
ALTER TABLE `party` TYPE = MYISAM;
ALTER TABLE `pet` TYPE = MYISAM;
ALTER TABLE `ragsrvinfo` TYPE = MYISAM;
ALTER TABLE `sc_data` TYPE = MYISAM;
ALTER TABLE `skill` TYPE = MYISAM;
ALTER TABLE `skill_homunculus` TYPE = MYISAM;
ALTER TABLE `sstatus` TYPE = MYISAM;
ALTER TABLE `storage` TYPE = MYISAM;
