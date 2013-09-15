// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/mmo.h"
#include "../common/core.h"
#include "../common/socket.h"
#include "../common/db.h"
#include "../common/timer.h"
#include "../common/malloc.h"
#include "../common/strlib.h"
#include "../common/showmsg.h"
#include "../common/version.h"
#include "../common/md5calc.h"
#include "../common/lock.h"
#include "login.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> // for stat/lstat/fstat

struct Login_Config {

	uint32 login_ip;								// the address to bind to
	uint16 login_port;								// the port to bind to
	unsigned int ip_sync_interval;					// interval (in minutes) to execute a DNS/IP update (for dynamic IPs)
	bool log_login;									// whether to log login server actions or not
	char date_format[32];							// date format used in messages
	bool console;									// console input system enabled?
	bool new_account_flag;							// autoregistration via _M/_F ?
//	bool case_sensitive;							// are logins case sensitive ?
	bool use_md5_passwds;							// work with password hashes instead of plaintext passwords?
//	bool login_gm_read;								// should the login server handle info about gm accounts?
	uint8 min_level_to_connect;						// minimum level of player/GM (0: player, 1-99: GM) to connect
	bool online_check;								// reject incoming players that are already registered as online ?
	bool check_client_version;						// check the clientversion set in the clientinfo ?
	unsigned int client_version_to_connect;			// the client version needed to connect (if checking is enabled)

//	bool ipban;										// perform IP blocking (via contents of `ipbanlist`) ?
//	bool dynamic_pass_failure_ban;					// automatic IP blocking due to failed login attemps ?
//	unsigned int dynamic_pass_failure_ban_interval;	// how far to scan the loginlog for password failures
//	unsigned int dynamic_pass_failure_ban_limit;	// number of failures needed to trigger the ipban
//	unsigned int dynamic_pass_failure_ban_duration;	// duration of the ipban
	bool use_dnsbl;									// dns blacklist blocking ?
	char dnsbl_servs[1024];							// comma-separated list of dnsbl servers

} login_config;

int login_fd; // login server socket
#define MAX_SERVERS 30
int server_fd[MAX_SERVERS]; // char server sockets
struct mmo_char_server server[MAX_SERVERS]; // char server data

// Advanced subnet check [LuzZza]
struct s_subnet {
	uint32 mask;
	uint32 char_ip;
	uint32 map_ip;
} subnet[16];

int subnet_count = 0;

struct gm_account* gm_account_db = NULL;
unsigned int GM_num = 0; // number of gm accounts
unsigned int GM_max = 0;

//Account registration flood protection [Kevin]
int allowed_regs = 1;
int time_allowed = 10; //in seconds
unsigned int new_reg_tick = 0;
int num_regs = 0;

uint32 account_id_count = START_ACCOUNT_NUM;

char account_filename[1024] = "save/account.txt";
char GM_account_filename[1024] = "conf/GM_account.txt";
char login_log_filename[1024] = "log/login.log";
FILE *log_fp = NULL;
char login_log_unknown_packets_filename[1024] = "log/login_unknown_packets.log";
int save_unknown_packets = 0;
long creation_time_GM_account_file;
int gm_account_filename_check_timer = 15; // Timer to check if GM_account file has been changed and reload GM account automaticaly (in seconds; default: 15)

int display_parse_login = 0; // 0: no, 1: yes
int display_parse_admin = 0; // 0: no, 1: yes
int display_parse_fromchar = 0; // 0: no, 1: yes (without packet 0x2714), 2: all packets


enum {
	ACO_DENY_ALLOW = 0,
	ACO_ALLOW_DENY,
	ACO_MUTUAL_FAILTURE,
	ACO_STRSIZE = 128,
};

int access_order = ACO_DENY_ALLOW;
int access_allownum = 0;
int access_denynum = 0;
char *access_allow = NULL;
char *access_deny = NULL;

int access_ladmin_allownum = 0;
char *access_ladmin_allow = NULL;

int add_to_unlimited_account = 0; // Give possibility or not to adjust (ladmin command: timeadd) the time of an unlimited account.
int start_limited_time = -1; // Starting additional sec from now for the limited time at creation of accounts (-1: unlimited time, 0 or more: additional sec from now)

struct login_session_data {
	uint16 md5keylen;
	char md5key[20];
};

#define AUTH_FIFO_SIZE 256
struct _auth_fifo {
	uint32 account_id;
	uint32 login_id1, login_id2;
	uint32 ip;
	uint8 sex;
	bool delflag;
} auth_fifo[AUTH_FIFO_SIZE];
int auth_fifo_pos = 0;

struct online_login_data {
	uint32 account_id;
	int waiting_disconnect;
	int char_server;
};

// holds info about all existing accounts
struct auth_data {
	uint32 account_id;
	uint8 sex; // 0, 1, 2
	char userid[24];
	char pass[32+1]; // 23+1 for normal, 32+1 for md5-ed passwords
	char lastlogin[24];
	int logincount;
	uint32 state; // packet 0x006a value + 1 (0: compte OK)
	char email[40]; // e-mail (by default: a@a.com)
	char error_message[20]; // Message of error code #6 = Your are Prohibited to log in until %s (packet 0x006a)
	time_t ban_until_time; // # of seconds 1/1/1970 (timestamp): ban time limit of the account (0 = no ban)
	time_t connect_until_time; // # of seconds 1/1/1970 (timestamp): Validity limit of the account (0 = unlimited)
	char last_ip[16]; // save of last IP of connection
	char memo[255]; // a memo field
	int account_reg2_num;
	struct global_reg account_reg2[ACCOUNT_REG2_NUM]; // account script variables (stored on login server)
} *auth_dat = NULL;

uint32 auth_num = 0, auth_max = 0;

// define the number of times that some players must authentify them before to save account file.
// it's just about normal authentification. If an account is created or modified, save is immediatly done.
// An authentification just change last connected IP and date. It already save in log file.
// set minimum auth change before save:
#define AUTH_BEFORE_SAVE_FILE 10
// set divider of auth_num to found number of change before save
#define AUTH_SAVE_FILE_DIVIDER 50
int auth_before_save_file = 0; // Counter. First save when 1st char-server do connection.

int admin_state = 0;
char admin_pass[24] = "";
char gm_pass[64] = "";
int level_new_gm = 60;


static struct dbt *online_db;

int charif_sendallwos(int sfd, unsigned char *buf, unsigned int len);

//------------------------------
// Writing function of logs file
//------------------------------
int login_log(char *fmt, ...)
{
	if( login_config.log_login ) {
		va_list ap;
		time_t raw_time;
		char tmpstr[2048];

		if(!log_fp)
			log_fp = fopen(login_log_filename, "a");

		if (log_fp) {
			if (fmt[0] == '\0') // jump a line if no message
				fprintf(log_fp, "\n");
			else {
				va_start(ap, fmt);
				time(&raw_time);
				strftime(tmpstr, 24, login_config.date_format, localtime(&raw_time));
				sprintf(tmpstr + strlen(tmpstr), ": %s", fmt);
				vfprintf(log_fp, tmpstr, ap);
				va_end(ap);
			}
			fflush(log_fp); // under cygwin or windows, if software is stopped, data are not written in the file -> fflush at every line
		}
	}

	return 0;
}

static void* create_online_user(DBKey key, va_list args)
{
	struct online_login_data *p;
	p = aCalloc(1, sizeof(struct online_login_data));
	p->account_id = key.i;
	p->char_server = -1;
	p->waiting_disconnect = -1;
	return p;
}
static int waiting_disconnect_timer(int tid, unsigned int tick, int id, int data);

//-----------------------------------------------------
// Online User Database [Wizputer]
//-----------------------------------------------------

void add_online_user(int char_server, int account_id)
{
	struct online_login_data *p;
	if( !login_config.online_check )
		return;
	p = idb_ensure(online_db, account_id, create_online_user);
	p->char_server = char_server;
	if( p->waiting_disconnect != -1 )
	{
		delete_timer(p->waiting_disconnect, waiting_disconnect_timer);
		p->waiting_disconnect = -1;
	}
}

void remove_online_user(int account_id)
{
	if( !login_config.online_check )
		return;
	if( account_id == 99 )
	{// reset all to offline
		online_db->clear(online_db, NULL); // purge db
		return;
	}
	idb_remove(online_db, account_id);
}

static int waiting_disconnect_timer(int tid, unsigned int tick, int id, int data)
{
	struct online_login_data* p = idb_get(online_db, id);
	if( p != NULL && p->waiting_disconnect == id )
	{
		p->waiting_disconnect = -1;
		remove_online_user(id);
	}
	return 0;
}

static int sync_ip_addresses(int tid, unsigned int tick, int id, int data)
{
	uint8 buf[2];
	ShowInfo("IP Sync in progress...\n");
	WBUFW(buf,0) = 0x2735;
	charif_sendallwos(-1, buf, 2);
	return 0;
}

//----------------------------------------------------------------------
// Determine if an account (id) is a GM account
// and returns its level (or 0 if it isn't a GM account or if not found)
//----------------------------------------------------------------------
int isGM(int account_id)
{
	unsigned int i;
	for(i=0; i < GM_num; i++)
		if(gm_account_db[i].account_id == account_id)
			return gm_account_db[i].level;
	return 0;
}

//----------------------------------------------------------------------
// Adds a new GM using acc id and level
//----------------------------------------------------------------------
void addGM(uint32 account_id, int level)
{
	unsigned int i;
	int do_add = 0;
	for(i = 0; i < auth_num; i++) {
		if (auth_dat[i].account_id == account_id) {
			do_add = 1;
			break;
		}
	}
	for(i = 0; i < GM_num; i++)
		if (gm_account_db[i].account_id == account_id) {
			if (gm_account_db[i].level == level)
				ShowWarning("addGM: GM account %d defined twice (same level: %d).\n", account_id, level);
			else {
				ShowWarning("addGM: GM account %d defined twice (levels: %d and %d).\n", account_id, gm_account_db[i].level, level);
				gm_account_db[i].level = level;
			}
			return;
		}

	// if new account
	if (i == GM_num && do_add) {
		if (GM_num >= GM_max) {
			GM_max += 256;
			gm_account_db = (struct gm_account*)aRealloc(gm_account_db, sizeof(struct gm_account) * GM_max);
			memset(gm_account_db + (GM_max - 256), 0, sizeof(struct gm_account) * 256);
		}
		gm_account_db[GM_num].account_id = account_id;
		gm_account_db[GM_num].level = level;
		GM_num++;
		if (GM_num >= 4000) {
			ShowWarning("4000 GM accounts found. Next GM accounts are not read.\n");
			login_log("***WARNING: 4000 GM accounts found. Next GM accounts are not read.\n");
		}
	}
}

//-------------------------------------------------------
// Reading function of GM accounts file (and their level)
//-------------------------------------------------------
int read_gm_account(void)
{
	char line[512];
	FILE *fp;
	int account_id, level;
	int line_counter;
	struct stat file_stat;
	int start_range = 0, end_range = 0, is_range = 0, current_id = 0;

	if(gm_account_db) aFree(gm_account_db);
	gm_account_db = (struct gm_account*)aCalloc(GM_max, sizeof(struct gm_account));
	GM_num = 0;

	// get last modify time/date
	if (stat(GM_account_filename, &file_stat))
		creation_time_GM_account_file = 0; // error
	else
		creation_time_GM_account_file = (long)file_stat.st_mtime;

	if ((fp = fopen(GM_account_filename, "r")) == NULL) {
		ShowError("read_gm_account: GM accounts file [%s] not found.\n", GM_account_filename);
		ShowError("                 Actually, there is no GM accounts on the server.\n");
		login_log("read_gm_account: GM accounts file [%s] not found.\n", GM_account_filename);
		login_log("                 Actually, there is no GM accounts on the server.\n");
		return 1;
	}

	line_counter = 0;
	// limited to 4000, because we send information to char-servers (more than 4000 GM accounts???)
	// int (id) + int (level) = 8 bytes * 4000 = 32k (limit of packets in windows)
	while(fgets(line, sizeof(line), fp) && GM_num < 4000)
	{
		line_counter++;
		if ((line[0] == '/' && line[1] == '/') || line[0] == '\0' || line[0] == '\n' || line[0] == '\r')
			continue;
		is_range = (sscanf(line, "%d%*[-~]%d %d",&start_range,&end_range,&level)==3); // ID Range [MC Cameri]
		if (!is_range && sscanf(line, "%d %d", &account_id, &level) != 2 && sscanf(line, "%d: %d", &account_id, &level) != 2)
			ShowError("read_gm_account: file [%s], invalid 'acount_id|range level' format (line #%d).\n", GM_account_filename, line_counter);
		else if (level <= 0)
			ShowError("read_gm_account: file [%s] %dth account (line #%d) (invalid level [0 or negative]: %d).\n", GM_account_filename, GM_num+1, line_counter, level);
		else {
			if (level > 99) {
				ShowNotice("read_gm_account: file [%s] %dth account (invalid level, but corrected: %d->99).\n", GM_account_filename, GM_num+1, level);
				level = 99;
			}
			if (is_range) {
				if (start_range==end_range)
					ShowError("read_gm_account: file [%s] invalid range, beginning of range is equal to end of range (line #%d).\n", GM_account_filename, line_counter);
				else if (start_range>end_range)
					ShowError("read_gm_account: file [%s] invalid range, beginning of range must be lower than end of range (line #%d).\n", GM_account_filename, line_counter);
				else
					for (current_id = start_range;current_id<=end_range;current_id++)
						addGM(current_id,level);
			} else {
				addGM(account_id,level);
			}
		}
	}
	fclose(fp);

	ShowStatus("read_gm_account: file '%s' read (%d GM accounts found).\n", GM_account_filename, GM_num);
	login_log("read_gm_account: file '%s' read (%d GM accounts found).\n", GM_account_filename, GM_num);

	return 0;
}

//--------------------------------------------------------------
// Test of the IP mask
// (ip: IP to be tested, str: mask x.x.x.x/# or x.x.x.x/y.y.y.y)
//--------------------------------------------------------------
int check_ipmask(uint32 ip, const unsigned char *str)
{
	unsigned int i = 0, m = 0;
	uint32 ip2, mask = 0;
	uint32 a0, a1, a2, a3;
	uint8* p = (uint8 *)&ip2, *p2 = (uint8 *)&mask;


	// scan ip address
	if (sscanf((const char*)str, "%u.%u.%u.%u/%n", &a0, &a1, &a2, &a3, &i) != 4 || i == 0)
		return 0;
	p[0] = (uint8)a3; p[1] = (uint8)a2; p[2] = (uint8)a1; p[3] = (uint8)a0;

	// scan mask
	if (sscanf((const char*)str+i, "%u.%u.%u.%u", &a0, &a1, &a2, &a3) == 4) {
		p2[0] = (uint8)a3; p2[1] = (uint8)a2; p2[2] = (uint8)a1; p2[3] = (uint8)a0;
	} else if (sscanf((const char*)(str+i), "%u", &m) == 1 && m <= 32) {
		for(i = 32 - m; i < 32; i++)
			mask |= (1 << i);
	} else {
		ShowError("check_ipmask: invalid mask [%s].\n", str);
		return 0;
	}

	return ((ip & mask) == (ip2 & mask));
}

//---------------------
// Access control by IP
//---------------------
int check_ip(uint32 ip)
{
	int i;
	char buf[20];
	char * access_ip;
	enum { ACF_DEF, ACF_ALLOW, ACF_DENY } flag = ACF_DEF;

	if (access_allownum == 0 && access_denynum == 0)
		return 1; // When there is no restriction, all IP are authorised.

//	+   012.345.: front match form, or
//	    all: all IP are matched, or
//	    012.345.678.901/24: network form (mask with # of bits), or
//	    012.345.678.901/255.255.255.0: network form (mask with ip mask)
//	+   Note about the DNS resolution (like www.ne.jp, etc.):
//	    There is no guarantee to have an answer.
//	    If we have an answer, there is no guarantee to have a 100% correct value.
//	    And, the waiting time (to check) can be long (over 1 minute to a timeout). That can block the software.
//	    So, DNS notation isn't authorised for ip checking.
	sprintf(buf, "%u.%u.%u.%u.", CONVIP(ip));

	for(i = 0; i < access_allownum; i++) {
		access_ip = access_allow + i * ACO_STRSIZE;
		if (memcmp(access_ip, buf, strlen(access_ip)) == 0 || check_ipmask(ip, (unsigned char*)access_ip)) {
			if(access_order == ACO_ALLOW_DENY)
				return 1; // With 'allow, deny' (deny if not allow), allow has priority
			flag = ACF_ALLOW;
			break;
		}
	}

	for(i = 0; i < access_denynum; i++) {
		access_ip = access_deny + i * ACO_STRSIZE;
		if (memcmp(access_ip, buf, strlen(access_ip)) == 0 || check_ipmask(ip, (unsigned char*)access_ip)) {
			//flag = ACF_DENY; // not necessary to define flag
			return 0; // At this point, if it's 'deny', we refuse connection.
		}
	}

	return (flag == ACF_ALLOW || access_order == ACO_DENY_ALLOW) ? 1:0;
		// With 'mutual-failture', only 'allow' and non 'deny' IP are authorised.
		//   A non 'allow' (even non 'deny') IP is not authorised. It's like: if allowed and not denied, it's authorised.
		//   So, it's disapproval if you have no description at the time of 'mutual-failture'.
		// With 'deny,allow' (allow if not deny), because here it's not deny, we authorise.
}

//--------------------------------
// Access control by IP for ladmin
//--------------------------------
int check_ladminip(uint32 ip)
{
	int i;
	char buf[20];
	char * access_ip;

	if (access_ladmin_allownum == 0)
		return 1; // When there is no restriction, all IP are authorised.

//	+   012.345.: front match form, or
//	    all: all IP are matched, or
//	    012.345.678.901/24: network form (mask with # of bits), or
//	    012.345.678.901/255.255.255.0: network form (mask with ip mask)
//	+   Note about the DNS resolution (like www.ne.jp, etc.):
//	    There is no guarantee to have an answer.
//	    If we have an answer, there is no guarantee to have a 100% correct value.
//	    And, the waiting time (to check) can be long (over 1 minute to a timeout). That can block the software.
//	    So, DNS notation isn't authorised for ip checking.
	sprintf(buf, "%u.%u.%u.%u.", CONVIP(ip));

	for(i = 0; i < access_ladmin_allownum; i++) {
		access_ip = access_ladmin_allow + i * ACO_STRSIZE;
		if (memcmp(access_ip, buf, strlen(access_ip)) == 0 || check_ipmask(ip, (unsigned char*)access_ip)) {
			return 1;
		}
	}

	return 0;
}

//-----------------------------------------------
// Search an account id
//   (return account index or -1 (if not found))
//   If exact account name is not found,
//   the function checks without case sensitive
//   and returns index if only 1 account is found
//   and similar to the searched name.
//-----------------------------------------------
int search_account_index(char* account_name)
{
	unsigned int i, quantity;
	int index;

	quantity = 0;
	index = -1;

	for(i = 0; i < auth_num; i++) {
		// Without case sensitive check (increase the number of similar account names found)
		if (stricmp(auth_dat[i].userid, account_name) == 0) {
			// Strict comparison (if found, we finish the function immediatly with correct value)
			if (strcmp(auth_dat[i].userid, account_name) == 0)
				return i;
			quantity++;
			index = i;
		}
	}
	// Here, the exact account name is not found
	// We return the found index of a similar account ONLY if there is 1 similar account
	if (quantity == 1)
		return index;

	// Exact account name is not found and 0 or more than 1 similar accounts have been found ==> we say not found
	return -1;
}

//--------------------------------------------------------
// Create a string to save the account in the account file
//--------------------------------------------------------
int mmo_auth_tostr(char* str, struct auth_data* p)
{
	int i;
	char *str_p = str;

	str_p += sprintf(str_p, "%u\t%s\t%s\t%s\t%c\t%d\t%u\t%s\t%s\t%ld\t%s\t%s\t%ld\t",
	                 p->account_id, p->userid, p->pass, p->lastlogin,
	                 p->sex == 2 ? 'S' : p->sex == 1 ? 'M' : 'F',
	                 p->logincount, p->state, p->email, p->error_message,
	                 (long)p->connect_until_time, p->last_ip, p->memo, (long)p->ban_until_time);

	for(i = 0; i < p->account_reg2_num; i++)
		if (p->account_reg2[i].str[0])
			str_p += sprintf(str_p, "%s,%s ", p->account_reg2[i].str, p->account_reg2[i].value);

	return 0;
}

//---------------------------------
// Reading of the accounts database
//---------------------------------
int mmo_auth_init(void)
{
	FILE *fp;
	uint32 account_id;
	uint32 state;
	int logincount, n;
	uint32 i, j;
	char line[2048], *p, userid[2048], pass[2048], lastlogin[2048], sex, email[2048], error_message[2048], last_ip[2048], memo[2048];
	long ban_until_time;
	long connect_until_time;
	char str[2048];
	char v[2048];
	int GM_count = 0;
	int server_count = 0;

	auth_max = 256;
	auth_dat = (struct auth_data*)aCalloc(auth_max, sizeof(struct auth_data));

	if ((fp = fopen(account_filename, "r")) == NULL) {
		// no account file -> no account -> no login, including char-server (ERROR)
		ShowError(CL_RED"mmmo_auth_init: Accounts file [%s] not found."CL_RESET"\n", account_filename);
		return 0;
	}

	while(fgets(line, sizeof(line), fp) != NULL)
	{
		if (line[0] == '/' && line[1] == '/')
			continue;

		p = line;

		memset(userid, 0, sizeof(userid));
		memset(pass, 0, sizeof(pass));
		memset(lastlogin, 0, sizeof(lastlogin));
		memset(email, 0, sizeof(email));
		memset(error_message, 0, sizeof(error_message));
		memset(last_ip, 0, sizeof(last_ip));
		memset(memo, 0, sizeof(memo));

		// database version reading (v2)
		if (((i = sscanf(line, "%u\t%[^\t]\t%[^\t]\t%[^\t]\t%c\t%d\t%u\t"
		                 "%[^\t]\t%[^\t]\t%ld\t%[^\t]\t%[^\t]\t%ld%n",
		    &account_id, userid, pass, lastlogin, &sex, &logincount, &state,
		    email, error_message, &connect_until_time, last_ip, memo, &ban_until_time, &n)) == 13 && line[n] == '\t') ||
		    ((i = sscanf(line, "%u\t%[^\t]\t%[^\t]\t%[^\t]\t%c\t%d\t%u\t"
		                 "%[^\t]\t%[^\t]\t%ld\t%[^\t]\t%[^\t]%n",
		    &account_id, userid, pass, lastlogin, &sex, &logincount, &state,
		    email, error_message, &connect_until_time, last_ip, memo, &n)) == 12 && line[n] == '\t')) {
			n = n + 1;

			// Some checks
			if (account_id > END_ACCOUNT_NUM) {
				ShowError(CL_RED"mmmo_auth_init: an account has an id higher than %d\n", END_ACCOUNT_NUM);
				ShowError("               account id #%d -> account not read (saved in log file)."CL_RESET"\n", account_id);
				login_log("mmmo_auth_init: ******Error: an account has an id higher than %d.\n", END_ACCOUNT_NUM);
				login_log("               account id #%d -> account not read (saved in next line):\n", account_id);
				login_log("%s", line);
				continue;
			}
			userid[23] = '\0';
			remove_control_chars(userid);
			for(j = 0; j < auth_num; j++) {
				if (auth_dat[j].account_id == account_id) {
					ShowError(CL_RED"mmmo_auth_init: an account has an identical id to another.\n");
					ShowError("               account id #%d -> new account not read (saved in log file)."CL_RED"\n", account_id);
					login_log("mmmo_auth_init: ******Error: an account has an identical id to another.\n");
					login_log("               account id #%d -> new account not read (saved in next line):\n", account_id);
					login_log("%s", line);
					break;
				} else if (strcmp(auth_dat[j].userid, userid) == 0) {
					ShowError(CL_RED"mmmo_auth_init: account name already exists.\n");
					ShowError("               account name '%s' -> new account not read (saved in log file)."CL_RESET"\n", userid); // 2 lines, account name can be long.
					login_log("mmmo_auth_init: ******Error: an account has an identical name to another.\n");
					login_log("               account name '%s' -> new account not read (saved in next line):\n", userid);
					login_log("%s", line);
					break;
				}
			}
			if (j != auth_num)
				continue;

			if (auth_num >= auth_max) {
				auth_max += 256;
				auth_dat = (struct auth_data*)aRealloc(auth_dat, sizeof(struct auth_data) * auth_max);
			}

			memset(&auth_dat[auth_num], '\0', sizeof(struct auth_data));

			auth_dat[auth_num].account_id = account_id;

			strncpy(auth_dat[auth_num].userid, userid, 24);

			pass[32] = '\0';
			remove_control_chars(pass);
			strncpy(auth_dat[auth_num].pass, pass, 32);

			lastlogin[23] = '\0';
			remove_control_chars(lastlogin);
			strncpy(auth_dat[auth_num].lastlogin, lastlogin, 24);

			auth_dat[auth_num].sex = (sex == 'S' || sex == 's') ? 2 : (sex == 'M' || sex == 'm');

			if (logincount >= 0)
				auth_dat[auth_num].logincount = logincount;
			else
				auth_dat[auth_num].logincount = 0;

			if (state > 255)
				auth_dat[auth_num].state = 100;
			else
				auth_dat[auth_num].state = state;

			if (e_mail_check(email) == 0) {
				ShowNotice("Account %s (%d): invalid e-mail (replaced par a@a.com).\n", auth_dat[auth_num].userid, auth_dat[auth_num].account_id);
				strncpy(auth_dat[auth_num].email, "a@a.com", 40);
			} else {
				remove_control_chars(email);
				strncpy(auth_dat[auth_num].email, email, 40);
			}

			error_message[19] = '\0';
			remove_control_chars(error_message);
			if (error_message[0] == '\0' || state != 7) { // 7, because state is packet 0x006a value + 1
				strncpy(auth_dat[auth_num].error_message, "-", 20);
			} else {
				strncpy(auth_dat[auth_num].error_message, error_message, 20);
			}

			if (i == 13)
				auth_dat[auth_num].ban_until_time = (time_t)ban_until_time;
			else
				auth_dat[auth_num].ban_until_time = 0;

			auth_dat[auth_num].connect_until_time = (time_t)connect_until_time;

			last_ip[15] = '\0';
			remove_control_chars(last_ip);
			strncpy(auth_dat[auth_num].last_ip, last_ip, 16);

			memo[254] = '\0';
			remove_control_chars(memo);
			strncpy(auth_dat[auth_num].memo, memo, 255);

			for(j = 0; j < ACCOUNT_REG2_NUM; j++) {
				p += n;
				if (sscanf(p, "%[^\t,],%[^\t ] %n", str, v, &n) != 2) { 
					// We must check if a str is void. If it's, we can continue to read other REG2.
					// Account line will have something like: str2,9 ,9 str3,1 (here, ,9 is not good)
					if (p[0] == ',' && sscanf(p, ",%[^\t ] %n", v, &n) == 1) { 
						j--;
						continue;
					} else
						break;
				}
				str[31] = '\0';
				remove_control_chars(str);
				strncpy(auth_dat[auth_num].account_reg2[j].str, str, 32);
				strncpy(auth_dat[auth_num].account_reg2[j].value,v,256);
			}
			auth_dat[auth_num].account_reg2_num = j;

			if (isGM(account_id) > 0)
				GM_count++;
			if (auth_dat[auth_num].sex == 2)
				server_count++;

			auth_num++;
			if (account_id >= account_id_count)
				account_id_count = account_id + 1;

		// Old athena database version reading (v1)
		} else if ((i = sscanf(line, "%u\t%[^\t]\t%[^\t]\t%[^\t]\t%c\t%d\t%u\t%n",
		           &account_id, userid, pass, lastlogin, &sex, &logincount, &state, &n)) >= 5) {
			if (account_id > END_ACCOUNT_NUM) {
				ShowError(CL_RED"mmmo_auth_init: an account has an id higher than %d\n", END_ACCOUNT_NUM);
				ShowError("               account id #%d -> account not read (saved in log file)."CL_RESET"\n", account_id);
				login_log("mmmo_auth_init: ******Error: an account has an id higher than %d.\n", END_ACCOUNT_NUM);
				login_log("               account id #%d -> account not read (saved in next line):\n", account_id);
				login_log("%s", line);
				continue;
			}
			userid[23] = '\0';
			remove_control_chars(userid);
			for(j = 0; j < auth_num; j++) {
				if (auth_dat[j].account_id == account_id) {
					ShowError(CL_RED"mmo_auth_init: an account has an identical id to another.\n");
					ShowError("               account id #%d -> new account not read (saved in log file)."CL_RESET"\n", account_id);
					login_log("mmmo_auth_init: ******Error: an account has an identical id to another.\n");
					login_log("               account id #%d -> new account not read (saved in next line):\n", account_id);
					login_log("%s", line);
					break;
				} else if (strcmp(auth_dat[j].userid, userid) == 0) {
					ShowError(CL_RED"mmo_auth_init: account name already exists.\n");
					ShowError("               account name '%s' -> new account not read (saved in log file)."CL_RESET"\n", userid);
					login_log("mmmo_auth_init: ******Error: an account has an identical id to another.\n");
					login_log("               account id #%d -> new account not read (saved in next line):\n", account_id);
					login_log("%s", line);
					break;
				}
			}
			if (j != auth_num)
				continue;

			if (auth_num >= auth_max) {
				auth_max += 256;
				auth_dat = (struct auth_data*)aRealloc(auth_dat, sizeof(struct auth_data) * auth_max);
			}

			memset(&auth_dat[auth_num], '\0', sizeof(struct auth_data));

			auth_dat[auth_num].account_id = account_id;

			strncpy(auth_dat[auth_num].userid, userid, 24);

			pass[23] = '\0';
			remove_control_chars(pass);
			strncpy(auth_dat[auth_num].pass, pass, 24);

			lastlogin[23] = '\0';
			remove_control_chars(lastlogin);
			strncpy(auth_dat[auth_num].lastlogin, lastlogin, 24);

			auth_dat[auth_num].sex = (sex == 'S' || sex == 's') ? 2 : (sex == 'M' || sex == 'm');

			if (i >= 6) {
				if (logincount >= 0)
					auth_dat[auth_num].logincount = logincount;
				else
					auth_dat[auth_num].logincount = 0;
			} else
				auth_dat[auth_num].logincount = 0;

			if (i >= 7) {
				if (state > 255)
					auth_dat[auth_num].state = 100;
				else
					auth_dat[auth_num].state = state;
			} else
				auth_dat[auth_num].state = 0;

			// Initialization of new data
			strncpy(auth_dat[auth_num].email, "a@a.com", 40);
			strncpy(auth_dat[auth_num].error_message, "-", 20);
			auth_dat[auth_num].ban_until_time = 0;
			auth_dat[auth_num].connect_until_time = 0;
			strncpy(auth_dat[auth_num].last_ip, "-", 16);
			strncpy(auth_dat[auth_num].memo, "-", 255);

			for(j = 0; j < ACCOUNT_REG2_NUM; j++) {
				p += n;
				if (sscanf(p, "%[^\t,],%[^\t ] %n", str, v, &n) != 2) { 
					// We must check if a str is void. If it's, we can continue to read other REG2.
					// Account line will have something like: str2,9 ,9 str3,1 (here, ,9 is not good)
					if (p[0] == ',' && sscanf(p, ",%[^\t ] %n", v, &n) == 1) { 
						j--;
						continue;
					} else
						break;
				}
				str[31] = '\0';
				remove_control_chars(str);
				strncpy(auth_dat[auth_num].account_reg2[j].str, str, 32);
				strncpy(auth_dat[auth_num].account_reg2[j].value,v,256);
			}
			auth_dat[auth_num].account_reg2_num = j;

			if (isGM(account_id) > 0)
				GM_count++;
			if (auth_dat[auth_num].sex == 2)
				server_count++;

			auth_num++;
			if (account_id >= account_id_count)
				account_id_count = account_id + 1;

		} else {
			int i = 0;
			if (sscanf(line, "%u\t%%newid%%\n%n", &account_id, &i) == 1 &&
			    i > 0 && account_id > account_id_count)
				account_id_count = account_id;
		}
	}
	fclose(fp);

	if (auth_num == 0) {
		ShowNotice("mmo_auth_init: No account found in %s.\n", account_filename);
		sprintf(line, "No account found in %s.", account_filename);
	} else {
		if (auth_num == 1) {
			ShowStatus("mmo_auth_init: 1 account read in %s,\n", account_filename);
			sprintf(line, "1 account read in %s,", account_filename);
		} else {
			ShowStatus("mmo_auth_init: %d accounts read in %s,\n", auth_num, account_filename);
			sprintf(line, "%u accounts read in %s,", auth_num, account_filename);
		}
		if (GM_count == 0) {
			ShowStatus("               of which is no GM account, and ");
			sprintf(str, "%s of which is no GM account and", line);
		} else if (GM_count == 1) {
			ShowStatus("               of which is 1 GM account, and ");
			sprintf(str, "%s of which is 1 GM account and", line);
		} else {
			ShowStatus("               of which is %d GM accounts, and ", GM_count);
			sprintf(str, "%s of which is %d GM accounts and", line, GM_count);
		}
		if (server_count == 0) {
			printf("no server account ('S').\n");
			sprintf(line, "%s no server account ('S').", str);
		} else if (server_count == 1) {
			printf("1 server account ('S').\n");
			sprintf(line, "%s 1 server account ('S').", str);
		} else {
			printf("%d server accounts ('S').\n", server_count);
			sprintf(line, "%s %d server accounts ('S').", str, server_count);
		}
	}
	login_log("%s\n", line);

	return 0;
}

//------------------------------------------
// Writing of the accounts database file
//   (accounts are sorted by id before save)
//------------------------------------------
void mmo_auth_sync(void)
{
	FILE *fp;
	unsigned int i, j, k;
	int lock;
	uint32 account_id;
	CREATE_BUFFER(id, int, auth_num);
	char line[65536];

	// Sorting before save
	for(i = 0; i < auth_num; i++) {
		id[i] = i;
		account_id = auth_dat[i].account_id;
		for(j = 0; j < i; j++) {
			if (account_id < auth_dat[id[j]].account_id) {
				for(k = i; k > j; k--)
					id[k] = id[k-1];
				id[j] = i; // id[i]
				break;
			}
		}
	}

	// Data save
	if ((fp = lock_fopen(account_filename, &lock)) == NULL) {
		//if (id) aFree(id); // aFree, right?
		DELETE_BUFFER(id);
		return;
	}

	fprintf(fp, "// Accounts file: here are saved all information about the accounts.\n");
	fprintf(fp, "// Structure: ID, account name, password, last login time, sex, # of logins, state, email, error message for state 7, validity time, last (accepted) login ip, memo field, ban timestamp, repeated(register text, register value)\n");
	fprintf(fp, "// Some explanations:\n");
	fprintf(fp, "//   account name    : between 4 to 23 char for a normal account (standard client can't send less than 4 char).\n");
	fprintf(fp, "//   account password: between 4 to 23 char\n");
	fprintf(fp, "//   sex             : M or F for normal accounts, S for server accounts\n");
	fprintf(fp, "//   state           : 0: account is ok, 1 to 256: error code of packet 0x006a + 1\n");
	fprintf(fp, "//   email           : between 3 to 39 char (a@a.com is like no email)\n");
	fprintf(fp, "//   error message   : text for the state 7: 'Your are Prohibited to login until <text>'. Max 19 char\n");
	fprintf(fp, "//   valitidy time   : 0: unlimited account, <other value>: date calculated by addition of 1/1/1970 + value (number of seconds since the 1/1/1970)\n");
	fprintf(fp, "//   memo field      : max 254 char\n");
	fprintf(fp, "//   ban time        : 0: no ban, <other value>: banned until the date: date calculated by addition of 1/1/1970 + value (number of seconds since the 1/1/1970)\n");
	for(i = 0; i < auth_num; i++) {
		k = id[i]; // use of sorted index
		if (auth_dat[k].account_id == (uint32)-1)
			continue;

		mmo_auth_tostr(line, &auth_dat[k]);
		fprintf(fp, "%s\n", line);
	}
	fprintf(fp, "%u\t%%newid%%\n", account_id_count);

	lock_fclose(fp, account_filename, &lock);

	// set new counter to minimum number of auth before save
	auth_before_save_file = auth_num / AUTH_SAVE_FILE_DIVIDER; // Re-initialise counter. We have save.
	if (auth_before_save_file < AUTH_BEFORE_SAVE_FILE)
		auth_before_save_file = AUTH_BEFORE_SAVE_FILE;

	//if (id) aFree(id);
	DELETE_BUFFER(id);

	return;
}

//-----------------------------------------------------
// Check if we must save accounts file or not
//   every minute, we check if we must save because we
//   have do some authentifications without arrive to
//   the minimum of authentifications for the save.
// Note: all other modification of accounts (deletion,
//       change of some informations excepted lastip/
//       lastlogintime, creation) are always save
//       immediatly and set  the minimum of
//       authentifications to its initialization value.
//-----------------------------------------------------
int check_auth_sync(int tid, unsigned int tick, int id, int data)
{
	// we only save if necessary:
	// we have do some authentifications without do saving
	if (auth_before_save_file < AUTH_BEFORE_SAVE_FILE ||
	    auth_before_save_file < (int)(auth_num / AUTH_SAVE_FILE_DIVIDER))
		mmo_auth_sync();

	return 0;
}

//--------------------------------------------------------------------
// Packet send to all char-servers, except one (wos: without our self)
//--------------------------------------------------------------------
int charif_sendallwos(int sfd, unsigned char *buf, unsigned int len)
{
	int i, c, fd;

	for(i = 0, c = 0; i < MAX_SERVERS; i++) {
		if ((fd = server_fd[i]) >= 0 && fd != sfd) {
			WFIFOHEAD(fd,len);
			memcpy(WFIFOP(fd,0), buf, len);
			WFIFOSET(fd,len);
			c++;
		}
	}

	return c;
}

//-----------------------------------------------------
// Send GM accounts to one or all char-servers
//-----------------------------------------------------
void send_GM_accounts(int fd)
{
	unsigned int i;
	uint8 buf[32767];
	uint16 len;

	len = 4;
	WBUFW(buf,0) = 0x2732;
	for(i = 0; i < GM_num; i++)
		// send only existing accounts. We can not create a GM account when server is online.
		if (gm_account_db[i].level > 0) {
			WBUFL(buf,len) = gm_account_db[i].account_id;
			WBUFB(buf,len+4) = (uint8)gm_account_db[i].level;
			len += 5;
			if (len >= 32000) {
				ShowWarning("send_GM_accounts: Too many accounts! Only %d out of %d were sent.\n", i, GM_num);
				break;
			}
		}

	WBUFW(buf,2) = len;
	if (fd == -1) // send to all charservers
		charif_sendallwos(-1, buf, len);
	else { // send only to target
		WFIFOHEAD(fd,len);
		memcpy(WFIFOP(fd,0), buf, len);
		WFIFOSET(fd,len);
	}

	return;
}

//-----------------------------------------------------
// Check if GM file account have been changed
//-----------------------------------------------------
int check_GM_file(int tid, unsigned int tick, int id, int data)
{
	struct stat file_stat;
	long new_time;

	// if we would not check
	if (gm_account_filename_check_timer < 1)
		return 0;

	// get last modify time/date
	if (stat(GM_account_filename, &file_stat))
		new_time = 0; // error
	else
		new_time = (long)file_stat.st_mtime;

	if (new_time != creation_time_GM_account_file) {
		read_gm_account();
		send_GM_accounts(-1);
	}

	return 0;
}


//-----------------------------------------------------
// encrypted/unencrypted password check
//-----------------------------------------------------
bool check_encrypted(const char* str1, const char* str2, const char* passwd)
{
	char md5str[64], md5bin[32];
	snprintf(md5str, sizeof(md5str), "%s%s", str1, str2);
	md5str[sizeof(md5str)-1] = '\0';
	MD5_String2binary(md5str, md5bin);

	return (0==memcmp(passwd, md5bin, 16));
}

bool check_password(struct login_session_data* ld, int passwdenc, const char* passwd, const char* refpass)
{	
	if(passwdenc == 0)
	{
		return (0==strcmp(passwd, refpass));
	}
	else if (ld)
	{
		// password mode set to 1 -> (md5key, refpass) enable with <passwordencrypt></passwordencrypt>
		// password mode set to 2 -> (refpass, md5key) enable with <passwordencrypt2></passwordencrypt2>
		
		return ((passwdenc&0x01) && check_encrypted(ld->md5key, refpass, passwd)) ||
		       ((passwdenc&0x02) && check_encrypted(refpass, ld->md5key, passwd));
	}
	return false;
}


//-------------------------------------
// Make new account
//-------------------------------------
int mmo_auth_new(struct mmo_account* account, char sex, char* email)
{
	time_t timestamp, timestamp_temp;
	struct tm *tmtime;
	int i = auth_num;

	if (auth_num >= auth_max) {
		auth_max += 256;
		auth_dat = (struct auth_data*)aRealloc(auth_dat, sizeof(struct auth_data) * auth_max);
	}

	memset(&auth_dat[i], '\0', sizeof(struct auth_data));

	while (isGM(account_id_count) > 0)
		account_id_count++;

	auth_dat[i].account_id = account_id_count++;
	safestrncpy(auth_dat[i].userid, account->userid, NAME_LENGTH);
	if( login_config.use_md5_passwds )
		MD5_String(account->passwd, auth_dat[i].pass);
	else
		safestrncpy(auth_dat[i].pass, account->passwd, NAME_LENGTH);
	safestrncpy(auth_dat[i].lastlogin, "-", sizeof(auth_dat[i].lastlogin));
	auth_dat[i].sex = (sex == 'M' || sex == 'm');
	auth_dat[i].logincount = 0;
	auth_dat[i].state = 0;
	safestrncpy(auth_dat[i].email, e_mail_check(email) ? email : "a@a.com", sizeof(auth_dat[i].email));
	safestrncpy(auth_dat[i].error_message, "-", sizeof(auth_dat[i].error_message));
	auth_dat[i].ban_until_time = 0;
	if (start_limited_time < 0)
		auth_dat[i].connect_until_time = 0; // unlimited
	else { // limited time
		timestamp = time(NULL) + start_limited_time;
		// double conversion to be sure that it is possible
		tmtime = localtime(&timestamp);
		timestamp_temp = mktime(tmtime);
		if (timestamp_temp != -1 && (timestamp_temp + 3600) >= timestamp) // check possible value and overflow (and avoid summer/winter hour)
			auth_dat[i].connect_until_time = timestamp_temp;
		else
			auth_dat[i].connect_until_time = 0; // unlimited
	}

	strncpy(auth_dat[i].last_ip, "-", 16);
	strncpy(auth_dat[i].memo, "-", 255);
	auth_dat[i].account_reg2_num = 0;

	auth_num++;

	return (account_id_count - 1);
}

//-----------------------------------------------------
// Check/authentication of a connection
//-----------------------------------------------------
int mmo_auth(struct mmo_account* account, int fd)
{
	unsigned int i;
	time_t raw_time;
	char tmpstr[256];
	int len;
	int newaccount = 0;
	char user_password[32+1]; // reserve for md5-ed pw

	char ip[16];
	uint8* sin_addr = (uint8*)&session[fd]->client_addr;
	sprintf(ip, "%u.%u.%u.%u", sin_addr[3], sin_addr[2], sin_addr[1], sin_addr[0]);

	// DNS Blacklist check
	if( login_config.use_dnsbl )
	{
		char r_ip[16];
		char ip_dnsbl[256];
		char* dnsbl_serv;
		bool matched = false;

		sprintf(r_ip, "%u.%u.%u.%u", sin_addr[0], sin_addr[1], sin_addr[2], sin_addr[3]);

		for( dnsbl_serv = strtok(login_config.dnsbl_servs,","); !matched && dnsbl_serv != NULL; dnsbl_serv = strtok(NULL,",") )
		{
			sprintf(ip_dnsbl, "%s.%s", r_ip, dnsbl_serv);
			if( host2ip(ip_dnsbl) )
				matched = true;
		}

		if( matched )
		{
			ShowInfo("DNSBL: (%s) Blacklisted. User Kicked.\n", r_ip);
			return 3;
		}
	}

	//Client Version check
	if( login_config.check_client_version && account->version != 0 &&
		account->version != login_config.client_version_to_connect )
		return 5;

	len = strnlen(account->userid, NAME_LENGTH);

	// Account creation with _M/_F
	if( login_config.new_account_flag )
	{
		if( len > 2 && strnlen(account->passwd, NAME_LENGTH) > 0 && // valid user and password lengths
			account->passwdenc == 0 && // unencoded password
			account->userid[len-2] == '_' && memchr("FfMm", (unsigned char)account->userid[len-1], 4) && // _M/_F suffix
			account_id_count <= END_ACCOUNT_NUM )
		{
			//only continue if amount in this time limit is allowed (account registration flood protection)[Kevin]
			if(DIFF_TICK(gettick(), new_reg_tick) < 0 && num_regs >= allowed_regs) {
				ShowNotice("Account registration denied (registration limit exceeded) to %s!\n", ip);
				login_log("Notice: Account registration denied (registration limit exceeded) to %s!", ip);
				return 3;
			}
			newaccount = 1;
			account->userid[len-2] = '\0';
		}
	}
	
	// Strict account search
	ARR_FIND( 0, auth_num, i, strcmp(account->userid, auth_dat[i].userid) == 0 );

	if( newaccount )
	{
		if( i != auth_num )
		{
			login_log("Attempt of creation of an already existant account (account: %s_%c, pass: %s, received pass: %s, ip: %s)\n", account->userid, account->userid[len-1], auth_dat[i].pass, account->passwd, ip);
			return 1; // 1 = Incorrect Password
		}
		else
		{
			int new_id = mmo_auth_new(account, account->userid[len-1], "a@a.com");
			unsigned int tick = gettick();
			login_log("Account creation (account %s (id: %d), pass: %s, sex: %c, connection with _F/_M, ip: %s)\n", account->userid, new_id, account->passwd, account->userid[len-1], ip);
			auth_before_save_file = 0; // Creation of an account -> save accounts file immediatly
			
			if( DIFF_TICK(tick, new_reg_tick) > 0 )
			{	//Update the registration check.
				num_regs = 0;
				new_reg_tick=tick +time_allowed*1000;
			}
			num_regs++;
		}
	}

	// if there is no creation request and strict account search fails, we do a no sensitive case research for index
	if( !newaccount && i == auth_num )
	{
		i = search_account_index(account->userid);
		if( i == -1 )
			i = auth_num;
		else
			memcpy(account->userid, auth_dat[i].userid, NAME_LENGTH); // for the possible tests/checks afterwards (copy correcte sensitive case).
	}

	if( i == auth_num )
	{
		login_log("Unknown account (account: %s, received pass: %s, ip: %s)\n", account->userid, account->passwd, ip);
		return 0; // 0 = Unregistered ID
	}

	if( login_config.use_md5_passwds )
		MD5_String(account->passwd, user_password);
	else
		safestrncpy(user_password, account->passwd, NAME_LENGTH);

	if( !check_password(session[fd]->session_data, account->passwdenc, user_password, auth_dat[i].pass) )
	{
		login_log("Invalid password (account: %s, pass: %s, received pass: %s, ip: %s)\n", account->userid, auth_dat[i].pass, (account->passwdenc) ? "[MD5]" : account->passwd, ip);
		return 1; // 1 = Incorrect Password
	}

	if( auth_dat[i].connect_until_time != 0 && auth_dat[i].connect_until_time < time(NULL) )
	{
		login_log("Connection refused (account: %s, pass: %s, expired ID, ip: %s)\n", account->userid, account->passwd, ip);
		return 2; // 2 = This ID is expired
	}

	if( auth_dat[i].ban_until_time != 0 && auth_dat[i].ban_until_time > time(NULL) )
	{
		strftime(tmpstr, 20, login_config.date_format, localtime(&auth_dat[i].ban_until_time));
		tmpstr[19] = '\0';
		login_log("Connection refused (account: %s, pass: %s, banned until %s, ip: %s)\n", account->userid, account->passwd, tmpstr, ip);
		return 6; // 6 = Your are Prohibited to log in until %s
	}

	if( auth_dat[i].state )
	{
		login_log("Connection refused (account: %s, pass: %s, state: %d, ip: %s)\n", account->userid, account->passwd, auth_dat[i].state, ip);
		return auth_dat[i].state - 1;
	}

	if( login_config.online_check )
	{
		struct online_login_data* data = idb_get(online_db,auth_dat[i].account_id);
		if( data && data->char_server > -1 )
		{
			//Request char servers to kick this account out. [Skotlex]
			unsigned char buf[8];
			ShowNotice("User [%d] is already online - Rejected.\n",auth_dat[i].account_id);
			WBUFW(buf,0) = 0x2734;
			WBUFL(buf,2) = auth_dat[i].account_id;
			charif_sendallwos(-1, buf, 6);
			if( data->waiting_disconnect == -1 )
				data->waiting_disconnect = add_timer(gettick()+30000, waiting_disconnect_timer, auth_dat[i].account_id, 0);
			return 3; // Rejected
		}
	}

	login_log("Authentification accepted (account: %s (id: %d), ip: %s)\n", account->userid, auth_dat[i].account_id, ip);

	// auth start : time seed
	time(&raw_time);
	strftime(tmpstr, 24, "%Y-%m-%d %H:%M:%S",localtime(&raw_time));

	account->account_id = auth_dat[i].account_id;
	account->login_id1 = rand();
	account->login_id2 = rand();
	safestrncpy(account->lastlogin, auth_dat[i].lastlogin, 24);
	account->sex = auth_dat[i].sex;

	if( account->sex != 2 && account->account_id < START_ACCOUNT_NUM )
		ShowWarning("Account %s has account id %d! Account IDs must be over %d to work properly!\n", account->userid, account->account_id, START_ACCOUNT_NUM);

	safestrncpy(auth_dat[i].lastlogin, tmpstr, sizeof(auth_dat[i].lastlogin));
	safestrncpy(auth_dat[i].last_ip, ip, sizeof(auth_dat[i].last_ip));
	auth_dat[i].ban_until_time = 0;
	auth_dat[i].logincount++;

	// Save until for change ip/time of auth is not very useful => limited save for that
	// Save there informations isnot necessary, because they are saved in log file.
	if (--auth_before_save_file <= 0) // Reduce counter. 0 or less, we save
		mmo_auth_sync();

	return -1; // account OK
}

static int online_db_setoffline(DBKey key, void* data, va_list ap)
{
	struct online_login_data* p = (struct online_login_data*)data;
	int server = va_arg(ap, int);
	if( server == -1 )
	{
		p->char_server = -1;
		if( p->waiting_disconnect != -1 )
		{
			delete_timer(p->waiting_disconnect, waiting_disconnect_timer);
			p->waiting_disconnect = -1;
		}
	}
	else if( p->char_server == server )
		p->char_server = -2; //Char server disconnected.
	return 0;
}

//--------------------------------
// Packet parsing for char-servers
//--------------------------------
int parse_fromchar(int fd)
{
	uint32 i;
	int j, id;

	uint32 ipl = session[fd]->client_addr;
	char ip[16];
	ip2str(ipl, ip);

	ARR_FIND( 0, MAX_SERVERS, id, server_fd[id] == fd );
	if (id == MAX_SERVERS)
	{// not a char server
		set_eof(fd);
		do_close(fd);
		return 0;
	}

	if( session[fd]->eof )
	{
		ShowStatus("Char-server '%s' has disconnected.\n", server[id].name);
		login_log("Char-server '%s' has disconnected (ip: %s).\n", server[id].name, ip);
		server_fd[id] = -1;
		memset(&server[id], 0, sizeof(struct mmo_char_server));
		online_db->foreach(online_db, online_db_setoffline, id); //Set all chars from this char server to offline.
		do_close(fd);
		return 0;
	}

	while( RFIFOREST(fd) >= 2 )
	{
		uint16 command = RFIFOW(fd,0);

		if (display_parse_fromchar == 2 || (display_parse_fromchar == 1 && command != 0x2714)) // 0x2714 is done very often (number of players)
			ShowDebug("parse_fromchar: connection #%d, packet: 0x%x (with being read: %d bytes).\n", fd, command, RFIFOREST(fd));

		switch( command )
		{
		
		case 0x2709: // request from map-server via char-server to reload GM accounts
			login_log("Char-server '%s': Request to re-load GM configuration file (ip: %s).\n", server[id].name, ip);
			read_gm_account();
			// send GM accounts to all char-servers
			send_GM_accounts(-1);
			RFIFOSKIP(fd,2);
		break;

		case 0x2712: // request from char-server to authenticate an account
			if( RFIFOREST(fd) < 19 )
				return 0;
		{
			uint32 account_id = RFIFOL(fd,2);
			for( i = 0; i < AUTH_FIFO_SIZE; ++i )
			{
				if( auth_fifo[i].account_id == RFIFOL(fd,2) &&
				    auth_fifo[i].login_id1  == RFIFOL(fd,6) &&
				    auth_fifo[i].login_id2  == RFIFOL(fd,10) &&
				    auth_fifo[i].sex        == RFIFOB(fd,14) &&
				    auth_fifo[i].ip         == ntohl(RFIFOL(fd,15)) &&
				    !auth_fifo[i].delflag)
				{
					auth_fifo[i].delflag = 1;
					break;
				}
			}

			if( i != AUTH_FIFO_SIZE && account_id > 0 )
			{// send ack 
				time_t connect_until_time;
				char email[40];
				unsigned int k;

				login_log("Char-server '%s': authentification of the account %d accepted (ip: %s).\n", server[id].name, account_id, ip);

				ARR_FIND( 0, auth_num, k, auth_dat[k].account_id == account_id );
				if( k != auth_num ) {
					strcpy(email, auth_dat[k].email);
					connect_until_time = auth_dat[k].connect_until_time;
				} else {
					memset(email, 0, sizeof(email));
					connect_until_time = 0;
				}

				WFIFOHEAD(fd,51);
				WFIFOW(fd,0) = 0x2713;
				WFIFOL(fd,2) = account_id;
				WFIFOB(fd,6) = 0;
				memcpy(WFIFOP(fd, 7), email, 40);
				WFIFOL(fd,47) = (unsigned long)connect_until_time;
				WFIFOSET(fd,51);
			}
			else
			{// authentification not found
				login_log("Char-server '%s': authentification of the account %d REFUSED (ip: %s).\n", server[id].name, account_id, ip);
				WFIFOHEAD(fd,51);
				WFIFOW(fd,0) = 0x2713;
				WFIFOL(fd,2) = account_id;
				WFIFOB(fd,6) = 1;
				// It is unnecessary to send email
				// It is unnecessary to send validity date of the account
				WFIFOSET(fd,51);
			}

			RFIFOSKIP(fd,19);
		}
		break;

		case 0x2714:
			if (RFIFOREST(fd) < 6)
				return 0;
			//printf("parse_fromchar: Receiving of the users number of the server '%s': %d\n", server[id].name, RFIFOL(fd,2));
			server[id].users = RFIFOL(fd,2);
			// send some answer
			WFIFOHEAD(fd,2);
			WFIFOW(fd,0) = 0x2718;
			WFIFOSET(fd,2);

			RFIFOSKIP(fd,6);
		break;

		case 0x2715: // request from char server to change e-email from default "a@a.com"
			if (RFIFOREST(fd) < 46)
				return 0;
		{
			char email[40];
			uint32 acc = RFIFOL(fd,2);
			memcpy(email, RFIFOP(fd,6), 40); email[39] = '\0';
			remove_control_chars(email);
			if (e_mail_check(email) == 0)
				login_log("Char-server '%s': Attempt to create an e-mail on an account with a default e-mail REFUSED - e-mail is invalid (account: %d, ip: %s)\n", server[id].name, acc, ip);
			else {
				for(i = 0; i < auth_num; i++) {
					if (auth_dat[i].account_id == acc && (strcmp(auth_dat[i].email, "a@a.com") == 0 || auth_dat[i].email[0] == '\0')) {
						memcpy(auth_dat[i].email, email, 40);
						login_log("Char-server '%s': Create an e-mail on an account with a default e-mail (account: %d, new e-mail: %s, ip: %s).\n", server[id].name, acc, email, ip);
						// Save
						mmo_auth_sync();
						break;
					}
				}
				if (i == auth_num)
					login_log("Char-server '%s': Attempt to create an e-mail on an account with a default e-mail REFUSED - account doesn't exist or e-mail of account isn't default e-mail (account: %d, ip: %s).\n", server[id].name, acc, ip);
			}

			RFIFOSKIP(fd,46);
		}
		break;

		case 0x2716: // received an e-mail/limited time request, because a player comes back from a map-server to the char-server
			if (RFIFOREST(fd) < 6)
				return 0;

			for(i = 0; i < auth_num; i++) {
				if (auth_dat[i].account_id == RFIFOL(fd,2)) {
					login_log("Char-server '%s': e-mail of the account %d found (ip: %s).\n",
					          server[id].name, RFIFOL(fd,2), ip);
					WFIFOW(fd,0) = 0x2717;
					WFIFOL(fd,2) = RFIFOL(fd,2);
					memcpy(WFIFOP(fd, 6), auth_dat[i].email, 40);
					WFIFOL(fd,46) = (unsigned long)auth_dat[i].connect_until_time;
					WFIFOSET(fd,50);
					break;
				}
			}
			if (i == auth_num)
				login_log("Char-server '%s': e-mail of the account %d NOT found (ip: %s).\n",
				          server[id].name, RFIFOL(fd,2), ip);

			RFIFOSKIP(fd,6);
		break;

		case 0x2720: // Request to become a GM
			if (RFIFOREST(fd) < 4 || RFIFOREST(fd) < RFIFOW(fd,2))
				return 0;
		{
			unsigned char buf[10];
			FILE *fp;
			int acc = RFIFOL(fd,4);
			//printf("parse_fromchar: Request to become a GM acount from %d account.\n", acc);
			WBUFW(buf,0) = 0x2721;
			WBUFL(buf,2) = acc;
			WBUFL(buf,6) = 0;
			if (strcmp((char*)RFIFOP(fd,8), gm_pass) == 0) {
				// only non-GM can become GM
				if (isGM(acc) == 0) {
					// if we autorise creation
					if (level_new_gm > 0) {
						// if we can open the file to add the new GM
						if ((fp = fopen(GM_account_filename, "a")) != NULL) {
							char tmpstr[24];
							time_t raw_time;
							time(&raw_time);
							strftime(tmpstr, 23, login_config.date_format, localtime(&raw_time));
							fprintf(fp, "\n// %s: @GM command on account %d\n%d %d\n", tmpstr, acc, acc, level_new_gm);
							fclose(fp);
							WBUFL(buf,6) = level_new_gm;
							read_gm_account();
							send_GM_accounts(-1);
							ShowNotice("GM Change of the account %d: level 0 -> %d.\n", acc, level_new_gm);
							login_log("Char-server '%s': GM Change of the account %d: level 0 -> %d (ip: %s).\n",
							          server[id].name, acc, level_new_gm, ip);
						} else {
							ShowError("Error of GM change (suggested account: %d, correct password, unable to add a GM account in GM accounts file)\n", acc);
							login_log("Char-server '%s': Error of GM change (suggested account: %d, correct password, unable to add a GM account in GM accounts file, ip: %s).\n",
							          server[id].name, acc, ip);
						}
					} else {
						ShowError("Error of GM change (suggested account: %d, correct password, but GM creation is disable (level_new_gm = 0))\n", acc);
						login_log("Char-server '%s': Error of GM change (suggested account: %d, correct password, but GM creation is disable (level_new_gm = 0), ip: %s).\n",
						          server[id].name, acc, ip);
					}
				} else {
					ShowError("Error of GM change (suggested account: %d (already GM), correct password).\n", acc);
					login_log("Char-server '%s': Error of GM change (suggested account: %d (already GM), correct password, ip: %s).\n",
					          server[id].name, acc, ip);
				}
			} else {
				ShowError("Error of GM change (suggested account: %d, invalid password).\n", acc);
				login_log("Char-server '%s': Error of GM change (suggested account: %d, invalid password, ip: %s).\n",
				          server[id].name, acc, ip);
			}
			charif_sendallwos(-1, buf, 10);

			RFIFOSKIP(fd, RFIFOW(fd,2));
			return 0;
		}

		// Map server send information to change an email of an account via char-server
		case 0x2722:	// 0x2722 <account_id>.L <actual_e-mail>.40B <new_e-mail>.40B
			if (RFIFOREST(fd) < 86)
				return 0;
		{
			char actual_email[40], new_email[40];
			uint32 acc = RFIFOL(fd,2);
			memcpy(actual_email, RFIFOP(fd,6), 40); actual_email[39] = '\0'; remove_control_chars(actual_email);
			memcpy(new_email, RFIFOP(fd,46), 40); new_email[39] = '\0'; remove_control_chars(new_email);
			if (e_mail_check(actual_email) == 0)
				login_log("Char-server '%s': Attempt to modify an e-mail on an account (@email GM command), but actual email is invalid (account: %d, ip: %s)\n",
				          server[id].name, acc, ip);
			else if (e_mail_check(new_email) == 0)
				login_log("Char-server '%s': Attempt to modify an e-mail on an account (@email GM command) with a invalid new e-mail (account: %d, ip: %s)\n",
				          server[id].name, acc, ip);
			else if (strcmpi(new_email, "a@a.com") == 0)
				login_log("Char-server '%s': Attempt to modify an e-mail on an account (@email GM command) with a default e-mail (account: %d, ip: %s)\n",
				          server[id].name, acc, ip);
			else {
				for(i = 0; i < auth_num; i++) {
					if (auth_dat[i].account_id == acc) {
						if (strcmpi(auth_dat[i].email, actual_email) == 0) {
							memcpy(auth_dat[i].email, new_email, 40);
							login_log("Char-server '%s': Modify an e-mail on an account (@email GM command) (account: %d (%s), new e-mail: %s, ip: %s).\n",
							          server[id].name, acc, auth_dat[i].userid, new_email, ip);
							// Save
							mmo_auth_sync();
						} else
							login_log("Char-server '%s': Attempt to modify an e-mail on an account (@email GM command), but actual e-mail is incorrect (account: %d (%s), actual e-mail: %s, proposed e-mail: %s, ip: %s).\n",
							          server[id].name, acc, auth_dat[i].userid, auth_dat[i].email, actual_email, ip);
						break;
					}
				}
				if (i == auth_num)
					login_log("Char-server '%s': Attempt to modify an e-mail on an account (@email GM command), but account doesn't exist (account: %d, ip: %s).\n",
					          server[id].name, acc, ip);
			}

			RFIFOSKIP(fd, 86);
		}
		break;

		case 0x2724: // Receiving of map-server via char-server a status change request
			if (RFIFOREST(fd) < 10)
				return 0;
		{
			uint32 acc, statut;
			acc = RFIFOL(fd,2);
			statut = RFIFOL(fd,6);
			for(i = 0; i < auth_num && auth_dat[i].account_id != acc; i++);

			if (i == auth_num) {
				login_log("Char-server '%s': Error of Status change (account: %d not found, suggested status %d, ip: %s).\n",
				          server[id].name, acc, statut, ip);
			} else {
				if (auth_dat[i].state != statut) {
					login_log("Char-server '%s': Status change (account: %d, new status %d, ip: %s).\n",
					          server[id].name, acc, statut, ip);
					if (statut != 0) {
						unsigned char buf[16];
						WBUFW(buf,0) = 0x2731;
						WBUFL(buf,2) = acc;
						WBUFB(buf,6) = 0; // 0: change of statut, 1: ban
						WBUFL(buf,7) = statut; // status or final date of a banishment
						charif_sendallwos(-1, buf, 11);
						for(j = 0; j < AUTH_FIFO_SIZE; j++)
							if (auth_fifo[j].account_id == acc)
								auth_fifo[j].login_id1++; // to avoid reconnection error when come back from map-server (char-server will ask again the authentification)
					}
					auth_dat[i].state = statut;
					// Save
					mmo_auth_sync();
				} else
					login_log("Char-server '%s':  Error of Status change - actual status is already the good status (account: %d, status %d, ip: %s).\n",
					          server[id].name, acc, statut, ip);
			}

			RFIFOSKIP(fd,10);
			return 0;
		}

		case 0x2725: // Receiving of map-server via char-server a ban request
			if (RFIFOREST(fd) < 18)
				return 0;
		{
			uint32 acc = RFIFOL(fd,2);
			for(i = 0; i < auth_num; i++) {
				if (auth_dat[i].account_id == acc) {
					time_t timestamp;
					struct tm *tmtime;
					if (auth_dat[i].ban_until_time == 0 || auth_dat[i].ban_until_time < time(NULL))
						timestamp = time(NULL);
					else
						timestamp = auth_dat[i].ban_until_time;
					tmtime = localtime(&timestamp);
					tmtime->tm_year = tmtime->tm_year + (short)RFIFOW(fd,6);
					tmtime->tm_mon = tmtime->tm_mon + (short)RFIFOW(fd,8);
					tmtime->tm_mday = tmtime->tm_mday + (short)RFIFOW(fd,10);
					tmtime->tm_hour = tmtime->tm_hour + (short)RFIFOW(fd,12);
					tmtime->tm_min = tmtime->tm_min + (short)RFIFOW(fd,14);
					tmtime->tm_sec = tmtime->tm_sec + (short)RFIFOW(fd,16);
					timestamp = mktime(tmtime);
					if (timestamp != -1) {
						if (timestamp <= time(NULL))
							timestamp = 0;
						if (auth_dat[i].ban_until_time != timestamp) {
							if (timestamp != 0) {
								unsigned char buf[16];
								char tmpstr[2048];
								strftime(tmpstr, 24, login_config.date_format, localtime(&timestamp));
								login_log("Char-server '%s': Ban request (account: %d, new final date of banishment: %d (%s), ip: %s).\n",
								          server[id].name, acc, timestamp, (timestamp == 0 ? "no banishment" : tmpstr), ip);
								WBUFW(buf,0) = 0x2731;
								WBUFL(buf,2) = auth_dat[i].account_id;
								WBUFB(buf,6) = 1; // 0: change of statut, 1: ban
								WBUFL(buf,7) = (unsigned int)timestamp; // status or final date of a banishment
								charif_sendallwos(-1, buf, 11);
								for(j = 0; j < AUTH_FIFO_SIZE; j++)
									if (auth_fifo[j].account_id == acc)
										auth_fifo[j].login_id1++; // to avoid reconnection error when come back from map-server (char-server will ask again the authentification)
							} else {
								login_log("Char-server '%s': Error of ban request (account: %d, new date unbans the account, ip: %s).\n",
								          server[id].name, acc, ip);
							}
							auth_dat[i].ban_until_time = timestamp;
							// Save
							mmo_auth_sync();
						} else {
							login_log("Char-server '%s': Error of ban request (account: %d, no change for ban date, ip: %s).\n",
							          server[id].name, acc, ip);
						}
					} else {
						login_log("Char-server '%s': Error of ban request (account: %d, invalid date, ip: %s).\n",
						          server[id].name, acc, ip);
					}
					break;
				}
			}
			if (i == auth_num)
				login_log("Char-server '%s': Error of ban request (account: %d not found, ip: %s).\n",
				          server[id].name, acc, ip);

			RFIFOSKIP(fd,18);
			return 0;
		}

		case 0x2727: // Change of sex (sex is reversed)
			if (RFIFOREST(fd) < 6)
				return 0;
		{
			uint8 sex;
			uint32 acc = RFIFOL(fd,2);
			for(i = 0; i < auth_num; i++) {
				if (auth_dat[i].account_id == acc) {
					if (auth_dat[i].sex == 2)
						login_log("Char-server '%s': Error of sex change - Server account (suggested account: %d, actual sex %d (Server), ip: %s).\n",
						          server[id].name, acc, auth_dat[i].sex, ip);
					else {
						unsigned char buf[16];
						if (auth_dat[i].sex == 0)
							sex = 1;
						else
							sex = 0;
						login_log("Char-server '%s': Sex change (account: %d, new sex %c, ip: %s).\n",
						          server[id].name, acc, (sex == 2) ? 'S' : (sex == 1 ? 'M' : 'F'), ip);
						for(j = 0; j < AUTH_FIFO_SIZE; j++)
							if (auth_fifo[j].account_id == acc)
								auth_fifo[j].login_id1++; // to avoid reconnection error when come back from map-server (char-server will ask again the authentification)
						auth_dat[i].sex = sex;
						WBUFW(buf,0) = 0x2723;
						WBUFL(buf,2) = acc;
						WBUFB(buf,6) = sex;
						charif_sendallwos(-1, buf, 7);
						// Save
						mmo_auth_sync();
					}
					break;
				}
			}
			if (i == auth_num)
				login_log("Char-server '%s': Error of sex change (account: %d not found, sex would be reversed, ip: %s).\n",
				          server[id].name, acc, ip);

			RFIFOSKIP(fd,6);
			return 0;
		}

		case 0x2728:	// We receive account_reg2 from a char-server, and we send them to other map-servers.
			if (RFIFOREST(fd) < 4 || RFIFOREST(fd) < RFIFOW(fd,2))
				return 0;
			{
				int p;
				uint32 acc = RFIFOL(fd,4);
				for(i = 0; i < auth_num; i++) {
					if (auth_dat[i].account_id == acc) {
						//unsigned char buf[rfifow(fd,2)+1];
						unsigned char *buf;
						int len;
						buf = (unsigned char*)aCalloc(RFIFOW(fd,2)+1, sizeof(unsigned char));
						login_log("char-server '%s': receiving (from the char-server) of account_reg2 (account: %d, ip: %s).\n",
						          server[id].name, acc, ip);
						for(j=0,p=13;j<ACCOUNT_REG2_NUM && p<RFIFOW(fd,2);j++){
							sscanf((char*)RFIFOP(fd,p), "%31c%n",auth_dat[i].account_reg2[j].str,&len);
							auth_dat[i].account_reg2[j].str[len]='\0';
							p +=len+1; //+1 to skip the '\0' between strings.
							sscanf((char*)RFIFOP(fd,p), "%255c%n",auth_dat[i].account_reg2[j].value,&len);
							auth_dat[i].account_reg2[j].value[len]='\0';
							p +=len+1;
							remove_control_chars(auth_dat[i].account_reg2[j].str);
							remove_control_chars(auth_dat[i].account_reg2[j].value);
						}
						auth_dat[i].account_reg2_num = j;
						// Sending information towards the other char-servers.
						memcpy(WBUFP(buf,0), RFIFOP(fd,0), RFIFOW(fd,2));
						WBUFW(buf,0) = 0x2729;
						charif_sendallwos(fd, buf, WBUFW(buf,2));
						// Save
						mmo_auth_sync();
//						printf("parse_fromchar: receiving (from the char-server) of account_reg2 (account id: %d).\n", acc);
						if (buf) aFree(buf);
						break;
					}
				}
				if (i == auth_num) {
//					printf("parse_fromchar: receiving (from the char-server) of account_reg2 (unknwon account id: %d).\n", acc);
					login_log("Char-server '%s': receiving (from the char-server) of account_reg2 (account: %d not found, ip: %s).\n",
					          server[id].name, acc, ip);
				}
			}

			RFIFOSKIP(fd,RFIFOW(fd,2));
		break;

		case 0x272a:	// Receiving of map-server via char-server an unban request
			if (RFIFOREST(fd) < 6)
				return 0;
		{
			uint32 acc = RFIFOL(fd,2);
			for(i = 0; i < auth_num; i++) {
				if (auth_dat[i].account_id == acc) {
					if (auth_dat[i].ban_until_time != 0) {
						auth_dat[i].ban_until_time = 0;
						login_log("Char-server '%s': UnBan request (account: %d, ip: %s).\n",
						          server[id].name, acc, ip);
					} else {
						login_log("Char-server '%s': Error of UnBan request (account: %d, no change for unban date, ip: %s).\n",
						          server[id].name, acc, ip);
					}
					break;
				}
			}
			if (i == auth_num)
				login_log("Char-server '%s': Error of UnBan request (account: %d not found, ip: %s).\n",
				          server[id].name, acc, ip);

			RFIFOSKIP(fd,6);
			return 0;
		}

		case 0x272b:    // Set account_id to online [Wizputer]
			if (RFIFOREST(fd) < 6)
				return 0;
			add_online_user(id, RFIFOL(fd,2));
			RFIFOSKIP(fd,6);
		break;
		
		case 0x272c:   // Set account_id to offline [Wizputer]
			if (RFIFOREST(fd) < 6)
				return 0;
			remove_online_user(RFIFOL(fd,2));
			RFIFOSKIP(fd,6);
		break;

		case 0x272d:	// Receive list of all online accounts. [Skotlex]
			if (RFIFOREST(fd) < 4 || RFIFOREST(fd) < RFIFOW(fd,2))
				return 0;
			if( login_config.online_check )
			{
				struct online_login_data *p;
				int aid;
			  	uint32 i, users;
				online_db->foreach(online_db, online_db_setoffline, id); //Set all chars from this char-server offline first
				users = RFIFOW(fd,4);
				for (i = 0; i < users; i++) {
					aid = RFIFOL(fd,6+i*4);
					p = idb_ensure(online_db, aid, create_online_user);
					p->char_server = id;
					if (p->waiting_disconnect != -1)
					{
						delete_timer(p->waiting_disconnect, waiting_disconnect_timer);
						p->waiting_disconnect = -1;
					}
				}
			}
			RFIFOSKIP(fd,RFIFOW(fd,2));
		break;

		case 0x272e: //Request account_reg2 for a character.
			if (RFIFOREST(fd) < 10)
				return 0;
		{
			uint32 account_id = RFIFOL(fd, 2);
			uint32 char_id = RFIFOL(fd, 6);
			int p;
			WFIFOW(fd,0) = 0x2729;
			WFIFOL(fd,4) = account_id;
			WFIFOL(fd,8) = char_id;
			WFIFOB(fd,12) = 1; //Type 1 for Account2 registry
			for(i = 0; i < auth_num && auth_dat[i].account_id != account_id; i++);
			if (i == auth_num) {
				//Account not found? Send at least empty data, map servers need a reply!
				WFIFOW(fd,2) = 13;
				WFIFOSET(fd,WFIFOW(fd,2));
				break;
			}
			for(p = 13, j = 0; j < auth_dat[i].account_reg2_num; j++) {
				if (auth_dat[i].account_reg2[j].str[0]) {
					p+= sprintf((char*)WFIFOP(fd,p), "%s", auth_dat[i].account_reg2[j].str)+1; //We add 1 to consider the '\0' in place.
					p+= sprintf((char*)WFIFOP(fd,p), "%s", auth_dat[i].account_reg2[j].value)+1;
				}
			}
			WFIFOW(fd,2) = (uint16) p;
			WFIFOSET(fd,WFIFOW(fd,2));

			RFIFOSKIP(fd,10);
		}
		break;

		case 0x2736: // WAN IP update from char-server
			if (RFIFOREST(fd) < 6)
				return 0;
			server[id].ip = ntohl(RFIFOL(fd,2));
			ShowInfo("Updated IP of Server #%d to %d.%d.%d.%d.\n",id, CONVIP(server[id].ip));
			RFIFOSKIP(fd,6);
		break;

		case 0x2737: //Request to set all offline.
			ShowInfo("Setting accounts from char-server %d offline.\n", id);
			online_db->foreach(online_db, online_db_setoffline, id);
			RFIFOSKIP(fd,2);
		break;

		default:
		{
			FILE* logfp;
			char tmpstr[24];
			time_t raw_time;
			logfp = fopen(login_log_unknown_packets_filename, "a");
			if (logfp) {
				time(&raw_time);
				strftime(tmpstr, 23, login_config.date_format, localtime(&raw_time));
				fprintf(logfp, "%s: receiving of an unknown packet -> disconnection\n", tmpstr);
				fprintf(logfp, "parse_fromchar: connection #%d (ip: %s), packet: 0x%x (with being read: %lu).\n", fd, ip, command, (unsigned long)RFIFOREST(fd));
				fprintf(logfp, "Detail (in hex):\n");
				fprintf(logfp, "---- 00-01-02-03-04-05-06-07  08-09-0A-0B-0C-0D-0E-0F\n");
				memset(tmpstr, '\0', sizeof(tmpstr));
				for(i = 0; i < RFIFOREST(fd); i++) {
					if ((i & 15) == 0)
						fprintf(logfp, "%04X ",i);
					fprintf(logfp, "%02x ", RFIFOB(fd,i));
					if (RFIFOB(fd,i) > 0x1f)
						tmpstr[i % 16] = RFIFOB(fd,i);
					else
						tmpstr[i % 16] = '.';
					if ((i - 7) % 16 == 0) // -8 + 1
						fprintf(logfp, " ");
					else if ((i + 1) % 16 == 0) {
						fprintf(logfp, " %s\n", tmpstr);
						memset(tmpstr, '\0', sizeof(tmpstr));
					}
				}
				if (i % 16 != 0) {
					for(j = i; j % 16 != 0; j++) {
						fprintf(logfp, "   ");
						if ((j - 7) % 16 == 0) // -8 + 1
							fprintf(logfp, " ");
					}
					fprintf(logfp, " %s\n", tmpstr);
				}
				fprintf(logfp, "\n");
				fclose(logfp);
			}

			ShowError("parse_fromchar: Unknown packet 0x%x from a char-server! Disconnecting!\n", command);
			set_eof(fd);
			return 0;
		}
		} // switch
	} // while

	RFIFOSKIP(fd,RFIFOREST(fd));
	return 0;
}

//---------------------------------------
// Packet parsing for administation login
//---------------------------------------
int parse_admin(int fd)
{
	unsigned int i, j;
	char* account_name;

	uint32 ipl = session[fd]->client_addr;
	char ip[16];
	ip2str(ipl, ip);

	if( session[fd]->eof )
	{
		do_close(fd);
		ShowInfo("Remote administration has disconnected (session #%d).\n", fd);
		return 0;
	}

	while( RFIFOREST(fd) >= 2 )
	{
		uint16 command = RFIFOW(fd,0);

        if (display_parse_admin)
			ShowDebug("parse_admin: connection #%d, packet: 0x%x (with being read: %d).\n", fd, command, RFIFOREST(fd));

		switch( command )
		{
		
		case 0x7530:	// Request of the server version
			login_log("'ladmin': Sending of the server version (ip: %s)\n", ip);
			WFIFOHEAD(fd,10);
			WFIFOW(fd,0) = 0x7531;
			WFIFOB(fd,2) = ATHENA_MAJOR_VERSION;
			WFIFOB(fd,3) = ATHENA_MINOR_VERSION;
			WFIFOB(fd,4) = ATHENA_REVISION;
			WFIFOB(fd,5) = ATHENA_RELEASE_FLAG;
			WFIFOB(fd,6) = ATHENA_OFFICIAL_FLAG;
			WFIFOB(fd,7) = ATHENA_SERVER_LOGIN;
			WFIFOW(fd,8) = ATHENA_MOD_VERSION;
			WFIFOSET(fd,10);
			RFIFOSKIP(fd,2);
			break;

		case 0x7532:	// Request of end of connection
			login_log("'ladmin': End of connection (ip: %s)\n", ip);
			RFIFOSKIP(fd,2);
			set_eof(fd);
			break;

		case 0x7920:	// Request of an accounts list
			if (RFIFOREST(fd) < 10)
				return 0;
			{
				int st, ed;
				uint16 len;
				CREATE_BUFFER(id, int, auth_num);
				st = RFIFOL(fd,2);
				ed = RFIFOL(fd,6);
				RFIFOSKIP(fd,10);
				WFIFOW(fd,0) = 0x7921;
				if (st < 0)
					st = 0;
				if (ed > END_ACCOUNT_NUM || ed < st || ed <= 0)
					ed = END_ACCOUNT_NUM;
				login_log("'ladmin': Sending an accounts list (ask: from %d to %d, ip: %s)\n", st, ed, ip);
				// Sort before send
				for(i = 0; i < auth_num; i++) {
					unsigned int k;
					id[i] = i;
					for(j = 0; j < i; j++) {
						if (auth_dat[id[i]].account_id < auth_dat[id[j]].account_id) {
							for(k = i; k > j; k--) {
								id[k] = id[k-1];
							}
							id[j] = i; // id[i]
							break;
						}
					}
				}
				// Sending accounts information
				len = 4;
				for(i = 0; i < auth_num && len < 30000; i++) {
					int account_id = auth_dat[id[i]].account_id; // use sorted index
					if (account_id >= st && account_id <= ed) {
						j = id[i];
						WFIFOL(fd,len) = account_id;
						WFIFOB(fd,len+4) = (unsigned char)isGM(account_id);
						memcpy(WFIFOP(fd,len+5), auth_dat[j].userid, 24);
						WFIFOB(fd,len+29) = auth_dat[j].sex;
						WFIFOL(fd,len+30) = auth_dat[j].logincount;
						if (auth_dat[j].state == 0 && auth_dat[j].ban_until_time != 0) // if no state and banished
							WFIFOL(fd,len+34) = 7; // 6 = Your are Prohibited to log in until %s
						else
							WFIFOL(fd,len+34) = auth_dat[j].state;
						len += 38;
					}
				}
				WFIFOW(fd,2) = len;
				WFIFOSET(fd,len);
				//if (id) free(id);
				DELETE_BUFFER(id);
			}
			break;

		case 0x7930:	// Request for an account creation
			if (RFIFOREST(fd) < 91)
				return 0;
			{
				struct mmo_account ma;
				memcpy(ma.userid,RFIFOP(fd, 2),NAME_LENGTH);
				ma.userid[23] = '\0';
				memcpy(ma.passwd, RFIFOP(fd, 26), NAME_LENGTH);
				ma.passwd[23] = '\0';
				memcpy(ma.lastlogin, "-", 2);
				ma.sex = RFIFOB(fd,50);
				WFIFOW(fd,0) = 0x7931;
				WFIFOL(fd,2) = 0xffffffff;
				memcpy(WFIFOP(fd,6), RFIFOP(fd,2), 24);
				if (strlen(ma.userid) < 4 || strlen(ma.passwd) < 4) {
					login_log("'ladmin': Attempt to create an invalid account (account or pass is too short, ip: %s)\n",
					          ip);
				} else if (ma.sex != 'F' && ma.sex != 'M') {
					login_log("'ladmin': Attempt to create an invalid account (account: %s, received pass: %s, invalid sex, ip: %s)\n",
					          ma.userid, ma.passwd, ip);
				} else if (account_id_count > END_ACCOUNT_NUM) {
					login_log("'ladmin': Attempt to create an account, but there is no more available id number (account: %s, pass: %s, sex: %c, ip: %s)\n",
					          ma.userid, ma.passwd, ma.sex, ip);
				} else {
					remove_control_chars(ma.userid);
					remove_control_chars(ma.passwd);
					for(i = 0; i < auth_num; i++) {
						if (strncmp(auth_dat[i].userid, ma.userid, 24) == 0) {
							login_log("'ladmin': Attempt to create an already existing account (account: %s, pass: %s, received pass: %s, ip: %s)\n",
							          auth_dat[i].userid, auth_dat[i].pass, ma.passwd, ip);
							break;
						}
					}
					if (i == auth_num) {
						int new_id;
						char email[40];
						memcpy(email, RFIFOP(fd,51), 40);
						email[39] = '\0';
						remove_control_chars(email);
						new_id = mmo_auth_new(&ma, ma.sex, email);
						login_log("'ladmin': Account creation (account: %s (id: %d), pass: %s, sex: %c, email: %s, ip: %s)\n",
						          ma.userid, new_id, ma.passwd, ma.sex, auth_dat[i].email, ip);
						WFIFOL(fd,2) = new_id;
						mmo_auth_sync();
					}
				}
				WFIFOSET(fd,30);
				RFIFOSKIP(fd,91);
			}
			break;

		case 0x7932:	// Request for an account deletion
			if (RFIFOREST(fd) < 26)
				return 0;
			WFIFOW(fd,0) = 0x7933;
			WFIFOL(fd,2) = 0xFFFFFFFF;
			account_name = (char*)RFIFOP(fd,2);
			account_name[23] = '\0';
			remove_control_chars(account_name);
			i = search_account_index(account_name);
			if (i != -1) {
				// Char-server is notified of deletion (for characters deletion).
				unsigned char buf[65535];
				WBUFW(buf,0) = 0x2730;
				WBUFL(buf,2) = auth_dat[i].account_id;
				charif_sendallwos(-1, buf, 6);
				// send answer
				memcpy(WFIFOP(fd,6), auth_dat[i].userid, 24);
				WFIFOL(fd,2) = auth_dat[i].account_id;
				// save deleted account in log file
				login_log("'ladmin': Account deletion (account: %s, id: %d, ip: %s) - saved in next line:\n",
				          auth_dat[i].userid, auth_dat[i].account_id, ip);
				mmo_auth_tostr((char*)buf, &auth_dat[i]);
				login_log("%s\n", buf);
				// delete account
				memset(auth_dat[i].userid, '\0', sizeof(auth_dat[i].userid));
				auth_dat[i].account_id = (uint32)-1;
				mmo_auth_sync();
			} else {
				memcpy(WFIFOP(fd,6), account_name, 24);
				login_log("'ladmin': Attempt to delete an unknown account (account: %s, ip: %s)\n",
				          account_name, ip);
			}
			WFIFOSET(fd,30);
			RFIFOSKIP(fd,26);
			break;

		case 0x7934:	// Request to change a password
			if (RFIFOREST(fd) < 50)
				return 0;
			WFIFOW(fd,0) = 0x7935;
			WFIFOL(fd,2) = 0xFFFFFFFF; /// WTF??? an unsigned being set to a -1
			account_name = (char*)RFIFOP(fd,2);
			account_name[23] = '\0';
			remove_control_chars(account_name);
			i = search_account_index(account_name);
			if (i != -1) {
				memcpy(WFIFOP(fd,6), auth_dat[i].userid, 24);
				memcpy(auth_dat[i].pass, RFIFOP(fd,26), 24);
				auth_dat[i].pass[23] = '\0';
				remove_control_chars(auth_dat[i].pass);
				WFIFOL(fd,2) = auth_dat[i].account_id;
				login_log("'ladmin': Modification of a password (account: %s, new password: %s, ip: %s)\n",
				          auth_dat[i].userid, auth_dat[i].pass, ip);
				mmo_auth_sync();
			} else {
				memcpy(WFIFOP(fd,6), account_name, 24);
				login_log("'ladmin': Attempt to modify the password of an unknown account (account: %s, ip: %s)\n",
				          account_name, ip);
			}
			WFIFOSET(fd,30);
			RFIFOSKIP(fd,50);
			break;

		case 0x7936:	// Request to modify a state
			if (RFIFOREST(fd) < 50)
				return 0;
			{
				char error_message[20];
				uint32 statut;
				WFIFOW(fd,0) = 0x7937;
				WFIFOL(fd,2) = 0xFFFFFFFF; // WTF???
				account_name = (char*)RFIFOP(fd,2);
				account_name[23] = '\0';
				remove_control_chars(account_name);
				statut = RFIFOL(fd,26);
				memcpy(error_message, RFIFOP(fd,30), 20);
				error_message[19] = '\0';
				remove_control_chars(error_message);
				if (statut != 7 || error_message[0] == '\0') { // 7: // 6 = Your are Prohibited to log in until %s
					strcpy(error_message, "-");
				}
				i = search_account_index(account_name);
				if (i != -1) {
					memcpy(WFIFOP(fd,6), auth_dat[i].userid, 24);
					WFIFOL(fd,2) = auth_dat[i].account_id;
					if (auth_dat[i].state == statut && strcmp(auth_dat[i].error_message, error_message) == 0)
						login_log("'ladmin': Modification of a state, but the state of the account is already the good state (account: %s, received state: %d, ip: %s)\n",
						          account_name, statut, ip);
					else {
						if (statut == 7)
							login_log("'ladmin': Modification of a state (account: %s, new state: %d - prohibited to login until '%s', ip: %s)\n",
							          auth_dat[i].userid, statut, error_message, ip);
						else
							login_log("'ladmin': Modification of a state (account: %s, new state: %d, ip: %s)\n",
							          auth_dat[i].userid, statut, ip);
						if (auth_dat[i].state == 0) {
							unsigned char buf[16];
							WBUFW(buf,0) = 0x2731;
							WBUFL(buf,2) = auth_dat[i].account_id;
							WBUFB(buf,6) = 0; // 0: change of statut, 1: ban
							WBUFL(buf,7) = statut; // status or final date of a banishment
							charif_sendallwos(-1, buf, 11);
							for(j = 0; j < AUTH_FIFO_SIZE; j++)
								if (auth_fifo[j].account_id == auth_dat[i].account_id)
									auth_fifo[j].login_id1++; // to avoid reconnection error when come back from map-server (char-server will ask again the authentification)
						}
						auth_dat[i].state = statut;
						memcpy(auth_dat[i].error_message, error_message, 20);
						mmo_auth_sync();
					}
				} else {
					memcpy(WFIFOP(fd,6), account_name, 24);
					login_log("'ladmin': Attempt to modify the state of an unknown account (account: %s, received state: %d, ip: %s)\n",
					          account_name, statut, ip);
				}
				WFIFOL(fd,30) = statut;
			}
			WFIFOSET(fd,34);
			RFIFOSKIP(fd,50);
			break;

		case 0x7938:	// Request for servers list and # of online players
		{		
			uint8 server_num = 0;
			login_log("'ladmin': Sending of servers list (ip: %s)\n", ip);
			for(i = 0; i < MAX_SERVERS; i++) {
				if (server_fd[i] >= 0) {
					WFIFOL(fd,4+server_num*32) = htonl(server[i].ip);
					WFIFOW(fd,4+server_num*32+4) = htons(server[i].port);
					memcpy(WFIFOP(fd,4+server_num*32+6), server[i].name, 20);
					WFIFOW(fd,4+server_num*32+26) = server[i].users;
					WFIFOW(fd,4+server_num*32+28) = server[i].maintenance;
					WFIFOW(fd,4+server_num*32+30) = server[i].new_;
					server_num++;
				}
			}
			WFIFOW(fd,0) = 0x7939;
			WFIFOW(fd,2) = 4 + 32 * server_num;
			WFIFOSET(fd,4+32*server_num);
			RFIFOSKIP(fd,2);
			break;
		}

		case 0x793a:	// Request to password check
			if (RFIFOREST(fd) < 50)
				return 0;
			WFIFOW(fd,0) = 0x793b;
			WFIFOL(fd,2) = 0xFFFFFFFF; // WTF???
			account_name = (char*)RFIFOP(fd,2);
			account_name[23] = '\0';
			remove_control_chars(account_name);
			i = search_account_index(account_name);
			if (i != -1) {
				char pass[25];
				memcpy(WFIFOP(fd,6), auth_dat[i].userid, 24);
				memcpy(pass, RFIFOP(fd,26), 24);
				pass[24] = '\0';
				remove_control_chars(pass);
				if (strcmp(auth_dat[i].pass, pass) == 0) {
					WFIFOL(fd,2) = auth_dat[i].account_id;
					login_log("'ladmin': Check of password OK (account: %s, password: %s, ip: %s)\n",
					          auth_dat[i].userid, auth_dat[i].pass, ip);
				} else {
					login_log("'ladmin': Failure of password check (account: %s, proposed pass: %s, ip: %s)\n",
					          auth_dat[i].userid, pass, ip);
				}
			} else {
				memcpy(WFIFOP(fd,6), account_name, 24);
				login_log("'ladmin': Attempt to check the password of an unknown account (account: %s, ip: %s)\n",
				          account_name, ip);
			}
			WFIFOSET(fd,30);
			RFIFOSKIP(fd,50);
			break;

		case 0x793c:	// Request to modify sex
			if (RFIFOREST(fd) < 27)
				return 0;
			WFIFOW(fd,0) = 0x793d;
			WFIFOL(fd,2) = 0xFFFFFFFF; // WTF???
			account_name = (char*)RFIFOP(fd,2);
			account_name[23] = '\0';
			remove_control_chars(account_name);
			memcpy(WFIFOP(fd,6), account_name, 24);
			{
				char sex;
				sex = RFIFOB(fd,26);
				if (sex != 'F' && sex != 'M') {
					if (sex > 31)
						login_log("'ladmin': Attempt to give an invalid sex (account: %s, received sex: %c, ip: %s)\n",
						          account_name, sex, ip);
					else
						login_log("'ladmin': Attempt to give an invalid sex (account: %s, received sex: 'control char', ip: %s)\n",
						          account_name, ip);
				} else {
					i = search_account_index(account_name);
					if (i != -1) {
						memcpy(WFIFOP(fd,6), auth_dat[i].userid, 24);
						if (auth_dat[i].sex != ((sex == 'S' || sex == 's') ? 2 : (sex == 'M' || sex == 'm'))) {
							unsigned char buf[16];
							WFIFOL(fd,2) = auth_dat[i].account_id;
							for(j = 0; j < AUTH_FIFO_SIZE; j++)
								if (auth_fifo[j].account_id == auth_dat[i].account_id)
									auth_fifo[j].login_id1++; // to avoid reconnection error when come back from map-server (char-server will ask again the authentification)
							auth_dat[i].sex = (sex == 'S' || sex == 's') ? 2 : (sex == 'M' || sex == 'm');
							login_log("'ladmin': Modification of a sex (account: %s, new sex: %c, ip: %s)\n",
							          auth_dat[i].userid, sex, ip);
							mmo_auth_sync();
							// send to all char-server the change
							WBUFW(buf,0) = 0x2723;
							WBUFL(buf,2) = auth_dat[i].account_id;
							WBUFB(buf,6) = auth_dat[i].sex;
							charif_sendallwos(-1, buf, 7);
						} else {
							login_log("'ladmin': Modification of a sex, but the sex is already the good sex (account: %s, sex: %c, ip: %s)\n",
							          auth_dat[i].userid, sex, ip);
						}
					} else {
						login_log("'ladmin': Attempt to modify the sex of an unknown account (account: %s, received sex: %c, ip: %s)\n",
						          account_name, sex, ip);
					}
				}
			}
			WFIFOSET(fd,30);
			RFIFOSKIP(fd,27);
			break;

		case 0x793e:	// Request to modify GM level
			if (RFIFOREST(fd) < 27)
				return 0;
			WFIFOW(fd,0) = 0x793f;
			WFIFOL(fd,2) = 0xFFFFFFFF; // WTF???
			account_name = (char*)RFIFOP(fd,2);
			account_name[23] = '\0';
			remove_control_chars(account_name);
			memcpy(WFIFOP(fd,6), account_name, 24);
			{
				char new_gm_level;
				new_gm_level = RFIFOB(fd,26);
				if (new_gm_level < 0 || new_gm_level > 99) {
					login_log("'ladmin': Attempt to give an invalid GM level (account: %s, received GM level: %d, ip: %s)\n",
					          account_name, (int)new_gm_level, ip);
				} else {
					i = search_account_index(account_name);
					if (i != -1) {
						int acc = auth_dat[i].account_id;
						memcpy(WFIFOP(fd,6), auth_dat[i].userid, 24);
						if (isGM(acc) != new_gm_level) {
							// modification of the file
							FILE *fp, *fp2;
							int lock;
							char line[512];
							int GM_account, GM_level;
							int modify_flag;
							char tmpstr[24];
							time_t raw_time;
							if ((fp2 = lock_fopen(GM_account_filename, &lock)) != NULL) {
								if ((fp = fopen(GM_account_filename, "r")) != NULL) {
									time(&raw_time);
									strftime(tmpstr, 23, login_config.date_format, localtime(&raw_time));
									modify_flag = 0;
									// read/write GM file
									while(fgets(line, sizeof(line), fp))
									{
										while(line[0] != '\0' && (line[strlen(line)-1] == '\n' || line[strlen(line)-1] == '\r'))
											line[strlen(line)-1] = '\0'; // TODO: remove this
										if ((line[0] == '/' && line[1] == '/') || line[0] == '\0')
											fprintf(fp2, "%s\n", line);
										else {
											if (sscanf(line, "%d %d", &GM_account, &GM_level) != 2 && sscanf(line, "%d: %d", &GM_account, &GM_level) != 2)
												fprintf(fp2, "%s\n", line);
											else if (GM_account != acc)
												fprintf(fp2, "%s\n", line);
											else if (new_gm_level < 1) {
												fprintf(fp2, "// %s: 'ladmin' GM level removed on account %d '%s' (previous level: %d)\n//%d %d\n", tmpstr, acc, auth_dat[i].userid, GM_level, acc, new_gm_level);
												modify_flag = 1;
											} else {
												fprintf(fp2, "// %s: 'ladmin' GM level on account %d '%s' (previous level: %d)\n%d %d\n", tmpstr, acc, auth_dat[i].userid, GM_level, acc, new_gm_level);
												modify_flag = 1;
											}
										}
									}
									if (modify_flag == 0)
										fprintf(fp2, "// %s: 'ladmin' GM level on account %d '%s' (previous level: 0)\n%d %d\n", tmpstr, acc, auth_dat[i].userid, acc, new_gm_level);
									fclose(fp);
								} else {
									login_log("'ladmin': Attempt to modify of a GM level - impossible to read GM accounts file (account: %s (%d), received GM level: %d, ip: %s)\n",
									          auth_dat[i].userid, acc, (int)new_gm_level, ip);
								}
								if (lock_fclose(fp2, GM_account_filename, &lock) == 0) {
									WFIFOL(fd,2) = acc;
									login_log("'ladmin': Modification of a GM level (account: %s (%d), new GM level: %d, ip: %s)\n",
									          auth_dat[i].userid, acc, (int)new_gm_level, ip);
									// read and send new GM informations
									read_gm_account();
									send_GM_accounts(-1);
								} else {
									login_log("'ladmin': Attempt to modify of a GM level - impossible to write GM accounts file (account: %s (%d), received GM level: %d, ip: %s)\n",
									          auth_dat[i].userid, acc, (int)new_gm_level, ip);
								}
							} else {
								login_log("'ladmin': Attempt to modify of a GM level - impossible to write GM accounts file (account: %s (%d), received GM level: %d, ip: %s)\n",
								          auth_dat[i].userid, acc, (int)new_gm_level, ip);
							}
						} else {
							login_log("'ladmin': Attempt to modify of a GM level, but the GM level is already the good GM level (account: %s (%d), GM level: %d, ip: %s)\n",
							          auth_dat[i].userid, acc, (int)new_gm_level, ip);
						}
					} else {
						login_log("'ladmin': Attempt to modify the GM level of an unknown account (account: %s, received GM level: %d, ip: %s)\n",
						          account_name, (int)new_gm_level, ip);
					}
				}
			}
			WFIFOSET(fd,30);
			RFIFOSKIP(fd,27);
			break;

		case 0x7940:	// Request to modify e-mail
			if (RFIFOREST(fd) < 66)
				return 0;
			WFIFOW(fd,0) = 0x7941;
			WFIFOL(fd,2) = 0xFFFFFFFF; // WTF???
			account_name = (char*)RFIFOP(fd,2);
			account_name[23] = '\0';
			remove_control_chars(account_name);
			memcpy(WFIFOP(fd,6), account_name, 24);
			{
				char email[40];
				memcpy(email, RFIFOP(fd,26), 40);
				if (e_mail_check(email) == 0) {
					login_log("'ladmin': Attempt to give an invalid e-mail (account: %s, ip: %s)\n",
					          account_name, ip);
				} else {
					remove_control_chars(email);
					i = search_account_index(account_name);
					if (i != -1) {
						memcpy(WFIFOP(fd,6), auth_dat[i].userid, 24);
						memcpy(auth_dat[i].email, email, 40);
						WFIFOL(fd,2) = auth_dat[i].account_id;
						login_log("'ladmin': Modification of an email (account: %s, new e-mail: %s, ip: %s)\n",
						          auth_dat[i].userid, email, ip);
						mmo_auth_sync();
					} else {
						login_log("'ladmin': Attempt to modify the e-mail of an unknown account (account: %s, received e-mail: %s, ip: %s)\n",
						          account_name, email, ip);
					}
				}
			}
			WFIFOSET(fd,30);
			RFIFOSKIP(fd,66);
			break;

		case 0x7942:	// Request to modify memo field
			if ((int)RFIFOREST(fd) < 28 || (int)RFIFOREST(fd) < (28 + RFIFOW(fd,26)))
				return 0;
			WFIFOW(fd,0) = 0x7943;
			WFIFOL(fd,2) = 0xFFFFFFFF; // WTF???
			account_name = (char*)RFIFOP(fd,2);
			account_name[23] = '\0';
			remove_control_chars(account_name);
			i = search_account_index(account_name);
			if (i != -1) {
				int size_of_memo = sizeof(auth_dat[i].memo);
				memcpy(WFIFOP(fd,6), auth_dat[i].userid, 24);
				memset(auth_dat[i].memo, '\0', size_of_memo);
				if (RFIFOW(fd,26) == 0) {
					strncpy(auth_dat[i].memo, "-", size_of_memo);
				} else if (RFIFOW(fd,26) > size_of_memo - 1) {
					memcpy(auth_dat[i].memo, RFIFOP(fd,28), size_of_memo - 1);
				} else {
					memcpy(auth_dat[i].memo, RFIFOP(fd,28), RFIFOW(fd,26));
				}
				auth_dat[i].memo[size_of_memo - 1] = '\0';
				remove_control_chars(auth_dat[i].memo);
				WFIFOL(fd,2) = auth_dat[i].account_id;
				login_log("'ladmin': Modification of a memo field (account: %s, new memo: %s, ip: %s)\n",
				          auth_dat[i].userid, auth_dat[i].memo, ip);
				mmo_auth_sync();
			} else {
				memcpy(WFIFOP(fd,6), account_name, 24);
				login_log("'ladmin': Attempt to modify the memo field of an unknown account (account: %s, ip: %s)\n",
				          account_name, ip);
			}
			WFIFOSET(fd,30);
			RFIFOSKIP(fd,28 + RFIFOW(fd,26));
			break;

		case 0x7944:	// Request to found an account id
			if (RFIFOREST(fd) < 26)
				return 0;
			WFIFOW(fd,0) = 0x7945;
			WFIFOL(fd,2) = 0xFFFFFFFF; // WTF???
			account_name = (char*)RFIFOP(fd,2);
			account_name[23] = '\0';
			remove_control_chars(account_name);
			i = search_account_index(account_name);
			if (i != -1) {
				memcpy(WFIFOP(fd,6), auth_dat[i].userid, 24);
				WFIFOL(fd,2) = auth_dat[i].account_id;
				login_log("'ladmin': Request (by the name) of an account id (account: %s, id: %d, ip: %s)\n",
				          auth_dat[i].userid, auth_dat[i].account_id, ip);
			} else {
				memcpy(WFIFOP(fd,6), account_name, 24);
				login_log("'ladmin': ID request (by the name) of an unknown account (account: %s, ip: %s)\n",
				          account_name, ip);
			}
			WFIFOSET(fd,30);
			RFIFOSKIP(fd,26);
			break;

		case 0x7946:	// Request to found an account name
			if (RFIFOREST(fd) < 6)
				return 0;
			WFIFOW(fd,0) = 0x7947;
			WFIFOL(fd,2) = RFIFOL(fd,2);
			memset(WFIFOP(fd,6), '\0', 24);
			for(i = 0; i < auth_num; i++) {
				if (auth_dat[i].account_id == RFIFOL(fd,2)) {
					strncpy((char*)WFIFOP(fd,6), auth_dat[i].userid, 24);
					login_log("'ladmin': Request (by id) of an account name (account: %s, id: %d, ip: %s)\n",
					          auth_dat[i].userid, RFIFOL(fd,2), ip);
					break;
				}
			}
			if (i == auth_num) {
				login_log("'ladmin': Name request (by id) of an unknown account (id: %d, ip: %s)\n",
				          RFIFOL(fd,2), ip);
				strncpy((char*)WFIFOP(fd,6), "", 24);
			}
			WFIFOSET(fd,30);
			RFIFOSKIP(fd,6);
			break;

		case 0x7948:	// Request to change the validity limit (timestamp) (absolute value)
			if (RFIFOREST(fd) < 30)
				return 0;
			{
				time_t timestamp;
				char tmpstr[2048];
				WFIFOW(fd,0) = 0x7949;
				WFIFOL(fd,2) = 0xFFFFFFFF; // WTF???
				account_name = (char*)RFIFOP(fd,2);
				account_name[23] = '\0';
				remove_control_chars(account_name);
				timestamp = (time_t)RFIFOL(fd,26);
				strftime(tmpstr, 24, login_config.date_format, localtime(&timestamp));
				i = search_account_index(account_name);
				if (i != -1) {
					memcpy(WFIFOP(fd,6), auth_dat[i].userid, 24);
					login_log("'ladmin': Change of a validity limit (account: %s, new validity: %d (%s), ip: %s)\n",
					          auth_dat[i].userid, timestamp, (timestamp == 0 ? "unlimited" : tmpstr), ip);
					auth_dat[i].connect_until_time = timestamp;
					WFIFOL(fd,2) = auth_dat[i].account_id;
					mmo_auth_sync();
				} else {
					memcpy(WFIFOP(fd,6), account_name, 24);
					login_log("'ladmin': Attempt to change the validity limit of an unknown account (account: %s, received validity: %d (%s), ip: %s)\n",
					          account_name, timestamp, (timestamp == 0 ? "unlimited" : tmpstr), ip);
				}
				WFIFOL(fd,30) = (unsigned int)timestamp;
			}
			WFIFOSET(fd,34);
			RFIFOSKIP(fd,30);
			break;

		case 0x794a:	// Request to change the final date of a banishment (timestamp) (absolute value)
			if (RFIFOREST(fd) < 30)
				return 0;
			{
				time_t timestamp;
				char tmpstr[2048];
				WFIFOW(fd,0) = 0x794b;
				WFIFOL(fd,2) = 0xFFFFFFFF; // WTF???
				account_name = (char*)RFIFOP(fd,2);
				account_name[23] = '\0';
				remove_control_chars(account_name);
				timestamp = (time_t)RFIFOL(fd,26);
				if (timestamp <= time(NULL))
					timestamp = 0;
				strftime(tmpstr, 24, login_config.date_format, localtime(&timestamp));
				i = search_account_index(account_name);
				if (i != -1) {
					memcpy(WFIFOP(fd,6), auth_dat[i].userid, 24);
					WFIFOL(fd,2) = auth_dat[i].account_id;
					login_log("'ladmin': Change of the final date of a banishment (account: %s, new final date of banishment: %d (%s), ip: %s)\n",
					          auth_dat[i].userid, timestamp, (timestamp == 0 ? "no banishment" : tmpstr), ip);
					if (auth_dat[i].ban_until_time != timestamp) {
						if (timestamp != 0) {
							unsigned char buf[16];
							WBUFW(buf,0) = 0x2731;
							WBUFL(buf,2) = auth_dat[i].account_id;
							WBUFB(buf,6) = 1; // 0: change of statut, 1: ban
							WBUFL(buf,7) = (unsigned int)timestamp; // status or final date of a banishment
							charif_sendallwos(-1, buf, 11);
							for(j = 0; j < AUTH_FIFO_SIZE; j++)
								if (auth_fifo[j].account_id == auth_dat[i].account_id)
									auth_fifo[j].login_id1++; // to avoid reconnection error when come back from map-server (char-server will ask again the authentification)
						}
						auth_dat[i].ban_until_time = timestamp;
						mmo_auth_sync();
					}
				} else {
					memcpy(WFIFOP(fd,6), account_name, 24);
					login_log("'ladmin': Attempt to change the final date of a banishment of an unknown account (account: %s, received final date of banishment: %d (%s), ip: %s)\n",
					          account_name, timestamp, (timestamp == 0 ? "no banishment" : tmpstr), ip);
				}
				WFIFOL(fd,30) = (unsigned int)timestamp;
			}
			WFIFOSET(fd,34);
			RFIFOSKIP(fd,30);
			break;

		case 0x794c:	// Request to change the final date of a banishment (timestamp) (relative change)
			if (RFIFOREST(fd) < 38)
				return 0;
			{
				time_t timestamp;
				struct tm *tmtime;
				char tmpstr[2048];
				WFIFOW(fd,0) = 0x794d;
				WFIFOL(fd,2) = 0xFFFFFFFF; // WTF???
				account_name = (char*)RFIFOP(fd,2);
				account_name[23] = '\0';
				remove_control_chars(account_name);
				i = search_account_index(account_name);
				if (i != -1) {
					WFIFOL(fd,2) = auth_dat[i].account_id;
					memcpy(WFIFOP(fd,6), auth_dat[i].userid, 24);
					if (auth_dat[i].ban_until_time == 0 || auth_dat[i].ban_until_time < time(NULL))
						timestamp = time(NULL);
					else
						timestamp = auth_dat[i].ban_until_time;
					tmtime = localtime(&timestamp);
					tmtime->tm_year = tmtime->tm_year + (short)RFIFOW(fd,26);
					tmtime->tm_mon = tmtime->tm_mon + (short)RFIFOW(fd,28);
					tmtime->tm_mday = tmtime->tm_mday + (short)RFIFOW(fd,30);
					tmtime->tm_hour = tmtime->tm_hour + (short)RFIFOW(fd,32);
					tmtime->tm_min = tmtime->tm_min + (short)RFIFOW(fd,34);
					tmtime->tm_sec = tmtime->tm_sec + (short)RFIFOW(fd,36);
					timestamp = mktime(tmtime);
					if (timestamp != -1) {
						if (timestamp <= time(NULL))
							timestamp = 0;
						strftime(tmpstr, 24, login_config.date_format, localtime(&timestamp));
						login_log("'ladmin': Adjustment of a final date of a banishment (account: %s, (%+d y %+d m %+d d %+d h %+d mn %+d s) -> new validity: %d (%s), ip: %s)\n",
						          auth_dat[i].userid, (short)RFIFOW(fd,26), (short)RFIFOW(fd,28), (short)RFIFOW(fd,30), (short)RFIFOW(fd,32), (short)RFIFOW(fd,34), (short)RFIFOW(fd,36), timestamp, (timestamp == 0 ? "no banishment" : tmpstr), ip);
						if (auth_dat[i].ban_until_time != timestamp) {
							if (timestamp != 0) {
								unsigned char buf[16];
								WBUFW(buf,0) = 0x2731;
								WBUFL(buf,2) = auth_dat[i].account_id;
								WBUFB(buf,6) = 1; // 0: change of statut, 1: ban
								WBUFL(buf,7) = (unsigned int)timestamp; // status or final date of a banishment
								charif_sendallwos(-1, buf, 11);
								for(j = 0; j < AUTH_FIFO_SIZE; j++)
									if (auth_fifo[j].account_id == auth_dat[i].account_id)
										auth_fifo[j].login_id1++; // to avoid reconnection error when come back from map-server (char-server will ask again the authentification)
							}
							auth_dat[i].ban_until_time = timestamp;
							mmo_auth_sync();
						}
					} else {
						strftime(tmpstr, 24, login_config.date_format, localtime(&auth_dat[i].ban_until_time));
						login_log("'ladmin': Impossible to adjust the final date of a banishment (account: %s, %d (%s) + (%+d y %+d m %+d d %+d h %+d mn %+d s) -> ???, ip: %s)\n",
						          auth_dat[i].userid, auth_dat[i].ban_until_time, (auth_dat[i].ban_until_time == 0 ? "no banishment" : tmpstr), (short)RFIFOW(fd,26), (short)RFIFOW(fd,28), (short)RFIFOW(fd,30), (short)RFIFOW(fd,32), (short)RFIFOW(fd,34), (short)RFIFOW(fd,36), ip);
					}
					WFIFOL(fd,30) = (unsigned long)auth_dat[i].ban_until_time;
				} else {
					memcpy(WFIFOP(fd,6), account_name, 24);
					login_log("'ladmin': Attempt to adjust the final date of a banishment of an unknown account (account: %s, ip: %s)\n",
					          account_name, ip);
					WFIFOL(fd,30) = 0;
				}
			}
			WFIFOSET(fd,34);
			RFIFOSKIP(fd,38);
			break;

		case 0x794e:	// Request to send a broadcast message
			if (RFIFOREST(fd) < 8 || RFIFOREST(fd) < (8 + RFIFOL(fd,4)))
				return 0;
			WFIFOW(fd,0) = 0x794f;
			WFIFOW(fd,2) = 0xFFFF; // WTF???
			if (RFIFOL(fd,4) < 1) {
				login_log("'ladmin': Receiving a message for broadcast, but message is void (ip: %s)\n",
				          ip);
			} else {
				// at least 1 char-server
				for(i = 0; i < MAX_SERVERS; i++)
					if (server_fd[i] >= 0)
						break;
				if (i == MAX_SERVERS) {
					login_log("'ladmin': Receiving a message for broadcast, but no char-server is online (ip: %s)\n",
					          ip);
				} else {
					unsigned char buf[32000];
					char message[32000];
					WFIFOW(fd,2) = 0;
					memset(message, '\0', sizeof(message));
					memcpy(message, RFIFOP(fd,8), RFIFOL(fd,4));
					message[sizeof(message)-1] = '\0';
					remove_control_chars(message);
					if (RFIFOW(fd,2) == 0)
						login_log("'ladmin': Receiving a message for broadcast (message (in yellow): %s, ip: %s)\n",
						          message, ip);
					else
						login_log("'ladmin': Receiving a message for broadcast (message (in blue): %s, ip: %s)\n",
						          message, ip);
					// send same message to all char-servers (no answer)
					memcpy(WBUFP(buf,0), RFIFOP(fd,0), 8 + RFIFOL(fd,4));
					WBUFW(buf,0) = 0x2726;
					charif_sendallwos(-1, buf, 8 + RFIFOL(fd,4));
				}
			}
			WFIFOSET(fd,4);
			RFIFOSKIP(fd,8 + RFIFOL(fd,4));
			break;

		case 0x7950:	// Request to change the validity limite (timestamp) (relative change)
			if (RFIFOREST(fd) < 38)
				return 0;
			{
				time_t timestamp;
				struct tm *tmtime;
				char tmpstr[2048];
				char tmpstr2[2048];
				WFIFOW(fd,0) = 0x7951;
				WFIFOL(fd,2) = 0xFFFFFFFF; // WTF???
				account_name = (char*)RFIFOP(fd,2);
				account_name[23] = '\0';
				remove_control_chars(account_name);
				i = search_account_index(account_name);
				if (i != -1) {
					WFIFOL(fd,2) = auth_dat[i].account_id;
					memcpy(WFIFOP(fd,6), auth_dat[i].userid, 24);
					timestamp = auth_dat[i].connect_until_time;
					if (add_to_unlimited_account == 0 && timestamp == 0) {
						login_log("'ladmin': Attempt to adjust the validity limit of an unlimited account (account: %s, ip: %s)\n",
						          auth_dat[i].userid, ip);
						WFIFOL(fd,30) = 0;
					} else {
						if (timestamp == 0 || timestamp < time(NULL))
							timestamp = time(NULL);
						tmtime = localtime(&timestamp);
						tmtime->tm_year = tmtime->tm_year + (short)RFIFOW(fd,26);
						tmtime->tm_mon = tmtime->tm_mon + (short)RFIFOW(fd,28);
						tmtime->tm_mday = tmtime->tm_mday + (short)RFIFOW(fd,30);
						tmtime->tm_hour = tmtime->tm_hour + (short)RFIFOW(fd,32);
						tmtime->tm_min = tmtime->tm_min + (short)RFIFOW(fd,34);
						tmtime->tm_sec = tmtime->tm_sec + (short)RFIFOW(fd,36);
						timestamp = mktime(tmtime);
						if (timestamp != -1) {
							strftime(tmpstr, 24, login_config.date_format, localtime(&auth_dat[i].connect_until_time));
							strftime(tmpstr2, 24, login_config.date_format, localtime(&timestamp));
							login_log("'ladmin': Adjustment of a validity limit (account: %s, %d (%s) + (%+d y %+d m %+d d %+d h %+d mn %+d s) -> new validity: %d (%s), ip: %s)\n",
							          auth_dat[i].userid, auth_dat[i].connect_until_time, (auth_dat[i].connect_until_time == 0 ? "unlimited" : tmpstr), (short)RFIFOW(fd,26), (short)RFIFOW(fd,28), (short)RFIFOW(fd,30), (short)RFIFOW(fd,32), (short)RFIFOW(fd,34), (short)RFIFOW(fd,36), timestamp, (timestamp == 0 ? "unlimited" : tmpstr2), ip);
							auth_dat[i].connect_until_time = timestamp;
							mmo_auth_sync();
							WFIFOL(fd,30) = (unsigned long)auth_dat[i].connect_until_time;
						} else {
							strftime(tmpstr, 24, login_config.date_format, localtime(&auth_dat[i].connect_until_time));
							login_log("'ladmin': Impossible to adjust a validity limit (account: %s, %d (%s) + (%+d y %+d m %+d d %+d h %+d mn %+d s) -> ???, ip: %s)\n",
							          auth_dat[i].userid, auth_dat[i].connect_until_time, (auth_dat[i].connect_until_time == 0 ? "unlimited" : tmpstr), (short)RFIFOW(fd,26), (short)RFIFOW(fd,28), (short)RFIFOW(fd,30), (short)RFIFOW(fd,32), (short)RFIFOW(fd,34), (short)RFIFOW(fd,36), ip);
							WFIFOL(fd,30) = 0;
						}
					}
				} else {
					memcpy(WFIFOP(fd,6), account_name, 24);
					login_log("'ladmin': Attempt to adjust the validity limit of an unknown account (account: %s, ip: %s)\n",
					          account_name, ip);
					WFIFOL(fd,30) = 0;
				}
			}
			WFIFOSET(fd,34);
			RFIFOSKIP(fd,38);
			break;

		case 0x7952:	// Request about informations of an account (by account name)
			if (RFIFOREST(fd) < 26)
				return 0;
			WFIFOW(fd,0) = 0x7953;
			WFIFOL(fd,2) = 0xFFFFFFFF; // WTF???
			account_name = (char*)RFIFOP(fd,2);
			account_name[23] = '\0';
			remove_control_chars(account_name);
			i = search_account_index(account_name);
			if (i != -1) {
				WFIFOL(fd,2) = auth_dat[i].account_id;
				WFIFOB(fd,6) = (unsigned char)isGM(auth_dat[i].account_id);
				memcpy(WFIFOP(fd,7), auth_dat[i].userid, 24);
				WFIFOB(fd,31) = auth_dat[i].sex;
				WFIFOL(fd,32) = auth_dat[i].logincount;
				WFIFOL(fd,36) = auth_dat[i].state;
				memcpy(WFIFOP(fd,40), auth_dat[i].error_message, 20);
				memcpy(WFIFOP(fd,60), auth_dat[i].lastlogin, 24);
				memcpy(WFIFOP(fd,84), auth_dat[i].last_ip, 16);
				memcpy(WFIFOP(fd,100), auth_dat[i].email, 40);
				WFIFOL(fd,140) = (unsigned long)auth_dat[i].connect_until_time;
				WFIFOL(fd,144) = (unsigned long)auth_dat[i].ban_until_time;
				WFIFOW(fd,148) = (uint16)strlen(auth_dat[i].memo);
				if (auth_dat[i].memo[0]) {
					memcpy(WFIFOP(fd,150), auth_dat[i].memo, strlen(auth_dat[i].memo));
				}
				login_log("'ladmin': Sending information of an account (request by the name; account: %s, id: %d, ip: %s)\n",
				          auth_dat[i].userid, auth_dat[i].account_id, ip);
				WFIFOSET(fd,150+strlen(auth_dat[i].memo));
			} else {
				memcpy(WFIFOP(fd,7), account_name, 24);
				WFIFOW(fd,148) = 0;
				login_log("'ladmin': Attempt to obtain information (by the name) of an unknown account (account: %s, ip: %s)\n",
				          account_name, ip);
				WFIFOSET(fd,150);
			}
			RFIFOSKIP(fd,26);
			break;

		case 0x7954:	// Request about information of an account (by account id)
			if (RFIFOREST(fd) < 6)
				return 0;
			WFIFOW(fd,0) = 0x7953;
			WFIFOL(fd,2) = RFIFOL(fd,2);
			memset(WFIFOP(fd,7), '\0', 24);
			for(i = 0; i < auth_num; i++) {
				if (auth_dat[i].account_id == RFIFOL(fd,2)) {
					login_log("'ladmin': Sending information of an account (request by the id; account: %s, id: %d, ip: %s)\n",
					          auth_dat[i].userid, RFIFOL(fd,2), ip);
					WFIFOB(fd,6) = (unsigned char)isGM(auth_dat[i].account_id);
					memcpy(WFIFOP(fd,7), auth_dat[i].userid, 24);
					WFIFOB(fd,31) = auth_dat[i].sex;
					WFIFOL(fd,32) = auth_dat[i].logincount;
					WFIFOL(fd,36) = auth_dat[i].state;
					memcpy(WFIFOP(fd,40), auth_dat[i].error_message, 20);
					memcpy(WFIFOP(fd,60), auth_dat[i].lastlogin, 24);
					memcpy(WFIFOP(fd,84), auth_dat[i].last_ip, 16);
					memcpy(WFIFOP(fd,100), auth_dat[i].email, 40);
					WFIFOL(fd,140) = (unsigned long)auth_dat[i].connect_until_time;
					WFIFOL(fd,144) = (unsigned long)auth_dat[i].ban_until_time;
					WFIFOW(fd,148) = (uint16)strlen(auth_dat[i].memo);
					if (auth_dat[i].memo[0]) {
						memcpy(WFIFOP(fd,150), auth_dat[i].memo, strlen(auth_dat[i].memo));
					}
					WFIFOSET(fd,150+strlen(auth_dat[i].memo));
					break;
				}
			}
			if (i == auth_num) {
				login_log("'ladmin': Attempt to obtain information (by the id) of an unknown account (id: %d, ip: %s)\n",
				          RFIFOL(fd,2), ip);
				strncpy((char*)WFIFOP(fd,7), "", 24);
				WFIFOW(fd,148) = 0;
				WFIFOSET(fd,150);
			}
			RFIFOSKIP(fd,6);
			break;

		case 0x7955:	// Request to reload GM file (no answer)
			login_log("'ladmin': Request to re-load GM configuration file (ip: %s).\n", ip);
			read_gm_account();
			// send GM accounts to all char-servers
			send_GM_accounts(-1);
			RFIFOSKIP(fd,2);
			break;

		default:
			{
				FILE *logfp;
				char tmpstr[24];
				time_t raw_time;
				logfp = fopen(login_log_unknown_packets_filename, "a");
				if (logfp) {
					time(&raw_time);
					strftime(tmpstr, 23, login_config.date_format, localtime(&raw_time));
					fprintf(logfp, "%s: receiving of an unknown packet -> disconnection\n", tmpstr);
					fprintf(logfp, "parse_admin: connection #%d (ip: %s), packet: 0x%x (with being read: %lu).\n", fd, ip, command, (unsigned long)RFIFOREST(fd));
					fprintf(logfp, "Detail (in hex):\n");
					fprintf(logfp, "---- 00-01-02-03-04-05-06-07  08-09-0A-0B-0C-0D-0E-0F\n");
					memset(tmpstr, '\0', sizeof(tmpstr));
					for(i = 0; i < RFIFOREST(fd); i++) {
						if ((i & 15) == 0)
							fprintf(logfp, "%04X ",i);
						fprintf(logfp, "%02x ", RFIFOB(fd,i));
						if (RFIFOB(fd,i) > 0x1f)
							tmpstr[i % 16] = RFIFOB(fd,i);
						else
							tmpstr[i % 16] = '.';
						if ((i - 7) % 16 == 0) // -8 + 1
							fprintf(logfp, " ");
						else if ((i + 1) % 16 == 0) {
							fprintf(logfp, " %s\n", tmpstr);
							memset(tmpstr, '\0', sizeof(tmpstr));
						}
					}
					if (i % 16 != 0) {
						for(j = i; j % 16 != 0; j++) {
							fprintf(logfp, "   ");
							if ((j - 7) % 16 == 0) // -8 + 1
								fprintf(logfp, " ");
						}
						fprintf(logfp, " %s\n", tmpstr);
					}
					fprintf(logfp, "\n");
					fclose(logfp);
				}
			}
			login_log("'ladmin': End of connection, unknown packet (ip: %s)\n", ip);
			set_eof(fd);
			ShowWarning("Remote administration has been disconnected (unknown packet).\n");
			return 0;
		}
	}
	RFIFOSKIP(fd,RFIFOREST(fd));
	return 0;
}

//--------------------------------------------
// Test to know if an IP come from LAN or WAN.
//--------------------------------------------
int lan_subnetcheck(uint32 ip)
{
	int i;
	ARR_FIND( 0, subnet_count, i, (subnet[i].char_ip & subnet[i].mask) == (ip & subnet[i].mask) );
	return ( i < subnet_count ) ? subnet[i].char_ip : 0;
}

//----------------------------------------------------------------------------------------
// Default packet parsing (normal players or administation/char-server connection requests)
//----------------------------------------------------------------------------------------
int parse_login(int fd)
{	
	struct mmo_account account;
	int result, j;
	unsigned int i;
	uint32 ipl = session[fd]->client_addr;
	char ip[16];

	if (session[fd]->eof) {
		do_close(fd);
		return 0;
	}

	ip2str(ipl, ip);

	while( RFIFOREST(fd) >= 2 )
	{
		uint16 command = RFIFOW(fd,0);

		if (display_parse_login)
		{
			if (command == 0x0064 || command == 0x01dd)
			{
				if ((int)RFIFOREST(fd) >= ((command == 0x0064) ? 55 : 47))
					ShowDebug("parse_login: connection #%d, packet: 0x%x (with being read: %d), account: %s.\n", fd, command, RFIFOREST(fd), RFIFOP(fd,6));
			}
			else if (command == 0x2710)
			{
				if (RFIFOREST(fd) >= 86)
					ShowDebug("parse_login: connection #%d, packet: 0x%x (with being read: %d), server: %s.\n", fd, command, RFIFOREST(fd), RFIFOP(fd,60));
			}
			else
				ShowDebug("parse_login: connection #%d, packet: 0x%x (with being read: %d).\n", fd, command, RFIFOREST(fd));
		}

		switch(command)
		{
		case 0x0200:		// New alive packet: structure: 0x200 <account.userid>.24B. used to verify if client is always alive.
			if (RFIFOREST(fd) < 26)
				return 0;
			RFIFOSKIP(fd,26);
		break;

		case 0x0204:		// New alive packet: structure: 0x204 <encrypted.account.userid>.16B. (new ragexe from 22 june 2004)
			if (RFIFOREST(fd) < 18)
				return 0;
			RFIFOSKIP(fd,18);
		break;

		case 0x0064:		// request client login
		case 0x01dd:		// request client login (encryption mode)
		case 0x0277:		// New login packet (kRO 2006-04-24aSakexe langtype 0)
		case 0x02b0:		// New login packet (kRO 2007-05-14aSakexe langtype 0)
		{
			int packet_len = RFIFOREST(fd); // assume no other packet was sent

			//Perform ip-ban check
			if (!check_ip(ipl))
			{
				login_log("Connection refused: IP isn't authorised (deny/allow, ip: %s).\n", ip);
				WFIFOHEAD(fd,23);
				WFIFOW(fd,0) = 0x6a;
				WFIFOB(fd,2) = 3; // 3 = Rejected from Server
				WFIFOSET(fd,23);
				RFIFOSKIP(fd,packet_len);
				set_eof(fd);
				break;
			}

			if( (command == 0x0064 && packet_len < 55)
			||  (command == 0x01dd && packet_len < 47)
			||  (command == 0x0277 && packet_len < 84)
			||  (command == 0x02b0 && packet_len < 85) )
				return 0;

			// S 0064 <version>.l <account name>.24B <password>.24B <version2>.B
			// S 01dd <version>.l <account name>.24B <md5 binary>.16B <version2>.B
			// S 0277 <version>.l <account name>.24B <password>.24B <junk?>.29B <version2>.B
			// S 02b0 <version>.l <account name>.24B <password>.24B <junk?>.30B <version2>.B
			
			memset(&account, 0, sizeof(account));
			account.version = RFIFOL(fd,2);
			if (!account.version) account.version = 1; //Force some version...
			memcpy(account.userid,RFIFOP(fd,6),NAME_LENGTH); account.userid[23] = '\0';
			remove_control_chars(account.userid);
			if (command != 0x01dd) {
				login_log("Request for connection (non encryption mode) of %s (ip: %s).\n", account.userid, ip);
				memcpy(account.passwd, RFIFOP(fd,30), NAME_LENGTH); account.passwd[23] = '\0';
				remove_control_chars(account.passwd);
			} else {
				login_log("Request for connection (encryption mode) of %s (ip: %s).\n", account.userid, ip);
				memcpy(account.passwd, RFIFOP(fd,30), 16); account.passwd[16] = '\0'; // binary data here
			}
			account.passwdenc = (command == 0x01dd) ? PASSWORDENC : 0;

			result = mmo_auth(&account, fd);
			if( result == -1 )
			{	// auth success
				int gm_level = isGM(account.account_id);
				if( login_config.min_level_to_connect > gm_level )
				{
					login_log("Connection refused: the minimum GM level for connection is %d (account: %s, GM level: %d, ip: %s).\n",
					          login_config.min_level_to_connect, account.userid, gm_level, ip);
					WFIFOHEAD(fd,3);
					WFIFOW(fd,0) = 0x81;
					WFIFOB(fd,2) = 1; // 01 = Server closed
					WFIFOSET(fd,3);
				}
				else
				{
					uint8 server_num = 0;

					WFIFOHEAD(fd,47+32*MAX_SERVERS);
					for( i = 0; i < MAX_SERVERS; ++i )
					{
						if( session_isValid(server_fd[i]) )
						{
							// Advanced subnet check [LuzZza]
							uint32 subnet_char_ip = lan_subnetcheck(ipl);
							WFIFOL(fd,47+server_num*32) = htonl((subnet_char_ip) ? subnet_char_ip : server[i].ip);
							WFIFOW(fd,47+server_num*32+4) = ntows(htons(server[i].port)); // [!] LE byte order here [!]
							memcpy(WFIFOP(fd,47+server_num*32+6), server[i].name, 20);
							WFIFOW(fd,47+server_num*32+26) = server[i].users;
							WFIFOW(fd,47+server_num*32+28) = server[i].maintenance;
							WFIFOW(fd,47+server_num*32+30) = server[i].new_;
							server_num++;
						}
					}
					if (server_num > 0) { // if at least 1 char-server
						if (gm_level)
							ShowStatus("Connection of the GM (level:%d) account '%s' accepted.\n", gm_level, account.userid);
						else
							ShowStatus("Connection of the account '%s' accepted.\n", account.userid);

						WFIFOW(fd,0) = 0x69;
						WFIFOW(fd,2) = 47+32*server_num;
						WFIFOL(fd,4) = account.login_id1;
						WFIFOL(fd,8) = account.account_id;
						WFIFOL(fd,12) = account.login_id2;
						WFIFOL(fd,16) = 0; // in old version, that was for ip (not more used)
						//memcpy(WFIFOP(fd,20), account.lastlogin, 24); // in old version, that was for name (not more used)
						WFIFOB(fd,46) = account.sex;
						WFIFOSET(fd,47+32*server_num);
						if (auth_fifo_pos >= AUTH_FIFO_SIZE)
							auth_fifo_pos = 0;
						auth_fifo[auth_fifo_pos].account_id = account.account_id;
						auth_fifo[auth_fifo_pos].login_id1 = account.login_id1;
						auth_fifo[auth_fifo_pos].login_id2 = account.login_id2;
						auth_fifo[auth_fifo_pos].sex = account.sex;
						auth_fifo[auth_fifo_pos].delflag = 0;
						auth_fifo[auth_fifo_pos].ip = session[fd]->client_addr;
						auth_fifo_pos++;
					} else { // if no char-server, don't send void list of servers, just disconnect the player with proper message
						ShowStatus("Connection refused: there is no char-server online (account: %s, ip: %s).\n", account.userid, ip);
						login_log("Connection refused: there is no char-server online (account: %s, ip: %s).\n", account.userid, ip);
						WFIFOW(fd,0) = 0x81;
						WFIFOB(fd,2) = 1; // 01 = Server closed
						WFIFOSET(fd,3);
					}
				}
			}
			else
			{	// auth failed
				WFIFOHEAD(fd,23);
				WFIFOW(fd,0) = 0x6a;
				WFIFOB(fd,2) = (uint8)result;
				if( result == 6 )
				{// 6 = Your are Prohibited to log in until %s
					char tmpstr[20];
					time_t ban_until_time;
					i = search_account_index(account.userid);
					ban_until_time = (i) ? auth_dat[i].ban_until_time : 0;
					strftime(tmpstr, 20, login_config.date_format, localtime(&ban_until_time));
					safestrncpy((char*)WFIFOP(fd,3), tmpstr, 20); // ban timestamp goes here
				}
				else
					memset(WFIFOP(fd,3), '\0', 20);
				WFIFOSET(fd,23);
			}

			RFIFOSKIP(fd,packet_len);
		}
		break;

		case 0x01db:	// Sending request of the coding key
		case 0x791a:	// Sending request of the coding key (administration packet)
		{
			struct login_session_data* ld;
			if( session[fd]->session_data )
			{
				ShowWarning("login: abnormal request of MD5 key (already opened session).\n");
				set_eof(fd);
				return 0;
			}

			CREATE(ld, struct login_session_data, 1);
			session[fd]->session_data = ld;
			
			// Creation of the coding key
			memset(ld->md5key, '\0', sizeof(ld->md5key));
			ld->md5keylen = (uint16)(12 + rand() % 4);
			for( i = 0; i < ld->md5keylen; ++i )
				ld->md5key[i] = (char)(1 + rand() % 255);
			
			WFIFOHEAD(fd,4 + ld->md5keylen);
			WFIFOW(fd,0) = 0x01dc;
			WFIFOW(fd,2) = 4 + ld->md5keylen;
			memcpy(WFIFOP(fd,4), ld->md5key, ld->md5keylen);
			WFIFOSET(fd,WFIFOW(fd,2));

			RFIFOSKIP(fd,2);
		}
		break;

		case 0x2710:	// Connection request of a char-server
			if (RFIFOREST(fd) < 86)
				return 0;
		{
			char* server_name;
			uint32 server_ip;
			uint16 server_port;
			
			memset(&account, 0, sizeof(account));
			safestrncpy(account.userid, RFIFOP(fd,2), NAME_LENGTH); remove_control_chars(account.userid);
			safestrncpy(account.passwd, RFIFOP(fd,26), NAME_LENGTH); remove_control_chars(account.passwd);
			account.passwdenc = 0;
			server_name = (char*)RFIFOP(fd,60); server_name[20] = '\0'; remove_control_chars(server_name);
			server_ip = ntohl(RFIFOL(fd, 54));
			server_port = ntohs(RFIFOW(fd, 58));
			
			ShowInfo("Connection request of the char-server '%s' @ %d.%d.%d.%d:%d (account: '%s', pass: '%s', ip: '%s')\n", server_name, CONVIP(server_ip), server_port, account.userid, account.passwd, ip);
			login_log("Connection request of the char-server '%s' @ %d.%d.%d.%d:%d (account: '%s', pass: '%s', ip: '%s')\n", server_name, CONVIP(server_ip), server_port, account.userid, account.passwd, ip);
			
			result = mmo_auth(&account, fd);
			if( result == -1 && account.sex == 2 && account.account_id < MAX_SERVERS && server_fd[account.account_id] == -1 )
			{
				ShowStatus("Connection of the char-server '%s' accepted.\n", server_name);
				login_log("Connection of the char-server '%s' accepted.\n", server_name);
				memset(&server[account.account_id], 0, sizeof(struct mmo_char_server));
				server[account.account_id].ip = ntohl(RFIFOL(fd,54));
				server[account.account_id].port = ntohs(RFIFOW(fd,58));
				safestrncpy(server[account.account_id].name, server_name, sizeof(server[account.account_id].name));
				server[account.account_id].users = 0;
				server[account.account_id].maintenance = RFIFOW(fd,82);
				server[account.account_id].new_ = RFIFOW(fd,84);
				server_fd[account.account_id] = fd;
				
 				WFIFOHEAD(fd,3);
				WFIFOW(fd,0) = 0x2711;
				WFIFOB(fd,2) = 0;
				WFIFOSET(fd,3);
				
				session[fd]->func_parse = parse_fromchar;
				realloc_fifo(fd, FIFOSIZE_SERVERLINK, FIFOSIZE_SERVERLINK);
				
				send_GM_accounts(fd); // send GM account to char-server
			}
			else
			{
				ShowNotice("Connection of the char-server '%s' REFUSED.\n", server_name);
				login_log("Connection of the char-server '%s' REFUSED.\n", server_name);
				WFIFOHEAD(fd,3);
				WFIFOW(fd,0) = 0x2711;
				WFIFOB(fd,2) = 3;
				WFIFOSET(fd,3);
			}
		}

			RFIFOSKIP(fd,86);
			return 0;

		case 0x7530:	// Server version information request
			login_log("Sending of the server version (ip: %s)\n", ip);
			WFIFOHEAD(fd,10);
			WFIFOW(fd,0) = 0x7531;
			WFIFOB(fd,2) = ATHENA_MAJOR_VERSION;
			WFIFOB(fd,3) = ATHENA_MINOR_VERSION;
			WFIFOB(fd,4) = ATHENA_REVISION;
			WFIFOB(fd,5) = ATHENA_RELEASE_FLAG;
			WFIFOB(fd,6) = ATHENA_OFFICIAL_FLAG;
			WFIFOB(fd,7) = ATHENA_SERVER_LOGIN;
			WFIFOW(fd,8) = ATHENA_MOD_VERSION;
			WFIFOSET(fd,10);

			RFIFOSKIP(fd,2);
		break;

		case 0x7532:	// Request to end connection
			login_log("End of connection (ip: %s)\n", ip);
			set_eof(fd);
			return 0;

		case 0x7918:	// Request for administation login
			if ((int)RFIFOREST(fd) < 4 || (int)RFIFOREST(fd) < ((RFIFOW(fd,2) == 0) ? 28 : 20))
				return 0;
			WFIFOW(fd,0) = 0x7919;
			WFIFOB(fd,2) = 1;
			if (!check_ladminip(session[fd]->client_addr)) {
				login_log("'ladmin'-login: Connection in administration mode refused: IP isn't authorised (ladmin_allow, ip: %s).\n", ip);
			} else {
				struct login_session_data *ld = (struct login_session_data*)session[fd]->session_data;
				if (RFIFOW(fd,2) == 0) {	// non encrypted password
					char password[25];
					memcpy(password, RFIFOP(fd,4), 24);
					password[24] = '\0';
					remove_control_chars(password);
					// If remote administration is enabled and password sent by client matches password read from login server configuration file
					if ((admin_state == 1) && (strcmp(password, admin_pass) == 0)) {
						login_log("'ladmin'-login: Connection in administration mode accepted (non encrypted password: %s, ip: %s)\n", password, ip);
						ShowNotice("Connection of a remote administration accepted (non encrypted password).\n");
						WFIFOB(fd,2) = 0;
						session[fd]->func_parse = parse_admin;
					} else if (admin_state != 1)
						login_log("'ladmin'-login: Connection in administration mode REFUSED - remote administration is disabled (non encrypted password: %s, ip: %s)\n", password, ip);
					else
						login_log("'ladmin'-login: Connection in administration mode REFUSED - invalid password (non encrypted password: %s, ip: %s)\n", password, ip);
				} else {	// encrypted password
					if (!ld)
						ShowError("'ladmin'-login: error! MD5 key not created/requested for an administration login.\n");
					else {
						char md5str[64] = "", md5bin[32];
						if (RFIFOW(fd,2) == 1) {
							sprintf(md5str, "%s%s", ld->md5key, admin_pass); // 20 24
						} else if (RFIFOW(fd,2) == 2) {
							sprintf(md5str, "%s%s", admin_pass, ld->md5key); // 24 20
						}
						MD5_String2binary(md5str, md5bin);
						// If remote administration is enabled and password hash sent by client matches hash of password read from login server configuration file
						if ((admin_state == 1) && (memcmp(md5bin, RFIFOP(fd,4), 16) == 0)) {
							login_log("'ladmin'-login: Connection in administration mode accepted (encrypted password, ip: %s)\n", ip);
							ShowNotice("Connection of a remote administration accepted (encrypted password).\n");
							WFIFOB(fd,2) = 0;
							session[fd]->func_parse = parse_admin;
						} else if (admin_state != 1)
							login_log("'ladmin'-login: Connection in administration mode REFUSED - remote administration is disabled (encrypted password, ip: %s)\n", ip);
						else
							login_log("'ladmin'-login: Connection in administration mode REFUSED - invalid password (encrypted password, ip: %s)\n", ip);
					}
				}
			}
			WFIFOSET(fd,3);

			RFIFOSKIP(fd, (RFIFOW(fd,2) == 0) ? 28 : 20);
		break;

		default:
			if (save_unknown_packets) {
				FILE *logfp;
				char tmpstr[24];
				time_t raw_time;
				logfp = fopen(login_log_unknown_packets_filename, "a");
				if (logfp) {
					time(&raw_time);
					strftime(tmpstr, 23, login_config.date_format, localtime(&raw_time));
					fprintf(logfp, "%s: receiving of an unknown packet -> disconnection\n", tmpstr);
					fprintf(logfp, "parse_login: connection #%d (ip: %s), packet: 0x%x (with being read: %lu).\n", fd, ip, command, (unsigned long)RFIFOREST(fd));
					fprintf(logfp, "Detail (in hex):\n");
					fprintf(logfp, "---- 00-01-02-03-04-05-06-07  08-09-0A-0B-0C-0D-0E-0F\n");
					memset(tmpstr, '\0', sizeof(tmpstr));
					for(i = 0; i < RFIFOREST(fd); i++) {
						if ((i & 15) == 0)
							fprintf(logfp, "%04X ",i);
						fprintf(logfp, "%02x ", RFIFOB(fd,i));
						if (RFIFOB(fd,i) > 0x1f)
							tmpstr[i % 16] = RFIFOB(fd,i);
						else
							tmpstr[i % 16] = '.';
						if ((i - 7) % 16 == 0) // -8 + 1
							fprintf(logfp, " ");
						else if ((i + 1) % 16 == 0) {
							fprintf(logfp, " %s\n", tmpstr);
							memset(tmpstr, '\0', sizeof(tmpstr));
						}
					}
					if (i % 16 != 0) {
						for(j = i; j % 16 != 0; j++) {
							fprintf(logfp, "   ");
							if ((j - 7) % 16 == 0) // -8 + 1
								fprintf(logfp, " ");
						}
						fprintf(logfp, " %s\n", tmpstr);
					}
					fprintf(logfp, "\n");
					fclose(logfp);
				}
			}
			login_log("Abnormal end of connection (ip: %s): Unknown packet 0x%x\n", ip, command);
			set_eof(fd);
			return 0;
		}
	}

	RFIFOSKIP(fd,RFIFOREST(fd));
	return 0;
}

//-----------------------
// Console Command Parser [Wizputer]
//-----------------------
int parse_console(char* buf)
{
	char command[256];

	memset(command, 0, sizeof(command));

	sscanf(buf, "%[^\n]", command);

	ShowInfo("Console command :%s", command);
	login_log("Console command :%s\n", command);

	if( strcmpi("shutdown", command) == 0 ||
		strcmpi("exit", command) == 0 ||
		strcmpi("quit", command) == 0 ||
		strcmpi("end", command) == 0 )
		runflag = 0;
	else
	if( strcmpi("alive", command) == 0 ||
		strcmpi("status", command) == 0 )
		ShowInfo(CL_CYAN"Console: "CL_BOLD"I'm Alive."CL_RESET"\n");
	else
	if( strcmpi("help", command) == 0 ) {
		printf(CL_BOLD"Help of commands:"CL_RESET"\n");
		printf("  To shutdown the server:\n");
		printf("  'shutdown|exit|quit|end'\n");
		printf("  To know if server is alive:\n");
		printf("  'alive|status'\n");
	}

	return 0;
}

static int online_data_cleanup_sub(DBKey key, void *data, va_list ap)
{
	struct online_login_data *character= (struct online_login_data*)data;
	if (character->char_server == -2) //Unknown server.. set them offline
		remove_online_user(character->account_id);
	else if (character->char_server < 0)
		//Free data from players that have not been online for a while.
		db_remove(online_db, key);
	return 0;
}

static int online_data_cleanup(int tid, unsigned int tick, int id, int data)
{
	online_db->foreach(online_db, online_data_cleanup_sub);
	return 0;
} 

//----------------------------------
// Reading Lan Support configuration
//----------------------------------
int login_lan_config_read(const char *lancfgName)
{
	FILE *fp;
	int line_num = 0;
	char line[1024], w1[64], w2[64], w3[64], w4[64];
	
	if((fp = fopen(lancfgName, "r")) == NULL) {
		ShowWarning("LAN Support configuration file is not found: %s\n", lancfgName);
		return 1;
	}

	ShowInfo("Reading the configuration file %s...\n", lancfgName);

	while(fgets(line, sizeof(line), fp))
	{
		line_num++;
		if ((line[0] == '/' && line[1] == '/') || line[0] == '\n' || line[1] == '\n')
			continue;

		if(sscanf(line,"%[^:]: %[^:]:%[^:]:%[^\r\n]", w1, w2, w3, w4) != 4)
		{
			ShowWarning("Error syntax of configuration file %s in line %d.\n", lancfgName, line_num);
			continue;
		}

		remove_control_chars(w1);
		remove_control_chars(w2);
		remove_control_chars(w3);
		remove_control_chars(w4);

		if( strcmpi(w1, "subnet") == 0 )
		{
			subnet[subnet_count].mask = str2ip(w2);
			subnet[subnet_count].char_ip = str2ip(w3);
			subnet[subnet_count].map_ip = str2ip(w4);

			if( (subnet[subnet_count].char_ip & subnet[subnet_count].mask) != (subnet[subnet_count].map_ip&subnet[subnet_count].mask) )
			{
				ShowError("%s: Configuration Error: The char server (%s) and map server (%s) belong to different subnetworks!\n", lancfgName, w3, w4);
				continue;
			}

			subnet_count++;
		}

		ShowStatus("Read information about %d subnetworks.\n", subnet_count);
	}

	fclose(fp);
	return 0;
}

//-----------------------------------
// Reading main configuration file
//-----------------------------------
int login_config_read(const char* cfgName)
{
	char line[1024], w1[1024], w2[1024];
	FILE* fp = fopen(cfgName, "r");
	if (fp == NULL) {
		ShowError("Configuration file (%s) not found.\n", cfgName);
		return 1;
	}
	ShowInfo("Reading configuration file %s...\n", cfgName);
	while(fgets(line, sizeof(line), fp))
	{
		if (line[0] == '/' && line[1] == '/')
			continue;

		if (sscanf(line, "%[^:]: %[^\r\n]", w1, w2) < 2)
			continue;

		remove_control_chars(w1);
		remove_control_chars(w2);

		if(!strcmpi(w1,"timestamp_format"))
			strncpy(timestamp_format, w2, 20);
		else if(!strcmpi(w1,"stdout_with_ansisequence"))
			stdout_with_ansisequence = config_switch(w2);
		else if(!strcmpi(w1,"console_silent")) {
			ShowInfo("Console Silent Setting: %d\n", atoi(w2));
			msg_silent = atoi(w2);
		}
		else if (strcmpi(w1, "admin_state") == 0) {
			admin_state = config_switch(w2);
		} else if (strcmpi(w1, "admin_pass") == 0) {
			memset(admin_pass, 0, sizeof(admin_pass));
			strncpy(admin_pass, w2, sizeof(admin_pass));
			admin_pass[sizeof(admin_pass)-1] = '\0';
		} else if (strcmpi(w1, "ladminallowip") == 0) {
			if (strcmpi(w2, "clear") == 0) {
				if (access_ladmin_allow)
					aFree(access_ladmin_allow);
				access_ladmin_allow = NULL;
				access_ladmin_allownum = 0;
			} else {
				if (strcmpi(w2, "all") == 0) {
					// reset all previous values
					if (access_ladmin_allow)
						aFree(access_ladmin_allow);
					// set to all
					access_ladmin_allow = (char*)aCalloc(ACO_STRSIZE, sizeof(char));
					access_ladmin_allownum = 1;
					access_ladmin_allow[0] = '\0';
				} else if (w2[0] && !(access_ladmin_allownum == 1 && access_ladmin_allow[0] == '\0')) { // don't add IP if already 'all'
					if (access_ladmin_allow)
						access_ladmin_allow = (char*)aRealloc(access_ladmin_allow, (access_ladmin_allownum+1) * ACO_STRSIZE);
					else
						access_ladmin_allow = (char*)aCalloc(ACO_STRSIZE, sizeof(char));
					strncpy(access_ladmin_allow + (access_ladmin_allownum++) * ACO_STRSIZE, w2, ACO_STRSIZE);
					access_ladmin_allow[access_ladmin_allownum * ACO_STRSIZE - 1] = '\0';
				}
			}
		} else if (strcmpi(w1, "gm_pass") == 0) {
			memset(gm_pass, 0, sizeof(gm_pass));
			strncpy(gm_pass, w2, sizeof(gm_pass));
			gm_pass[sizeof(gm_pass)-1] = '\0';
		} else if (strcmpi(w1, "level_new_gm") == 0) {
			level_new_gm = atoi(w2);
		}
		else if( !strcmpi(w1, "bind_ip") ) {
			char ip_str[16];
			login_config.login_ip = host2ip(w2);
			if( login_config.login_ip )
				ShowStatus("Login server binding IP address : %s -> %s\n", w2, ip2str(login_config.login_ip, ip_str));
		}
		else if( !strcmpi(w1, "login_port") ) {
			login_config.login_port = (uint16)atoi(w2);
			ShowStatus("set login_port : %s\n",w2);
		}
		else if (strcmpi(w1, "account_filename") == 0) {
			memset(account_filename, 0, sizeof(account_filename));
			strncpy(account_filename, w2, sizeof(account_filename));
			account_filename[sizeof(account_filename)-1] = '\0';
		} else if (strcmpi(w1, "gm_account_filename") == 0) {
			memset(GM_account_filename, 0, sizeof(GM_account_filename));
			strncpy(GM_account_filename, w2, sizeof(GM_account_filename));
			GM_account_filename[sizeof(GM_account_filename)-1] = '\0';
		} else if (strcmpi(w1, "gm_account_filename_check_timer") == 0) {
			gm_account_filename_check_timer = atoi(w2);
		} else if (strcmpi(w1, "log_login") == 0) {
			login_config.log_login = config_switch(w2);
		} else if (strcmpi(w1, "login_log_filename") == 0) {
			memset(login_log_filename, 0, sizeof(login_log_filename));
			strncpy(login_log_filename, w2, sizeof(login_log_filename));
			login_log_filename[sizeof(login_log_filename)-1] = '\0';
		} else if (strcmpi(w1, "login_log_unknown_packets_filename") == 0) {
			memset(login_log_unknown_packets_filename, 0, sizeof(login_log_unknown_packets_filename));
			strncpy(login_log_unknown_packets_filename, w2, sizeof(login_log_unknown_packets_filename));
			login_log_unknown_packets_filename[sizeof(login_log_unknown_packets_filename)-1] = '\0';
		} else if (strcmpi(w1, "save_unknown_packets") == 0) {
			save_unknown_packets = config_switch(w2);
		} else if (strcmpi(w1, "display_parse_login") == 0) {
			display_parse_login = config_switch(w2);
		} else if (strcmpi(w1, "display_parse_admin") == 0) {
			display_parse_admin = config_switch(w2);
		} else if (strcmpi(w1, "display_parse_fromchar") == 0) {
			display_parse_fromchar = config_switch(w2); // 0: no, 1: yes (without packet 0x2714), 2: all packets
		} else if (strcmpi(w1, "add_to_unlimited_account") == 0) {
			add_to_unlimited_account = config_switch(w2);
		} else if (strcmpi(w1, "start_limited_time") == 0) {
			start_limited_time = atoi(w2);
		} else if (strcmpi(w1, "order") == 0) {
			access_order = atoi(w2);
			if (strcmpi(w2, "deny,allow") == 0 ||
			    strcmpi(w2, "deny, allow") == 0) access_order = ACO_DENY_ALLOW;
			if (strcmpi(w2, "allow,deny") == 0 ||
			    strcmpi(w2, "allow, deny") == 0) access_order = ACO_ALLOW_DENY;
			if (strcmpi(w2, "mutual-failture") == 0 ||
			    strcmpi(w2, "mutual-failure") == 0) access_order = ACO_MUTUAL_FAILTURE;
		} else if (strcmpi(w1, "allow") == 0) {
			if (strcmpi(w2, "clear") == 0) {
				if (access_allow)
					aFree(access_allow);
				access_allow = NULL;
				access_allownum = 0;
			} else {
				if (strcmpi(w2, "all") == 0) {
					// reset all previous values
					if (access_allow)
						aFree(access_allow);
					// set to all
					access_allow = (char*)aCalloc(ACO_STRSIZE, sizeof(char));
					access_allownum = 1;
					access_allow[0] = '\0';
				} else if (w2[0] && !(access_allownum == 1 && access_allow[0] == '\0')) { // don't add IP if already 'all'
					if (access_allow)
						access_allow = (char*)aRealloc(access_allow, (access_allownum+1) * ACO_STRSIZE);
					else
						access_allow = (char*)aCalloc(ACO_STRSIZE, sizeof(char));
					strncpy(access_allow + (access_allownum++) * ACO_STRSIZE, w2, ACO_STRSIZE);
					access_allow[access_allownum * ACO_STRSIZE - 1] = '\0';
				}
			}
		} else if (strcmpi(w1, "deny") == 0) {
			if (strcmpi(w2, "clear") == 0) {
				if (access_deny)
					aFree(access_deny);
				access_deny = NULL;
				access_denynum = 0;
			} else {
				if (strcmpi(w2, "all") == 0) {
					// reset all previous values
					if (access_deny)
						aFree(access_deny);
					// set to all
					access_deny = (char*)aCalloc(ACO_STRSIZE, sizeof(char));
					access_denynum = 1;
					access_deny[0] = '\0';
				} else if (w2[0] && !(access_denynum == 1 && access_deny[0] == '\0')) { // don't add IP if already 'all'
					if (access_deny)
						access_deny = (char*)aRealloc(access_deny, (access_denynum+1) * ACO_STRSIZE);
					else
						access_deny = (char*)aCalloc(ACO_STRSIZE, sizeof(char));
					strncpy(access_deny + (access_denynum++) * ACO_STRSIZE, w2, ACO_STRSIZE);
					access_deny[access_denynum * ACO_STRSIZE - 1] = '\0';
				}
			}
		}

		else if(!strcmpi(w1, "new_account"))
			login_config.new_account_flag = (bool)config_switch(w2);
		else if(!strcmpi(w1, "check_client_version"))
			login_config.check_client_version = (bool)config_switch(w2);
		else if(!strcmpi(w1, "client_version_to_connect"))
			login_config.client_version_to_connect = (unsigned int)atoi(w2);
		else if(!strcmpi(w1, "use_MD5_passwords"))
			login_config.use_md5_passwds = (bool)config_switch(w2);
		else if(!strcmpi(w1, "min_level_to_connect"))
			login_config.min_level_to_connect = (uint8)atoi(w2);
		else if(!strcmpi(w1, "date_format"))
			safestrncpy(login_config.date_format, w2, sizeof(login_config.date_format));
		else if(!strcmpi(w1, "console"))
			login_config.console = config_switch(w2);
//		else if(!strcmpi(w1, "case_sensitive"))
//			login_config.case_sensitive = config_switch(w2);
		else if(!strcmpi(w1, "allowed_regs")) //account flood protection system
			allowed_regs = atoi(w2);
		else if(!strcmpi(w1, "time_allowed"))
			time_allowed = atoi(w2);
		else if(!strcmpi(w1, "online_check"))
			login_config.online_check = (bool)config_switch(w2);
		else if(!strcmpi(w1, "use_dnsbl"))
			login_config.use_dnsbl = (bool)config_switch(w2);
		else if(!strcmpi(w1, "dnsbl_servers"))
			safestrncpy(login_config.dnsbl_servs, w2, sizeof(login_config.dnsbl_servs));
		else if(!strcmpi(w1, "ip_sync_interval"))
			login_config.ip_sync_interval = (unsigned int)1000*60*atoi(w2); //w2 comes in minutes.
		else if(!strcmpi(w1, "import"))
			login_config_read(w2);
	}
	fclose(fp);
	ShowInfo("Finished reading %s.\n", cfgName);
	return 0;
}

//-------------------------------------
// Displaying of configuration warnings
//-------------------------------------
void display_conf_warnings(void)
{
	if (admin_state != 0 && admin_state != 1) {
		ShowWarning("Invalid value for admin_state parameter -> setting to 0 (no remote admin).\n");
		admin_state = 0;
	}

	if (admin_state == 1) {
		if (admin_pass[0] == '\0') {
			ShowWarning("Administrator password is void (admin_pass).\n");
		} else if (strcmp(admin_pass, "admin") == 0) {
			ShowWarning("You are using the default administrator password (admin_pass).\n");
			ShowWarning("  We highly recommend that you change it.\n");
		}
	}

	if (gm_pass[0] == '\0') {
		ShowWarning("'To GM become' password is void (gm_pass).\n");
		ShowWarning("  We highly recommend that you set one password.\n");
	} else if (strcmp(gm_pass, "gm") == 0) {
		ShowWarning("You are using the default GM password (gm_pass).\n");
		ShowWarning("  We highly recommend that you change it.\n");
	}

	if (level_new_gm < 0 || level_new_gm > 99) {
		ShowWarning("Invalid value for level_new_gm parameter -> setting to 60 (default).\n");
		level_new_gm = 60;
	}

	if (login_config.new_account_flag != 0 && login_config.new_account_flag != 1) {
		ShowWarning("Invalid value for new_account parameter -> setting to 0 (no new account).\n");
		login_config.new_account_flag = 0;
	}

	if (login_config.login_port < 1024) {
		ShowWarning("Invalid value for login_port parameter -> setting to 6900 (default).\n");
		login_config.login_port = 6900;
	}

	if (gm_account_filename_check_timer < 0) {
		ShowWarning("Invalid value for gm_account_filename_check_timer parameter. Setting to 15 sec (default).\n");
		gm_account_filename_check_timer = 15;
	} else if (gm_account_filename_check_timer == 1) {
		ShowWarning("Invalid value for gm_account_filename_check_timer parameter. Setting to 2 sec (minimum value).\n");
		gm_account_filename_check_timer = 2;
	}

	if (save_unknown_packets != 0 && save_unknown_packets != 1) {
		ShowWarning("Invalid value for save_unknown_packets parameter -> setting to 0-no save.\n");
		save_unknown_packets = 0;
	}

	if (display_parse_login != 0 && display_parse_login != 1) { // 0: no, 1: yes
		ShowWarning("Invalid value for display_parse_login parameter -> setting to 0 (no display).\n");
		display_parse_login = 0;
	}

	if (display_parse_admin != 0 && display_parse_admin != 1) { // 0: no, 1: yes
		ShowWarning("Invalid value for display_parse_admin parameter -> setting to 0 (no display).\n");
		display_parse_admin = 0;
	}

	if (display_parse_fromchar < 0 || display_parse_fromchar > 2) { // 0: no, 1: yes (without packet 0x2714), 2: all packets
		ShowWarning("Invalid value for display_parse_fromchar parameter -> setting to 0 (no display).\n");
		display_parse_fromchar = 0;
	}

	if (login_config.min_level_to_connect < 0) { // 0: all players, 1-99 at least gm level x
		ShowWarning("Invalid value for min_level_to_connect (%d) parameter -> setting 0 (any player).\n", login_config.min_level_to_connect);
		login_config.min_level_to_connect = 0;
	} else if (login_config.min_level_to_connect > 99) { // 0: all players, 1-99 at least gm level x
		ShowWarning("Invalid value for min_level_to_connect (%d) parameter -> setting to 99 (only GM level 99)\n", login_config.min_level_to_connect);
		login_config.min_level_to_connect = 99;
	}

	if (add_to_unlimited_account != 0 && add_to_unlimited_account != 1) { // 0: no, 1: yes
		ShowWarning("Invalid value for add_to_unlimited_account parameter\n");
		ShowWarning("  -> setting to 0 (impossible to add a time to an unlimited account).\n");
		add_to_unlimited_account = 0;
	}

	if (start_limited_time < -1) { // -1: create unlimited account, 0 or more: additionnal sec from now to create limited time
		ShowWarning("Invalid value for start_limited_time parameter\n");
		ShowWarning("  -> setting to -1 (new accounts are created with unlimited time).\n");
		start_limited_time = -1;
	}

	if (access_order == ACO_DENY_ALLOW) {
		if (access_denynum == 1 && access_deny[0] == '\0') {
			ShowWarning("The IP security order is 'deny,allow' (allow if not deny) and you refuse ALL IP.\n");
		}
	} else if (access_order == ACO_ALLOW_DENY) {
		if (access_allownum == 0) {
			ShowWarning("The IP security order is 'allow,deny' (deny if not allow) but, NO IP IS AUTHORISED!\n");
		}
	} else { // ACO_MUTUAL_FAILTURE
		if (access_allownum == 0) {
			ShowWarning("The IP security order is 'mutual-failture'\n");
			ShowWarning("  (allow if in the allow list and not in the deny list).\n");
			ShowWarning("  But, NO IP IS AUTHORISED!\n");
		} else if (access_denynum == 1 && access_deny[0] == '\0') {
			ShowWarning("The IP security order is mutual-failture\n");
			ShowWarning("  (allow if in the allow list and not in the deny list).\n");
			ShowWarning("  But, you refuse ALL IP!\n");
		}
	}

	return;
}

//-------------------------------
// Save configuration in log file
//-------------------------------
void save_config_in_log(void)
{
	int i;

	// a newline in the log...
	login_log("");
	login_log("The login-server starting...\n");

	// save configuration in log file
	login_log("The configuration of the server is set:\n");

	if (admin_state != 1)
		login_log("- with no remote administration.\n");
	else if (admin_pass[0] == '\0')
		login_log("- with a remote administration with a VOID password.\n");
	else if (strcmp(admin_pass, "admin") == 0)
		login_log("- with a remote administration with the DEFAULT password.\n");
	else
		login_log("- with a remote administration with the password of %d character(s).\n", strlen(admin_pass));
	if (access_ladmin_allownum == 0 || (access_ladmin_allownum == 1 && access_ladmin_allow[0] == '\0')) {
		login_log("- to accept any IP for remote administration\n");
	} else {
		login_log("- to accept following IP for remote administration:\n");
		for(i = 0; i < access_ladmin_allownum; i++)
			login_log("  %s\n", (char *)(access_ladmin_allow + i * ACO_STRSIZE));
	}

	if (gm_pass[0] == '\0')
		login_log("- with a VOID 'To GM become' password (gm_pass).\n");
	else if (strcmp(gm_pass, "gm") == 0)
		login_log("- with the DEFAULT 'To GM become' password (gm_pass).\n");
	else
		login_log("- with a 'To GM become' password (gm_pass) of %d character(s).\n", strlen(gm_pass));
	if (level_new_gm == 0)
		login_log("- to refuse any creation of GM with @gm.\n");
	else
		login_log("- to create GM with level '%d' when @gm is used.\n", level_new_gm);

	if (login_config.new_account_flag == 1)
		login_log("- to ALLOW new users (with _F/_M).\n");
	else
		login_log("- to NOT ALLOW new users (with _F/_M).\n");
	login_log("- with port: %d.\n", login_config.login_port);
	login_log("- with the accounts file name: '%s'.\n", account_filename);
	login_log("- with the GM accounts file name: '%s'.\n", GM_account_filename);
	if (gm_account_filename_check_timer == 0)
		login_log("- to NOT check GM accounts file modifications.\n");
	else
		login_log("- to check GM accounts file modifications every %d seconds.\n", gm_account_filename_check_timer);

	if (login_config.use_md5_passwds == 0)
		login_log("- to save password in plain text.\n");
	else
		login_log("- to save password with MD5 encrypting.\n");

	// not necessary to log the 'login_log_filename', we are inside :)

	login_log("- with the unknown packets file name: '%s'.\n", login_log_unknown_packets_filename);
	if (save_unknown_packets)
		login_log("- to SAVE all unkown packets.\n");
	else
		login_log("- to SAVE only unkown packets sending by a char-server or a remote administration.\n");
	if (display_parse_login)
		login_log("- to display normal parse packets on console.\n");
	else
		login_log("- to NOT display normal parse packets on console.\n");
	if (display_parse_admin)
		login_log("- to display administration parse packets on console.\n");
	else
		login_log("- to NOT display administration parse packets on console.\n");
	if (display_parse_fromchar)
		login_log("- to display char-server parse packets on console.\n");
	else
		login_log("- to NOT display char-server parse packets on console.\n");

	if (login_config.min_level_to_connect == 0) // 0: all players, 1-99 at least gm level x
		login_log("- with no minimum level for connection.\n");
	else if (login_config.min_level_to_connect == 99)
		login_log("- to accept only GM with level 99.\n");
	else
		login_log("- to accept only GM with level %d or more.\n", login_config.min_level_to_connect);

	if (add_to_unlimited_account)
		login_log("- to authorize adjustment (with timeadd ladmin) on an unlimited account.\n");
	else
		login_log("- to refuse adjustment (with timeadd ladmin) on an unlimited account. You must use timeset (ladmin command) before.\n");

	if (start_limited_time < 0)
		login_log("- to create new accounts with an unlimited time.\n");
	else if (start_limited_time == 0)
		login_log("- to create new accounts with a limited time: time of creation.\n");
	else
		login_log("- to create new accounts with a limited time: time of creation + %d second(s).\n", start_limited_time);

	if (access_order == ACO_DENY_ALLOW) {
		if (access_denynum == 0) {
			login_log("- with the IP security order: 'deny,allow' (allow if not deny). You refuse no IP.\n");
		} else if (access_denynum == 1 && access_deny[0] == '\0') {
			login_log("- with the IP security order: 'deny,allow' (allow if not deny). You refuse ALL IP.\n");
		} else {
			login_log("- with the IP security order: 'deny,allow' (allow if not deny). Refused IP are:\n");
			for(i = 0; i < access_denynum; i++)
				login_log("  %s\n", (char *)(access_deny + i * ACO_STRSIZE));
		}
	} else if (access_order == ACO_ALLOW_DENY) {
		if (access_allownum == 0) {
			login_log("- with the IP security order: 'allow,deny' (deny if not allow). But, NO IP IS AUTHORISED!\n");
		} else if (access_allownum == 1 && access_allow[0] == '\0') {
			login_log("- with the IP security order: 'allow,deny' (deny if not allow). You authorise ALL IP.\n");
		} else {
			login_log("- with the IP security order: 'allow,deny' (deny if not allow). Authorised IP are:\n");
			for(i = 0; i < access_allownum; i++)
				login_log("  %s\n", (char *)(access_allow + i * ACO_STRSIZE));
		}
	} else { // ACO_MUTUAL_FAILTURE
		login_log("- with the IP security order: 'mutual-failture' (allow if in the allow list and not in the deny list).\n");
		if (access_allownum == 0) {
			login_log("  But, NO IP IS AUTHORISED!\n");
		} else if (access_denynum == 1 && access_deny[0] == '\0') {
			login_log("  But, you refuse ALL IP!\n");
		} else {
			if (access_allownum == 1 && access_allow[0] == '\0') {
				login_log("  You authorise ALL IP.\n");
			} else {
				login_log("  Authorised IP are:\n");
				for(i = 0; i < access_allownum; i++)
					login_log("    %s\n", (char *)(access_allow + i * ACO_STRSIZE));
			}
			login_log("  Refused IP are:\n");
			for(i = 0; i < access_denynum; i++)
				login_log("    %s\n", (char *)(access_deny + i * ACO_STRSIZE));
		}

	}
}

void login_set_defaults()
{
	login_config.login_ip = INADDR_ANY;
	login_config.login_port = 6900;
	login_config.ip_sync_interval = 0;
	login_config.log_login = true;
	safestrncpy(login_config.date_format, "%Y-%m-%d %H:%M:%S", sizeof(login_config.date_format));
	login_config.console = false;
	login_config.new_account_flag = true;
//	login_config.case_sensitive = true;
	login_config.use_md5_passwds = false;
//	login_config.login_gm_read = true;
	login_config.min_level_to_connect = 0;
	login_config.online_check = true;
	login_config.check_client_version = false;
	login_config.client_version_to_connect = 20;

//	login_config.ipban = true;
//	login_config.dynamic_pass_failure_ban = true;
//	login_config.dynamic_pass_failure_ban_interval = 5;
//	login_config.dynamic_pass_failure_ban_limit = 7;
//	login_config.dynamic_pass_failure_ban_duration = 5;
	login_config.use_dnsbl = false;
	safestrncpy(login_config.dnsbl_servs, "", sizeof(login_config.dnsbl_servs));
}

//--------------------------------------
// Function called at exit of the server
//--------------------------------------
void do_final(void)
{
	int i, fd;
	ShowInfo("Terminating...\n");
	mmo_auth_sync();
	online_db->destroy(online_db, NULL);

	if(auth_dat) aFree(auth_dat);
	if(gm_account_db) aFree(gm_account_db);
	if(access_ladmin_allow) aFree(access_ladmin_allow);
	if(access_allow) aFree(access_allow);
	if(access_deny) aFree(access_deny);
	for (i = 0; i < MAX_SERVERS; i++) {
		if ((fd = server_fd[i]) >= 0) {
			server_fd[i] = -1;
			memset(&server[i], 0, sizeof(struct mmo_char_server));
			do_close(fd);
		}
	}
	do_close(login_fd);

	login_log("----End of login-server (normal end with closing of all files).\n");

	if(log_fp)
		fclose(log_fp);
	ShowStatus("Finished.\n");
}

//------------------------------
// Function called when the server
// has received a crash signal.
//------------------------------
void do_abort(void)
{
}

void set_server_type(void)
{
	SERVER_TYPE = ATHENA_SERVER_LOGIN;
}

//------------------------------
// Login server initialization
//------------------------------
int do_init(int argc, char** argv)
{
	int i, j;

	login_set_defaults();

	// read login-server configuration
	login_config_read((argc > 1) ? argv[1] : LOGIN_CONF_NAME);
	display_conf_warnings(); // not in login_config_read, because we can use 'import' option, and display same message twice or more
	save_config_in_log(); // not before, because log file name can be changed
	login_lan_config_read((argc > 2) ? argv[2] : LAN_CONF_NAME);

	srand((unsigned int)time(NULL));

	for( i = 0; i < AUTH_FIFO_SIZE; i++ )
		auth_fifo[i].delflag = 1;

	for( i = 0; i < MAX_SERVERS; i++ )
		server_fd[i] = -1;

	// Online user database init
	online_db = db_alloc(__FILE__,__LINE__,DB_INT,DB_OPT_RELEASE_DATA,sizeof(int));
	add_timer_func_list(waiting_disconnect_timer, "waiting_disconnect_timer");

	// Auth init
	mmo_auth_init();

	// Read account information.
	read_gm_account();

	// set default parser as parse_login function
	set_defaultparse(parse_login);

	add_timer_func_list(check_auth_sync, "check_auth_sync");
	add_timer_interval(gettick() + 60000, check_auth_sync, 0, 0, 60000); // every 60 sec we check if we must save accounts file (only if necessary to save)

	// add timer to check GM accounts file modification
	j = gm_account_filename_check_timer;
	if (j == 0) // if we would not to check, we check every 60 sec, just to have timer (if we change timer, is was not necessary to check if timer already exists)
		j = 60;

	// every x sec we check if gm file has been changed
	add_timer_func_list(check_GM_file, "check_GM_file");
	add_timer_interval(gettick() + j * 1000, check_GM_file, 0, 0, j * 1000); 

	// every 10 minutes cleanup online account db.
	add_timer_func_list(online_data_cleanup, "online_data_cleanup");
	add_timer_interval(gettick() + 600*1000, online_data_cleanup, 0, 0, 600*1000);

	// add timer to detect ip address change and perform update
	if (login_config.ip_sync_interval) {
		add_timer_func_list(sync_ip_addresses, "sync_ip_addresses");
		add_timer_interval(gettick() + login_config.ip_sync_interval, sync_ip_addresses, 0, 0, login_config.ip_sync_interval);
	}

	if( login_config.console )
	{
		//##TODO invoke a CONSOLE_START plugin event
	}

	new_reg_tick = gettick();

	// server port open & binding
	login_fd = make_listen_bind(login_config.login_ip, login_config.login_port);

	login_log("The login-server is ready (Server is listening on the port %d).\n", login_config.login_port);
	ShowStatus("The login-server is "CL_GREEN"ready"CL_RESET" (Server is listening on the port %u).\n\n", login_config.login_port);

	return 0;
}
