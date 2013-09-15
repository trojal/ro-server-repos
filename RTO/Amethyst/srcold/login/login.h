// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _LOGIN_H_
#define _LOGIN_H_

#define LOGIN_CONF_NAME "conf/login_athena.conf"
#define LAN_CONF_NAME "conf/subnet_athena.conf"

// supported encryption types: 1- passwordencrypt, 2- passwordencrypt2, 3- both
#define PASSWORDENC 3

extern uint16 login_port;

struct mmo_account {
	int version;
	char userid[NAME_LENGTH];
	char passwd[NAME_LENGTH];
	int passwdenc;

	uint32 account_id;
	long login_id1;
	long login_id2;
	long char_id;
	char lastlogin[24];
	char sex;
};

struct mmo_char_server {
	char name[20];
	uint32 ip;
	uint16 port;
	uint16 users;		// user count on this server
	uint16 maintenance;	// in maintenance mode?
	uint16 new_;		// allows creating new chars?
};

#endif /* _LOGIN_H_ */
