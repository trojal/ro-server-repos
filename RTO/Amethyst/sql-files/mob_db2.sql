#
# Table structure for table `mob_db2`
#

DROP TABLE IF EXISTS `mob_db2`;
CREATE TABLE `mob_db2` (
  `ID` mediumint(9) unsigned NOT NULL default '0',
  `Sprite` text NOT NULL,
  `kName` text NOT NULL,
  `iName` text NOT NULL,
  `LV` tinyint(6) unsigned NOT NULL default '0',
  `HP` int(9) unsigned NOT NULL default '0',
  `SP` mediumint(9) unsigned NOT NULL default '0',
  `EXP` mediumint(9) unsigned NOT NULL default '0',
  `JEXP` mediumint(9) unsigned NOT NULL default '0',
  `Range1` tinyint(4) unsigned NOT NULL default '0',
  `ATK1` smallint(6) unsigned NOT NULL default '0',
  `ATK2` smallint(6) unsigned NOT NULL default '0',
  `DEF` smallint(6) unsigned NOT NULL default '0',
  `MDEF` smallint(6) unsigned NOT NULL default '0',
  `STR` tinyint(4) unsigned NOT NULL default '0',
  `AGI` tinyint(4) unsigned NOT NULL default '0',
  `VIT` tinyint(4) unsigned NOT NULL default '0',
  `INT` tinyint(4) unsigned NOT NULL default '0',
  `DEX` tinyint(4) unsigned NOT NULL default '0',
  `LUK` tinyint(4) unsigned NOT NULL default '0',
  `Range2` tinyint(4) unsigned NOT NULL default '0',
  `Range3` tinyint(4) unsigned NOT NULL default '0',
  `Scale` tinyint(4) unsigned NOT NULL default '0',
  `Race` tinyint(4) unsigned NOT NULL default '0',
  `Element` tinyint(4) unsigned NOT NULL default '0',
  `Mode` smallint(6) unsigned NOT NULL default '0',
  `Speed` smallint(6) unsigned NOT NULL default '0',
  `aDelay` smallint(6) unsigned NOT NULL default '0',
  `aMotion` smallint(6) unsigned NOT NULL default '0',
  `dMotion` smallint(6) unsigned NOT NULL default '0',
  `MEXP` mediumint(9) unsigned NOT NULL default '0',
  `ExpPer` smallint(9) unsigned NOT NULL default '0',
  `MVP1id` smallint(9) unsigned NOT NULL default '0',
  `MVP1per` smallint(9) unsigned NOT NULL default '0',
  `MVP2id` smallint(9) unsigned NOT NULL default '0',
  `MVP2per` smallint(9) unsigned NOT NULL default '0',
  `MVP3id` smallint(9) unsigned NOT NULL default '0',
  `MVP3per` smallint(9) unsigned NOT NULL default '0',
  `Drop1id` smallint(9) unsigned NOT NULL default '0',
  `Drop1per` smallint(9) unsigned NOT NULL default '0',
  `Drop2id` smallint(9) unsigned NOT NULL default '0',
  `Drop2per` smallint(9) unsigned NOT NULL default '0',
  `Drop3id` smallint(9) unsigned NOT NULL default '0',
  `Drop3per` smallint(9) unsigned NOT NULL default '0',
  `Drop4id` smallint(9) unsigned NOT NULL default '0',
  `Drop4per` smallint(9) unsigned NOT NULL default '0',
  `Drop5id` smallint(9) unsigned NOT NULL default '0',
  `Drop5per` smallint(9) unsigned NOT NULL default '0',
  `Drop6id` smallint(9) unsigned NOT NULL default '0',
  `Drop6per` smallint(9) unsigned NOT NULL default '0',
  `Drop7id` smallint(9) unsigned NOT NULL default '0',
  `Drop7per` smallint(9) unsigned NOT NULL default '0',
  `Drop8id` smallint(9) unsigned NOT NULL default '0',
  `Drop8per` smallint(9) unsigned NOT NULL default '0',
  `Drop9id` smallint(9) unsigned NOT NULL default '0',
  `Drop9per` smallint(9) unsigned NOT NULL default '0',
  `DropCardid` smallint(9) unsigned NOT NULL default '0',
  `DropCardper` smallint(9) unsigned NOT NULL default '0',
  PRIMARY KEY  (`ID`)
) TYPE=MyISAM;

# // Monsters Additional Database
# //
# // Structure of Database :
# // ID,Sprite_Name,kROName,iROName,LV,HP,SP,EXP,JEXP,Range1,ATK1,ATK2,DEF,MDEF,STR,AGI,VIT,INT,DEX,LUK,Range2,Range3,Scale,Race,Element,Mode,Speed,aDelay,aMotion,dMotion,MEXP,ExpPer,MVP1id,MVP1per,MVP2id,MVP2per,MVP3id,MVP3per,Drop1id,Drop1per,Drop2id,Drop2per,Drop3id,Drop3per,Drop4id,Drop4per,Drop5id,Drop5per,Drop6id,Drop6per,Drop7id,Drop7per,Drop8id,Drop8per,Drop9id,Drop9per,DropCardid,DropCardper
# // Crusader quest monsters with poring stats (No drops)
REPLACE INTO `mob_db2` VALUES (1910,'C_GHOUL','Ghoul','Ghoul',1,50,0,2,1,1,7,10,0,5,1,1,1,1,6,30,10,12,1,3,21,131,400,1872,672,480,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0);
REPLACE INTO `mob_db2` VALUES (1911,'C_KHALITZBURG','Khalitzburg','Khalitzburg',1,50,0,2,1,1,7,10,0,5,1,1,1,1,6,30,10,12,1,3,21,131,400,1872,672,480,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0);
REPLACE INTO `mob_db2` VALUES (1912,'C_INJUSTICE','Injustice','Injustice',1,50,0,2,1,1,7,10,0,5,1,1,1,1,6,30,10,12,1,3,21,131,400,1872,672,480,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0);
REPLACE INTO `mob_db2` VALUES (1913,'C_REQUIEM','Requiem','Requiem',1,50,0,2,1,1,7,10,0,5,1,1,1,1,6,30,10,12,1,3,21,131,400,1872,672,480,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0);
REPLACE INTO `mob_db2` VALUES (1914,'C_RAYDRIC_ARCHER','Raydric Archer','Raydric Archer',1,50,0,2,1,1,7,10,0,5,1,1,1,1,6,30,10,12,1,3,21,131,400,1872,672,480,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0);
# // Easter Event Monsters
REPLACE INTO `mob_db2` VALUES (1920,'EASTER_EGG','Easter Egg','Easter Egg',3,300,0,4,4,0,1,2,20,20,1,1,1,1,1,20,10,12,0,0,60,128,1000,1001,1,1,0,0,0,0,0,0,0,0,1010,250,935,500,558,300,501,200,501,200,713,800,558,300,558,300,0,0,0,0);
REPLACE INTO `mob_db2` VALUES (1921,'EASTER_BUNNY','Easter Bunny','Easter Bunny',6,1800,0,60,55,1,20,26,0,40,1,36,6,1,11,80,10,10,0,2,60,181,200,1456,456,336,0,0,0,0,0,0,0,0,2250,200,515,8000,727,1200,746,1500,706,30,622,50,534,5000,0,0,0,0,4006,70);
# // eAthena Dev Team
REPLACE INTO `mob_db2` VALUES (1900,'VALARIS','Valaris','Valaris',99,668000,0,107250,37895,2,3220,4040,35,45,1,152,96,85,120,95,10,10,2,6,67,1973,100,1068,768,576,13000,5000,608,1000,750,400,923,3800,1466,200,2256,200,2607,800,714,500,617,3000,984,4300,985,5600,0,0,0,0,4147,1);
REPLACE INTO `mob_db2` VALUES (1901,'VALARIS_WORSHIPPER','Valaris\'s Worshipper','Valaris\'s Worshipper',50,8578,0,2706,1480,1,487,590,15,25,1,75,55,1,93,45,10,12,0,6,27,1685,100,868,480,120,0,0,0,0,0,0,0,0,923,500,984,63,1464,2,607,50,610,100,503,300,2405,50,0,0,0,0,4129,1);
REPLACE INTO `mob_db2` VALUES (1902,'MC_CAMERI','MC Cameri','MC Cameri',99,668000,0,107250,37895,2,3220,4040,35,45,1,152,96,85,120,95,10,10,2,6,67,1973,100,1068,768,576,13000,5000,608,1000,750,400,923,3800,1466,200,2256,200,2607,800,714,500,617,3000,984,4300,985,5600,0,0,0,0,4147,1);
REPLACE INTO `mob_db2` VALUES (1903,'POKI','Poki#3','Poki#3',99,1349000,0,4093000,1526000,9,4892,9113,22,35,1,180,39,67,193,130,10,12,1,7,64,1973,120,500,672,480,92100,7000,603,5500,617,3000,1723,1000,1228,100,1236,500,617,2500,1234,75,1237,125,1722,250,1724,100,1720,50,0,0,0,0);
REPLACE INTO `mob_db2` VALUES (1904,'SENTRY','Sentry','Sentry',99,668000,0,107250,37895,2,3220,4040,35,45,1,152,96,85,120,95,10,10,2,6,67,1973,100,1068,768,576,13000,5000,608,1000,750,400,923,3800,1466,200,2256,200,2607,800,714,500,617,3000,984,4300,985,5600,0,0,0,0,4147,1);
# // Mobs used for eAthena's Custom Equipped Mobs
REPLACE INTO `mob_db2` VALUES (1970,'PORING_','Pet Poring','Pet Poring',1,50,0,2,1,1,7,10,0,5,1,1,1,1,6,30,10,12,1,3,21,131,400,1872,672,480,0,0,0,0,0,0,0,0,909,7000,1202,100,938,400,512,1000,713,1500,741,5,619,20,0,0,0,0,4001,20);
REPLACE INTO `mob_db2` VALUES (1971,'CHONCHON__','Pet Chonchon','Pet Chonchon',4,67,0,5,4,1,10,13,10,0,1,10,4,5,12,2,10,12,0,4,24,129,200,1076,576,480,0,0,0,0,0,0,0,0,998,50,935,6500,909,1500,1205,55,601,100,742,5,1002,150,0,0,0,0,4009,5);
REPLACE INTO `mob_db2` VALUES (1972,'SPORE_','Pet Spore','Pet Spore',9,327,0,22,17,1,24,29,0,5,1,9,9,1,14,5,10,12,1,3,21,129,200,1872,672,288,0,0,0,0,0,0,0,0,921,5000,507,800,510,50,743,5,2220,40,921,5,578,100,0,0,0,0,4022,5);
REPLACE INTO `mob_db2` VALUES (1973,'PECOPECO_','Pet Peco Peco','Pet Peco Peco',13,531,0,85,36,1,35,46,0,0,1,13,13,25,27,9,10,12,2,2,23,649,200,1564,864,576,0,0,0,0,0,0,0,0,925,5500,2402,20,508,50,507,900,1604,100,582,60,0,0,0,0,0,0,4031,3);
REPLACE INTO `mob_db2` VALUES (1974,'ORK_WARRIOR_','Pet Orc Warrior','Pet Orc Warrior',24,1400,0,261,160,1,104,126,10,5,1,24,48,25,34,10,10,12,1,7,22,2693,200,1864,864,288,0,0,0,0,0,0,0,0,998,210,931,5500,756,40,2267,3,1352,10,1304,5,1301,100,0,0,0,0,4066,1);
REPLACE INTO `mob_db2` VALUES (1975,'MUNAK_','Pet Munak','Pet Munak',30,2872,0,361,218,1,180,230,0,0,1,15,20,5,46,15,10,12,1,1,29,2693,200,2468,768,288,0,0,0,0,0,0,0,0,901,5500,2264,1,2404,15,609,20,2337,1,2305,100,1558,5,0,0,0,0,4090,3);
REPLACE INTO `mob_db2` VALUES (1976,'ISIS_','Pet Isis','Pet Isis',43,4828,0,2396,993,1,423,507,10,35,1,65,43,30,72,15,10,12,2,6,27,661,200,1384,768,336,0,0,0,0,0,0,0,0,936,5500,2233,5,2603,1,733,150,732,20,954,1000,731,5,0,0,0,0,4116,1);
REPLACE INTO `mob_db2` VALUES (1977,'POPORING_','Pet Poporing','Pet Poporing',14,344,0,81,44,1,59,72,0,10,1,14,14,1,19,15,10,12,1,3,25,131,300,1672,672,480,0,0,0,0,0,0,0,0,938,5500,910,1500,511,500,514,200,729,5,0,0,0,0,0,0,0,0,4033,5);
REPLACE INTO `mob_db2` VALUES (1978,'HUNTER_FLY_','Pet Hunter Fly','Pet Hunter Fly',42,5242,0,1517,952,1,246,333,25,15,1,105,32,15,72,30,10,12,0,4,44,2693,150,676,576,480,0,0,0,0,0,0,0,0,996,30,999,100,943,5500,912,1300,756,129,2259,1,1226,2,0,0,0,0,4115,1);
REPLACE INTO `mob_db2` VALUES (1979,'STEEL_CHONCHON_','Pet Steel Chonchon','Pet Steel Chonchon',17,530,0,109,71,1,54,65,15,0,1,43,17,5,33,10,10,12,0,4,24,651,150,1076,576,480,0,0,0,0,0,0,0,0,992,70,999,30,910,2400,935,3500,943,30,998,200,1002,500,0,0,0,0,4042,1);
REPLACE INTO `mob_db2` VALUES (1980,'PICKY__','Pet Picky','Pet Picky',3,80,0,4,3,1,9,12,0,0,1,3,3,5,10,30,10,12,0,2,23,129,200,988,288,168,0,0,0,0,0,0,0,0,916,6500,949,700,2302,150,507,550,519,300,715,50,0,0,0,0,0,0,4008,10);
REPLACE INTO `mob_db2` VALUES (1981,'ROCKER_','Pet Rocker','Pet Rocker',9,198,0,20,16,1,24,29,5,10,1,9,18,10,14,15,10,12,1,4,22,129,200,1864,864,540,0,0,0,0,0,0,0,0,940,5000,909,5500,2298,4,1402,80,520,10,752,5,703,3,0,0,0,0,4021,10);
REPLACE INTO `mob_db2` VALUES (1982,'SMOKIE_','Pet Smokie','Pet Smokie',18,641,0,134,86,1,61,72,0,10,1,18,36,25,26,35,10,12,0,2,22,145,200,1576,576,420,0,0,0,0,0,0,0,0,945,5500,919,5500,516,800,2213,2,754,2,912,6,729,3,0,0,0,0,4044,1);
REPLACE INTO `mob_db2` VALUES (1983,'YOYO_','Pet Yoyo','Pet Yoyo',19,879,0,148,93,1,71,82,0,0,1,24,30,35,32,55,10,12,0,2,22,651,200,1054,54,384,0,0,0,0,0,0,0,0,942,5500,513,1500,508,100,919,5000,753,5,756,24,578,200,0,0,0,0,4051,1);
REPLACE INTO `mob_db2` VALUES (1984,'LUNATIC_','Pet Lunatic','Pet Lunatic',3,60,0,6,2,1,9,12,0,20,1,3,3,10,8,60,10,12,0,2,60,129,200,1456,456,336,0,0,0,0,0,0,0,0,705,6500,949,1000,2262,4,1102,100,512,600,515,1100,622,20,0,0,0,0,4006,15);
REPLACE INTO `mob_db2` VALUES (1985,'POISON_SPORE_','Pet Poison Spore','Pet Poison Spore',19,665,0,186,93,1,89,101,0,0,1,19,25,1,24,1,10,12,1,3,25,2693,200,1672,672,288,0,0,0,0,0,0,0,0,7033,5500,2221,20,511,550,510,50,972,30,921,1200,912,6,0,0,0,0,4048,2);
REPLACE INTO `mob_db2` VALUES (1986,'BAPHOMET__','Pet Baphomet Jr.','Pet Baphomet Jr.',50,8578,0,2706,1480,1,487,590,15,25,1,75,55,1,93,45,10,12,0,6,27,1685,100,868,480,120,0,0,0,0,0,0,0,0,923,500,984,63,1464,2,607,50,610,100,503,300,2405,50,0,0,0,0,4129,1);
REPLACE INTO `mob_db2` VALUES (1987,'DESERT_WOLF_B_','Pet Baby Desert Wolf','Pet Baby Desert Wolf',9,164,0,20,16,1,30,36,0,0,1,9,9,5,21,40,10,12,0,2,23,649,300,1600,900,240,0,0,0,0,0,0,0,0,1010,85,919,5500,2306,60,517,600,2301,200,0,0,0,0,0,0,0,0,4023,10);
REPLACE INTO `mob_db2` VALUES (1988,'DEVIRUCHI_','Pet Deviruchi','Pet Deviruchi',46,7360,0,2662,1278,1,475,560,10,25,1,69,40,55,87,30,10,12,0,6,27,1685,150,980,600,384,0,0,0,0,0,0,0,0,1038,5500,1039,400,0,0,1458,2,1009,5,912,1500,756,154,0,0,0,0,4122,1);
REPLACE INTO `mob_db2` VALUES (1989,'DOKEBI__','Pet Dokebi','Pet Dokebi',33,2697,0,889,455,1,197,249,0,10,1,50,40,35,69,40,10,12,0,6,27,145,250,1156,456,384,0,0,0,0,0,0,0,0,1021,5500,757,112,1517,2,1613,1,969,1,1501,300,1005,5,0,0,0,0,4098,1);
REPLACE INTO `mob_db2` VALUES (1990,'DROPS_','Pet Drops','Pet Drops',3,55,0,4,3,1,10,13,0,0,1,3,3,1,12,15,10,12,1,3,23,131,400,1452,672,480,0,0,0,0,0,0,0,0,909,7500,1602,80,938,500,512,1100,713,1700,741,5,620,20,0,0,0,0,4004,10);
REPLACE INTO `mob_db2` VALUES (1991,'PETIT__','Pet Earth Petite','Pet Earth Petite',44,6881,0,1677,1034,1,360,427,30,30,1,44,62,69,79,60,10,12,1,9,22,661,200,1624,620,384,0,0,0,0,0,0,0,0,1035,5500,1037,300,756,140,509,1000,1510,150,912,1500,606,15,0,0,0,0,4118,1);
REPLACE INTO `mob_db2` VALUES (1992,'SAVAGE_BABE_','Pet Savage Babe','Pet Savage Babe',7,182,0,14,12,1,20,25,0,0,1,7,14,5,12,35,10,12,0,2,22,129,400,1624,624,576,0,0,0,0,0,0,0,0,919,5500,1302,100,517,500,1750,1000,949,850,1010,80,627,20,0,0,0,0,4017,1);
REPLACE INTO `mob_db2` VALUES (1993,'SOHEE_','Pet Sohee','Pet Sohee',33,5628,0,739,455,1,210,251,0,10,1,33,33,10,58,15,10,12,1,6,21,145,300,2112,912,576,0,0,0,0,0,0,0,0,1020,5500,1049,50,2277,1,2504,5,1217,5,501,1000,662,100,0,0,0,0,4100,1);
REPLACE INTO `mob_db2` VALUES (1994,'BON_GUN_','Pet Bon Gun','Pet Bon Gun',32,3520,0,424,242,1,220,260,0,0,1,15,36,10,48,15,10,12,1,1,29,661,200,1720,500,420,0,0,0,0,0,0,0,0,1094,5500,7014,40,618,60,2337,2,609,15,508,1000,502,250,5046,1,0,0,4212,1);
# //Custom Fire Poring. Warning, Colides with META_DENIRO
# //1239,FIRE_PORING,Fire Poring,Fire Poring,1,50,0,2,1,1,7,10,0,5,1,1,1,1,6,30,10,12,1,3,21,131,400,1872,672,480,0,0,0,0,0,0,0,0,909,7000,1202,100,938,400,512,1000,713,1500,741,5,619,20,0,0,0,0,4001,20
