// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/socket.h"
#include "../common/timer.h"
#include "../common/malloc.h"
#include "../common/version.h"
#include "../common/nullpo.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"
#include "../common/utils.h"

#include "map.h"
#include "chrif.h"
#include "pc.h"
#include "status.h"
#include "npc.h"
#include "itemdb.h"
#include "chat.h"
#include "trade.h"
#include "storage.h"
#include "script.h"
#include "skill.h"
#include "atcommand.h"
#include "charcommand.h"
#include "intif.h"
#include "battle.h"
#include "mob.h"
#include "party.h"
#include "unit.h"
#include "guild.h"
#include "vending.h"
#include "pet.h"
#include "mercenary.h"	//[orn]
#include "log.h"
#include "irc.h"
#include "clif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#define DUMP_UNKNOWN_PACKET	0

struct Clif_Config {
	int packet_db_ver;	//Preferred packet version.
	int connect_cmd[MAX_PACKET_VER + 1]; //Store the connect command for all versions. [Skotlex]
} clif_config;

struct s_packet_db packet_db[MAX_PACKET_VER + 1][MAX_PACKET_DB + 1];

//Converts item type in case of pet eggs.
#define itemtype(a) (a == IT_PETEGG)?IT_WEAPON:a

#define WBUFPOS(p,pos,x,y,dir) \
	do { \
		uint8 *__p = (p); \
		__p+=(pos); \
		__p[0] = (uint8)((x)>>2); \
		__p[1] = (uint8)(((x)<<6) | (((y)>>4)&0x3f)); \
		__p[2] = (uint8)(((y)<<4) | ((dir)&0xf)); \
	} while(0)
// client-side: x0+=sx0*0.0625-0.5 and y0+=sy0*0.0625-0.5
#define WBUFPOS2(p,pos,x0,y0,x1,y1,sx0,sy0) \
	do { \
		uint8 *__p = (p); \
		__p+=(pos);	\
		__p[0]=(uint8)((x0)>>2); \
		__p[1]=(uint8)(((x0)<<6) | (((y0)>>4)&0x3f)); \
		__p[2]=(uint8)(((y0)<<4) | (((x1)>>6)&0x0f)); \
		__p[3]=(uint8)(((x1)<<2) | (((y1)>>8)&0x03)); \
		__p[4]=(uint8)(y1); \
		__p[5]=(uint8)(((sx0)<<4) | ((sy0)&0x0f)); \
	} while(0)

#define WFIFOPOS(fd,pos,x,y,dir) WBUFPOS(WFIFOP(fd,pos),0,x,y,dir)
#define WFIFOPOS2(fd,pos,x0,y0,x1,y1,sx0,sy0) WBUFPOS2(WFIFOP(fd,pos),0,x0,y0,x1,y1,sx0,sy0)

//To make the assignation of the level based on limits clearer/easier. [Skotlex]
#define clif_setlevel(lv) (lv<battle_config.max_lv?lv:battle_config.max_lv-(lv<battle_config.aura_lv?1:0));

//To idenfity disguised characters.
#define disguised(bl) ((bl)->type==BL_PC && ((TBL_PC*)bl)->disguise)

//Guarantees that the given string does not exceeds the allowed size, as well as making sure it's null terminated. [Skotlex\]
#define mes_len_check(mes, len, max) if (len > max) { mes[max-1] = '\0'; len = max; } else mes[len-1] = '\0';
static char map_ip_str[128];
static uint32 map_ip;
static uint32 bind_ip = INADDR_ANY;
static uint16 map_port = 5121;
int map_fd;

//These two will be used to verify the incoming player's validity.
//It helps identify their client packet version correctly. [Skotlex]
static int max_account_id = DEFAULT_MAX_ACCOUNT_ID;
static int max_char_id = DEFAULT_MAX_CHAR_ID;

int clif_parse (int fd);
static void clif_hpmeter_single(int fd, struct map_session_data *sd);

/*==========================================
 * map�I��ip�ݒ�
 *------------------------------------------*/
int clif_setip(const char* ip)
{
	char ip_str[16];
	map_ip = host2ip(ip);
	if (!map_ip) {
		ShowWarning("Failed to Resolve Map Server Address! (%s)\n", ip);
		return 0;
	}

	strncpy(map_ip_str, ip, sizeof(map_ip_str));
	ShowInfo("Map Server IP Address : '"CL_WHITE"%s"CL_RESET"' -> '"CL_WHITE"%s"CL_RESET"'.\n", ip, ip2str(map_ip, ip_str));
	return 1;
}

void clif_setbindip(const char* ip)
{
	char ip_str[16];
	bind_ip = host2ip(ip);
	if (bind_ip) {
		ShowInfo("Map Server Bind IP Address : '"CL_WHITE"%s"CL_RESET"' -> '"CL_WHITE"%s"CL_RESET"'.\n", ip, ip2str(bind_ip, ip_str));
	} else {
		ShowWarning("Failed to Resolve Map Server Address! (%s)\n", ip);
	}
}

/*==========================================
 * map�I��port�ݒ�
 *------------------------------------------*/
void clif_setport(uint16 port)
{
	map_port = port;
}

/*==========================================
 * map�I��ip�ǂݏo��
 *------------------------------------------*/
uint32 clif_getip(void)
{
	return map_ip;
}

//Refreshes map_server ip, returns the new ip if the ip changed, otherwise it returns 0.
uint32 clif_refresh_ip(void)
{
	uint32 new_ip;

	new_ip = host2ip(map_ip_str);
	if (new_ip && new_ip != map_ip) {
		map_ip = new_ip;
		ShowInfo("Updating IP resolution of [%s].\n", map_ip_str);
		return map_ip;
	}
	return 0;
}

/*==========================================
 * map�I��port�ǂݏo��
 *------------------------------------------*/
uint16 clif_getport(void)
{
	return map_port;
}

/*==========================================
 * Counts connected players.
 *------------------------------------------*/
int clif_countusers(void)
{
	int users = 0, i;
	struct map_session_data *sd;

	for(i = 0; i < fd_max; i++) {
		if (session[i] && session[i]->func_parse == clif_parse &&
			(sd = (struct map_session_data*)session[i]->session_data) &&
		  	sd->state.auth && !(battle_config.hide_GM_session && pc_isGM(sd)))
			users++;
	}
	return users;
}

/*==========================================
 * �S�Ă�client�ɑ΂���func()���s
 *------------------------------------------*/
int clif_foreachclient(int (*func)(struct map_session_data*, va_list),...) //recoded by sasuke, bug when player count gets higher [Kevin]
{
	int i;
	va_list ap;
	struct map_session_data *sd;

	va_start(ap,func);

	for(i = 0; i < fd_max; i++) {
		if ( session[i] && session[i]->func_parse == clif_parse) {
			sd = (struct map_session_data*)session[i]->session_data;
			if ( sd && sd->state.auth && !sd->state.waitingdisconnect )
				func(sd, ap);
		}
	}

	va_end(ap);
	return 0;
}

/*==========================================
 * clif_send��AREA*�w�莞�p
 *------------------------------------------*/
int clif_send_sub(struct block_list *bl, va_list ap)
{
	struct block_list *src_bl;
	struct map_session_data *sd;
	unsigned char *buf;
	int len, type, fd;

	nullpo_retr(0, bl);
	nullpo_retr(0, ap);
	nullpo_retr(0, sd = (struct map_session_data *)bl);

	fd = sd->fd;
	if (!fd) //Don't send to disconnected clients.
		return 0;

	buf = va_arg(ap,unsigned char*);
	len = va_arg(ap,int);
	nullpo_retr(0, src_bl = va_arg(ap,struct block_list*));
	type = va_arg(ap,int);

	switch(type)
	{
	case AREA_WOS:
		if (bl == src_bl)
			return 0;
	break;
	case AREA_WOC:
		if (sd->chatID || bl == src_bl)
			return 0;
	break;
	case AREA_WOSC:
	{
		struct map_session_data *ssd = (struct map_session_data *)src_bl;
		if (ssd && (src_bl->type == BL_PC) && sd->chatID && (sd->chatID == ssd->chatID))
			return 0;
	}
	break;
	}

	if (session[fd] != NULL) {
		WFIFOHEAD(fd, len);
		if (WFIFOP(fd,0) == buf) {
			printf("WARNING: Invalid use of clif_send function\n");
			printf("         Packet x%4x use a WFIFO of a player instead of to use a buffer.\n", WBUFW(buf,0));
			printf("         Please correct your code.\n");
			// don't send to not move the pointer of the packet for next sessions in the loop
			WFIFOSET(fd,0);//## TODO is this ok?
		} else {
			if (packet_db[sd->packet_ver][RBUFW(buf,0)].len) { // packet must exist for the client version
				memcpy(WFIFOP(fd,0), buf, len);
				WFIFOSET(fd,len);
			}
		}
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_send(const uint8* buf, int len, struct block_list* bl, enum send_target type)
{
	int i;
	struct map_session_data *sd;
	struct party_data *p = NULL;
	struct guild *g = NULL;
	int x0 = 0, x1 = 0, y0 = 0, y1 = 0, fd;

	if( type != ALL_CLIENT && type != CHAT_MAINCHAT )
		nullpo_retr(0, bl);

	BL_CAST(BL_PC, bl, sd);

	switch(type) {
	case ALL_CLIENT: //All player clients.
		for (i = 0; i < fd_max; i++) {
			if (session[i] && session[i]->func_parse == clif_parse &&
				(sd = (struct map_session_data *)session[i]->session_data) != NULL&&
				sd->state.auth) {
				if (packet_db[sd->packet_ver][RBUFW(buf,0)].len) { // packet must exist for the client version
					WFIFOHEAD(i, len);
					memcpy(WFIFOP(i,0), buf, len);
					WFIFOSET(i,len);
				}
			}
		}
		break;
	case ALL_SAMEMAP: //All players on the same map
		for(i = 0; i < fd_max; i++) {
			if (session[i] && session[i]->func_parse == clif_parse &&
				(sd = (struct map_session_data*)session[i]->session_data) != NULL &&
				sd->state.auth && sd->bl.m == bl->m) {
				if (packet_db[sd->packet_ver][RBUFW(buf,0)].len) { // packet must exist for the client version
					WFIFOHEAD(i,len);
					memcpy(WFIFOP(i,0), buf, len);
					WFIFOSET(i,len);
				}
			}
		}
		break;
	case AREA:
	case AREA_WOSC:
		if (sd && bl->prev == NULL) //Otherwise source misses the packet.[Skotlex]
			clif_send (buf, len, bl, SELF);
	case AREA_WOC:
	case AREA_WOS:
		map_foreachinarea(clif_send_sub, bl->m, bl->x-AREA_SIZE, bl->y-AREA_SIZE, bl->x+AREA_SIZE, bl->y+AREA_SIZE,
			BL_PC, buf, len, bl, type);
		break;
	case AREA_CHAT_WOC:
		map_foreachinarea(clif_send_sub, bl->m, bl->x-(AREA_SIZE-5), bl->y-(AREA_SIZE-5),
			bl->x+(AREA_SIZE-5), bl->y+(AREA_SIZE-5), BL_PC, buf, len, bl, AREA_WOC);
		break;
	case CHAT:
	case CHAT_WOS:
		{
			struct chat_data *cd;
			if (sd) {
				cd = (struct chat_data*)map_id2bl(sd->chatID);
			} else if (bl->type == BL_CHAT) {
				cd = (struct chat_data*)bl;
			} else break;
			if (cd == NULL)
				break;
			for(i = 0; i < cd->users; i++) {
				if (type == CHAT_WOS && cd->usersd[i] == sd)
					continue;
				if (packet_db[cd->usersd[i]->packet_ver][RBUFW(buf,0)].len) { // packet must exist for the client version
					if ((fd=cd->usersd[i]->fd) >0 && session[fd]) // Added check to see if session exists [PoW]
					{
						WFIFOHEAD(fd,len);
						memcpy(WFIFOP(fd,0), buf, len);
						WFIFOSET(fd,len);
					}
				}
			}
		}
		break;
	case CHAT_MAINCHAT: //[LuzZza]
		for(i=1; i<fd_max; i++) {
			if (session[i] && session[i]->func_parse == clif_parse &&
				(sd = (struct map_session_data*)session[i]->session_data) != NULL &&
				sd->state.mainchat && !sd->chatID && (fd=sd->fd))
			{
				WFIFOHEAD(fd,len);
				memcpy(WFIFOP(fd,0), buf, len);
				WFIFOSET(fd, len);
			}
		}
		break;
	case PARTY_AREA:
	case PARTY_AREA_WOS:
		x0 = bl->x - AREA_SIZE;
		y0 = bl->y - AREA_SIZE;
		x1 = bl->x + AREA_SIZE;
		y1 = bl->y + AREA_SIZE;
	case PARTY:
	case PARTY_WOS:
	case PARTY_SAMEMAP:
	case PARTY_SAMEMAP_WOS:
		if (sd && sd->status.party_id)
			p = party_search(sd->status.party_id);
			
		if (p) {
			for(i=0;i<MAX_PARTY;i++){
				if ((sd = p->data[i].sd) == NULL)
					continue;
				if (!(fd=sd->fd) || session[fd] == NULL || sd->state.auth == 0
					|| session[fd]->session_data == NULL || sd->packet_ver > MAX_PACKET_VER)
					continue;
				
				if (sd->bl.id == bl->id && (type == PARTY_WOS || type == PARTY_SAMEMAP_WOS || type == PARTY_AREA_WOS))
					continue;
				
				if (type != PARTY && type != PARTY_WOS && bl->m != sd->bl.m)
					continue;
				
				if ((type == PARTY_AREA || type == PARTY_AREA_WOS) &&
					(sd->bl.x < x0 || sd->bl.y < y0 || sd->bl.x > x1 || sd->bl.y > y1))
					continue;
				
				if (packet_db[sd->packet_ver][RBUFW(buf,0)].len) { // packet must exist for the client version
					WFIFOHEAD(fd,len);
					memcpy(WFIFOP(fd,0), buf, len);
					WFIFOSET(fd,len);
				}
			}
			if (!enable_spy) //Skip unnecessary parsing. [Skotlex]
				break;
			for (i = 1; i < fd_max; i++){ // partyspy [Syrus22]

				if (session[i] && session[i]->func_parse == clif_parse &&
					(sd = (struct map_session_data*)session[i]->session_data) != NULL &&
				  	sd->state.auth && (fd=sd->fd) && sd->partyspy == p->party.party_id)
		  		{
					if (packet_db[sd->packet_ver][RBUFW(buf,0)].len) { // packet must exist for the client version
						WFIFOHEAD(fd,len);
						memcpy(WFIFOP(fd,0), buf, len);
						WFIFOSET(fd,len);
					}
				}
			}
		}
		break;
	case DUEL:
	case DUEL_WOS:
		if (!sd || !sd->duel_group) break; //Invalid usage.

		x0 = sd->duel_group; //Here we use x0 to store the duel group. [Skotlex]
		for (i = 0; i < fd_max; i++) {
			if (session[i] && session[i]->func_parse == clif_parse &&
				(sd = (struct map_session_data *)session[i]->session_data) != NULL &&
				sd->state.auth && sd->duel_group == x0) {
				if (type == DUEL_WOS && bl->id == sd->bl.id)
					continue;
				if (packet_db[sd->packet_ver][RBUFW(buf,0)].len) { 
					WFIFOHEAD(i, len);
					memcpy(WFIFOP(i,0), buf, len);
					WFIFOSET(i,len);
				}
			}
		}
		break;
	case SELF:
		if (sd && (fd=sd->fd) && packet_db[sd->packet_ver][RBUFW(buf,0)].len) { // packet must exist for the client version
			WFIFOHEAD(fd,len);
			memcpy(WFIFOP(fd,0), buf, len);
			WFIFOSET(fd,len);
		}
		break;

	// New definitions for guilds [Valaris] - Cleaned up and reorganized by [Skotlex]
	case GUILD_AREA:
	case GUILD_AREA_WOS:
		x0 = bl->x - AREA_SIZE;
		y0 = bl->y - AREA_SIZE;
		x1 = bl->x + AREA_SIZE;
		y1 = bl->y + AREA_SIZE;
	case GUILD_SAMEMAP:
	case GUILD_SAMEMAP_WOS:
	case GUILD:
	case GUILD_WOS:
		if (sd && sd->status.guild_id)
			g = guild_search(sd->status.guild_id);

		if (g) {
			for(i = 0; i < g->max_member; i++) {
				if ((sd = g->member[i].sd) != NULL) {
					if (!(fd=sd->fd) || session[fd] == NULL || sd->state.auth == 0
						|| session[fd]->session_data == NULL || sd->packet_ver > MAX_PACKET_VER)
						continue;
					
					if (sd->bl.id == bl->id && (type == GUILD_WOS || type == GUILD_SAMEMAP_WOS || type == GUILD_AREA_WOS))
						continue;
					
					if (type != GUILD && type != GUILD_WOS && sd->bl.m != bl->m)
						continue;
					
					if ((type == GUILD_AREA || type == GUILD_AREA_WOS) &&
						(sd->bl.x < x0 || sd->bl.y < y0 || sd->bl.x > x1 || sd->bl.y > y1))
						continue;

					if (packet_db[sd->packet_ver][RBUFW(buf,0)].len) { // packet must exist for the client version
						WFIFOHEAD(fd,len);
						memcpy(WFIFOP(fd,0), buf, len);
						WFIFOSET(fd,len);
					}
				}
			}
			if (!enable_spy) //Skip unnecessary parsing. [Skotlex]
				break;
			for (i = 1; i < fd_max; i++){ // guildspy [Syrus22]
				if (session[i] && session[i]->func_parse == clif_parse &&
					(sd = (struct map_session_data*)session[i]->session_data) != NULL &&
				  	sd->state.auth && (fd=sd->fd) && sd->guildspy == g->guild_id) {
					if (packet_db[sd->packet_ver][RBUFW(buf,0)].len) { // packet must exist for the client version
						WFIFOHEAD(fd,len);
						memcpy(WFIFOP(fd,0), buf, len);
						WFIFOSET(fd,len);
					}
				}
			}
		}
		break;

	default:
		if (battle_config.error_log)
			ShowError("clif_send: Unrecognized type %d\n",type);
		return -1;
	}

	return 0;
}

//
// �p�P�b�g����đ��M
//
/*==========================================
 *
 *------------------------------------------*/
int clif_authok(struct map_session_data *sd)
{
	int fd;

	if (!sd->fd)
		return 0;
	fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0x73));
	WFIFOW(fd, 0) = 0x73;
	WFIFOL(fd, 2) = gettick();
	WFIFOPOS(fd, 6, sd->bl.x, sd->bl.y, sd->ud.dir);
	WFIFOB(fd, 9) = 5; // ignored
	WFIFOB(fd,10) = 5; // ignored
	WFIFOSET(fd,packet_len(0x73));

	return 0;
}

/*==========================================
 * Authentication failed/disconnect client.
 *------------------------------------------
 * The client closes it's socket and displays a message according to type:
 *  1 - server closed -> MsgStringTable[4]
 *  2 - ID already logged in -> MsgStringTable[5]
 *  3 - timeout/too much lag -> MsgStringTable[241]
 *  4 - server full -> MsgStringTable[264]
 *  5 - underaged -> MsgStringTable[305]
 *  9 - too many connections from this ip -> MsgStringTable[529]
 *  10 - out of available time paid for -> MsgStringTable[530]
 *  15 - disconnected by a GM -> if( servicetype == taiwan ) MsgStringTable[579]
 *  other - disconnected -> MsgStringTable[3]
 */
int clif_authfail_fd(int fd, int type)
{
	if (!fd || !session[fd] || session[fd]->func_parse != clif_parse) //clif_authfail should only be invoked on players!
		return 0;

	WFIFOHEAD(fd, packet_len(0x81));
	WFIFOW(fd,0) = 0x81;
	WFIFOB(fd,2) = type;
	WFIFOSET(fd,packet_len(0x81));
	clif_setwaitclose(fd);
	return 0;
}

/*==========================================
 * Used to know which is the max valid account/char id [Skotlex]
 *------------------------------------------*/
void clif_updatemaxid(int account_id, int char_id)
{
	max_account_id = account_id;
	max_char_id = char_id;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_charselectok(int id)
{
	struct map_session_data* sd;
	int fd;

	if ((sd = map_id2sd(id)) == NULL || !sd->fd)
		return 1;

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0xb3));
	WFIFOW(fd,0) = 0xb3;
	WFIFOB(fd,2) = 1;
	WFIFOSET(fd,packet_len(0xb3));

	return 0;
}

/*==========================================
 * Makes an item appear on the ground
 * 009e <ID>.l <name ID>.w <identify flag>.B <X>.w <Y>.w <subX>.B <subY>.B <amount>.w
 *------------------------------------------*/
int clif_dropflooritem(struct flooritem_data* fitem)
{
	uint8 buf[17];
	int view;

	nullpo_retr(0, fitem);

	if (fitem->item_data.nameid <= 0)
		return 0;

	WBUFW(buf, 0) = 0x9e;
	WBUFL(buf, 2) = fitem->bl.id;
	WBUFW(buf, 6) = ((view = itemdb_viewid(fitem->item_data.nameid)) > 0) ? view : fitem->item_data.nameid;
	WBUFB(buf, 8) = fitem->item_data.identify;
	WBUFW(buf, 9) = fitem->bl.x;
	WBUFW(buf,11) = fitem->bl.y;
	WBUFB(buf,13) = fitem->subx;
	WBUFB(buf,14) = fitem->suby;
	WBUFW(buf,15) = fitem->item_data.amount;

	clif_send(buf, packet_len(0x9e), &fitem->bl, AREA);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_clearflooritem(struct flooritem_data *fitem, int fd)
{
	unsigned char buf[16];

	nullpo_retr(0, fitem);

	WBUFW(buf,0) = 0xa1;
	WBUFL(buf,2) = fitem->bl.id;

	if (fd == 0) {
		clif_send(buf, packet_len(0xa1), &fitem->bl, AREA);
	} else {
		WFIFOHEAD(fd,packet_len(0xa1));
		memcpy(WFIFOP(fd,0), buf, packet_len(0xa1));
		WFIFOSET(fd,packet_len(0xa1));
	}

	return 0;
}

/*==========================================
 * make a unit (char, npc, mob, homun) disappear to one client
 * id  : the id of the unit
 * type: 0 - moved out of sight
 *       1 - died
 *       2 - logged out
 *       3 - teleported / winged away
 * fd  : the target client
 *------------------------------------------*/
int clif_clearunit_single(int id, uint8 type, int fd)
{
	WFIFOHEAD(fd, packet_len(0x80));
	WFIFOW(fd,0) = 0x80;
	WFIFOL(fd,2) = id;
	WFIFOB(fd,6) = type;
	WFIFOSET(fd, packet_len(0x80));

	return 0;
}

/*==========================================
 * make a unit (char, npc, mob, homun) disappear to all clients in area
 * type: 0 - moved out of sight
 *       1 - died
 *       2 - logged out
 *       3 - teleported / winged away
 *------------------------------------------*/
int clif_clearunit_area(struct block_list* bl, uint8 type)
{
	unsigned char buf[16];

	nullpo_retr(0, bl);

	WBUFW(buf,0) = 0x80;
	WBUFL(buf,2) = bl->id;
	WBUFB(buf,6) = type;

	clif_send(buf, packet_len(0x80), bl, type == 1 ? AREA : AREA_WOS);

	if(disguised(bl)) {
		WBUFL(buf,2) = -bl->id;
		clif_send(buf, packet_len(0x80), bl, SELF);
	}

	return 0;
}

static int clif_clearunit_delayed_sub(int tid, unsigned int tick, int id, int data)
{
	struct block_list *bl = (struct block_list *)id;
	clif_clearunit_area(bl, 0);
	aFree(bl);
	return 0;
}

int clif_clearunit_delayed(struct block_list* bl, unsigned int tick)
{
	struct block_list *tbl;
	tbl = aMalloc(sizeof (struct block_list));
	memcpy (tbl, bl, sizeof (struct block_list));
	add_timer(tick, clif_clearunit_delayed_sub, (int)tbl, 0);
	return 0;
}

void clif_get_weapon_view(struct map_session_data* sd, unsigned short *rhand, unsigned short *lhand)
{
#if PACKETVER > 3
	struct item_data *id;
#endif

	if(sd->sc.option&(OPTION_WEDDING|OPTION_XMAS|OPTION_SUMMER))
	{
		*rhand = *lhand = 0;
		return;
	}

#if PACKETVER < 4
	*rhand = sd->status.weapon;
	*lhand = sd->status.shield;
#else
	if (sd->equip_index[EQI_HAND_R] >= 0 &&
		sd->inventory_data[sd->equip_index[EQI_HAND_R]]) 
	{
		id = sd->inventory_data[sd->equip_index[EQI_HAND_R]];
		if (id->view_id > 0)
			*rhand = id->view_id;
		else
			*rhand = id->nameid;
	} else
		*rhand = 0;

	if (sd->equip_index[EQI_HAND_L] >= 0 &&
		sd->equip_index[EQI_HAND_L] != sd->equip_index[EQI_HAND_R] &&
		sd->inventory_data[sd->equip_index[EQI_HAND_L]]) 
	{
		id = sd->inventory_data[sd->equip_index[EQI_HAND_L]];
		if (id->view_id > 0)
			*lhand = id->view_id;
		else
			*lhand = id->nameid;
	} else
		*lhand = 0;
#endif
}	

/*==========================================
 *
 *------------------------------------------*/
static int clif_set0078(struct block_list* bl, unsigned char* buf)
{
	struct status_change* sc;
	struct view_data* vd;
	int guild_id, emblem_id, dir, lv;

	nullpo_retr(0, bl);
	sc = status_get_sc(bl);
	vd = status_get_viewdata(bl);

	guild_id = status_get_guild_id(bl);
	emblem_id = status_get_emblem_id(bl);
	dir = unit_getdir(bl);
	lv = status_get_lv(bl);

	if(pcdb_checkid(vd->class_))
	{ 
		struct map_session_data* sd;
		BL_CAST(BL_PC, bl, sd);

#if PACKETVER >= 7
		memset(buf,0,packet_len(0x22a));

		WBUFW(buf,0)=0x22a;
		WBUFL(buf,2)=bl->id;
		WBUFW(buf,6)=status_get_speed(bl);
		if (sc) {
			WBUFW(buf,8)=sc->opt1;
			WBUFW(buf,10)=sc->opt2;
			WBUFL(buf,12)=sc->option;
			WBUFL(buf,44)=sc->opt3;
		}
		WBUFW(buf,16)=vd->class_;
		WBUFW(buf,18)=vd->hair_style;
		WBUFW(buf,20)=vd->weapon;
		WBUFW(buf,22)=vd->shield;
		WBUFW(buf,24)=vd->head_bottom;
		WBUFW(buf,26)=vd->head_top;
		WBUFW(buf,28)=vd->head_mid;
		WBUFW(buf,30)=vd->hair_color;
		WBUFW(buf,32)=vd->cloth_color;
		WBUFW(buf,34)=sd?sd->head_dir:0;
		WBUFL(buf,36)=guild_id;
		WBUFW(buf,40)=emblem_id;
		if (sd) {
			WBUFW(buf,42)=sd->status.manner;
			WBUFB(buf,48)=sd->status.karma;
		}
		WBUFB(buf,49)=vd->sex;
		WBUFPOS(buf,50,bl->x,bl->y,dir);
		WBUFB(buf,53)=5;
		WBUFB(buf,54)=5;
		WBUFB(buf,55)=vd->dead_sit;
		WBUFW(buf,56)=clif_setlevel(lv);
		return packet_len(0x22a);
#elif PACKETVER > 3
		memset(buf,0,packet_len(0x1d8));

		WBUFW(buf,0)=0x1d8;
		WBUFL(buf,2)=bl->id;
		WBUFW(buf,6)=status_get_speed(bl);
		if (sc) {
			WBUFW(buf,8)=sc->opt1;
			WBUFW(buf,10)=sc->opt2;
			WBUFW(buf,12)=sc->option;
			WBUFW(buf,42)=sc->opt3;
		}
		WBUFW(buf,14)=vd->class_;
		WBUFW(buf,16)=vd->hair_style;
		WBUFW(buf,18)=vd->weapon;
		WBUFW(buf,20)=vd->shield;
		WBUFW(buf,22)=vd->head_bottom;
		WBUFW(buf,24)=vd->head_top;
		WBUFW(buf,26)=vd->head_mid;
		WBUFW(buf,28)=vd->hair_color;
		WBUFW(buf,30)=vd->cloth_color;
		WBUFW(buf,32)=sd?sd->head_dir:0;
		WBUFL(buf,34)=guild_id;
		WBUFW(buf,38)=emblem_id;
		if (sd) {
			WBUFW(buf,40)=sd->status.manner;
			WBUFB(buf,44)=sd->status.karma;
		}
		WBUFB(buf,45)=vd->sex;
		WBUFPOS(buf,46,bl->x,bl->y,dir);
		WBUFB(buf,49)=5;
		WBUFB(buf,50)=5;
		WBUFB(buf,51)=vd->dead_sit;
		WBUFW(buf,52)=clif_setlevel(lv);
		return packet_len(0x1d8);
#else
		memset(buf,0,packet_len(0x78));

		WBUFW(buf,0)=0x78;
		WBUFL(buf,2)=bl->id;
		WBUFW(buf,6)=status_get_speed(bl);
		if (sc) {
			WBUFW(buf,8)=sc->opt1;
			WBUFW(buf,10)=sc->opt2;
			WBUFW(buf,12)=sc->option;
			WBUFW(buf,42)=sc->opt3;
		}
		WBUFW(buf,14)=vd->class_;
		WBUFW(buf,16)=vd->hair_style;
		WBUFW(buf,18)=vd->weapon;
		WBUFW(buf,20)=vd->head_bottom;
		WBUFW(buf,22)=vd->shield;
		WBUFW(buf,24)=vd->head_top;
		WBUFW(buf,26)=vd->head_mid;
		WBUFW(buf,28)=vd->hair_color;
		WBUFW(buf,30)=vd->cloth_color;
		WBUFW(buf,32)=sd?sd->head_dir:0;
		WBUFL(buf,34)=guild_id;
		WBUFW(buf,38)=emblem_id;
		if (sd) {
			WBUFW(buf,40)=sd->status.manner;
			WBUFB(buf,44)=sd->status.karma;
		}
		WBUFB(buf,45)=vd->sex;
		WBUFPOS(buf,46,bl->x,bl->y,dir);
		WBUFB(buf,49)=5;
		WBUFB(buf,50)=5;
		WBUFB(buf,51)=vd->dead_sit;
		WBUFW(buf,52)=clif_setlevel(lv);
		return packet_len(0x78);
#endif
	}
	//Non-player sprites need just a few fields filled.
	memset(buf,0,packet_len(0x78));

	WBUFW(buf,0)=0x78;
	WBUFL(buf,2)=bl->id;
	WBUFW(buf,6)=status_get_speed(bl);
	if (sc) {
		WBUFW(buf,8)=sc->opt1;
		WBUFW(buf,10)=sc->opt2;
		WBUFW(buf,12)=sc->option;
		WBUFW(buf,42)=sc->opt3;
	}
	WBUFW(buf,14)=vd->class_;
	WBUFW(buf,16)=vd->hair_style; //Required for pets (removes attack cursor)
	//18W: Weapon
	WBUFW(buf,20)=vd->head_bottom; //Pet armor (ignored by client)
	if (bl->type == BL_NPC && vd->class_ == FLAG_CLASS)
	{	//The hell, why flags work like this?
		WBUFL(buf,22)=emblem_id;
		WBUFL(buf,26)=guild_id;
	}
	//22W: shield
	//24W: Head top
	//26W: Head mid
	//28W: Hair color
	//30W: Clothes color
	//32W: Head dir
	WBUFL(buf,34)=guild_id;
	WBUFW(buf,38)=emblem_id;
	//40W: Manner
	//44B: Karma
	//45B: Sex
	WBUFPOS(buf,46,bl->x,bl->y,dir);
	//49B: ???
	//50B: ???
	//51B: Sit/Stand
	WBUFW(buf,52)=clif_setlevel(lv);
	return packet_len(0x78);
}

/*==========================================
 * Prepares 'unit walking' packet
 *------------------------------------------*/
static int clif_set007b(struct block_list *bl, struct view_data *vd, struct unit_data *ud, unsigned char *buf)
{
	struct status_change *sc;
	int guild_id, emblem_id, lv;

	nullpo_retr(0, bl);
	sc = status_get_sc(bl);
	
	guild_id = status_get_guild_id(bl);
	emblem_id = status_get_emblem_id(bl);
	lv = status_get_lv(bl);
	
	if(pcdb_checkid(vd->class_))
	{ 
		struct map_session_data* sd;
		BL_CAST(BL_PC, bl, sd);

#if PACKETVER >= 7
		memset(buf,0,packet_len(0x22c));

		WBUFW(buf,0)=0x22c;
		WBUFL(buf,2)=bl->id;
		WBUFW(buf,6)=status_get_speed(bl);
		if (sc) {
			WBUFW(buf,8)= sc->opt1;
			WBUFW(buf,10)= sc->opt2;
			WBUFL(buf,12)= sc->option;
			WBUFL(buf,48)= sc->opt3;
		}
		WBUFW(buf,16)=vd->class_;
		WBUFW(buf,18)=vd->hair_style;
		WBUFW(buf,20)=vd->weapon;
		WBUFW(buf,22)=vd->shield;
		WBUFW(buf,24)=vd->head_bottom;
		WBUFL(buf,26)=gettick();
		WBUFW(buf,30)=vd->head_top;
		WBUFW(buf,32)=vd->head_mid;
		WBUFW(buf,34)=vd->hair_color;
		WBUFW(buf,36)=vd->cloth_color;
		WBUFW(buf,38)=sd?sd->head_dir:0;
		WBUFL(buf,40)=guild_id;
		WBUFW(buf,44)=emblem_id;
		if (sd) {
			WBUFW(buf,46)=sd->status.manner;
			WBUFB(buf,52)=sd->status.karma;
		}
		WBUFB(buf,53)=vd->sex;
		WBUFPOS2(buf,54,bl->x,bl->y,ud->to_x,ud->to_y,8,8);
		WBUFB(buf,60)=5;
		WBUFB(buf,61)=5;
		WBUFW(buf,62)=clif_setlevel(lv);

		return packet_len(0x22c);	
#elif PACKETVER > 3
		memset(buf,0,packet_len(0x1da));

		WBUFW(buf,0)=0x1da;
		WBUFL(buf,2)=bl->id;
		WBUFW(buf,6)=status_get_speed(bl);
		if (sc) {
			WBUFW(buf,8)=sc->opt1;
			WBUFW(buf,10)=sc->opt2;
			WBUFW(buf,12)=sc->option;
			WBUFW(buf,46)=sc->opt3;
		}
		WBUFW(buf,14)=vd->class_;
		WBUFW(buf,16)=vd->hair_style;
		WBUFW(buf,18)=vd->weapon;
		WBUFW(buf,20)=vd->shield;
		WBUFW(buf,22)=vd->head_bottom;
		WBUFL(buf,24)=gettick();
		WBUFW(buf,28)=vd->head_top;
		WBUFW(buf,30)=vd->head_mid;
		WBUFW(buf,32)=vd->hair_color;
		WBUFW(buf,34)=vd->cloth_color;
		WBUFW(buf,36)=sd?sd->head_dir:0;
		WBUFL(buf,38)=guild_id;
		WBUFW(buf,42)=emblem_id;
		if (sd) {
			WBUFW(buf,44)=sd->status.manner;
			WBUFB(buf,48)=sd->status.karma;
		}
		WBUFB(buf,49)=vd->sex;
		WBUFPOS2(buf,50,bl->x,bl->y,ud->to_x,ud->to_y,8,8);
		WBUFB(buf,56)=5;
		WBUFB(buf,57)=5;
		WBUFW(buf,58)=clif_setlevel(lv);

		return packet_len(0x1da);
#else
		memset(buf,0,packet_len(0x7b));

		WBUFW(buf,0)=0x7b;
		WBUFL(buf,2)=bl->id;
		WBUFW(buf,6)=status_get_speed(bl);
		if (sc) {
			WBUFW(buf,8)=sc->opt1;
			WBUFW(buf,10)=sc->opt2;
			WBUFW(buf,12)=sc->option;
			WBUFW(buf,46)=sc->opt3;
		}
		WBUFW(buf,14)=vd->class_;
		WBUFW(buf,16)=vd->hair_style;
		WBUFW(buf,18)=vd->weapon;
		WBUFW(buf,20)=vd->head_bottom;
		WBUFL(buf,22)=gettick();
		WBUFW(buf,26)=vd->shield;
		WBUFW(buf,28)=vd->head_top;
		WBUFW(buf,30)=vd->head_mid;
		WBUFW(buf,32)=vd->hair_color;
		WBUFW(buf,34)=vd->cloth_color;
		WBUFW(buf,36)=sd?sd->head_dir:0;
		WBUFL(buf,38)=guild_id;
		WBUFW(buf,42)=emblem_id;
		if (sd) {
			WBUFW(buf,44)=sd->status.manner;
			WBUFB(buf,48)=sd->status.karma;
		}
		WBUFB(buf,49)=vd->sex;
		WBUFPOS2(buf,50,bl->x,bl->y,ud->to_x,ud->to_y,8,8);
		WBUFB(buf,56)=5;
		WBUFB(buf,57)=5;
		WBUFW(buf,58)=clif_setlevel(lv);

		return packet_len(0x7b);
#endif
	}
	//Non-player sprites only require a few fields.
#if PACKETVER >= 7
	memset(buf,0,packet_len(0x22c));

	WBUFW(buf,0)=0x22c;
	WBUFL(buf,2)=bl->id;
	WBUFW(buf,6)=status_get_speed(bl);
	if (sc) {
		WBUFW(buf,8)=sc->opt1;
		WBUFW(buf,10)=sc->opt2;
		WBUFL(buf,12)=sc->option;
		WBUFL(buf,48)=sc->opt3;
	}
	WBUFW(buf,16)=vd->class_;
	WBUFW(buf,18)=vd->hair_style; //Required for pets (removes attack cursor)
	//20L: Weapon/Shield
	WBUFW(buf,24)=vd->head_bottom; //Pet armor
	WBUFL(buf,26)=gettick();
	//30W: Head top
	//32W: Head mid
	//34W: Hair color
	//36W: Cloth color
 	//38W: Head dir
	WBUFL(buf,40)=guild_id;
	WBUFW(buf,44)=emblem_id;
	//46W: Manner
	//52B: Karma
	//53B: Sex
	WBUFPOS2(buf,54,bl->x,bl->y,ud->to_x,ud->to_y,8,8);
	//60B: ???
	//61B: ???
	WBUFW(buf,62)=clif_setlevel(lv);
	return packet_len(0x22c);
#else
	memset(buf,0,packet_len(0x7b));

	WBUFW(buf,0)=0x7b;
	WBUFL(buf,2)=bl->id;
	WBUFW(buf,6)=status_get_speed(bl);
	if (sc) {
		WBUFW(buf,8)=sc->opt1;
		WBUFW(buf,10)=sc->opt2;
		WBUFW(buf,12)=sc->option;
		WBUFW(buf,46)=sc->opt3;
	}
	WBUFW(buf,14)=vd->class_;
	WBUFW(buf,16)=vd->hair_style; //Required for pets (removes attack cursor)
	//18W: Weapon
	WBUFW(buf,20)=vd->head_bottom; //Pet armor
	WBUFL(buf,22)=gettick();
	//26W: Shield
	//28W: Head top
	//30W: Head mid
	//32W: Hair color
	//34W: Cloth color
 	//36W: Head dir
	WBUFL(buf,38)=guild_id;
	WBUFW(buf,42)=emblem_id;
	//44W: Manner
	//46W: Opt3
	//48B: Karma
	//49B: Sex
	WBUFPOS2(buf,50,bl->x,bl->y,ud->to_x,ud->to_y,8,8);
	WBUFB(buf,56)=5;
	WBUFB(buf,57)=5;
	WBUFW(buf,58)=clif_setlevel(lv);
	return packet_len(0x7b);
#endif
}

//Modifies the buffer for disguise characters and sends it to self.
//Flag = 0: change id to negative, buf will have disguise data.
//Flag = 1: change id to positive, class and option to make your own char invisible.
//Luckily, the offsets that need to be changed are the same in packets 0x78, 0x7b, 0x1d8 and 0x1da
//But no longer holds true for those packet of PACKETVER 7.
static void clif_setdisguise(struct map_session_data *sd, unsigned char *buf,int len, int flag)
{
	if (flag) {
		WBUFL(buf,2)=sd->bl.id;
#if PACKETVER >= 7
		switch (WBUFW(buf,0)) {
		case 0x22c:
		case 0x22b:
		case 0x22a:
			WBUFL(buf,12)=OPTION_INVISIBLE;
			WBUFW(buf,16)=sd->status.class_;
			break;
		default:
#endif
			WBUFW(buf,12)=OPTION_INVISIBLE;
			WBUFW(buf,14)=sd->status.class_;
#if PACKETVER >= 7
			break;
		}
#endif
	} else {
		WBUFL(buf,2)=-sd->bl.id;
	}
	clif_send(buf, len, &sd->bl, SELF);
}

/*==========================================
 * �N���X�`�F���W type��Mob�̏ꍇ��1�ő���0�H
 *------------------------------------------*/
int clif_class_change(struct block_list *bl,int class_,int type)
{
	unsigned char buf[16];

	nullpo_retr(0, bl);

	if(!pcdb_checkid(class_)) {
		WBUFW(buf,0)=0x1b0;
		WBUFL(buf,2)=bl->id;
		WBUFB(buf,6)=type;
		WBUFL(buf,7)=class_;
		clif_send(buf,packet_len(0x1b0),bl,AREA);
	}
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
static void clif_spiritball_single(int fd, struct map_session_data *sd)
{
	WFIFOHEAD(fd, packet_len(0x1e1));
	WFIFOW(fd,0)=0x1e1;
	WFIFOL(fd,2)=sd->bl.id;
	WFIFOW(fd,6)=sd->spiritball;
	WFIFOSET(fd, packet_len(0x1e1));
}

// new and improved weather display [Valaris]
static void clif_weather_sub(int fd, int id, int type)
{
	WFIFOHEAD(fd,packet_len(0x1f3));
	WFIFOW(fd,0) = 0x1f3;
	WFIFOL(fd,2) = id;
	WFIFOL(fd,6) = type;
	WFIFOSET(fd,packet_len(0x1f3));
}

/*==========================================
 *
 *------------------------------------------*/
static void clif_weather_check(struct map_session_data *sd)
{
	int m = sd->bl.m, fd = sd->fd;
	
	if (map[m].flag.snow
		|| map[m].flag.clouds
		|| map[m].flag.fog
		|| map[m].flag.fireworks
		|| map[m].flag.sakura
		|| map[m].flag.leaves
		|| map[m].flag.rain
		|| map[m].flag.clouds2)
	{
		if (map[m].flag.snow)
			clif_weather_sub(fd, sd->bl.id, 162);
		if (map[m].flag.clouds)
			clif_weather_sub(fd, sd->bl.id, 233);
		if (map[m].flag.clouds2)
			clif_weather_sub(fd, sd->bl.id, 516);
		if (map[m].flag.fog)
			clif_weather_sub(fd, sd->bl.id, 515);
		if (map[m].flag.fireworks) {
			clif_weather_sub(fd, sd->bl.id, 297);
			clif_weather_sub(fd, sd->bl.id, 299);
			clif_weather_sub(fd, sd->bl.id, 301);
		}
		if (map[m].flag.sakura)
			clif_weather_sub(fd, sd->bl.id, 163);
		if (map[m].flag.leaves)
			clif_weather_sub(fd, sd->bl.id, 333);
		if (map[m].flag.rain)
			clif_weather_sub(fd, sd->bl.id, 161);
	}
}

int clif_weather(int m)
{
	int i;

	struct map_session_data *sd=NULL;

	for(i = 0; i < fd_max; i++) {
		if (session[i] && session[i]->func_parse == clif_parse &&	
			(sd = session[i]->session_data) != NULL &&
		  	sd->state.auth && sd->bl.m == m) {
			clif_weather_check(sd);
		}
	}

	return 0;
}

int clif_spawn(struct block_list *bl)
{
	unsigned char buf[128];
	struct view_data *vd;

	nullpo_retr(0, bl);
	vd = status_get_viewdata(bl);
	if (!vd || vd->class_ == INVISIBLE_CLASS)
		return 0;

	if (pcdb_checkid(vd->class_))
	{	// player spawn packet
		clif_set0078(bl, buf);
		switch(WBUFW(buf,0))
		{
			case 0x78: //Convert to 0x79
				WBUFW(buf, 0) = 0x79;
				WBUFW(buf,51) = WBUFW(buf,52); //Lv is placed on offset 52
				clif_send(buf, packet_len(0x79), bl, AREA_WOS);
				if (disguised(bl))
					clif_setdisguise((TBL_PC*)bl, buf, packet_len(0x79), 0);
				break;
#if PACKETVER > 3
			case 0x1d8: //Convert to 0x1d9
				WBUFW(buf, 0) = 0x1d9;
				WBUFW(buf,51) = WBUFW(buf,52); //Lv is placed on offset 52
				clif_send(buf, packet_len(0x1d9), bl, AREA_WOS);
				if (disguised(bl))
					clif_setdisguise((TBL_PC*)bl, buf, packet_len(0x1d9), 0);
				break;
#endif
#if PACKETVER >= 7
			case 0x22a: //Convert to 0x22b
				WBUFW(buf, 0) = 0x22b;
				WBUFW(buf,55) = WBUFW(buf,56); //Lv is placed on offset 56
				clif_send(buf, packet_len(0x22b), bl, AREA_WOS);
				if (disguised(bl))
					clif_setdisguise((TBL_PC*)bl, buf, packet_len(0x22b), 0);
				break;
#endif
		}
	} else {	// npc/mob/pet/homun spawn packet
		struct status_change *sc = status_get_sc(bl);
		memset(buf,0,sizeof(buf));
		WBUFW(buf,0)=0x7c;
		WBUFL(buf,2)=bl->id;
		WBUFW(buf,6)=status_get_speed(bl);
		if (sc) {
			WBUFW(buf,8)=sc->opt1;
			WBUFW(buf,10)=sc->opt2;
			WBUFW(buf,12)=sc->option;
		}
		WBUFW(buf,14)=vd->hair_style; //Required for pets (removes attack cursor)
		//16W: Weapon
		WBUFW(buf,18)=vd->head_bottom; //Pet armor (ignored by client)
		WBUFW(buf,20)=vd->class_;
		//22W: Shield
		//24W: Head top
		//26W: Head mid
		//28W: Hair color
		//30W: Cloth color
		//32W: Head dir
		//34B: karma
		//35B: Sex
		WBUFPOS(buf,36,bl->x,bl->y,unit_getdir(bl));
		//39B: ???
		//40B: ???
		clif_send(buf,packet_len(0x7c),bl,AREA_WOS);
		if (disguised(bl)) {
			WBUFL(buf,2)=-bl->id;
			clif_send(buf,packet_len(0x7c),bl,SELF);
		}
	}
	
	if (vd->cloth_color)
		clif_refreshlook(bl,bl->id,LOOK_CLOTHES_COLOR,vd->cloth_color,AREA_WOS);
		
	switch (bl->type)
	{
	case BL_PC:
		{
			TBL_PC *sd = ((TBL_PC*)bl);
			if (sd->spiritball > 0)
				clif_spiritball(sd);
			if(sd->state.size==2) // tiny/big players [Valaris]
				clif_specialeffect(bl,423,AREA);
			else if(sd->state.size==1)
				clif_specialeffect(bl,421,AREA);
		}
	break;
	case BL_MOB:
		{
			TBL_MOB *md = ((TBL_MOB*)bl);
			if(md->special_state.size==2) // tiny/big mobs [Valaris]
				clif_specialeffect(&md->bl,423,AREA);
			else if(md->special_state.size==1)
				clif_specialeffect(&md->bl,421,AREA);
		}
	break;
	case BL_PET:
		if (vd->head_bottom)
			clif_pet_equip_area((TBL_PET*)bl); // needed to display pet equip properly
		break;
	}
	return 0;
}

//[orn]
int clif_hominfo(struct map_session_data *sd, struct homun_data *hd, int flag)
{
	struct status_data *status;
	unsigned char buf[128];
	
	nullpo_retr(0, hd);

	status = &hd->battle_status;
	memset(buf,0,packet_len(0x22e));
	WBUFW(buf,0)=0x22e;
	memcpy(WBUFP(buf,2),hd->homunculus.name,NAME_LENGTH);
	// Bit field, bit 0 : rename_flag (1 = already renamed), bit 1 : homunc vaporized (1 = true), bit 2 : homunc dead (1 = true)
	WBUFB(buf,26)=(battle_config.hom_rename?0:hd->homunculus.rename_flag) | (hd->homunculus.vaporize << 1) | (hd->homunculus.hp?0:4);
	WBUFW(buf,27)=hd->homunculus.level;
	WBUFW(buf,29)=hd->homunculus.hunger;
	WBUFW(buf,31)=(unsigned short) (hd->homunculus.intimacy / 100) ;
	WBUFW(buf,33)=0; // equip id
	WBUFW(buf,35)=cap_value(status->rhw.atk2+status->batk, 0, SHRT_MAX);
	WBUFW(buf,37)=cap_value(status->matk_max, 0, SHRT_MAX);
	WBUFW(buf,39)=status->hit;
	if (battle_config.hom_setting&0x10)
		WBUFW(buf,41)=status->luk/3 + 1;	//crit is a +1 decimal value! Just display purpose.[Vicious]
	else
		WBUFW(buf,41)=status->cri/10;
	WBUFW(buf,43)=status->def + status->vit ;
	WBUFW(buf,45)=status->mdef;
	WBUFW(buf,47)=status->flee;
	WBUFW(buf,49)=(flag)?0:status->amotion;
	if (status->max_hp > SHRT_MAX) {
		WBUFW(buf,51) = status->hp/(status->max_hp/100);
		WBUFW(buf,53) = 100;
	} else {
		WBUFW(buf,51)=status->hp;
		WBUFW(buf,53)=status->max_hp;
	}
	if (status->max_sp > SHRT_MAX) {
		WBUFW(buf,55) = status->sp/(status->max_sp/100);
		WBUFW(buf,57) = 100;
	} else {
		WBUFW(buf,55)=status->sp;
		WBUFW(buf,57)=status->max_sp;
	}
	WBUFL(buf,59)=hd->homunculus.exp;
	WBUFL(buf,63)=hd->exp_next;
	WBUFW(buf,67)=hd->homunculus.skillpts;
	WBUFW(buf,69)=1; // FIXME: Attackable? When exactly is a homun not attackable? [Skotlex]
	clif_send(buf,packet_len(0x22e),&sd->bl,SELF);
	return 0;
}

void clif_send_homdata(struct map_session_data *sd, int type, int param)
{	//[orn]
	int fd = sd->fd;
	WFIFOHEAD(fd, packet_len(0x230));
	nullpo_retv(sd->hd);
	WFIFOW(fd,0)=0x230;
	WFIFOW(fd,2)=type;
	WFIFOL(fd,4)=sd->hd->bl.id;
	WFIFOL(fd,8)=param;
	WFIFOSET(fd,packet_len(0x230));

	return;
}

int clif_homskillinfoblock(struct map_session_data *sd)
{	//[orn]
	struct homun_data *hd;
	int fd = sd->fd;
	int i,j,len=4,id;
	WFIFOHEAD(fd, 4+37*MAX_HOMUNSKILL);

	hd = sd->hd;
	if ( !hd ) 
		return 0 ;

	WFIFOW(fd,0)=0x235;
	for ( i = 0; i < MAX_HOMUNSKILL; i++){
		if( (id = hd->homunculus.hskill[i].id) != 0 ){
			j = id - HM_SKILLBASE - 1 ;
			WFIFOW(fd,len  ) = id ;
			WFIFOW(fd,len+2) = skill_get_inf(id) ;
			WFIFOW(fd,len+4) = 0 ;
			WFIFOW(fd,len+6) = hd->homunculus.hskill[j].lv ;
			WFIFOW(fd,len+8) = skill_get_sp(id,hd->homunculus.hskill[j].lv) ;
			WFIFOW(fd,len+10)= skill_get_range2(&sd->hd->bl, id,hd->homunculus.hskill[j].lv) ;
//			strncpy((char*)WFIFOP(fd,len+12), skill_get_name(id), NAME_LENGTH) ;
			WFIFOB(fd,len+36) = (hd->homunculus.hskill[j].lv < merc_skill_tree_get_max(id, hd->homunculus.class_))?1:0;
			len+=37;
		}
	}
	WFIFOW(fd,2)=len;
	WFIFOSET(fd,len);

	return 0;
}

void clif_homskillup(struct map_session_data *sd, int skill_num)
{	//[orn]
	struct homun_data *hd;
	int fd=sd->fd, skillid;
	WFIFOHEAD(fd, packet_len(0x239));
	nullpo_retv(sd);
	skillid = skill_num - HM_SKILLBASE - 1;

	hd=sd->hd;

	WFIFOW(fd,0) = 0x239;
	WFIFOW(fd,2) = skill_num;
	WFIFOW(fd,4) = hd->homunculus.hskill[skillid].lv;
	WFIFOW(fd,6) = skill_get_sp(skill_num,hd->homunculus.hskill[skillid].lv);
	WFIFOW(fd,8) = skill_get_range2(&hd->bl, skill_num,hd->homunculus.hskill[skillid].lv);
	WFIFOB(fd,10) = (hd->homunculus.hskill[skillid].lv < skill_get_max(hd->homunculus.hskill[skillid].id)) ? 1 : 0;
	WFIFOSET(fd,packet_len(0x239));

	return;
}

int clif_hom_food(struct map_session_data *sd,int foodid,int fail)	//[orn]
{
	int fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x22f));
	WFIFOW(fd,0)=0x22f;
	WFIFOB(fd,2)=fail;
	WFIFOW(fd,3)=foodid;
	WFIFOSET(fd,packet_len(0x22f));

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_walkok(struct map_session_data *sd)
{
	int fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0x87));
	WFIFOW(fd,0)=0x87;
	WFIFOL(fd,2)=gettick();
	WFIFOPOS2(fd,6,sd->bl.x,sd->bl.y,sd->ud.to_x,sd->ud.to_y,8,8);
	WFIFOSET(fd,packet_len(0x87));

	return 0;
}

static void clif_move2(struct block_list *bl, struct view_data *vd, struct unit_data *ud)
{
	unsigned char buf[256];
	int len;
	
	len = clif_set007b(bl,vd,ud,buf);
	clif_send(buf,len,bl,AREA_WOS);
	if (disguised(bl))
		clif_setdisguise((TBL_PC*)bl, buf, len, 0);
		
	if(vd->cloth_color)
		clif_refreshlook(bl,bl->id,LOOK_CLOTHES_COLOR,vd->cloth_color,AREA_WOS);

	switch(bl->type)
	{
	case BL_PC:
		{
			TBL_PC *sd = ((TBL_PC*)bl);
//			clif_movepc(sd);
			if(sd->state.size==2) // tiny/big players [Valaris]
				clif_specialeffect(&sd->bl,423,AREA);
			else if(sd->state.size==1)
				clif_specialeffect(&sd->bl,421,AREA);
		}
		break;
	case BL_MOB:
		{
			TBL_MOB *md = ((TBL_MOB*)bl);
			if(md->special_state.size==2) // tiny/big mobs [Valaris]
				clif_specialeffect(&md->bl,423,AREA);
			else if(md->special_state.size==1)
				clif_specialeffect(&md->bl,421,AREA);
		}
		break;
	}
	return;
}

/// Move the unit (does nothing if the client has no info about the unit)
/// Note: unit must not be self
void clif_move(struct unit_data *ud)
{
	unsigned char buf[16];
	struct view_data* vd;
	struct block_list* bl = ud->bl;
	vd = status_get_viewdata(bl);
	if (!vd || vd->class_ == INVISIBLE_CLASS)
		return; //This performance check is needed to keep GM-hidden objects from being notified to bots.
	
	if (ud->state.speed_changed) {
		// Since we don't know how to update the speed of other objects,
		// use the old walk packet to update the data.
		ud->state.speed_changed = 0;
		clif_move2(bl, vd, ud);
		return;
	}

	WBUFW(buf,0)=0x86;
	WBUFL(buf,2)=bl->id;
	WBUFPOS2(buf,6,bl->x,bl->y,ud->to_x,ud->to_y,8,8);
	WBUFL(buf,12)=gettick();
	clif_send(buf, 16, bl, AREA_WOS);
	if (disguised(bl))
		clif_setdisguise((TBL_PC*)bl, buf, 16, 0);
}

/*==========================================
 * Delays the map_quit of a player after they are disconnected. [Skotlex]
 *------------------------------------------*/
static int clif_delayquit(int tid, unsigned int tick, int id, int data)
{
	struct map_session_data *sd = NULL;

	//Remove player from map server
	if ((sd = map_id2sd(id)) != NULL && sd->fd == 0) //Should be a disconnected player.
		map_quit(sd);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
void clif_quitsave(int fd,struct map_session_data *sd)
{
	if (sd->state.waitingdisconnect || //Was already waiting to be disconnected.
		!battle_config.prevent_logout ||
	  	DIFF_TICK(gettick(), sd->canlog_tick) > battle_config.prevent_logout)
		map_quit(sd);
	else if (sd->fd)
	{	//Disassociate session from player (session is deleted after this function was called)
		//And set a timer to make him quit later.
		session[sd->fd]->session_data = NULL;
		sd->fd = 0;
		add_timer(gettick() + 10000, clif_delayquit, sd->bl.id, 0);
	}
}

/*==========================================
 *
 *------------------------------------------*/
static int clif_waitclose(int tid, unsigned int tick, int id, int data)
{
	if (session[id] && session[id]->func_parse == clif_parse) //Avoid disconnecting non-players, as pointed out by End of Exam [Skotlex]
		set_eof(id);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
void clif_setwaitclose(int fd)
{
	struct map_session_data *sd;

	// if player is not already in the game (double connection probably)
	if ((sd = (struct map_session_data*)session[fd]->session_data) == NULL) {
		// limited timer, just to send information.
		add_timer(gettick() + 1000, clif_waitclose, fd, 0);
	} else
		add_timer(gettick() + 5000, clif_waitclose, fd, 0);
}

/*==========================================
 *
 *------------------------------------------*/
void clif_changemap(struct map_session_data *sd, short map, int x, int y)
{
	int fd;
	nullpo_retv(sd);
	fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x91));
	WFIFOW(fd,0) = 0x91;
	mapindex_getmapname_ext(mapindex_id2name(map), (char*)WFIFOP(fd,2));
	WFIFOW(fd,18) = x;
	WFIFOW(fd,20) = y;
	WFIFOSET(fd,packet_len(0x91));
}

/*==========================================
 * Tells the client to connect to another map-server
 *------------------------------------------*/
void clif_changemapserver(struct map_session_data* sd, unsigned short map_index, int x, int y, uint32 ip, uint16 port)
{
	int fd;
	nullpo_retv(sd);
	fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x92));
	WFIFOW(fd,0) = 0x92;
	mapindex_getmapname_ext(mapindex_id2name(map_index), (char*)WFIFOP(fd,2));
	WFIFOW(fd,18) = x;
	WFIFOW(fd,20) = y;
	WFIFOL(fd,22) = htonl(ip);
	WFIFOW(fd,26) = ntows(htons(port)); // [!] LE byte order here [!]
	WFIFOSET(fd,packet_len(0x92));
}

int clif_blown(struct block_list *bl)
{
//Aegis packets says fixpos, but it's unsure whether slide works better or not.
//	return clif_fixpos(bl);
	return clif_slide(bl, bl->x, bl->y);
}
/*==========================================
 *
 *------------------------------------------*/
int clif_fixpos(struct block_list *bl)
{
	unsigned char buf[16];

	nullpo_retr(0, bl);

	WBUFW(buf,0)=0x88;
	WBUFL(buf,2)=bl->id;
	WBUFW(buf,6)=bl->x;
	WBUFW(buf,8)=bl->y;
	clif_send(buf, packet_len(0x88), bl, AREA);
	if (disguised(bl)) {
		WBUFL(buf,2)=-bl->id;
		clif_send(buf, packet_len(0x88), bl, SELF);
	}
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_npcbuysell(struct map_session_data* sd, int id)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0xc4));
	WFIFOW(fd,0)=0xc4;
	WFIFOL(fd,2)=id;
	WFIFOSET(fd,packet_len(0xc4));

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_buylist(struct map_session_data *sd, struct npc_data *nd)
{
	struct item_data *id;
	int fd,i,val;

	nullpo_retr(0, sd);
	nullpo_retr(0, nd);

	fd=sd->fd;
 	WFIFOHEAD(fd, 200 * 11 + 4);
	WFIFOW(fd,0)=0xc6;
	for(i=0;nd->u.shop_item[i].nameid > 0;i++){
		id = itemdb_search(nd->u.shop_item[i].nameid);
		val=nd->u.shop_item[i].value;
		WFIFOL(fd,4+i*11)=val;
		if (!id->flag.value_notdc)
			val=pc_modifybuyvalue(sd,val);
		WFIFOL(fd,8+i*11)=val;
		WFIFOB(fd,12+i*11)=itemtype(id->type);
		if (id->view_id > 0)
			WFIFOW(fd,13+i*11)=id->view_id;
		else
			WFIFOW(fd,13+i*11)=nd->u.shop_item[i].nameid;
	}
	WFIFOW(fd,2)=i*11+4;
	WFIFOSET(fd,WFIFOW(fd,2));

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_selllist(struct map_session_data *sd)
{
	int fd,i,c=0,val;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd, MAX_INVENTORY * 10 + 4);
	WFIFOW(fd,0)=0xc7;
	for(i=0;i<MAX_INVENTORY;i++) {
		if(sd->status.inventory[i].nameid > 0 && sd->inventory_data[i]) {
			if (!itemdb_cansell(&sd->status.inventory[i], pc_isGM(sd)))
				continue;

			val=sd->inventory_data[i]->value_sell;
			if (val < 0)
				continue;
			WFIFOW(fd,4+c*10)=i+2;
			WFIFOL(fd,6+c*10)=val;
			if (!sd->inventory_data[i]->flag.value_notoc)
				val=pc_modifysellvalue(sd,val);
			WFIFOL(fd,10+c*10)=val;
			c++;
		}
	}
	WFIFOW(fd,2)=c*10+4;
	WFIFOSET(fd,WFIFOW(fd,2));

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_scriptmes(struct map_session_data *sd, int npcid, const char *mes)
{
	int fd = sd->fd;
	int slen = strlen(mes) + 9;
	WFIFOHEAD(fd, slen);
	WFIFOW(fd,0)=0xb4;
	WFIFOW(fd,2)=slen;
	WFIFOL(fd,4)=npcid;
	strcpy((char*)WFIFOP(fd,8),mes);
	WFIFOSET(fd,WFIFOW(fd,2));

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_scriptnext(struct map_session_data *sd,int npcid)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0xb5));
	WFIFOW(fd,0)=0xb5;
	WFIFOL(fd,2)=npcid;
	WFIFOSET(fd,packet_len(0xb5));

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_scriptclose(struct map_session_data *sd, int npcid)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0xb6));
	WFIFOW(fd,0)=0xb6;
	WFIFOL(fd,2)=npcid;
	WFIFOSET(fd,packet_len(0xb6));

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
void clif_sendfakenpc(struct map_session_data *sd, int npcid)
{
	int fd = sd->fd;
	WFIFOHEAD(fd, packet_len(0x78));
	sd->state.using_fake_npc = 1;
	memset(WFIFOP(fd,0), 0, packet_len(0x78));
	WFIFOW(fd,0)=0x78;
	WFIFOL(fd,2)=npcid;
	WFIFOW(fd,14)=111;
	WFIFOPOS(fd,46,sd->bl.x,sd->bl.y,sd->ud.dir);
	WFIFOB(fd,49)=5;
	WFIFOB(fd,50)=5;
	WFIFOSET(fd, packet_len(0x78));
	return;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_scriptmenu(struct map_session_data* sd, int npcid, const char* mes)
{
	int fd = sd->fd;
	int slen = strlen(mes) + 8;
	struct block_list *bl = NULL;

	if (!sd->state.using_fake_npc && (npcid == fake_nd->bl.id || ((bl = map_id2bl(npcid)) && (bl->m!=sd->bl.m ||
	   bl->x<sd->bl.x-AREA_SIZE-1 || bl->x>sd->bl.x+AREA_SIZE+1 ||
	   bl->y<sd->bl.y-AREA_SIZE-1 || bl->y>sd->bl.y+AREA_SIZE+1))))
	   clif_sendfakenpc(sd, npcid);

	WFIFOHEAD(fd, slen);
	WFIFOW(fd,0)=0xb7;
	WFIFOW(fd,2)=slen;
	WFIFOL(fd,4)=npcid;
	strcpy((char*)WFIFOP(fd,8),mes);
	WFIFOSET(fd,WFIFOW(fd,2));
	
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_scriptinput(struct map_session_data *sd, int npcid)
{
	int fd;
	struct block_list *bl = NULL;

	nullpo_retr(0, sd);

	if (!sd->state.using_fake_npc && (npcid == fake_nd->bl.id || ((bl = map_id2bl(npcid)) && (bl->m!=sd->bl.m ||
	   bl->x<sd->bl.x-AREA_SIZE-1 || bl->x>sd->bl.x+AREA_SIZE+1 ||
	   bl->y<sd->bl.y-AREA_SIZE-1 || bl->y>sd->bl.y+AREA_SIZE+1))))
	   clif_sendfakenpc(sd, npcid);
	
	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0x142));
	WFIFOW(fd,0)=0x142;
	WFIFOL(fd,2)=npcid;
	WFIFOSET(fd,packet_len(0x142));

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_scriptinputstr(struct map_session_data *sd, int npcid)
{
	int fd;
	struct block_list *bl = NULL;

	nullpo_retr(0, sd);

	if (!sd->state.using_fake_npc && (npcid == fake_nd->bl.id || ((bl = map_id2bl(npcid)) && (bl->m!=sd->bl.m ||
	   bl->x<sd->bl.x-AREA_SIZE-1 || bl->x>sd->bl.x+AREA_SIZE+1 ||
	   bl->y<sd->bl.y-AREA_SIZE-1 || bl->y>sd->bl.y+AREA_SIZE+1))))
	   clif_sendfakenpc(sd, npcid);

	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0x1d4));
	WFIFOW(fd,0)=0x1d4;
	WFIFOL(fd,2)=npcid;
	WFIFOSET(fd,packet_len(0x1d4));
	
	return 0;
}

/// npc_id is ignored in the client
/// type=2     : Remove viewpoint
/// type=other : Show viewpoint
int clif_viewpoint(struct map_session_data *sd, int npc_id, int type, int x, int y, int id, int color)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0x144));
	WFIFOW(fd,0)=0x144;
	WFIFOL(fd,2)=npc_id;
	WFIFOL(fd,6)=type;
	WFIFOL(fd,10)=x;
	WFIFOL(fd,14)=y;
	WFIFOB(fd,18)=id;
	WFIFOL(fd,19)=color;
	WFIFOSET(fd,packet_len(0x144));

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_cutin(struct map_session_data* sd, const char* image, int type)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0x1b3));
	WFIFOW(fd,0)=0x1b3;
	strncpy((char*)WFIFOP(fd,2),image,64);
	WFIFOB(fd,66)=type;
	WFIFOSET(fd,packet_len(0x1b3));

	return 0;
}

/*==========================================
 * Fills in card data from the given item and into the buffer. [Skotlex]
 *------------------------------------------*/
static void clif_addcards(unsigned char* buf, struct item* item)
{
	int i=0,j;
	if( item == NULL ) { //Blank data
		WBUFW(buf,0) = 0;
		WBUFW(buf,2) = 0;
		WBUFW(buf,4) = 0;
		WBUFW(buf,6) = 0;
		return;
	}
	if( item->card[0] == CARD0_PET ) { //pet eggs
		WBUFW(buf,0) = 0;
		WBUFW(buf,2) = 0;
		WBUFW(buf,4) = 0;
		WBUFW(buf,6) = item->card[3]; //Pet renamed flag.
		return;
	}
	if( item->card[0] == CARD0_FORGE || item->card[0] == CARD0_CREATE ) { //Forged/created items
		WBUFW(buf,0) = item->card[0];
		WBUFW(buf,2) = item->card[1];
		WBUFW(buf,4) = item->card[2];
		WBUFW(buf,6) = item->card[3];
		return;
	}
	//Client only receives four cards.. so randomly send them a set of cards. [Skotlex]
	if( MAX_SLOTS > 4 && (j = itemdb_slot(item->nameid)) > 4 )
		i = rand()%(j-3); //eg: 6 slots, possible i values: 0->3, 1->4, 2->5 => i = rand()%3;

	//Normal items.
	if( item->card[i] > 0 && (j=itemdb_viewid(item->card[i])) > 0 )
		WBUFW(buf,0) = j;
	else
		WBUFW(buf,0) = item->card[i];

	if( item->card[++i] > 0 && (j=itemdb_viewid(item->card[i])) > 0 )
		WBUFW(buf,2) = j;
	else
		WBUFW(buf,2) = item->card[i];

	if( item->card[++i] > 0 && (j=itemdb_viewid(item->card[i])) > 0 )
		WBUFW(buf,4) = j;
	else
		WBUFW(buf,4) = item->card[i];

	if( item->card[++i] > 0 && (j=itemdb_viewid(item->card[i])) > 0 )
		WBUFW(buf,6) = j;
	else
		WBUFW(buf,6) = item->card[i];
}

/*==========================================
 *
 *------------------------------------------*/
int clif_additem(struct map_session_data *sd, int n, int amount, int fail)
{
	int fd;
	nullpo_retr(0, sd);

	fd = sd->fd;
	if (!session_isActive(fd))  //Sasuke-
		return 0;

	WFIFOHEAD(fd,packet_len(0xa0));
	if(fail) {
		WFIFOW(fd,0)=0xa0;
		WFIFOW(fd,2)=n+2;
		WFIFOW(fd,4)=amount;
		WFIFOW(fd,6)=0;
		WFIFOB(fd,8)=0;
		WFIFOB(fd,9)=0;
		WFIFOB(fd,10)=0;
		WFIFOW(fd,11)=0;
		WFIFOW(fd,13)=0;
		WFIFOW(fd,15)=0;
		WFIFOW(fd,17)=0;
		WFIFOW(fd,19)=0;
		WFIFOB(fd,21)=0;
		WFIFOB(fd,22)=fail;
	} else {
		if (n<0 || n>=MAX_INVENTORY || sd->status.inventory[n].nameid <=0 || sd->inventory_data[n] == NULL)
			return 1;

		WFIFOW(fd,0)=0xa0;
		WFIFOW(fd,2)=n+2;
		WFIFOW(fd,4)=amount;
		if (sd->inventory_data[n]->view_id > 0)
			WFIFOW(fd,6)=sd->inventory_data[n]->view_id;
		else
			WFIFOW(fd,6)=sd->status.inventory[n].nameid;
		WFIFOB(fd,8)=sd->status.inventory[n].identify;
		WFIFOB(fd,9)=sd->status.inventory[n].attribute;
		WFIFOB(fd,10)=sd->status.inventory[n].refine;
		clif_addcards(WFIFOP(fd,11), &sd->status.inventory[n]);
		WFIFOW(fd,19)=pc_equippoint(sd,n);
		WFIFOB(fd,21)=itemtype(sd->inventory_data[n]->type);
		WFIFOB(fd,22)=fail;
	}

	WFIFOSET(fd,packet_len(0xa0));
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_delitem(struct map_session_data *sd,int n,int amount)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0xaf));
	WFIFOW(fd,0)=0xaf;
	WFIFOW(fd,2)=n+2;
	WFIFOW(fd,4)=amount;
	WFIFOSET(fd,packet_len(0xaf));

	return 0;
}

// Simplifies inventory/cart/storage packets by handling the packet section relevant to items. [Skotlex]
// Equip is >= 0 for equippable items (holds the equip-point, is 0 for pet 
// armor/egg) -1 for stackable items, -2 for stackable items where arrows must send in the equip-point.
void clif_item_sub(unsigned char *buf, int n, struct item *i, struct item_data *id, int equip)
{
	if (id->view_id > 0)
		WBUFW(buf,n)=id->view_id;
	else
		WBUFW(buf,n)=i->nameid;
	WBUFB(buf,n+2)=itemtype(id->type);
	WBUFB(buf,n+3)=i->identify;
	if (equip >= 0) { //Equippable item 
		WBUFW(buf,n+4)=equip;
		WBUFW(buf,n+6)=i->equip;
		WBUFB(buf,n+8)=i->attribute;
		WBUFB(buf,n+9)=i->refine;
	} else { //Stackable item.
		WBUFW(buf,n+4)=i->amount;
		if (equip == -2 && id->equip == EQP_AMMO)
			WBUFW(buf,n+6)=EQP_AMMO;
		else
			WBUFW(buf,n+6)=0;
	}

}

//Unified inventory function which sends all of the inventory (requires two packets, one for equipable items and one for stackable ones. [Skotlex]
void clif_inventorylist(struct map_session_data *sd)
{
	int i,n,ne,fd = sd->fd,arrow=-1;
	unsigned char *buf;
	unsigned char bufe[MAX_INVENTORY*20+4];
#if PACKETVER < 5
	const int s = 10; //Entry size.
#else
	const int s = 18;
#endif
	WFIFOHEAD(fd, MAX_INVENTORY * s + 4);
	buf = WFIFOP(fd,0);
	
	for(i=0,n=0,ne=0;i<MAX_INVENTORY;i++){
		if (sd->status.inventory[i].nameid <=0 || sd->inventory_data[i] == NULL)
			continue;  
	
		if(!itemdb_isstackable2(sd->inventory_data[i])) 
		{	//Non-stackable (Equippable)
			WBUFW(bufe,ne*20+4)=i+2;
			clif_item_sub(bufe, ne*20+6, &sd->status.inventory[i], sd->inventory_data[i], pc_equippoint(sd,i));
			clif_addcards(WBUFP(bufe, ne*20+16), &sd->status.inventory[i]);
			ne++;
		} else { //Stackable.
			WBUFW(buf,n*s+4)=i+2;
			clif_item_sub(buf, n*s+6, &sd->status.inventory[i], sd->inventory_data[i], -2);
			if (sd->inventory_data[i]->equip == EQP_AMMO &&
				sd->status.inventory[i].equip)
				arrow=i;
#if PACKETVER >= 5
			clif_addcards(WBUFP(buf, n*s+14), &sd->status.inventory[i]);
#endif
			n++;
		}
	}
	if (n) {
#if PACKETVER < 5
		WBUFW(buf,0)=0xa3;
#else
		WBUFW(buf,0)=0x1ee;
#endif
		WBUFW(buf,2)=4+n*s;
		WFIFOSET(fd,WFIFOW(fd,2));
	}
	if(arrow >= 0)
		clif_arrowequip(sd,arrow);

	if(ne){
		WBUFW(bufe,0)=0xa4;
		WBUFW(bufe,2)=4+ne*20;
		clif_send(bufe, WBUFW(bufe,2), &sd->bl, SELF);
	}

}

//Required when items break/get-repaired. Only sends equippable item list.
void clif_equiplist(struct map_session_data *sd)
{
	int i,n,fd = sd->fd;
	unsigned char *buf;
	WFIFOHEAD(fd, MAX_INVENTORY * 20 + 4);
	buf = WFIFOP(fd,0);
	
	for(i=0,n=0;i<MAX_INVENTORY;i++){
		if (sd->status.inventory[i].nameid <=0 || sd->inventory_data[i] == NULL)
			continue;  
	
		if(itemdb_isstackable2(sd->inventory_data[i])) 
			continue;
		//Equippable
		WBUFW(buf,n*20+4)=i+2;
		clif_item_sub(buf, n*20+6, &sd->status.inventory[i], sd->inventory_data[i], pc_equippoint(sd,i));
		clif_addcards(WBUFP(buf, n*20+16), &sd->status.inventory[i]);
		n++;
	}
	if (n) {
		WBUFW(buf,0)=0xa4;
		WBUFW(buf,2)=4+n*20;
		WFIFOSET(fd,WFIFOW(fd,2));
	}
}

//Unified storage function which sends all of the storage (requires two packets, one for equipable items and one for stackable ones. [Skotlex]
void clif_storagelist(struct map_session_data *sd,struct storage *stor)
{
	struct item_data *id;
	int i,n,ne,fd=sd->fd;
	unsigned char *buf;
	unsigned char bufe[MAX_STORAGE*20+4];
#if PACKETVER < 5
	const int s = 10; //Entry size.
#else
	const int s = 18;
#endif
	WFIFOHEAD(fd,MAX_STORAGE * s + 4);
	buf = WFIFOP(fd,0);
	
	for(i=0,n=0,ne=0;i<MAX_STORAGE;i++){
		if(stor->storage_[i].nameid<=0)
			continue;
		id = itemdb_search(stor->storage_[i].nameid);
		if(!itemdb_isstackable2(id))
		{ //Equippable
			WBUFW(bufe,ne*20+4)=i+1;
			clif_item_sub(bufe, ne*20+6, &stor->storage_[i], id, id->equip);
			clif_addcards(WBUFP(bufe, ne*20+16), &stor->storage_[i]);
			ne++;
		} else { //Stackable
			WBUFW(buf,n*s+4)=i+1;
			clif_item_sub(buf, n*s+6, &stor->storage_[i], id,-1);
#if PACKETVER >= 5
			clif_addcards(WBUFP(buf,n*s+14), &stor->storage_[i]);
#endif
			n++;
		}
	}
	if(n){
#if PACKETVER < 5
		WBUFW(buf,0)=0xa5;
#else
		WBUFW(buf,0)=0x1f0;
#endif
		WBUFW(buf,2)=4+n*s;
		WFIFOSET(fd,WFIFOW(fd,2));
	}
	if(ne){
		WBUFW(bufe,0)=0xa6;
		WBUFW(bufe,2)=4+ne*20;
		clif_send(bufe, WBUFW(bufe,2), &sd->bl, SELF);
	}
}

//Unified storage function which sends all of the storage (requires two packets, one for equipable items and one for stackable ones. [Skotlex]
void clif_guildstoragelist(struct map_session_data *sd,struct guild_storage *stor)
{
	struct item_data *id;
	int i,n,ne,fd=sd->fd;
	unsigned char *buf;
	unsigned char bufe[MAX_GUILD_STORAGE*20+4];
#if PACKETVER < 5
	const int s = 10; //Entry size.
#else
	const int s = 18;
#endif
	WFIFOHEAD(fd,MAX_GUILD_STORAGE * s + 4);
	buf = WFIFOP(fd,0);
	
	for(i=0,n=0,ne=0;i<MAX_GUILD_STORAGE;i++){
		if(stor->storage_[i].nameid<=0)
			continue;
		id = itemdb_search(stor->storage_[i].nameid);
		if(!itemdb_isstackable2(id))
		{ //Equippable
			WBUFW(bufe,ne*20+4)=i+1;
			clif_item_sub(bufe, ne*20+6, &stor->storage_[i], id, id->equip);
			clif_addcards(WBUFP(bufe, ne*20+16), &stor->storage_[i]);
			ne++;
		} else { //Stackable
			WBUFW(buf,n*s+4)=i+1;
			clif_item_sub(buf, n*s+6, &stor->storage_[i], id,-1);
#if PACKETVER >= 5
			clif_addcards(WBUFP(buf,n*s+14), &stor->storage_[i]);
#endif
			n++;
		}
	}
	if(n){
#if PACKETVER < 5
		WBUFW(buf,0)=0xa5;
#else
		WBUFW(buf,0)=0x1f0;
#endif
		WBUFW(buf,2)=4+n*s;
		WFIFOSET(fd,WFIFOW(fd,2));
	}
	if(ne){
		WBUFW(bufe,0)=0xa6;
		WBUFW(bufe,2)=4+ne*20;
		clif_send(bufe, WBUFW(bufe,2), &sd->bl, SELF);
	}
}

void clif_cartlist(struct map_session_data *sd)
{
	struct item_data *id;
	int i,n,ne,fd=sd->fd;
	unsigned char *buf;
	unsigned char bufe[MAX_CART*20+4];
#if PACKETVER < 5
	const int s = 10; //Entry size.
#else
	const int s = 18;
#endif
	WFIFOHEAD(fd, MAX_CART * s + 4);
	buf = WFIFOP(fd,0);
	
	for(i=0,n=0,ne=0;i<MAX_CART;i++){
		if(sd->status.cart[i].nameid<=0)
			continue;
		id = itemdb_search(sd->status.cart[i].nameid);
		if(!itemdb_isstackable2(id))
		{ //Equippable
			WBUFW(bufe,ne*20+4)=i+2;
			clif_item_sub(bufe, ne*20+6, &sd->status.cart[i], id, id->equip);
			clif_addcards(WBUFP(bufe, ne*20+16), &sd->status.cart[i]);
			ne++;
		} else { //Stackable
			WBUFW(buf,n*s+4)=i+2;
			clif_item_sub(buf, n*s+6, &sd->status.cart[i], id,-1);
#if PACKETVER >= 5
			clif_addcards(WBUFP(buf,n*s+14), &sd->status.cart[i]);
#endif
			n++;
		}
	}
	if(n){
#if PACKETVER < 5
		WBUFW(buf,0)=0x123;
#else
		WBUFW(buf,0)=0x1ef;
#endif
		WBUFW(buf,2)=4+n*s;
		WFIFOSET(fd,WFIFOW(fd,2));
	}
	if(ne){
		WBUFW(bufe,0)=0x122;
		WBUFW(bufe,2)=4+ne*20;
		clif_send(bufe, WBUFW(bufe,2), &sd->bl, SELF);
	}
	return;
}

/// Client behaviour:
/// Closes the cart storage and removes all it's items from memory.
/// The Num & Weight values of the cart are left untouched and the cart is NOT removed.
void clif_clearcart(int fd)
{
	WFIFOHEAD(fd, packet_len(0x12b));
	WFIFOW(fd,0) = 0x12b;
}

// Guild XY locators [Valaris]
int clif_guild_xy(struct map_session_data *sd)
{
	unsigned char buf[10];

	nullpo_retr(0, sd);

	WBUFW(buf,0)=0x1eb;
	WBUFL(buf,2)=sd->status.account_id;
	WBUFW(buf,6)=sd->bl.x;
	WBUFW(buf,8)=sd->bl.y;
	clif_send(buf,packet_len(0x1eb),&sd->bl,GUILD_SAMEMAP_WOS);

	return 0;
}

/*==========================================
 * Sends x/y dot to a single fd. [Skotlex]
 *------------------------------------------*/
int clif_guild_xy_single(int fd, struct map_session_data *sd)
{
	WFIFOHEAD(fd,packet_len(0x1eb));
	WFIFOW(fd,0)=0x1eb;
	WFIFOL(fd,2)=sd->status.account_id;
	WFIFOW(fd,6)=sd->bl.x;
	WFIFOW(fd,8)=sd->bl.y;
	WFIFOSET(fd,packet_len(0x1eb));
	return 0;
}

// Guild XY locators [Valaris]
int clif_guild_xy_remove(struct map_session_data *sd)
{
	unsigned char buf[10];

	nullpo_retr(0, sd);

	WBUFW(buf,0)=0x1eb;
	WBUFL(buf,2)=sd->status.account_id;
	WBUFW(buf,6)=-1;
	WBUFW(buf,8)=-1;
	clif_send(buf,packet_len(0x1eb),&sd->bl,GUILD_SAMEMAP_WOS);

	return 0;
}

/*==========================================
 * �X�e�[�^�X�𑗂����
 * �\����p�����͂��̒��Ōv�Z���đ���
 *------------------------------------------*/
int clif_updatestatus(struct map_session_data *sd,int type)
{
	int fd,len=8;

	nullpo_retr(0, sd);

	fd=sd->fd;

	if ( !session_isActive(fd) ) // Invalid pointer fix, by sasuke [Kevin]
		return 0;
 
	WFIFOHEAD(fd, 14);
	WFIFOW(fd,0)=0xb0;
	WFIFOW(fd,2)=type;
	switch(type){
		// 00b0
	case SP_WEIGHT:
		pc_updateweightstatus(sd);
		WFIFOW(fd,0)=0xb0;	//Need to re-set as pc_updateweightstatus can alter the buffer. [Skotlex]
		WFIFOW(fd,2)=type;
		WFIFOL(fd,4)=sd->weight;
		break;
	case SP_MAXWEIGHT:
		WFIFOL(fd,4)=sd->max_weight;
		break;
	case SP_SPEED:
		WFIFOL(fd,4)=sd->battle_status.speed;
		break;
	case SP_BASELEVEL:
		WFIFOL(fd,4)=sd->status.base_level;
		break;
	case SP_JOBLEVEL:
		WFIFOL(fd,4)=sd->status.job_level;
		break;
	case SP_KARMA: // Adding this back, I wonder if the client intercepts this - [Lance]
		WFIFOL(fd,4)=sd->status.karma;
		break;
	case SP_MANNER:
		WFIFOL(fd,4)=sd->status.manner;
		clif_changestatus(&sd->bl,SP_MANNER,sd->status.manner);
		break;
	case SP_STATUSPOINT:
		WFIFOL(fd,4)=sd->status.status_point;
		break;
	case SP_SKILLPOINT:
		WFIFOL(fd,4)=sd->status.skill_point;
		break;
	case SP_HIT:
		WFIFOL(fd,4)=sd->battle_status.hit;
		break;
	case SP_FLEE1:
		WFIFOL(fd,4)=sd->battle_status.flee;
		break;
	case SP_FLEE2:
		WFIFOL(fd,4)=sd->battle_status.flee2/10;
		break;
	case SP_MAXHP:
		WFIFOL(fd,4)=sd->battle_status.max_hp;
		break;
	case SP_MAXSP:
		WFIFOL(fd,4)=sd->battle_status.max_sp;
		break;
	case SP_HP:
		WFIFOL(fd,4)=sd->battle_status.hp;
		if (battle_config.disp_hpmeter)
			clif_hpmeter(sd);
		if (!battle_config.party_hp_mode && sd->status.party_id)
			clif_party_hp(sd);
		break;
	case SP_SP:
		WFIFOL(fd,4)=sd->battle_status.sp;
		break;
	case SP_ASPD:
		WFIFOL(fd,4)=sd->battle_status.amotion;
		break;
	case SP_ATK1:
		WFIFOL(fd,4)=sd->battle_status.batk +sd->battle_status.rhw.atk +sd->battle_status.lhw->atk;
		break;
	case SP_DEF1:
		WFIFOL(fd,4)=sd->battle_status.def;
		break;
	case SP_MDEF1:
		WFIFOL(fd,4)=sd->battle_status.mdef;
		break;
	case SP_ATK2:
		WFIFOL(fd,4)=sd->battle_status.rhw.atk2 + sd->battle_status.lhw->atk2;
		break;
	case SP_DEF2:
		WFIFOL(fd,4)=sd->battle_status.def2;
		break;
	case SP_MDEF2:
		//negative check (in case you have something like Berserk active)
		len = sd->battle_status.mdef2 - (sd->battle_status.vit>>1);
		if (len < 0) len = 0;
		WFIFOL(fd,4)= len;
		len = 8;
		break;
	case SP_CRITICAL:
		WFIFOL(fd,4)=sd->battle_status.cri/10;
		break;
	case SP_MATK1:
		WFIFOL(fd,4)=sd->battle_status.matk_max;
		break;
	case SP_MATK2:
		WFIFOL(fd,4)=sd->battle_status.matk_min;
		break;


	case SP_ZENY:
		WFIFOW(fd,0)=0xb1;
		WFIFOL(fd,4)=sd->status.zeny;
		break;
	case SP_BASEEXP:
		WFIFOW(fd,0)=0xb1;
		WFIFOL(fd,4)=sd->status.base_exp;
		break;
	case SP_JOBEXP:
		WFIFOW(fd,0)=0xb1;
		WFIFOL(fd,4)=sd->status.job_exp;
		break;
	case SP_NEXTBASEEXP:
		WFIFOW(fd,0)=0xb1;
		WFIFOL(fd,4)=pc_nextbaseexp(sd);
		break;
	case SP_NEXTJOBEXP:
		WFIFOW(fd,0)=0xb1;
		WFIFOL(fd,4)=pc_nextjobexp(sd);
		break;

		// 00be �I��
	case SP_USTR:
	case SP_UAGI:
	case SP_UVIT:
	case SP_UINT:
	case SP_UDEX:
	case SP_ULUK:
		WFIFOW(fd,0)=0xbe;
		WFIFOB(fd,4)=pc_need_status_point(sd,type-SP_USTR+SP_STR);
		len=5;
		break;

		// 013a �I��
	case SP_ATTACKRANGE:
		WFIFOW(fd,0)=0x13a;
		WFIFOW(fd,2)=sd->battle_status.rhw.range;
		len=4;
		break;

		// 0141 �I��
	case SP_STR:
		WFIFOW(fd,0)=0x141;
		WFIFOL(fd,2)=type;
		WFIFOL(fd,6)=sd->status.str;
		WFIFOL(fd,10)=sd->battle_status.str - sd->status.str;
		len=14;
		break;
	case SP_AGI:
		WFIFOW(fd,0)=0x141;
		WFIFOL(fd,2)=type;
		WFIFOL(fd,6)=sd->status.agi;
		WFIFOL(fd,10)=sd->battle_status.agi - sd->status.agi;
		len=14;
		break;
	case SP_VIT:
		WFIFOW(fd,0)=0x141;
		WFIFOL(fd,2)=type;
		WFIFOL(fd,6)=sd->status.vit;
		WFIFOL(fd,10)=sd->battle_status.vit - sd->status.vit;
		len=14;
		break;
	case SP_INT:
		WFIFOW(fd,0)=0x141;
		WFIFOL(fd,2)=type;
		WFIFOL(fd,6)=sd->status.int_;
		WFIFOL(fd,10)=sd->battle_status.int_ - sd->status.int_;
		len=14;
		break;
	case SP_DEX:
		WFIFOW(fd,0)=0x141;
		WFIFOL(fd,2)=type;
		WFIFOL(fd,6)=sd->status.dex;
		WFIFOL(fd,10)=sd->battle_status.dex - sd->status.dex;
		len=14;
		break;
	case SP_LUK:
		WFIFOW(fd,0)=0x141;
		WFIFOL(fd,2)=type;
		WFIFOL(fd,6)=sd->status.luk;
		WFIFOL(fd,10)=sd->battle_status.luk - sd->status.luk;
		len=14;
		break;

	case SP_CARTINFO:
		WFIFOW(fd,0)=0x121;
		WFIFOW(fd,2)=sd->cart_num;
		WFIFOW(fd,4)=MAX_CART;
		WFIFOL(fd,6)=sd->cart_weight;
		WFIFOL(fd,10)=battle_config.max_cart_weight;
		len=14;
		break;

	default:
		if(battle_config.error_log)
			ShowError("clif_updatestatus : unrecognized type %d\n",type);
		return 1;
	}
	WFIFOSET(fd,len);

	return 0;
}

int clif_changestatus(struct block_list *bl,int type,int val)
{
	unsigned char buf[12];
	struct map_session_data *sd = NULL;

	nullpo_retr(0, bl);

	if(bl->type == BL_PC)
		sd = (struct map_session_data *)bl;

	if(sd){
		WBUFW(buf,0)=0x1ab;
		WBUFL(buf,2)=bl->id;
		WBUFW(buf,6)=type;
		switch(type){
		case SP_MANNER:
			WBUFL(buf,8)=val;
			break;
		default:
			if(battle_config.error_log)
				ShowError("clif_changestatus : unrecognized type %d.\n",type);
			return 1;
		}
		clif_send(buf,packet_len(0x1ab),bl,AREA_WOS);
	}
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_changelook(struct block_list *bl,int type,int val)
{
	unsigned char buf[32];
	struct map_session_data *sd = NULL;
	struct view_data *vd;
	vd = status_get_viewdata(bl);
	nullpo_retr(0, vd);

	BL_CAST(BL_PC, bl, sd);	

	switch(type) {
		case LOOK_WEAPON:
			if (sd)
			{
				clif_get_weapon_view(sd, &vd->weapon, &vd->shield);
				val = vd->weapon;
			}
			else vd->weapon = val;
		break;
		case LOOK_SHIELD:
			if (sd)
			{
				clif_get_weapon_view(sd, &vd->weapon, &vd->shield);
				val = vd->shield;
			}
			else vd->shield = val;
		break;
		case LOOK_BASE:
			vd->class_ = val;
			if (vd->class_ == JOB_WEDDING || vd->class_ == JOB_XMAS || vd->class_ == JOB_SUMMER)
				vd->weapon = vd->shield = 0;
			if (vd->cloth_color && (
				(vd->class_ == JOB_WEDDING && battle_config.wedding_ignorepalette) ||
				(vd->class_ == JOB_XMAS && battle_config.xmas_ignorepalette) ||
				(vd->class_ == JOB_SUMMER && battle_config.summer_ignorepalette)
			))
				clif_changelook(bl,LOOK_CLOTHES_COLOR,0);
		break;
		case LOOK_HAIR:
			vd->hair_style = val;
		break;
		case LOOK_HEAD_BOTTOM:
			vd->head_bottom = val;
		break;
		case LOOK_HEAD_TOP:
			vd->head_top = val;
		break;	
		case LOOK_HEAD_MID:
			vd->head_mid = val;
		break;
		case LOOK_HAIR_COLOR:
			vd->hair_color = val;
		break;
		case LOOK_CLOTHES_COLOR:
			if (val && (
				(vd->class_ == JOB_WEDDING && battle_config.wedding_ignorepalette) ||
				(vd->class_ == JOB_XMAS && battle_config.xmas_ignorepalette) ||
				(vd->class_ == JOB_SUMMER && battle_config.summer_ignorepalette)
			))
				val = 0;
			vd->cloth_color = val;
		break;
		case LOOK_SHOES:
#if PACKETVER > 3
			if (sd) {
				int n;
				if((n = sd->equip_index[2]) >= 0 && sd->inventory_data[n]) {
					if(sd->inventory_data[n]->view_id > 0)
						val = sd->inventory_data[n]->view_id;
					else
						val = sd->status.inventory[n].nameid;
					}
				val = 0;
			}
#endif
			//Shoes? No packet uses this....
		break;
	}
#if PACKETVER < 4
	WBUFW(buf,0)=0xc3;
	WBUFL(buf,2)=bl->id;
	WBUFB(buf,6)=type;
	WBUFB(buf,7)=val;
	clif_send(buf,packet_len(0xc3),bl,AREA);
#else
	if(type == LOOK_WEAPON || type == LOOK_SHIELD) {
		WBUFW(buf,0)=0x1d7;
		WBUFL(buf,2)=bl->id;
		WBUFB(buf,6)=LOOK_WEAPON;
		WBUFW(buf,7)=vd->weapon;
		WBUFW(buf,9)=vd->shield;
		clif_send(buf,packet_len(0x1d7),bl,AREA);
	}
	else
	{
		WBUFW(buf,0)=0x1d7;
		WBUFL(buf,2)=bl->id;
		WBUFB(buf,6)=type;
		WBUFW(buf,7)=val;
		WBUFW(buf,9)=0;
		clif_send(buf,packet_len(0x1d7),bl,AREA);
	}
#endif
	return 0;
}

//Sends a change-base-look packet required for traps as they are triggered.
void clif_changetraplook(struct block_list *bl,int val)
{
	unsigned char buf[32];
#if PACKETVER < 4
	WBUFW(buf,0)=0xc3;
	WBUFL(buf,2)=bl->id;
	WBUFB(buf,6)=LOOK_BASE;
	WBUFB(buf,7)=val;
	clif_send(buf,packet_len(0xc3),bl,AREA);
#else
	WBUFW(buf,0)=0x1d7;
	WBUFL(buf,2)=bl->id;
	WBUFB(buf,6)=LOOK_BASE;
	WBUFW(buf,7)=val;
	WBUFW(buf,9)=0;
	clif_send(buf,packet_len(0x1d7),bl,AREA);
#endif

	
}
//For the stupid cloth-dye bug. Resends the given view data
//to the area specified by bl.
void clif_refreshlook(struct block_list *bl,int id,int type,int val,int area)
{
	unsigned char buf[32];
#if PACKETVER < 4
	WBUFW(buf,0)=0xc3;
	WBUFL(buf,2)=id;
	WBUFB(buf,6)=type;
	WBUFB(buf,7)=val;
	clif_send(buf,packet_len(0xc3),bl,area);
#else
	WBUFW(buf,0)=0x1d7;
	WBUFL(buf,2)=id;
	WBUFB(buf,6)=type;
	WBUFW(buf,7)=val;
	WBUFW(buf,9)=0;
	clif_send(buf,packet_len(0x1d7),bl,area);
#endif
	return;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_initialstatus(struct map_session_data *sd)
{
	int fd;
	unsigned char *buf;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xbd));
	buf=WFIFOP(fd,0);

	WBUFW(buf,0)=0xbd;
	WBUFW(buf,2)=min(sd->status.status_point, SHRT_MAX);
	WBUFB(buf,4)=min(sd->status.str, UCHAR_MAX);
	WBUFB(buf,5)=pc_need_status_point(sd,SP_STR);
	WBUFB(buf,6)=min(sd->status.agi, UCHAR_MAX);
	WBUFB(buf,7)=pc_need_status_point(sd,SP_AGI);
	WBUFB(buf,8)=min(sd->status.vit, UCHAR_MAX);
	WBUFB(buf,9)=pc_need_status_point(sd,SP_VIT);
	WBUFB(buf,10)=min(sd->status.int_, UCHAR_MAX);
	WBUFB(buf,11)=pc_need_status_point(sd,SP_INT);
	WBUFB(buf,12)=min(sd->status.dex, UCHAR_MAX);
	WBUFB(buf,13)=pc_need_status_point(sd,SP_DEX);
	WBUFB(buf,14)=min(sd->status.luk, UCHAR_MAX);
	WBUFB(buf,15)=pc_need_status_point(sd,SP_LUK);

	WBUFW(buf,16) = sd->battle_status.batk + sd->battle_status.rhw.atk + sd->battle_status.lhw->atk;
	WBUFW(buf,18) = sd->battle_status.rhw.atk2 + sd->battle_status.lhw->atk2; //atk bonus
	WBUFW(buf,20) = sd->battle_status.matk_max;
	WBUFW(buf,22) = sd->battle_status.matk_min;
	WBUFW(buf,24) = sd->battle_status.def; // def
	WBUFW(buf,26) = sd->battle_status.def2;
	WBUFW(buf,28) = sd->battle_status.mdef; // mdef
	fd = sd->battle_status.mdef2 - (sd->battle_status.vit>>1);
	if (fd < 0) fd = 0; //Negative check for Frenzy'ed characters.
	WBUFW(buf,30) = fd;
	fd = sd->fd;
	WBUFW(buf,32) = sd->battle_status.hit;
	WBUFW(buf,34) = sd->battle_status.flee;
	WBUFW(buf,36) = sd->battle_status.flee2/10;
	WBUFW(buf,38) = sd->battle_status.cri/10;
	WBUFW(buf,40) = sd->status.karma;
	WBUFW(buf,42) = sd->status.manner;

	WFIFOSET(fd,packet_len(0xbd));

	clif_updatestatus(sd,SP_STR);
	clif_updatestatus(sd,SP_AGI);
	clif_updatestatus(sd,SP_VIT);
	clif_updatestatus(sd,SP_INT);
	clif_updatestatus(sd,SP_DEX);
	clif_updatestatus(sd,SP_LUK);

	clif_updatestatus(sd,SP_ATTACKRANGE);
	clif_updatestatus(sd,SP_ASPD);

	return 0;
}

/*==========================================
 *���
 *------------------------------------------*/
int clif_arrowequip(struct map_session_data *sd,int val)
{
	int fd;

	nullpo_retr(0, sd);

	pc_stop_attack(sd); // [Valaris]

	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0x013c));
	WFIFOW(fd,0)=0x013c;
	WFIFOW(fd,2)=val+2;//��̃A�C�e��ID
	WFIFOSET(fd,packet_len(0x013c));

	return 0;
}

/// Ammunition action message.
/// type=0 : MsgStringTable[242]="Please equip the proper ammunition first."
/// type=1 : MsgStringTable[243]="You can't Attack or use Skills because your Weight Limit has been exceeded."
/// type=2 : MsgStringTable[244]="You can't use Skills because Weight Limit has been exceeded."
/// type=3 : assassin, baby_assassin, assassin_cross => MsgStringTable[1040]="You have equipped throwing daggers."
///          gunslinger => MsgStringTable[1175]="Bullets have been equipped."
///          NOT ninja => MsgStringTable[245]="Ammunition has been equipped."
int clif_arrow_fail(struct map_session_data *sd,int type)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0x013b));
	WFIFOW(fd,0)=0x013b;
	WFIFOW(fd,2)=type;
	WFIFOSET(fd,packet_len(0x013b));

	return 0;
}

/*==========================================
 * �쐬�\ ��X�g���M
 *------------------------------------------*/
int clif_arrow_create_list(struct map_session_data *sd)
{
	int i, c, j;
	int fd;

	nullpo_retr(0, sd);

	fd = sd->fd;
	WFIFOHEAD(fd, MAX_SKILL_ARROW_DB*2+4);
	WFIFOW(fd,0) = 0x1ad;

	for (i = 0, c = 0; i < MAX_SKILL_ARROW_DB; i++) {
		if (skill_arrow_db[i].nameid > 0 &&
			(j = pc_search_inventory(sd, skill_arrow_db[i].nameid)) >= 0 &&
			!sd->status.inventory[j].equip && sd->status.inventory[j].identify)
		{
			if ((j = itemdb_viewid(skill_arrow_db[i].nameid)) > 0)
				WFIFOW(fd,c*2+4) = j;
			else
				WFIFOW(fd,c*2+4) = skill_arrow_db[i].nameid;
			c++;
		}
	}
	WFIFOW(fd,2) = c*2+4;
	WFIFOSET(fd, WFIFOW(fd,2));
	if (c > 0) {
		sd->menuskill_id = AC_MAKINGARROW;
		sd->menuskill_val = c;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_statusupack(struct map_session_data *sd,int type,int ok,int val)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xbc));
	WFIFOW(fd,0)=0xbc;
	WFIFOW(fd,2)=type;
	WFIFOB(fd,4)=ok;
	WFIFOB(fd,5)=val;
	WFIFOSET(fd,packet_len(0xbc));

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_equipitemack(struct map_session_data *sd,int n,int pos,int ok)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xaa));
	WFIFOW(fd,0)=0xaa;
	WFIFOW(fd,2)=n+2;
	WFIFOW(fd,4)=pos;
	WFIFOB(fd,6)=ok;
	WFIFOSET(fd,packet_len(0xaa));

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_unequipitemack(struct map_session_data *sd,int n,int pos,int ok)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xac));
	WFIFOW(fd,0)=0xac;
	WFIFOW(fd,2)=n+2;
	WFIFOW(fd,4)=pos;
	WFIFOB(fd,6)=ok;
	WFIFOSET(fd,packet_len(0xac));

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_misceffect(struct block_list* bl,int type)
{
	unsigned char buf[32];

	nullpo_retr(0, bl);

	WBUFW(buf,0) = 0x19b;
	WBUFL(buf,2) = bl->id;
	WBUFL(buf,6) = type;

	clif_send(buf,packet_len(0x19b),bl,AREA);

	return 0;
}
int clif_misceffect2(struct block_list *bl, int type)
{
	unsigned char buf[24];

	nullpo_retr(0, bl);

	memset(buf, 0, packet_len(0x1f3));

	WBUFW(buf,0) = 0x1f3;
	WBUFL(buf,2) = bl->id;
	WBUFL(buf,6) = type;

	clif_send(buf, packet_len(0x1f3), bl, AREA);

	return 0;

}
/*==========================================
 * �\���I�v�V�����ύX
 *------------------------------------------*/
int clif_changeoption(struct block_list* bl)
{
	unsigned char buf[32];
	struct status_change *sc;

	nullpo_retr(0, bl);
	sc = status_get_sc(bl);
	if (!sc) return 0; //How can an option change if there's no sc?
	
#if PACKETVER >= 7
	WBUFW(buf,0) = 0x229;
	WBUFL(buf,2) = bl->id;
	WBUFW(buf,6) = sc->opt1;
	WBUFW(buf,8) = sc->opt2;
	WBUFL(buf,10) = sc->option;
	WBUFB(buf,14) = 0;	// PK???
	if(disguised(bl)) {
		clif_send(buf,packet_len(0x229),bl,AREA_WOS);
		WBUFL(buf,2) = -bl->id;
		clif_send(buf,packet_len(0x229),bl,SELF);
		WBUFL(buf,2) = bl->id;
		WBUFL(buf,10) = OPTION_INVISIBLE;
		clif_send(buf,packet_len(0x229),bl,SELF);
	} else
		clif_send(buf,packet_len(0x229),bl,AREA);
#else
	WBUFW(buf,0) = 0x119;
	WBUFL(buf,2) = bl->id;
	WBUFW(buf,6) = sc->opt1;
	WBUFW(buf,8) = sc->opt2;
	WBUFW(buf,10) = sc->option;
	WBUFB(buf,12) = 0;	// ??
	if(disguised(bl)) {
		clif_send(buf,packet_len(0x119),bl,AREA_WOS);
		WBUFL(buf,2) = -bl->id;
		clif_send(buf,packet_len(0x119),bl,SELF);
		WBUFL(buf,2) = bl->id;
		WBUFW(buf,10) = OPTION_INVISIBLE;
		clif_send(buf,packet_len(0x119),bl,SELF);
	} else
		clif_send(buf,packet_len(0x119),bl,AREA);
#endif

	return 0;
}

int clif_changeoption2(struct block_list* bl)
{
	unsigned char buf[20];
	struct status_change *sc;
	
	sc = status_get_sc(bl);
	if (!sc) return 0; //How can an option change if there's no sc?

	WBUFW(buf,0) = 0x28a;
	WBUFL(buf,2) = bl->id;
	WBUFL(buf,6) = sc->option;
	WBUFL(buf,10) = clif_setlevel(status_get_lv(bl));
	WBUFL(buf,14) = sc->opt3;
	if(disguised(bl)) {
		clif_send(buf,packet_len(0x28a),bl,AREA_WOS);
		WBUFL(buf,2) = -bl->id;
		clif_send(buf,packet_len(0x28a),bl,SELF);
		WBUFL(buf,2) = bl->id;
		WBUFL(buf,6) = OPTION_INVISIBLE;
		clif_send(buf,packet_len(0x28a),bl,SELF);
	} else
		clif_send(buf,packet_len(0x28a),bl,AREA);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_useitemack(struct map_session_data *sd,int index,int amount,int ok)
{
	nullpo_retr(0, sd);

	if(!ok) {
		int fd=sd->fd;
  		WFIFOHEAD(fd,packet_len(0xa8));
		WFIFOW(fd,0)=0xa8;
		WFIFOW(fd,2)=index+2;
		WFIFOW(fd,4)=amount;
		WFIFOB(fd,6)=ok;
		WFIFOSET(fd,packet_len(0xa8));
	}
	else {
#if PACKETVER < 3
		int fd=sd->fd;
		WFIFOHEAD(fd,packet_len(0xa8));
		WFIFOW(fd,0)=0xa8;
		WFIFOW(fd,2)=index+2;
		WFIFOW(fd,4)=amount;
		WFIFOB(fd,6)=ok;
		WFIFOSET(fd,packet_len(0xa8));
#else
		unsigned char buf[32];

		WBUFW(buf,0)=0x1c8;
		WBUFW(buf,2)=index+2;
		if(sd->inventory_data[index] && sd->inventory_data[index]->view_id > 0)
			WBUFW(buf,4)=sd->inventory_data[index]->view_id;
		else
			WBUFW(buf,4)=sd->status.inventory[index].nameid;
		WBUFL(buf,6)=sd->bl.id;
		WBUFW(buf,10)=amount;
		WBUFB(buf,12)=ok;
		clif_send(buf,packet_len(0x1c8),&sd->bl,AREA);
#endif
	}

	return 0;
}

/*==========================================
 * Inform client whether chatroom creation was successful or not
 * R 00d6 <fail>.B
 *------------------------------------------*/
void clif_createchat(struct map_session_data* sd, int fail)
{
	int fd;

	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0xd6));
	WFIFOW(fd,0) = 0xd6;
	WFIFOB(fd,2) = fail;
	WFIFOSET(fd,packet_len(0xd6));
}

/*==========================================
 * Display a chat above the owner
 * R 00d7 <len>.w <owner ID>.l <chat ID>.l <limit>.w <users>.w <type>.B <title>.?B
 *------------------------------------------*/
int clif_dispchat(struct chat_data* cd, int fd)
{
	unsigned char buf[128];
	uint8 type;

	if( cd == NULL || cd->owner == NULL )
		return 1;

	// type - 0: private, 1: public, 2: npc, 3: non-clickable
	type = (cd->owner->type == BL_PC ) ? (cd->pub) ? 1 : 0
	     : (cd->owner->type == BL_NPC) ? (cd->limit) ? 2 : 3
	     : 1;

	WBUFW(buf, 0) = 0xd7;
	WBUFW(buf, 2) = 17 + strlen(cd->title);
	WBUFL(buf, 4) = cd->owner->id;
	WBUFL(buf, 8) = cd->bl.id;
	WBUFW(buf,12) = cd->limit;
	WBUFW(buf,14) = cd->users;
	WBUFB(buf,16) = type;
	strncpy((char*)WBUFP(buf,17), cd->title, strlen(cd->title)); // not zero-terminated

	if( fd ) {
		WFIFOHEAD(fd,WBUFW(buf,2));
		memcpy(WFIFOP(fd,0),buf,WBUFW(buf,2));
		WFIFOSET(fd,WBUFW(buf,2));
	} else {
		clif_send(buf,WBUFW(buf,2),cd->owner,AREA_WOSC);
	}

	return 0;
}

/*==========================================
 * Chatroom properties adjustment
 * R 00df <len>.w <owner ID>.l <chat ID>.l <limit>.w <users>.w <pub>.B <title>.?B
 *------------------------------------------*/
int clif_changechatstatus(struct chat_data* cd)
{
	unsigned char buf[128];

	if( cd == NULL || cd->usersd[0] == NULL )
		return 1;

	WBUFW(buf, 0) = 0xdf;
	WBUFW(buf, 2) = 17 + strlen(cd->title);
	WBUFL(buf, 4) = cd->usersd[0]->bl.id;
	WBUFL(buf, 8) = cd->bl.id;
	WBUFW(buf,12) = cd->limit;
	WBUFW(buf,14) = cd->users;
	WBUFB(buf,16) = cd->pub;
	strncpy((char*)WBUFP(buf,17), cd->title, strlen(cd->title)); // not zero-terminated

	clif_send(buf,WBUFW(buf,2),&cd->usersd[0]->bl,CHAT);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_clearchat(struct chat_data *cd,int fd)
{
	unsigned char buf[32];

	nullpo_retr(0, cd);

	WBUFW(buf,0) = 0xd8;
	WBUFL(buf,2) = cd->bl.id;
	if( fd ) {
		WFIFOHEAD(fd,packet_len(0xd8));
		memcpy(WFIFOP(fd,0),buf,packet_len(0xd8));
		WFIFOSET(fd,packet_len(0xd8));
	} else {
		clif_send(buf,packet_len(0xd8),cd->owner,AREA_WOSC);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_joinchatfail(struct map_session_data *sd,int fail)
{
	int fd;

	nullpo_retr(0, sd);

	fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0xda));
	WFIFOW(fd,0) = 0xda;
	WFIFOB(fd,2) = fail;
	WFIFOSET(fd,packet_len(0xda));

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_joinchatok(struct map_session_data *sd,struct chat_data* cd)
{
	int fd;
	int i;

	nullpo_retr(0, sd);
	nullpo_retr(0, cd);

	fd = sd->fd;
	if (!session_isActive(fd))
		return 0;
	WFIFOHEAD(fd, 8 + (28*cd->users));
	WFIFOW(fd, 0) = 0xdb;
	WFIFOW(fd, 2) = 8 + (28*cd->users);
	WFIFOL(fd, 4) = cd->bl.id;
	for (i = 0; i < cd->users; i++) {
		WFIFOL(fd, 8+i*28) = (i != 0 || cd->owner->type == BL_NPC);
		memcpy(WFIFOP(fd, 8+i*28+4), cd->usersd[i]->status.name, NAME_LENGTH);
	}
	WFIFOSET(fd, WFIFOW(fd, 2));

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_addchat(struct chat_data* cd,struct map_session_data *sd)
{
	unsigned char buf[32];

	nullpo_retr(0, sd);
	nullpo_retr(0, cd);

	WBUFW(buf, 0) = 0x0dc;
	WBUFW(buf, 2) = cd->users;
	memcpy(WBUFP(buf, 4),sd->status.name,NAME_LENGTH);
	clif_send(buf,packet_len(0xdc),&sd->bl,CHAT_WOS);

	return 0;
}

/*==========================================
 * Announce the new owner
 * R 00e1 <index>.l <nick>.24B
 *------------------------------------------*/
void clif_changechatowner(struct chat_data* cd, struct map_session_data* sd)
{
	unsigned char buf[64];

	nullpo_retv(sd);
	nullpo_retv(cd);

	//FIXME: this announces a swap between positions 0 and 1 (probably not what we want) [ultramage]
	//FIXME: aegis sends obviously incorrect packets; need to figure out what to send to display it correctly :X
	//TODO: is it just owner swap, or can it do general-purpose reordering?

	WBUFW(buf, 0) = 0xe1;
	WBUFL(buf, 2) = 1;
	memcpy(WBUFP(buf,6),cd->usersd[0]->status.name,NAME_LENGTH);

	WBUFW(buf,30) = 0xe1;
	WBUFL(buf,32) = 0;
	memcpy(WBUFP(buf,36),sd->status.name,NAME_LENGTH);

	clif_send(buf,packet_len(0xe1)*2,&sd->bl,CHAT);
}

/*==========================================
 * Notify about user leaving the chatroom
 * R 00dd <index>.w <nick>.24B <flag>.B
 *------------------------------------------*/
void clif_leavechat(struct chat_data* cd, struct map_session_data* sd, bool flag)
{
	unsigned char buf[32];

	nullpo_retv(sd);
	nullpo_retv(cd);

	WBUFW(buf, 0) = 0xdd;
	WBUFW(buf, 2) = cd->users-1;
	memcpy(WBUFP(buf,4),sd->status.name,NAME_LENGTH);
	WBUFB(buf,28) = flag; // 0: left, 1: was kicked

	clif_send(buf,packet_len(0xdd),&sd->bl,CHAT);
}

/*==========================================
 * Opens a trade request window from char 'name'
 * R 00e5 <nick>.24B
 *------------------------------------------*/
void clif_traderequest(struct map_session_data* sd, const char* name)
{
	int fd;
	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0xe5));
	WFIFOW(fd,0) = 0xe5;
	safestrncpy((char*)WFIFOP(fd,2), name, NAME_LENGTH);
	WFIFOSET(fd,packet_len(0xe5));
}

/*==========================================
 * �������v������
 *------------------------------------------*/
void clif_tradestart(struct map_session_data* sd, int type)
{
	int fd;
	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0xe7));
	WFIFOW(fd,0) = 0xe7;
	WFIFOB(fd,2) = type;
	WFIFOSET(fd,packet_len(0xe7));
}

/*==========================================
 * ���������̃A�C�e���ǉ�
 *------------------------------------------*/
void clif_tradeadditem(struct map_session_data* sd, struct map_session_data* tsd, int index, int amount)
{
	int fd;
	nullpo_retv(sd);
	nullpo_retv(tsd);

	fd = tsd->fd;
	WFIFOHEAD(fd,packet_len(0xe9));
	WFIFOW(fd,0) = 0xe9;
	WFIFOL(fd,2) = amount;
	if( index == 0 )
	{
		WFIFOW(fd,6) = 0; // type id
		WFIFOB(fd,8) = 0; //identify flag
		WFIFOB(fd,9) = 0; // attribute
		WFIFOB(fd,10)= 0; //refine
		WFIFOW(fd,11)= 0; //card (4w)
		WFIFOW(fd,13)= 0; //card (4w)
		WFIFOW(fd,15)= 0; //card (4w)
		WFIFOW(fd,17)= 0; //card (4w)
	}
	else
	{
		index -= 2; //index fix
		if(sd->inventory_data[index] && sd->inventory_data[index]->view_id > 0)
			WFIFOW(fd,6) = sd->inventory_data[index]->view_id;
		else
			WFIFOW(fd,6) = sd->status.inventory[index].nameid; // type id
		WFIFOB(fd,8) = sd->status.inventory[index].identify; //identify flag
		WFIFOB(fd,9) = sd->status.inventory[index].attribute; // attribute
		WFIFOB(fd,10)= sd->status.inventory[index].refine; //refine
		clif_addcards(WFIFOP(fd, 11), &sd->status.inventory[index]);
	}
	WFIFOSET(fd,packet_len(0xe9));
}

/*==========================================
 * �A�C�e���ǉ�����/���s
 *------------------------------------------*/
void clif_tradeitemok(struct map_session_data* sd, int index, int fail)
{
	int fd;
	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0xea));
	WFIFOW(fd,0) = 0xea;
	WFIFOW(fd,2) = index;
	WFIFOB(fd,4) = fail;
	WFIFOSET(fd,packet_len(0xea));
}

/*==========================================
 * ������ok����
 *------------------------------------------*/
void clif_tradedeal_lock(struct map_session_data* sd, int fail)
{
	int fd;
	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0xec));
	WFIFOW(fd,0) = 0xec;
	WFIFOB(fd,2) = fail; // 0=you 1=the other person
	WFIFOSET(fd,packet_len(0xec));
}

/*==========================================
 * ���������L�����Z������܂���
 *------------------------------------------*/
void clif_tradecancelled(struct map_session_data* sd)
{
	int fd;
	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0xee));
	WFIFOW(fd,0) = 0xee;
	WFIFOSET(fd,packet_len(0xee));
}

/*==========================================
 * ����������
 *------------------------------------------*/
void clif_tradecompleted(struct map_session_data* sd, int fail)
{
	int fd;
	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0xf0));
	WFIFOW(fd,0) = 0xf0;
	WFIFOB(fd,2) = fail;
	WFIFOSET(fd,packet_len(0xf0));
}

/*==========================================
 * �J�v���q�ɂ̃A�C�e�������X�V
 *------------------------------------------*/
int clif_updatestorageamount(struct map_session_data *sd,struct storage *stor)
{
	int fd;

	nullpo_retr(0, sd);
	nullpo_retr(0, stor);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xf2));
	WFIFOW(fd,0) = 0xf2;  // update storage amount
	WFIFOW(fd,2) = stor->storage_amount;  //items
	WFIFOW(fd,4) = MAX_STORAGE; //items max
	WFIFOSET(fd,packet_len(0xf2));

	return 0;
}

/*==========================================
 * �J�v���q�ɂɃA�C�e����ǉ�����
 *------------------------------------------*/
int clif_storageitemadded(struct map_session_data *sd,struct storage *stor,int index,int amount)
{
	int view,fd;

	nullpo_retr(0, sd);
	nullpo_retr(0, stor);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xf4));
	WFIFOW(fd,0) =0xf4; // Storage item added
	WFIFOW(fd,2) =index+1; // index
	WFIFOL(fd,4) =amount; // amount
	if((view = itemdb_viewid(stor->storage_[index].nameid)) > 0)
		WFIFOW(fd,8) =view;
	else
		WFIFOW(fd,8) =stor->storage_[index].nameid; // id
	WFIFOB(fd,10)=stor->storage_[index].identify; //identify flag
	WFIFOB(fd,11)=stor->storage_[index].attribute; // attribute
	WFIFOB(fd,12)=stor->storage_[index].refine; //refine
	clif_addcards(WFIFOP(fd,13), &stor->storage_[index]);
	WFIFOSET(fd,packet_len(0xf4));

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_updateguildstorageamount(struct map_session_data *sd,struct guild_storage *stor)
{
	int fd;

	nullpo_retr(0, sd);
	nullpo_retr(0, stor);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xf2));
	WFIFOW(fd,0) = 0xf2;  // update storage amount
	WFIFOW(fd,2) = stor->storage_amount;  //items
	WFIFOW(fd,4) = MAX_GUILD_STORAGE; //items max
	WFIFOSET(fd,packet_len(0xf2));

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_guildstorageitemadded(struct map_session_data *sd,struct guild_storage *stor,int index,int amount)
{
	int view,fd;

	nullpo_retr(0, sd);
	nullpo_retr(0, stor);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xf4));
	WFIFOW(fd,0) =0xf4; // Storage item added
	WFIFOW(fd,2) =index+1; // index
	WFIFOL(fd,4) =amount; // amount
	if((view = itemdb_viewid(stor->storage_[index].nameid)) > 0)
		WFIFOW(fd,8) =view;
	else
		WFIFOW(fd,8) =stor->storage_[index].nameid; // id
	WFIFOB(fd,10)=stor->storage_[index].identify; //identify flag
	WFIFOB(fd,11)=stor->storage_[index].attribute; // attribute
	WFIFOB(fd,12)=stor->storage_[index].refine; //refine
	clif_addcards(WFIFOP(fd,13), &stor->storage_[index]);
	WFIFOSET(fd,packet_len(0xf4));

	return 0;
}

/*==========================================
 * �J�v���q�ɂ���A�C�e������苎��
 *------------------------------------------*/
int clif_storageitemremoved(struct map_session_data *sd,int index,int amount)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xf6));
	WFIFOW(fd,0)=0xf6; // Storage item removed
	WFIFOW(fd,2)=index+1;
	WFIFOL(fd,4)=amount;
	WFIFOSET(fd,packet_len(0xf6));

	return 0;
}

/*==========================================
 * �J�v���q�ɂ����
 *------------------------------------------*/
int clif_storageclose(struct map_session_data *sd)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xf8));
	WFIFOW(fd,0)=0xf8; // Storage Closed
	WFIFOSET(fd,packet_len(0xf8));

	return 0;
}

//
// callback�n ?
//
/*==========================================
 * PC�\��
 *------------------------------------------*/
void clif_getareachar_pc(struct map_session_data* sd,struct map_session_data* dstsd)
{
	int len;
	if(dstsd->chatID)
	{
		struct chat_data *cd;
		cd=(struct chat_data*)map_id2bl(dstsd->chatID);
		if(cd && cd->usersd[0]==dstsd)
			clif_dispchat(cd,sd->fd);
	}

	if(dstsd->vender_id)
		clif_showvendingboard(&dstsd->bl,dstsd->message,sd->fd);

	if(dstsd->spiritball > 0)
		clif_spiritball_single(sd->fd, dstsd);

	if((sd->status.party_id && dstsd->status.party_id == sd->status.party_id) || //Party-mate, or hpdisp setting.
		(battle_config.disp_hpmeter && (len = pc_isGM(sd)) >= battle_config.disp_hpmeter && len >= pc_isGM(dstsd))
		)
		clif_hpmeter_single(sd->fd, dstsd);

	if(dstsd->status.manner < 0)
		clif_changestatus(&dstsd->bl,SP_MANNER,dstsd->status.manner);
		
	// pvp circle for duel [LuzZza]
	/*
	if(dstsd->duel_group)
		clif_specialeffect(&dstsd->bl, 159, 4);
	*/

}
void clif_getareachar_unit(struct map_session_data* sd,struct block_list *bl)
{
	struct unit_data *ud;
	struct view_data *vd;
	int len, fd = sd->fd;
	
	vd = status_get_viewdata(bl);

	if (!vd || vd->class_ == INVISIBLE_CLASS)
		return;

	ud = unit_bl2ud(bl);
	if (ud && ud->walktimer != -1)
	{
#if PACKETVER >= 7
		WFIFOHEAD(fd, packet_len(0x22c));
#elif PACKETVER > 3
		WFIFOHEAD(fd, packet_len(0x1da));
#else
		WFIFOHEAD(fd, packet_len(0x7b));
#endif
		len = clif_set007b(bl,vd,ud,WFIFOP(fd,0));
		WFIFOSET(fd,len);
	} else {
#if PACKETVER >= 7
		WFIFOHEAD(fd,packet_len(0x22a));
#elif PACKETVER > 3
		WFIFOHEAD(fd,packet_len(0x1d8));
#else
		WFIFOHEAD(fd,packet_len(0x78));
#endif
		len = clif_set0078(bl,WFIFOP(fd,0));
		WFIFOSET(fd,len);
	}

	if (vd->cloth_color)
		clif_refreshlook(&sd->bl,bl->id,LOOK_CLOTHES_COLOR,vd->cloth_color,SELF);

	switch (bl->type)
	{	// FIXME: 'AREA' causes unneccessary spam since this should be 1:1 communication [ultramage]
	case BL_PC:
		{
			TBL_PC* tsd = (TBL_PC*)bl;
			clif_getareachar_pc(sd, tsd);
			if(tsd->state.size==2) // tiny/big players [Valaris]
				clif_specialeffect(bl,423,AREA);
			else if(tsd->state.size==1)
				clif_specialeffect(bl,421,AREA);
		}
		break;
	case BL_NPC:
		{
			if(((TBL_NPC*)bl)->chat_id)
				clif_dispchat((struct chat_data*)map_id2bl(((TBL_NPC*)bl)->chat_id),sd->fd);
		}
		break;
	case BL_MOB:
		{
			TBL_MOB* md = (TBL_MOB*)bl;
			if(md->special_state.size==2) // tiny/big mobs [Valaris]
				clif_specialeffect(bl,423,AREA);
			else if(md->special_state.size==1)
				clif_specialeffect(bl,421,AREA);
		}
		break;
	case BL_PET:
		if (vd->head_bottom)
			clif_pet_equip(sd, (TBL_PET*)bl); // needed to display pet equip properly
		break;
	}
}

//Modifies the type of damage according to status changes [Skotlex]
#define clif_calc_delay(type,delay) (type==1||type==4||type==0x0a)?type:(delay==0?9:type)

/*==========================================
 * Estimates walk delay based on the damage criteria. [Skotlex]
 *------------------------------------------*/
static int clif_calc_walkdelay(struct block_list *bl,int delay, int type, int damage, int div_)
{
	if (type == 4 || type == 9 || damage <=0)
		return 0;
	
	if (bl->type == BL_PC) {
		if (battle_config.pc_walk_delay_rate != 100)
			delay = delay*battle_config.pc_walk_delay_rate/100;
	} else
		if (battle_config.walk_delay_rate != 100)
			delay = delay*battle_config.walk_delay_rate/100;
	
	if (div_ > 1) //Multi-hit skills mean higher delays.
		delay += battle_config.multihit_delay*(div_-1);

	return delay>0?delay:1; //Return 1 to specify there should be no noticeable delay, but you should stop walking.
}

/*==========================================
 * Sends a 'damage' packet (src performs action on dst)
 * R 008a <src ID>.l <dst ID>.l <server tick>.l <src speed>.l <dst speed>.l <param1>.w <param2>.w <type>.B <param3>.w
 *
 * type=00 damage [param1: total damage, param2: div, param3: assassin dual-wield damage]
 * type=01 pick up item
 * type=02 sit down
 * type=03 stand up
 * type=04 reflected/absorbed damage?
 * type=08 double attack
 * type=09 don't display flinch animation (endure)
 * type=0a critical hit
 * type=0b lucky dodge
 *------------------------------------------*/
int clif_damage(struct block_list* src, struct block_list* dst, unsigned int tick, int sdelay, int ddelay, int damage, int div, int type, int damage2)
{
	unsigned char buf[256];
	struct status_change *sc;

	nullpo_retr(0, src);
	nullpo_retr(0, dst);

	type = clif_calc_delay(type, ddelay); //Type defaults to 0 for normal attacks.

	sc = status_get_sc(dst);
	if(sc && sc->count) {
		if(sc->data[SC_HALLUCINATION].timer != -1) {
			if(damage > 0) damage = damage*(5+sc->data[SC_HALLUCINATION].val1) + rand()%100;
			if(damage2 > 0) damage2 = damage2*(5+sc->data[SC_HALLUCINATION].val1) + rand()%100;
		}
	}

	WBUFW(buf,0)=0x8a;
	WBUFL(buf,2)=src->id;
	WBUFL(buf,6)=dst->id;
	WBUFL(buf,10)=tick;
	WBUFL(buf,14)=sdelay;
	WBUFL(buf,18)=ddelay;
	if (battle_config.hide_woe_damage && map_flag_gvg(src->m)) {
		WBUFW(buf,22)=damage?div:0;
		WBUFW(buf,27)=damage2?div:0;
	} else {
		WBUFW(buf,22)=min(damage, SHRT_MAX);
		WBUFW(buf,27)=damage2;
	}
	WBUFW(buf,24)=div;
	WBUFB(buf,26)=type;
	clif_send(buf,packet_len(0x8a),src,AREA);

	if(disguised(src)) {
		WBUFL(buf,2) = -src->id;
		if(damage > 0) WBUFW(buf,22) = -1;
		if(damage2 > 0) WBUFW(buf,27) = -1;
		clif_send(buf,packet_len(0x8a),src,SELF);
	}
	if (disguised(dst)) {
		WBUFL(buf,6) = -dst->id;
		if (disguised(src)) WBUFL(buf,2) = src->id;
		else {
			if(damage > 0) WBUFW(buf,22) = -1;
			if(damage2 > 0) WBUFW(buf,27) = -1;
		}
		clif_send(buf,packet_len(0x8a),dst,SELF);
	}
	//Return adjusted can't walk delay for further processing.
	return clif_calc_walkdelay(dst,ddelay,type,damage+damage2,div);
}

/*==========================================
 * src picks up dst
 *------------------------------------------*/
void clif_takeitem(struct block_list* src, struct block_list* dst)
{
	//clif_damage(src,dst,0,0,0,0,0,1,0);
	unsigned char buf[32];

	nullpo_retv(src);
	nullpo_retv(dst);

	WBUFW(buf, 0) = 0x8a;
	WBUFL(buf, 2) = src->id;
	WBUFL(buf, 6) = dst->id;
	WBUFB(buf,26) = 1;
	clif_send(buf, packet_len(0x8a), src, AREA);

}

/*==========================================
 * inform clients in area that `bl` is sitting
 *------------------------------------------*/
void clif_sitting(struct block_list* bl)
{
	unsigned char buf[32];
	nullpo_retv(bl);

	WBUFW(buf, 0) = 0x8a;
	WBUFL(buf, 2) = bl->id;
	WBUFB(buf,26) = 2;
	clif_send(buf, packet_len(0x8a), bl, AREA);

	if(disguised(bl)) {
		WBUFL(buf, 2) = - bl->id;
		clif_send(buf, packet_len(0x8a), bl, SELF);
	}
}

/*==========================================
 * inform clients in area that `bl` is standing
 *------------------------------------------*/
void clif_standing(struct block_list* bl)
{
	unsigned char buf[32];
	nullpo_retv(bl);

	WBUFW(buf, 0) = 0x8a;
	WBUFL(buf, 2) = bl->id;
	WBUFB(buf,26) = 3;
	clif_send(buf, packet_len(0x8a), bl, AREA);

	if(disguised(bl)) {
		WBUFL(buf, 2) = - bl->id;
		clif_send(buf, packet_len(0x8a), bl, SELF);
	}
}

/*==========================================
 * Inform client(s) about a map-cell change
 *------------------------------------------*/
void clif_changemapcell(int fd, struct block_list* pos, int type, enum send_target target)
{
	unsigned char buf[32];
	nullpo_retv(pos);

	WBUFW(buf,0) = 0x192;
	WBUFW(buf,2) = pos->x;
	WBUFW(buf,4) = pos->y;
	WBUFW(buf,6) = type;
	mapindex_getmapname_ext(map[pos->m].name,(char*)WBUFP(buf,8));

	if( fd )
	{
		WFIFOHEAD(fd,packet_len(0x192));
		memcpy(WFIFOP(fd,0), buf, packet_len(0x192));
		WFIFOSET(fd,packet_len(0x192));
	}
	else
		clif_send(buf,packet_len(0x192),pos,target);
}

/*==========================================
 *
 *------------------------------------------*/
void clif_getareachar_item(struct map_session_data* sd,struct flooritem_data* fitem)
{
	int view,fd;
	fd=sd->fd;
	//009d <ID>.l <item ID>.w <identify flag>.B <X>.w <Y>.w <amount>.w <subX>.B <subY>.B
	WFIFOHEAD(fd,packet_len(0x9d));

	WFIFOW(fd,0)=0x9d;
	WFIFOL(fd,2)=fitem->bl.id;
	if((view = itemdb_viewid(fitem->item_data.nameid)) > 0)
		WFIFOW(fd,6)=view;
	else
		WFIFOW(fd,6)=fitem->item_data.nameid;
	WFIFOB(fd,8)=fitem->item_data.identify;
	WFIFOW(fd,9)=fitem->bl.x;
	WFIFOW(fd,11)=fitem->bl.y;
	WFIFOW(fd,13)=fitem->item_data.amount;
	WFIFOB(fd,15)=fitem->subx;
	WFIFOB(fd,16)=fitem->suby;
	WFIFOSET(fd,packet_len(0x9d));
}

/*==========================================
 * �ꏊ�X�L���G�t�F�N�g�����E�ɓ���
 *------------------------------------------*/
static void clif_getareachar_skillunit(struct map_session_data *sd, struct skill_unit *unit)
{
	int fd = sd->fd;

#if PACKETVER >= 3
	if(unit->group->unit_id==UNT_GRAFFITI)	{ // Graffiti [Valaris]
		WFIFOHEAD(fd,packet_len(0x1c9));
		WFIFOW(fd, 0)=0x1c9;
		WFIFOL(fd, 2)=unit->bl.id;
		WFIFOL(fd, 6)=unit->group->src_id;
		WFIFOW(fd,10)=unit->bl.x;
		WFIFOW(fd,12)=unit->bl.y;
		WFIFOB(fd,14)=unit->group->unit_id;
		WFIFOB(fd,15)=1;
		WFIFOB(fd,16)=1;
		safestrncpy(WFIFOP(fd,17),unit->group->valstr,MESSAGE_SIZE);
		WFIFOSET(fd,packet_len(0x1c9));
		return;
	}
#endif
	WFIFOHEAD(fd,packet_len(0x11f));
	WFIFOW(fd, 0)=0x11f;
	WFIFOL(fd, 2)=unit->bl.id;
	WFIFOL(fd, 6)=unit->group->src_id;
	WFIFOW(fd,10)=unit->bl.x;
	WFIFOW(fd,12)=unit->bl.y;
	if (battle_config.traps_setting&1 && skill_get_inf2(unit->group->skill_id)&INF2_TRAP)
		WFIFOB(fd,14)=UNT_ATTACK_SKILLS; //Use invisible unit id for traps.
	else
		WFIFOB(fd,14)=unit->group->unit_id;
	WFIFOB(fd,15)=0;
	WFIFOSET(fd,packet_len(0x11f));

	if(unit->group->skill_id == WZ_ICEWALL)
		clif_changemapcell(fd,&unit->bl,5,SELF);
}

/*==========================================
 * �ꏊ�X�L���G�t�F�N�g�����E���������
 *------------------------------------------*/
static void clif_clearchar_skillunit(struct skill_unit *unit, int fd)
{
	nullpo_retv(unit);

	WFIFOHEAD(fd,packet_len(0x120));
	WFIFOW(fd, 0)=0x120;
	WFIFOL(fd, 2)=unit->bl.id;
	WFIFOSET(fd,packet_len(0x120));

	if(unit->group && unit->group->skill_id == WZ_ICEWALL)
		clif_changemapcell(fd,&unit->bl,unit->val2,SELF);
}

/*==========================================
 * �ꏊ�X�L���G�t�F�N�g�폜
 *------------------------------------------*/
void clif_skill_delunit(struct skill_unit *unit)
{
	unsigned char buf[16];

	nullpo_retv(unit);

	WBUFW(buf, 0)=0x120;
	WBUFL(buf, 2)=unit->bl.id;
	clif_send(buf,packet_len(0x120),&unit->bl,AREA);
}

/*==========================================
 * Unknown... trap related?
 * Only affects units with class [139,153] client-side
 *------------------------------------------*/
int clif_01ac(struct block_list *bl)
{
	unsigned char buf[32];

	nullpo_retr(0, bl);

	WBUFW(buf, 0) = 0x1ac;
	WBUFL(buf, 2) = bl->id;

	clif_send(buf,packet_len(0x1ac),bl,AREA);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
 int clif_getareachar(struct block_list* bl,va_list ap)
{
	struct map_session_data *sd;

	nullpo_retr(0, bl);
	nullpo_retr(0, ap);

	sd=va_arg(ap,struct map_session_data*);

	if (sd == NULL || !sd->fd || session[sd->fd] == NULL)
		return 0;

	switch(bl->type){
	case BL_ITEM:
		clif_getareachar_item(sd,(struct flooritem_data*) bl);
		break;
	case BL_SKILL:
		clif_getareachar_skillunit(sd,(TBL_SKILL*)bl);
		break;
	default:
		if(&sd->bl == bl)
			break;
		clif_getareachar_unit(sd,bl);
		break;
	}
	return 0;
}

/*==========================================
 * tbl has gone out of view-size of bl
 *------------------------------------------*/
int clif_outsight(struct block_list *bl,va_list ap)
{
	struct block_list *tbl;
	struct view_data *vd;
	TBL_PC *sd, *tsd;
	tbl=va_arg(ap,struct block_list*);
	if(bl == tbl) return 0;
	BL_CAST(BL_PC, bl, sd);
	BL_CAST(BL_PC, tbl, tsd);

	if (tsd && tsd->fd)
	{	//tsd has lost sight of the bl object.
		switch(bl->type){
		case BL_PC:
			if (((TBL_PC*)bl)->vd.class_ != INVISIBLE_CLASS)
				clif_clearunit_single(bl->id,0,tsd->fd);
			if(sd->chatID){
				struct chat_data *cd;
				cd=(struct chat_data*)map_id2bl(sd->chatID);
				if(cd->usersd[0]==sd)
					clif_dispchat(cd,tsd->fd);
			}
			if(sd->vender_id)
				clif_closevendingboard(bl,tsd->fd);
			break;
		case BL_ITEM:
			clif_clearflooritem((struct flooritem_data*)bl,tsd->fd);
			break;
		case BL_SKILL:
			clif_clearchar_skillunit((struct skill_unit *)bl,tsd->fd);
			break;
		default:
			if ((vd=status_get_viewdata(bl)) && vd->class_ != INVISIBLE_CLASS)
				clif_clearunit_single(bl->id,0,tsd->fd);
			break;
		}
	}
	if (sd && sd->fd)
	{	//sd is watching tbl go out of view.
		if ((vd=status_get_viewdata(tbl)) && vd->class_ != INVISIBLE_CLASS)
			clif_clearunit_single(tbl->id,0,sd->fd);
	}
	return 0;
}

/*==========================================
 * tbl has come into view of bl
 *------------------------------------------*/
int clif_insight(struct block_list *bl,va_list ap)
{
	struct block_list *tbl;
	TBL_PC *sd, *tsd;
	tbl=va_arg(ap,struct block_list*);

	if (bl == tbl) return 0;
	
	BL_CAST(BL_PC, bl, sd);
	BL_CAST(BL_PC, tbl, tsd);

	if (tsd && tsd->fd)
	{	//Tell tsd that bl entered into his view
		switch(bl->type){
		case BL_ITEM:
			clif_getareachar_item(tsd,(struct flooritem_data*)bl);
			break;
		case BL_SKILL:
			clif_getareachar_skillunit(tsd,(TBL_SKILL*)bl);
			break;
		default:
			clif_getareachar_unit(tsd,bl);
			break;
		}
	}
	if (sd && sd->fd)
	{	//Tell sd that tbl walked into his view
		clif_getareachar_unit(sd,tbl);
	}
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_skillinfo(struct map_session_data *sd,int skillid,int type,int range)
{
	int fd,id;

	nullpo_retr(0, sd);

	fd=sd->fd;
	if( (id=sd->status.skill[skillid].id) <= 0 )
		return 0;
	WFIFOHEAD(fd,packet_len(0x147));
	WFIFOW(fd,0)=0x147;
	WFIFOW(fd,2) = id;
	if(type < 0)
		WFIFOW(fd,4) = skill_get_inf(id);
	else
		WFIFOW(fd,4) = type;
	WFIFOW(fd,6) = 0;
	WFIFOW(fd,8) = sd->status.skill[skillid].lv;
	WFIFOW(fd,10) = skill_get_sp(id,sd->status.skill[skillid].lv);
	if(range < 0)
		range = skill_get_range2(&sd->bl, id,sd->status.skill[skillid].lv);

	WFIFOW(fd,12)= range;
//	strncpy((char*)WFIFOP(fd,14), skill_get_name(id), NAME_LENGTH);
	if(sd->status.skill[skillid].flag ==0)
		WFIFOB(fd,38)= (sd->status.skill[skillid].lv < skill_tree_get_max(id, sd->status.class_))? 1:0;
	else
		WFIFOB(fd,38) = 0;
	WFIFOSET(fd,packet_len(0x147));

	return 0;
}

/*==========================================
 * �X�L�����X�g�𑗐M����
 *------------------------------------------*/
int clif_skillinfoblock(struct map_session_data *sd)
{
	int fd;
	int i,c,len,id;

	nullpo_retr(0, sd);

	fd=sd->fd;
	if (!fd) return 0;

	WFIFOHEAD(fd, MAX_SKILL * 37 + 4);
	WFIFOW(fd,0) = 0x10f;
	for ( i = 0, c = 0, len = 4; i < MAX_SKILL; i++)
	{
		if( (id = sd->status.skill[i].id) != 0 )
		{
			WFIFOW(fd,len)   = id;
			WFIFOW(fd,len+2) = skill_get_inf(id);
			WFIFOW(fd,len+4) = 0;
			WFIFOW(fd,len+6) = sd->status.skill[i].lv;
			WFIFOW(fd,len+8) = skill_get_sp(id,sd->status.skill[i].lv);
			WFIFOW(fd,len+10)= skill_get_range2(&sd->bl, id,sd->status.skill[i].lv);
//			strncpy((char*)WFIFOP(fd,len+12), skill_get_name(id), NAME_LENGTH);
			if(sd->status.skill[i].flag == 0)
				WFIFOB(fd,len+36) = (sd->status.skill[i].lv < skill_tree_get_max(id, sd->status.class_))? 1:0;
			else
				WFIFOB(fd,len+36) = 0;
			len += 37;
			c++;
		}
	}
	WFIFOW(fd,2)=len;
	WFIFOSET(fd,len);

	return 1;
}

/*==========================================
 * �X�L������U��ʒm
 *------------------------------------------*/
int clif_skillup(struct map_session_data *sd,int skill_num)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x10e));
	WFIFOW(fd,0) = 0x10e;
	WFIFOW(fd,2) = skill_num;
	WFIFOW(fd,4) = sd->status.skill[skill_num].lv;
	WFIFOW(fd,6) = skill_get_sp(skill_num,sd->status.skill[skill_num].lv);
	WFIFOW(fd,8) = skill_get_range2(&sd->bl,skill_num,sd->status.skill[skill_num].lv);
	WFIFOB(fd,10) = (sd->status.skill[skill_num].lv < skill_tree_get_max(sd->status.skill[skill_num].id, sd->status.class_)) ? 1 : 0;
	WFIFOSET(fd,packet_len(0x10e));

	return 0;
}

/*==========================================
 * �X�L���r���G�t�F�N�g�𑗐M����
 *------------------------------------------*/
int clif_skillcasting(struct block_list* bl,
	int src_id,int dst_id,int dst_x,int dst_y,int skill_num,int pl, int casttime)
{
	unsigned char buf[32];
	WBUFW(buf,0) = 0x13e;
	WBUFL(buf,2) = src_id;
	WBUFL(buf,6) = dst_id;
	WBUFW(buf,10) = dst_x;
	WBUFW(buf,12) = dst_y;
	WBUFW(buf,14) = skill_num;
	WBUFL(buf,16) = pl<0?0:pl; //Avoid sending negatives as element [Skotlex]
	WBUFL(buf,20) = casttime;
	clif_send(buf,packet_len(0x13e), bl, AREA);
	if (disguised(bl)) {
		WBUFL(buf,2) = -src_id;
		WBUFL(buf,20) = 0;
		clif_send(buf,packet_len(0x13e), bl, SELF);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_skillcastcancel(struct block_list* bl)
{
	unsigned char buf[16];

	nullpo_retr(0, bl);

	WBUFW(buf,0) = 0x1b9;
	WBUFL(buf,2) = bl->id;
	clif_send(buf,packet_len(0x1b9), bl, AREA);

	return 0;
}


/// only when type==0:
///  if(skill_id==NV_BASIC)
///   btype==0 "skill failed" MsgStringTable[159]
///   btype==1 "no emotions" MsgStringTable[160]
///   btype==2 "no sit" MsgStringTable[161]
///   btype==3 "no chat" MsgStringTable[162]
///   btype==4 "no party" MsgStringTable[163]
///   btype==5 "no shout" MsgStringTable[164]
///   btype==6 "no PKing" MsgStringTable[165]
///   btype==7 "no alligning" MsgStringTable[383]
///   btype>=8: ignored
///  if(skill_id==AL_WARP) "not enough skill level" MsgStringTable[214]
///  if(skill_id==TF_STEAL) "steal failed" MsgStringTable[205]
///  if(skill_id==TF_POISON) "envenom failed" MsgStringTable[207]
///  otherwise "skill failed" MsgStringTable[204]
/// btype irrelevant
///  type==1 "insufficient SP" MsgStringTable[202]
///  type==2 "insufficient HP" MsgStringTable[203]
///  type==3 "insufficient materials" MsgStringTable[808]
///  type==4 "there is a delay after using a skill" MsgStringTable[219]
///  type==5 "insufficient zeny" MsgStringTable[233]
///  type==6 "wrong weapon" MsgStringTable[239]
///  type==7 "red jemstone needed" MsgStringTable[246]
///  type==8 "blue jemstone needed" MsgStringTable[247]
///  type==9 "overweight" MsgStringTable[580]
///  type==10 "skill failed" MsgStringTable[285]
///  type>=11 ignored
///
/// if(success!=0) doesn't display any of the previous messages
/// Note: when this packet is received an unknown flag is always set to 0,
/// suggesting this is an ACK packet for the UseSkill packets and should be sent on success too [FlavioJS]
int clif_skill_fail(struct map_session_data *sd,int skill_id,int type,int btype)
{
	int fd;

	if (!sd) {	//Since this is the most common nullpo.... 
		ShowDebug("clif_skill_fail: Error, received NULL sd for skill %d\n", skill_id);
		return 0;
	}
	
	fd=sd->fd;
	if (!fd) return 0;

	if(battle_config.display_skill_fail&1)
		return 0; //Disable all skill failed messages

	if(type==0x4 && !sd->state.showdelay)
		return 0; //Disable delay failed messages
	
	if(skill_id == RG_SNATCHER && battle_config.display_skill_fail&4)
		return 0;

	if(skill_id == TF_POISON && battle_config.display_skill_fail&8)
		return 0;

	WFIFOHEAD(fd,packet_len(0x110));
	WFIFOW(fd,0) = 0x110;
	WFIFOW(fd,2) = skill_id;
	WFIFOL(fd,4) = btype;
	WFIFOB(fd,8) = 0;// success?
	WFIFOB(fd,9) = type;
	WFIFOSET(fd,packet_len(0x110));

	return 1;
}

/*==========================================
 * �X�L���U���G�t�F�N�g���_���[�W
 *------------------------------------------*/
int clif_skill_damage(struct block_list *src,struct block_list *dst,
	unsigned int tick,int sdelay,int ddelay,int damage,int div,int skill_id,int skill_lv,int type)
{
	unsigned char buf[64];
	struct status_change *sc;

	nullpo_retr(0, src);
	nullpo_retr(0, dst);

	type = (type>0)?type:skill_get_hit(skill_id);
	type = clif_calc_delay(type, ddelay);
	sc = status_get_sc(dst);

	if(sc && sc->count) {
		if(sc->data[SC_HALLUCINATION].timer != -1 && damage > 0)
			damage = damage*(5+sc->data[SC_HALLUCINATION].val1) + rand()%100;
	}

#if PACKETVER < 3
	WBUFW(buf,0)=0x114;
	WBUFW(buf,2)=skill_id;
	WBUFL(buf,4)=src->id;
	WBUFL(buf,8)=dst->id;
	WBUFL(buf,12)=tick;
	WBUFL(buf,16)=sdelay;
	WBUFL(buf,20)=ddelay;
	if (battle_config.hide_woe_damage && map_flag_gvg(src->m)) {
		WBUFW(buf,24)=damage?div:0;
	} else {
		WBUFW(buf,24)=damage;
	}
	WBUFW(buf,26)=skill_lv;
	WBUFW(buf,28)=div;
	WBUFB(buf,30)=type;
	clif_send(buf,packet_len(0x114),src,AREA);
	if(disguised(src)) {
		WBUFL(buf,4)=-src->id;
		if(damage > 0)
			WBUFW(buf,24)=-1;
		clif_send(buf,packet_len(0x114),src,SELF);
	}
	if (disguised(dst)) {
		WBUFL(buf,8)=-dst->id;
		if (disguised(src))
			WBUFL(buf,4)=src->id;
		else if(damage > 0)
			WBUFW(buf,24)=-1;
		clif_send(buf,packet_len(0x114),dst,SELF);
	}
#else
	WBUFW(buf,0)=0x1de;
	WBUFW(buf,2)=skill_id;
	WBUFL(buf,4)=src->id;
	WBUFL(buf,8)=dst->id;
	WBUFL(buf,12)=tick;
	WBUFL(buf,16)=sdelay;
	WBUFL(buf,20)=ddelay;
	if (battle_config.hide_woe_damage && map_flag_gvg(src->m)) {
		WBUFL(buf,24)=damage?div:0;
	} else {
		WBUFL(buf,24)=damage;
	}
	WBUFW(buf,28)=skill_lv;
	WBUFW(buf,30)=div;
	WBUFB(buf,32)=type;
	clif_send(buf,packet_len(0x1de),src,AREA);
	if(disguised(src)) {
		WBUFL(buf,4)=-src->id;
		if(damage > 0)
			WBUFL(buf,24)=-1;
		clif_send(buf,packet_len(0x1de),src,SELF);
	}
	if (disguised(dst)) {
		WBUFL(buf,8)=-dst->id;
		if (disguised(src))
			WBUFL(buf,4)=src->id;
		else if(damage > 0)
			WBUFL(buf,24)=-1;
		clif_send(buf,packet_len(0x1de),dst,SELF);
	}
#endif

	//Because the damage delay must be synced with the client, here is where the can-walk tick must be updated. [Skotlex]
	return clif_calc_walkdelay(dst,ddelay,type,damage,div);
}

/*==========================================
 * ������΂��X�L���U���G�t�F�N�g���_���[�W
 *------------------------------------------*/
int clif_skill_damage2(struct block_list *src,struct block_list *dst,
	unsigned int tick,int sdelay,int ddelay,int damage,int div,int skill_id,int skill_lv,int type)
{
	unsigned char buf[64];
	struct status_change *sc;

	nullpo_retr(0, src);
	nullpo_retr(0, dst);

	type = (type>0)?type:skill_get_hit(skill_id);
	type = clif_calc_delay(type, ddelay);
	sc = status_get_sc(dst);

	if(sc && sc->count) {
		if(sc->data[SC_HALLUCINATION].timer != -1 && damage > 0)
			damage = damage*(5+sc->data[SC_HALLUCINATION].val1) + rand()%100;
	}

	WBUFW(buf,0)=0x115;
	WBUFW(buf,2)=skill_id;
	WBUFL(buf,4)=src->id;
	WBUFL(buf,8)=dst->id;
	WBUFL(buf,12)=tick;
	WBUFL(buf,16)=sdelay;
	WBUFL(buf,20)=ddelay;
	WBUFW(buf,24)=dst->x;
	WBUFW(buf,26)=dst->y;
	if (battle_config.hide_woe_damage && map_flag_gvg(src->m)) {
		WBUFW(buf,28)=damage?div:0;
	} else {
		WBUFW(buf,28)=damage;
	}
	WBUFW(buf,30)=skill_lv;
	WBUFW(buf,32)=div;
	WBUFB(buf,34)=type;
	clif_send(buf,packet_len(0x115),src,AREA);
	if(disguised(src)) {
		WBUFL(buf,4)=-src->id;
		if(damage > 0)
			WBUFW(buf,28)=-1;
		clif_send(buf,packet_len(0x115),src,SELF);
	}
	if (disguised(dst)) {
		WBUFL(buf,8)=-dst->id;
		if (disguised(src))
			WBUFL(buf,4)=src->id;
		else if(damage > 0)
			WBUFW(buf,28)=-1;
		clif_send(buf,packet_len(0x115),dst,SELF);
	}

	//Because the damage delay must be synced with the client, here is where the can-walk tick must be updated. [Skotlex]
	return clif_calc_walkdelay(dst,ddelay,type,damage,div);
}

/*==========================================
 * �x��/�񕜃X�L���G�t�F�N�g
 *------------------------------------------*/
int clif_skill_nodamage(struct block_list *src,struct block_list *dst,int skill_id,int heal,int fail)
{
	unsigned char buf[32];

	nullpo_retr(0, dst);

	WBUFW(buf,0)=0x11a;
	WBUFW(buf,2)=skill_id;
	WBUFW(buf,4)=min(heal, SHRT_MAX);
	WBUFL(buf,6)=dst->id;
	WBUFL(buf,10)=src?src->id:0;
	WBUFB(buf,14)=fail;
	clif_send(buf,packet_len(0x11a),dst,AREA);

	if (disguised(dst)) {
		WBUFL(buf,6)=-dst->id;
		clif_send(buf,packet_len(0x115),dst,SELF);
	}

	if(src && disguised(src)) {
		WBUFL(buf,10)=-src->id;
		if (disguised(dst))
			WBUFL(buf,6)=dst->id;
		clif_send(buf,packet_len(0x115),src,SELF);
	}

	return fail;
}

/*==========================================
 * �ꏊ�X�L���G�t�F�N�g
 *------------------------------------------*/
int clif_skill_poseffect(struct block_list *src,int skill_id,int val,int x,int y,int tick)
{
	unsigned char buf[32];

	nullpo_retr(0, src);

	WBUFW(buf,0)=0x117;
	WBUFW(buf,2)=skill_id;
	WBUFL(buf,4)=src->id;
	WBUFW(buf,8)=val;
	WBUFW(buf,10)=x;
	WBUFW(buf,12)=y;
	WBUFL(buf,14)=tick;
	clif_send(buf,packet_len(0x117),src,AREA);
	if(disguised(src)) {
		WBUFL(buf,4)=-src->id;
		clif_send(buf,packet_len(0x117),src,SELF);
	}

	return 0;
}

/*==========================================
 * �ꏊ�X�L���G�t�F�N�g�\��
 *------------------------------------------*/
void clif_skill_setunit(struct skill_unit *unit)
{
	unsigned char buf[128];

	nullpo_retv(unit);

#if PACKETVER >= 3
	if(unit->group->unit_id==UNT_GRAFFITI)	{ // Graffiti [Valaris]
		WBUFW(buf, 0)=0x1c9;
		WBUFL(buf, 2)=unit->bl.id;
		WBUFL(buf, 6)=unit->group->src_id;
		WBUFW(buf,10)=unit->bl.x;
		WBUFW(buf,12)=unit->bl.y;
		WBUFB(buf,14)=unit->group->unit_id;
		WBUFB(buf,15)=1;
		WBUFB(buf,16)=1;
		safestrncpy((char*)WBUFP(buf,17),unit->group->valstr,MESSAGE_SIZE);
		clif_send(buf,packet_len(0x1c9),&unit->bl,AREA);
		return;
	}
#endif
	WBUFW(buf, 0)=0x11f;
	WBUFL(buf, 2)=unit->bl.id;
	WBUFL(buf, 6)=unit->group->src_id;
	WBUFW(buf,10)=unit->bl.x;
	WBUFW(buf,12)=unit->bl.y;
	if (unit->group->state.song_dance&0x1 && unit->val2&UF_ENSEMBLE)
		WBUFB(buf,14)=unit->val2&UF_SONG?UNT_DISSONANCE:UNT_UGLYDANCE;
	else
		WBUFB(buf,14)=unit->group->unit_id;
	WBUFB(buf,15)=0;
	clif_send(buf,packet_len(0x11f),&unit->bl,AREA);
}

/*==========================================
 * ���[�v�ꏊ�I��
 *------------------------------------------*/
void clif_skill_warppoint(struct map_session_data* sd, int skill_num, int skill_lv, int map1, int map2, int map3, int map4)
{
	int fd;
	nullpo_retv(sd);
	fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x11c));
	WFIFOW(fd,0) = 0x11c;
	WFIFOW(fd,2) = skill_num;
	memset(WFIFOP(fd,4), 0x00, 4*MAP_NAME_LENGTH_EXT);
	if (map1 == -1) strcpy((char*)WFIFOP(fd,4), "Random");
	if (map1 > 0) mapindex_getmapname_ext(mapindex_id2name(map1), (char*)WFIFOP(fd,4));
	if (map2 > 0) mapindex_getmapname_ext(mapindex_id2name(map2), (char*)WFIFOP(fd,20));
	if (map3 > 0) mapindex_getmapname_ext(mapindex_id2name(map3), (char*)WFIFOP(fd,36));
	if (map4 > 0) mapindex_getmapname_ext(mapindex_id2name(map4), (char*)WFIFOP(fd,52));
	WFIFOSET(fd,packet_len(0x11c));

	sd->menuskill_id = skill_num;
	if (skill_num == AL_WARP)
		sd->menuskill_val = (sd->ud.skillx<<16)|sd->ud.skilly; //Store warp position here.
	else
		sd->menuskill_val = skill_lv;
}
/*==========================================
 * ��������
 *------------------------------------------*/
int clif_skill_memo(struct map_session_data *sd,int flag)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;

	WFIFOHEAD(fd,packet_len(0x11e));
	WFIFOW(fd,0)=0x11e;
	WFIFOB(fd,2)=flag;
	WFIFOSET(fd,packet_len(0x11e));
	return 0;
}
int clif_skill_teleportmessage(struct map_session_data *sd,int flag)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x189));
	WFIFOW(fd,0)=0x189;
	WFIFOW(fd,2)=flag;
	WFIFOSET(fd,packet_len(0x189));
	return 0;
}

/*==========================================
 * �����X�^�[���
 *------------------------------------------*/
int clif_skill_estimation(struct map_session_data *sd,struct block_list *dst)
{
	struct status_data *status;
	unsigned char buf[64];
	int i;//, fix;

	nullpo_retr(0, sd);
	nullpo_retr(0, dst);

	if(dst->type!=BL_MOB )
		return 0;

	status = status_get_status_data(dst);

	WBUFW(buf, 0)=0x18c;
	WBUFW(buf, 2)=status_get_class(dst);
	WBUFW(buf, 4)=status_get_lv(dst);
	WBUFW(buf, 6)=status->size;
	WBUFL(buf, 8)=status->hp;
	WBUFW(buf,12)= (battle_config.estimation_type&1?status->def:0)
		+(battle_config.estimation_type&2?status->def2:0);
	WBUFW(buf,14)=status->race;
	WBUFW(buf,16)= (battle_config.estimation_type&1?status->mdef:0)
  		+(battle_config.estimation_type&2?status->mdef2:0);
	WBUFW(buf,18)= status->def_ele;
	for(i=0;i<9;i++)
		WBUFB(buf,20+i)= (unsigned char)battle_attr_fix(NULL,dst,100,i+1,status->def_ele, status->ele_lv);
//		The following caps negative attributes to 0 since the client displays them as 255-fix. [Skotlex]
//		WBUFB(buf,20+i)= (unsigned char)((fix=battle_attr_fix(NULL,dst,100,i+1,status->def_ele, status->ele_lv))<0?0:fix);

	clif_send(buf,packet_len(0x18c),&sd->bl,
		sd->status.party_id>0?PARTY_SAMEMAP:SELF);
	return 0;
}
/*==========================================
 * �A�C�e�������\���X�g
 *------------------------------------------*/
int clif_skill_produce_mix_list(struct map_session_data *sd, int trigger)
{
	int i,c,view,fd;
	nullpo_retr(0, sd);

	if(sd->menuskill_id == AM_PHARMACY)
		return 0; //Avoid resending the menu twice or more times...
	fd=sd->fd;
	WFIFOHEAD(fd, MAX_SKILL_PRODUCE_DB * 8 + 8);
	WFIFOW(fd, 0)=0x18d;

	for(i=0,c=0;i<MAX_SKILL_PRODUCE_DB;i++){
		if( skill_can_produce_mix(sd,skill_produce_db[i].nameid,trigger, 1) ){
			if((view = itemdb_viewid(skill_produce_db[i].nameid)) > 0)
				WFIFOW(fd,c*8+ 4)= view;
			else
				WFIFOW(fd,c*8+ 4)= skill_produce_db[i].nameid;
			WFIFOW(fd,c*8+ 6)= 0x0012;
			WFIFOL(fd,c*8+ 8)= sd->status.char_id;
			c++;
		}
	}
	WFIFOW(fd, 2)=c*8+8;
	WFIFOSET(fd,WFIFOW(fd,2));
	if(c > 0) {
		sd->menuskill_id = AM_PHARMACY;
		sd->menuskill_val = trigger;
		return 1;
	}
	return 0;
}

/*==========================================
 * Sends a status change packet to the object only, used for loading status changes. [Skotlex]
 *------------------------------------------*/
int clif_status_load(struct block_list *bl,int type, int flag)
{
	int fd;
	if (type == SI_BLANK)  //It shows nothing on the client...
		return 0;
	
	if (bl->type != BL_PC)
		return 0;

	fd = ((struct map_session_data*)bl)->fd;
	
	WFIFOHEAD(fd,packet_len(0x196));
	WFIFOW(fd,0)=0x0196;
	WFIFOW(fd,2)=type;
	WFIFOL(fd,4)=bl->id;
	WFIFOB(fd,8)=flag; //Status start
	WFIFOSET(fd, packet_len(0x196));
	return 0;
}
/*==========================================
 * ��Ԉُ�A�C�R��/���b�Z�[�W�\��
 *------------------------------------------*/
int clif_status_change(struct block_list *bl,int type,int flag)
{
	unsigned char buf[16];

	if (type == SI_BLANK)  //It shows nothing on the client...
		return 0;
	
	nullpo_retr(0, bl);

	WBUFW(buf,0)=0x0196;
	WBUFW(buf,2)=type;
	WBUFL(buf,4)=bl->id;
	WBUFB(buf,8)=flag;
	clif_send(buf,packet_len(0x196),bl,AREA);
	return 0;
}

/*==========================================
 * Send message (modified by [Yor])
 *------------------------------------------*/
int clif_displaymessage(const int fd, const char* mes)
{
	// invalid pointer?
	nullpo_retr(-1, mes);
	
	//Scrapped, as these are shared by disconnected players =X [Skotlex]
	if (fd == 0)
		return 0;
	else {
		int len_mes = strlen(mes);

		if (len_mes > 0) { // don't send a void message (it's not displaying on the client chat). @help can send void line.
			WFIFOHEAD(fd, 5 + len_mes);
			WFIFOW(fd,0) = 0x8e;
			WFIFOW(fd,2) = 5 + len_mes; // 4 + len + NULL teminate
			memcpy(WFIFOP(fd,4), mes, len_mes + 1);
			WFIFOSET(fd, 5 + len_mes);
		}
	}

	return 0;
}

/*==========================================
 * �V�̐��𑗐M����
 *------------------------------------------*/
int clif_GMmessage(struct block_list* bl, const char* mes, int len, int flag)
{
	unsigned char *buf;
	int lp;

	lp = (flag & 0x10) ? 8 : 4;
	buf = (unsigned char*)aMallocA((len + lp + 8)*sizeof(unsigned char));

	WBUFW(buf,0) = 0x9a;
	WBUFW(buf,2) = len + lp;
	WBUFL(buf,4) = 0x65756c62; //"blue":
	memcpy(WBUFP(buf,lp), mes, len);
	flag &= 0x07;
	clif_send(buf, WBUFW(buf,2), bl,
	          (flag == 1) ? ALL_SAMEMAP :
	          (flag == 2) ? AREA :
	          (flag == 3) ? SELF :
	          ALL_CLIENT);
	if(buf) aFree(buf);

	if(use_irc && irc_announce_flag && !flag)
		irc_announce(mes);

	return 0;
}

/*==========================================
 * �O���[�o�����b�Z�[�W
 *------------------------------------------*/
void clif_GlobalMessage(struct block_list* bl, const char* message)
{
	char buf[100];
	int len;

	nullpo_retv(bl);

	if(!message)
		return;

	len = strlen(message)+1;

	WBUFW(buf,0)=0x8d;
	WBUFW(buf,2)=len+8;
	WBUFL(buf,4)=bl->id;
	strncpy((char *) WBUFP(buf,8),message,len);
	clif_send((unsigned char *) buf,WBUFW(buf,2),bl,ALL_CLIENT);
}

/*==========================================
 * Send main chat message [LuzZza]
 *------------------------------------------*/
void clif_MainChatMessage(const char* message)
{
	char buf[200];
	int len;
	
	if(!message)
		return;
		
	len = strlen(message)+1;
	if (len+8 > sizeof(buf)) {
		ShowDebug("clif_MainChatMessage: Received message too long (len %d): %s\n", len, message);
		len = sizeof(buf)-8;
	}
	WBUFW(buf,0)=0x8d;
	WBUFW(buf,2)=len+8;
	WBUFL(buf,4)=0;
	strncpy((char *) WBUFP(buf,8),message,len);
	clif_send((unsigned char *) buf,WBUFW(buf,2),NULL,CHAT_MAINCHAT);
}

/*==========================================
 * Does an announce message in the given color. 
 *------------------------------------------*/
int clif_announce(struct block_list* bl, const char* mes, int len, unsigned long color, int flag)
{
	unsigned char *buf;
	buf = (unsigned char*)aMallocA((len + 16)*sizeof(unsigned char));
	WBUFW(buf,0) = 0x1c3;
	WBUFW(buf,2) = len + 16;
	WBUFL(buf,4) = color;
	WBUFW(buf,8) = 0x190; //Font style? Type?
	WBUFW(buf,10) = 0x0c;  //12? Font size?
	WBUFL(buf,12) = 0;	//Unknown!
	memcpy(WBUFP(buf,16), mes, len);
	
	flag &= 0x07;
	clif_send(buf, WBUFW(buf,2), bl,
	          (flag == 1) ? ALL_SAMEMAP :
	          (flag == 2) ? AREA :
	          (flag == 3) ? SELF :
	          ALL_CLIENT);

	if(buf) aFree(buf);
	return 0;
}
/*==========================================
 * HPSP�񕜃G�t�F�N�g�𑗐M����
 *------------------------------------------*/
int clif_heal(int fd,int type,int val)
{
	WFIFOHEAD(fd,packet_len(0x13d));
	WFIFOW(fd,0)=0x13d;
	WFIFOW(fd,2)=type;
	WFIFOW(fd,4)=cap_value(val,0,SHRT_MAX);
	WFIFOSET(fd,packet_len(0x13d));

	return 0;
}

/*==========================================
 * ��������
 *------------------------------------------*/
int clif_resurrection(struct block_list *bl,int type)
{
	unsigned char buf[16];

	nullpo_retr(0, bl);

	WBUFW(buf,0)=0x148;
	WBUFL(buf,2)=bl->id;
	WBUFW(buf,6)=type;

	clif_send(buf,packet_len(0x148),bl,type==1 ? AREA : AREA_WOS);
	if (disguised(bl))
		clif_spawn(bl);

	return 0;
}

/*==========================================
 * PVP�����H�i���j
 *------------------------------------------*/
int clif_set0199(int fd,int type)
{
	WFIFOHEAD(fd,packet_len(0x199));
	WFIFOW(fd,0)=0x199;
	WFIFOW(fd,2)=type;
	WFIFOSET(fd,packet_len(0x199));

	return 0;
}

/*==========================================
 * PVP�����H(��)
 *------------------------------------------*/
int clif_pvpset(struct map_session_data *sd,int pvprank,int pvpnum,int type)
{
	int fd = sd->fd;

	if(type == 2) {
		WFIFOHEAD(fd,packet_len(0x19a));
		WFIFOW(fd,0) = 0x19a;
		WFIFOL(fd,2) = sd->bl.id;
		WFIFOL(fd,6) = pvprank;
		WFIFOL(fd,10) = pvpnum;
		WFIFOSET(fd,packet_len(0x19a));
	} else {
		unsigned char buf[32];
		WBUFW(buf,0) = 0x19a;
		WBUFL(buf,2) = sd->bl.id;
		if(sd->sc.option&(OPTION_HIDE|OPTION_CLOAK))
			WBUFL(buf,6) = ULONG_MAX; //On client displays as --
		else
			WBUFL(buf,6) = pvprank;
		WBUFL(buf,10) = pvpnum;
		if(sd->sc.option&OPTION_INVISIBLE)
			clif_send(buf,packet_len(0x19a),&sd->bl,SELF);
		else if(!type)
			clif_send(buf,packet_len(0x19a),&sd->bl,AREA);
		else
			clif_send(buf,packet_len(0x19a),&sd->bl,ALL_SAMEMAP);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_send0199(int map,int type)
{
	struct block_list bl;
	unsigned char buf[16];

	bl.id = 0;
	bl.type = BL_NUL;
	bl.m = map;
	WBUFW(buf,0)=0x199;
	WBUFW(buf,2)=type;
	clif_send(buf,packet_len(0x199),&bl,ALL_SAMEMAP);

	return 0;
}

/*==========================================
 * ���B�G�t�F�N�g�𑗐M����
 *------------------------------------------*/
void clif_refine(int fd, int fail, int index, int val)
{
	WFIFOHEAD(fd,packet_len(0x188));
	WFIFOW(fd,0)=0x188;
	WFIFOW(fd,2)=fail;
	WFIFOW(fd,4)=index+2;
	WFIFOW(fd,6)=val;
	WFIFOSET(fd,packet_len(0x188));
}

/// result=0: "weapon upgrated: %s" MsgStringTable[911] in rgb(0,255,255)
/// result=1: "weapon upgrated: %s" MsgStringTable[912] in rgb(0,205,205)
/// result=2: "cannot upgrade %s until you level up the upgrade weapon skill" MsgStringTable[913] in rgb(255,200,200)
/// result=3: "you lack the item %s to upgrade the weapon" MsgStringTable[914] in rgb(255,200,200)
void clif_upgrademessage(int fd, int result, int item_id)
{
	WFIFOHEAD(fd,packet_len(0x223));
	WFIFOW(fd,0)=0x223;
	WFIFOL(fd,2)=result;
	WFIFOW(fd,6)=item_id;
	WFIFOSET(fd,packet_len(0x223));
}

/*==========================================
 * Wisp/page is transmitted to the destination player
 *------------------------------------------*/
int clif_wis_message(int fd, const char* nick, const char* mes, int mes_len) // R 0097 <len>.w <nick>.24B <message>.?B
{
//	printf("clif_wis_message(%d, %s, %s)\n", fd, nick, mes);

	WFIFOHEAD(fd, mes_len + NAME_LENGTH + 4);
	WFIFOW(fd,0) = 0x97;
	WFIFOW(fd,2) = mes_len + NAME_LENGTH + 4;
	memcpy(WFIFOP(fd,4), nick, NAME_LENGTH);
	memcpy(WFIFOP(fd,28), mes, mes_len);
	WFIFOSET(fd,WFIFOW(fd,2));
	return 0;
}

/*==========================================
 * Inform the player about the result of his whisper action
 * R 0098 <type>.B
 * 0: success to send wisper
 * 1: target character is not loged in
 * 2: ignored by target
 * 3: everyone ignored by target
 *------------------------------------------*/
int clif_wis_end(int fd, int flag)
{
	WFIFOHEAD(fd,packet_len(0x98));
	WFIFOW(fd,0) = 0x98;
	WFIFOW(fd,2) = flag;
	WFIFOSET(fd,packet_len(0x98));
	return 0;
}

/*==========================================
 * �L����ID���O�������ʂ𑗐M����
 *------------------------------------------*/
int clif_solved_charname(int fd, int charid, const char* name)
{
	WFIFOHEAD(fd,packet_len(0x194));
	WFIFOW(fd,0)=0x194;
	WFIFOL(fd,2)=charid;
	safestrncpy(WFIFOP(fd,6), name, NAME_LENGTH);
	WFIFOSET(fd,packet_len(0x194));
	return 0;
}

/*==========================================
 * �J�[�h�̑}���\���X�g��Ԃ�
 *------------------------------------------*/
int clif_use_card(struct map_session_data *sd,int idx)
{
	int i,c,ep;
	int fd=sd->fd;

	nullpo_retr(0, sd);
	if (idx < 0 || idx >= MAX_INVENTORY) //Crash-fix from bad packets.
		return 0;

	if (!sd->inventory_data[idx] || sd->inventory_data[idx]->type != IT_CARD)
		return 0; //Avoid parsing invalid item indexes (no card/no item)
			
	ep=sd->inventory_data[idx]->equip;
	WFIFOHEAD(fd,MAX_INVENTORY * 2 + 4);
	WFIFOW(fd,0)=0x017b;

	for(i=c=0;i<MAX_INVENTORY;i++){
		int j;

		if(sd->inventory_data[i] == NULL)
			continue;
		if(sd->inventory_data[i]->type!=IT_WEAPON && sd->inventory_data[i]->type!=IT_ARMOR)
			continue;
		if(itemdb_isspecial(sd->status.inventory[i].card[0])) //Can't slot it
			continue;

		if(sd->status.inventory[i].identify==0 )	//Not identified
			continue;

		if((sd->inventory_data[i]->equip&ep)==0)	//Not equippable on this part.
			continue;

		if(sd->inventory_data[i]->type==IT_WEAPON && ep==EQP_HAND_L) //Shield card won't go on left weapon.
			continue;

		for(j=0;j<sd->inventory_data[i]->slot;j++){
			if( sd->status.inventory[i].card[j]==0 )
				break;
		}
		if(j==sd->inventory_data[i]->slot)	// No room
			continue;

		WFIFOW(fd,4+c*2)=i+2;
		c++;
	}
	WFIFOW(fd,2)=4+c*2;
	WFIFOSET(fd,WFIFOW(fd,2));

	return 0;
}
/*==========================================
 * �J�[�h�̑}���I��
 *------------------------------------------*/
int clif_insert_card(struct map_session_data *sd,int idx_equip,int idx_card,int flag)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x17d));
	WFIFOW(fd,0)=0x17d;
	WFIFOW(fd,2)=idx_equip+2;
	WFIFOW(fd,4)=idx_card+2;
	WFIFOB(fd,6)=flag;
	WFIFOSET(fd,packet_len(0x17d));
	return 0;
}

/*==========================================
 * �Ӓ�\�A�C�e�����X�g���M
 *------------------------------------------*/
int clif_item_identify_list(struct map_session_data *sd)
{
	int i,c;
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;

	WFIFOHEAD(fd,MAX_INVENTORY * 2 + 4);
	WFIFOW(fd,0)=0x177;
	for(i=c=0;i<MAX_INVENTORY;i++){
		if(sd->status.inventory[i].nameid > 0 && !sd->status.inventory[i].identify){
			WFIFOW(fd,c*2+4)=i+2;
			c++;
		}
	}
	if(c > 0) {
		WFIFOW(fd,2)=c*2+4;
		WFIFOSET(fd,WFIFOW(fd,2));
		sd->menuskill_id = MC_IDENTIFY;
		sd->menuskill_val = c;
	}
	return 0;
}

/*==========================================
 * �Ӓ茋��
 *------------------------------------------*/
int clif_item_identified(struct map_session_data *sd,int idx,int flag)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x179));
	WFIFOW(fd, 0)=0x179;
	WFIFOW(fd, 2)=idx+2;
	WFIFOB(fd, 4)=flag;
	WFIFOSET(fd,packet_len(0x179));
	return 0;
}

/*==========================================
 * �C���\�A�C�e�����X�g���M
 *------------------------------------------*/
int clif_item_repair_list(struct map_session_data *sd,struct map_session_data *dstsd)
{
	int i,c;
	int fd;
	int nameid;

	nullpo_retr(0, sd);
	nullpo_retr(0, dstsd);

	fd=sd->fd;

	WFIFOHEAD(fd, MAX_INVENTORY * 13 + 4);
	WFIFOW(fd,0)=0x1fc;
	for(i=c=0;i<MAX_INVENTORY;i++){
		if((nameid=dstsd->status.inventory[i].nameid) > 0 && dstsd->status.inventory[i].attribute!=0){// && skill_can_repair(sd,nameid)){
			WFIFOW(fd,c*13+4) = i;
			WFIFOW(fd,c*13+6) = nameid;
			WFIFOL(fd,c*13+8) = sd->status.char_id;
			WFIFOL(fd,c*13+12)= dstsd->status.char_id;
			WFIFOB(fd,c*13+16)= c;
			c++;
		}
	}
	if(c > 0) {
		WFIFOW(fd,2)=c*13+4;
		WFIFOSET(fd,WFIFOW(fd,2));
		sd->menuskill_id = BS_REPAIRWEAPON;
		sd->menuskill_val = dstsd->bl.id;
	}else
		clif_skill_fail(sd,sd->ud.skillid,0,0);

	return 0;
}
int clif_item_repaireffect(struct map_session_data *sd,int nameid,int flag)
{
	int view,fd;

	nullpo_retr(0, sd);
	fd=sd->fd;

	WFIFOHEAD(fd,packet_len(0x1fe));
	WFIFOW(fd, 0)=0x1fe;
	if((view = itemdb_viewid(nameid)) > 0)
		WFIFOW(fd, 2)=view;
	else
		WFIFOW(fd, 2)=nameid;
	WFIFOB(fd, 4)=flag;
	WFIFOSET(fd,packet_len(0x1fe));

	return 0;
}

/*==========================================
 * Weapon Refining - Taken from jAthena
 *------------------------------------------*/
int clif_item_refine_list(struct map_session_data *sd)
{
	int i,c;
	int fd;
	int skilllv;
	int wlv;
	int refine_item[5];

	nullpo_retr(0, sd);

	skilllv = pc_checkskill(sd,WS_WEAPONREFINE);

	fd=sd->fd;

	refine_item[0] = -1;
	refine_item[1] = pc_search_inventory(sd,1010);
	refine_item[2] = pc_search_inventory(sd,1011);
	refine_item[3] = refine_item[4] = pc_search_inventory(sd,984);

	WFIFOHEAD(fd, MAX_INVENTORY * 13 + 4);
	WFIFOW(fd,0)=0x221;
	for(i=c=0;i<MAX_INVENTORY;i++){
		if(sd->status.inventory[i].nameid > 0 && sd->status.inventory[i].refine < skilllv &&
			sd->status.inventory[i].identify && (wlv=itemdb_wlv(sd->status.inventory[i].nameid)) >=1 &&
			refine_item[wlv]!=-1 && !(sd->status.inventory[i].equip&0x0022)){
			WFIFOW(fd,c*13+ 4)=i+2;
			WFIFOW(fd,c*13+ 6)=sd->status.inventory[i].nameid;
			WFIFOW(fd,c*13+ 8)=0; //TODO: Wonder what are these for? Perhaps ID of weapon's crafter if any?
			WFIFOW(fd,c*13+10)=0;
			WFIFOB(fd,c*13+12)=c;
			c++;
		}
	}
	WFIFOW(fd,2)=c*13+4;
	WFIFOSET(fd,WFIFOW(fd,2));
	if (c > 0) {
		sd->menuskill_id = WS_WEAPONREFINE;
		sd->menuskill_val = skilllv;
	}
	return 0;
}

/*==========================================
 * �A�C�e���ɂ��ꎞ�I�ȃX�L������
 *------------------------------------------*/
int clif_item_skill(struct map_session_data *sd,int skillid,int skilllv)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x147));
	WFIFOW(fd, 0)=0x147;
	WFIFOW(fd, 2)=skillid;
	WFIFOW(fd, 4)=skill_get_inf(skillid);
	WFIFOW(fd, 6)=0;
	WFIFOW(fd, 8)=skilllv;
	WFIFOW(fd,10)=skill_get_sp(skillid,skilllv);
	WFIFOW(fd,12)=skill_get_range2(&sd->bl, skillid,skilllv);
//	strncpy((char*)WFIFOP(fd,14),skill_get_name(skillid),NAME_LENGTH);
	WFIFOB(fd,38)=0;
	WFIFOSET(fd,packet_len(0x147));
	return 0;
}

/*==========================================
 * �J�[�g�ɃA�C�e���ǉ�
 *------------------------------------------*/
int clif_cart_additem(struct map_session_data *sd,int n,int amount,int fail)
{
	int view,fd;
	unsigned char *buf;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x124));
	buf=WFIFOP(fd,0);
	if(n<0 || n>=MAX_CART || sd->status.cart[n].nameid<=0)
		return 1;

	WBUFW(buf,0)=0x124;
	WBUFW(buf,2)=n+2;
	WBUFL(buf,4)=amount;
	if((view = itemdb_viewid(sd->status.cart[n].nameid)) > 0)
		WBUFW(buf,8)=view;
	else
		WBUFW(buf,8)=sd->status.cart[n].nameid;
	WBUFB(buf,10)=sd->status.cart[n].identify;
	WBUFB(buf,11)=sd->status.cart[n].attribute;
	WBUFB(buf,12)=sd->status.cart[n].refine;
	clif_addcards(WBUFP(buf,13), &sd->status.cart[n]);
	WFIFOSET(fd,packet_len(0x124));
	return 0;
}

/*==========================================
 * �J�[�g����A�C�e���폜
 *------------------------------------------*/
int clif_cart_delitem(struct map_session_data *sd,int n,int amount)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;

	WFIFOHEAD(fd,packet_len(0x125));
	WFIFOW(fd,0)=0x125;
	WFIFOW(fd,2)=n+2;
	WFIFOL(fd,4)=amount;

	WFIFOSET(fd,packet_len(0x125));

	return 0;
}

/*==========================================
 * Opens the shop creation menu.
 * R 012d <num>.w
 * 'num' is the number of allowed item slots
 *------------------------------------------*/
void clif_openvendingreq(struct map_session_data* sd, int num)
{
	int fd;

	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0x12d));
	WFIFOW(fd,0) = 0x12d;
	WFIFOW(fd,2) = num;
	WFIFOSET(fd,packet_len(0x12d));
}

/*==========================================
 * Displays a vending board to target/area
 * R 0131 <ID>.l <message>.80B
 *------------------------------------------*/
void clif_showvendingboard(struct block_list* bl, const char* message, int fd)
{
	unsigned char buf[128];

	nullpo_retv(bl);

	WBUFW(buf,0) = 0x131;
	WBUFL(buf,2) = bl->id;
	safestrncpy((char*)WBUFP(buf,6), message, 80);

	if( fd ) {
		WFIFOHEAD(fd,packet_len(0x131));
		memcpy(WFIFOP(fd,0),buf,packet_len(0x131));
		WFIFOSET(fd,packet_len(0x131));
	} else {
		clif_send(buf,packet_len(0x131),bl,AREA_WOS);
	}
}

/*==========================================
 * Removes a vending board from screen
 *------------------------------------------*/
void clif_closevendingboard(struct block_list* bl, int fd)
{
	unsigned char buf[16];

	nullpo_retv(bl);

	WBUFW(buf,0) = 0x132;
	WBUFL(buf,2) = bl->id;
	if( fd ) {
		WFIFOHEAD(fd,packet_len(0x132));
		memcpy(WFIFOP(fd,0),buf,packet_len(0x132));
		WFIFOSET(fd,packet_len(0x132));
	} else {
		clif_send(buf,packet_len(0x132),bl,AREA_WOS);
	}
}

/*==========================================
 * Sends a list of items in a shop
 * R 0133 <len>.w <ID>.l {<value>.l <amount>.w <index>.w <type>.B <item ID>.w <identify flag>.B <attribute?>.B <refine>.B <card>.4w}.22B
 *------------------------------------------*/
void clif_vendinglist(struct map_session_data* sd, int id, struct s_vending* vending)
{
	int i,fd;
	int count;
	struct map_session_data* vsd;

	nullpo_retv(sd);
	nullpo_retv(vending);
	nullpo_retv(vsd=map_id2sd(id));

	fd = sd->fd;
	count = vsd->vend_num;

    WFIFOHEAD(fd, 8+count*22);
	WFIFOW(fd,0) = 0x133;
	WFIFOW(fd,2) = 8+count*22;
	WFIFOL(fd,4) = id;
	for( i = 0; i < count; i++ )
	{
		int index = vending[i].index;
		struct item_data* data = itemdb_search(vsd->status.cart[index].nameid);
		WFIFOL(fd, 8+i*22) = vending[i].value;
		WFIFOW(fd,12+i*22) = vending[i].amount;
		WFIFOW(fd,14+i*22) = vending[i].index + 2;
		WFIFOB(fd,16+i*22) = itemtype(data->type);
		WFIFOW(fd,17+i*22) = ( data->view_id > 0 ) ? data->view_id : vsd->status.cart[index].nameid;
		WFIFOB(fd,19+i*22) = vsd->status.cart[index].identify;
		WFIFOB(fd,20+i*22) = vsd->status.cart[index].attribute;
		WFIFOB(fd,21+i*22) = vsd->status.cart[index].refine;
		clif_addcards(WFIFOP(fd, 22+i*22), &vsd->status.cart[index]);
	}
	WFIFOSET(fd,WFIFOW(fd,2));
}

/*==========================================
 * Shop purchase failure
 * R 0135 <index>.w <amount>.w <fail>.B
 * fail=1 - not enough zeny
 * fail=2 - overweight
 * fail=4 - out of stock
 * fail=5 - "cannot use an npc shop while in a trade"
 *------------------------------------------*/
void clif_buyvending(struct map_session_data* sd, int index, int amount, int fail)
{
	int fd;

	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0x135));
	WFIFOW(fd,0) = 0x135;
	WFIFOW(fd,2) = index+2;
	WFIFOW(fd,4) = amount;
	WFIFOB(fd,6) = fail;
	WFIFOSET(fd,packet_len(0x135));
}

/*==========================================
 * Shop creation success
 * R 0136 <len>.w <ID>.l {<value>.l <index>.w <amount>.w <type>.B <item ID>.w <identify flag>.B <attribute?>.B <refine>.B <card>.4w}.22B*
 *------------------------------------------*/
void clif_openvending(struct map_session_data* sd, int id, struct s_vending* vending)
{
	int i,fd;
	int count;

	nullpo_retv(sd);

	fd = sd->fd;
	count = sd->vend_num;

	WFIFOHEAD(fd, 8+count*22);
	WFIFOW(fd,0) = 0x136;
	WFIFOW(fd,2) = 8+count*22;
	WFIFOL(fd,4) = id;
	for( i = 0; i < count; i++ )
	{
		int index = vending[i].index;
		struct item_data* data = itemdb_search(sd->status.cart[index].nameid);
		WFIFOL(fd, 8+i*22) = vending[i].value;
		WFIFOW(fd,12+i*22) = vending[i].index + 2;
		WFIFOW(fd,14+i*22) = vending[i].amount;
		WFIFOB(fd,16+i*22) = itemtype(data->type);
		WFIFOW(fd,17+i*22) = ( data->view_id > 0 ) ? data->view_id : sd->status.cart[index].nameid;
		WFIFOB(fd,19+i*22) = sd->status.cart[index].identify;
		WFIFOB(fd,20+i*22) = sd->status.cart[index].attribute;
		WFIFOB(fd,21+i*22) = sd->status.cart[index].refine;
		clif_addcards(WFIFOP(fd,22+count*22), &sd->status.cart[index]);
	}
	WFIFOSET(fd,WFIFOW(fd,2));
}

/*==========================================
 * Inform merchant that someone has bought an item.
 * R 0137 <index>.w <amount>.w
 *------------------------------------------*/
void clif_vendingreport(struct map_session_data* sd, int index, int amount)
{
	int fd;

	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0x137));
	WFIFOW(fd,0) = 0x137;
	WFIFOW(fd,2) = index+2;
	WFIFOW(fd,4) = amount;
	WFIFOSET(fd,packet_len(0x137));
}
/*==========================================
 * �p�[�e�B�쐬����
 *------------------------------------------*/
int clif_party_created(struct map_session_data *sd,int flag)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xfa));
	WFIFOW(fd,0)=0xfa;
	WFIFOB(fd,2)=flag;
	WFIFOSET(fd,packet_len(0xfa));
	return 0;
}

int clif_party_member_info(struct party_data *p, struct map_session_data *sd)
{
	unsigned char buf[96];

	if (!sd) { //Pick any party member (this call is used when changing item share rules)
		int i;
		for (i=0; i<MAX_PARTY && !p->data[i].sd; i++);
		if (i >= MAX_PARTY) return 0; //Should never happen...
		sd = p->data[i].sd;
	}

	WBUFW(buf, 0) = 0x1e9;
	WBUFL(buf, 2) = sd->status.account_id;
	WBUFL(buf, 6) = 0; //Apparently setting this to 1 makes you adoptable.
	WBUFW(buf,10) = sd->bl.x;
	WBUFW(buf,12) = sd->bl.y;
	WBUFB(buf,14) = 0; //Unconfirmed byte, could be online/offline.
	memcpy(WBUFP(buf,15), p->party.name, NAME_LENGTH);
	memcpy(WBUFP(buf,39), sd->status.name, NAME_LENGTH);
	mapindex_getmapname_ext(mapindex_id2name(sd->mapindex), (char*)WBUFP(buf,63));
	WBUFB(buf,79) = (p->party.item&1)?1:0;
	WBUFB(buf,80) = (p->party.item&2)?1:0;
	clif_send(buf,packet_len(0x1e9),&sd->bl,PARTY);
	return 1;
}


/*==========================================
 * Sends party information
 * R 00fb <len>.w <party name>.24B {<ID>.l <nick>.24B <map name>.16B <leader>.B <offline>.B}.46B*
 *------------------------------------------*/
int clif_party_info(struct party_data* p, struct map_session_data *sd)
{
	unsigned char buf[2+2+NAME_LENGTH+(4+NAME_LENGTH+MAP_NAME_LENGTH_EXT+1+1)*MAX_PARTY];
	struct map_session_data* party_sd = NULL;
	int i, c;

	nullpo_retr(0, p);

	WBUFW(buf,0) = 0xfb;
	memcpy(WBUFP(buf,4), p->party.name, NAME_LENGTH);
	for(i = 0, c = 0; i < MAX_PARTY; i++)
	{
		struct party_member* m = &p->party.member[i];
		if(!m->account_id) continue;

		if(party_sd == NULL) party_sd = p->data[i].sd;

		WBUFL(buf,28+c*46) = m->account_id;
		memcpy(WBUFP(buf,28+c*46+4), m->name, NAME_LENGTH);
		mapindex_getmapname_ext(mapindex_id2name(m->map), (char*)WBUFP(buf,28+c*46+28));
		WBUFB(buf,28+c*46+44) = (m->leader) ? 0 : 1;
		WBUFB(buf,28+c*46+45) = (m->online) ? 0 : 1;
		c++;
	}
	WBUFW(buf,2) = 28+c*46;

	if(sd) { // send only to self
		clif_send(buf, WBUFW(buf,2), &sd->bl, SELF);
	} else if (party_sd) { // send to whole party
		clif_send(buf, WBUFW(buf,2), &party_sd->bl, PARTY);
	}
	
	return 0;
}
/*==========================================
 * �p�[�e�B���U
 *------------------------------------------*/
int clif_party_invite(struct map_session_data *sd,struct map_session_data *tsd)
{
	int fd;
	struct party_data *p;

	nullpo_retr(0, sd);
	nullpo_retr(0, tsd);

	fd=tsd->fd;

	if( (p=party_search(sd->status.party_id))==NULL )
		return 0;

	WFIFOHEAD(fd,packet_len(0xfe));
	WFIFOW(fd,0)=0xfe;
	WFIFOL(fd,2)=sd->status.account_id;
	memcpy(WFIFOP(fd,6),p->party.name,NAME_LENGTH);
	WFIFOSET(fd,packet_len(0xfe));
	return 0;
}

/*==========================================
 * Party invitation result. Flag values are:
 * 0 -> char is already in a party
 * 1 -> party invite was rejected
 * 2 -> party invite was accepted
 * 3 -> party is full
 * 4 -> char of the same account already joined the party
 *------------------------------------------*/
int clif_party_inviteack(struct map_session_data* sd, const char* nick, int flag)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xfd));
	WFIFOW(fd,0)=0xfd;
	memcpy(WFIFOP(fd,2),nick,NAME_LENGTH);
	WFIFOB(fd,26)=flag;
	WFIFOSET(fd,packet_len(0xfd));
	return 0;
}

/*==========================================
 * �p�[�e�B�ݒ著�M
 * flag & 0x001=exp�ύX�~�X
 *        0x010=item�ύX�~�X
 *        0x100=��l�ɂ̂ݑ��M
 *------------------------------------------*/
int clif_party_option(struct party_data *p,struct map_session_data *sd,int flag)
{
	unsigned char buf[16];

	nullpo_retr(0, p);

	if(!sd && flag==0){
		int i;
		for(i=0;i<MAX_PARTY && !p->data[i].sd;i++);
		if (i < MAX_PARTY)
			sd = p->data[i].sd;
	}
	if(!sd) return 0;
	WBUFW(buf,0)=0x101;
	// WBUFL(buf,2) // that's how the client reads it, still need to check it's uses [FlavioJS]
	WBUFW(buf,2)=((flag&0x01)?2:p->party.exp);
	WBUFW(buf,4)=0;
	if(flag==0)
		clif_send(buf,packet_len(0x101),&sd->bl,PARTY);
	else
		clif_send(buf,packet_len(0x101),&sd->bl,SELF);
	return 0;
}
/*==========================================
 * �p�[�e�B�E�ށi�E�ޑO�ɌĂԂ��Ɓj
 *------------------------------------------*/
int clif_party_leaved(struct party_data* p, struct map_session_data* sd, int account_id, const char* name, int flag)
{
	unsigned char buf[64];
	int i;

	nullpo_retr(0, p);

	if(!sd && (flag&0xf0)==0)
	{
		for(i=0;i<MAX_PARTY && !p->data[i].sd;i++);
			if (i < MAX_PARTY)
				sd = p->data[i].sd;
	}

	if(!sd) return 0;

	WBUFW(buf,0)=0x105;
	WBUFL(buf,2)=account_id;
	memcpy(WBUFP(buf,6),name,NAME_LENGTH);
	WBUFB(buf,30)=flag&0x0f;

	if((flag&0xf0)==0)
		clif_send(buf,packet_len(0x105),&sd->bl,PARTY);
	 else
		clif_send(buf,packet_len(0x105),&sd->bl,SELF);
	return 0;
}
/*==========================================
 * �p�[�e�B���b�Z�[�W���M
 *------------------------------------------*/
int clif_party_message(struct party_data* p, int account_id, const char* mes, int len)
{
	struct map_session_data *sd;
	int i;

	nullpo_retr(0, p);

	for(i=0; i < MAX_PARTY && !p->data[i].sd;i++);
	if(i < MAX_PARTY){
		unsigned char buf[1024];
		sd = p->data[i].sd;
		WBUFW(buf,0)=0x109;
		WBUFW(buf,2)=len+8;
		WBUFL(buf,4)=account_id;
		memcpy(WBUFP(buf,8),mes,len);
		clif_send(buf,len+8,&sd->bl,PARTY);
	}
	return 0;
}
/*==========================================
 * �p�[�e�B���W�ʒm
 *------------------------------------------*/
int clif_party_xy(struct map_session_data *sd)
{
	unsigned char buf[16];

	nullpo_retr(0, sd);

	WBUFW(buf,0)=0x107;
	WBUFL(buf,2)=sd->status.account_id;
	WBUFW(buf,6)=sd->bl.x;
	WBUFW(buf,8)=sd->bl.y;
	clif_send(buf,packet_len(0x107),&sd->bl,PARTY_SAMEMAP_WOS);
	
	return 0;
}

/*==========================================
 * Sends x/y dot to a single fd. [Skotlex]
 *------------------------------------------*/
int clif_party_xy_single(int fd, struct map_session_data *sd)
{
	WFIFOHEAD(fd,packet_len(0x107));
	WFIFOW(fd,0)=0x107;
	WFIFOL(fd,2)=sd->status.account_id;
	WFIFOW(fd,6)=sd->bl.x;
	WFIFOW(fd,8)=sd->bl.y;
	WFIFOSET(fd,packet_len(0x107));
	return 0;
}


/*==========================================
 * �p�[�e�BHP�ʒm
 *------------------------------------------*/
int clif_party_hp(struct map_session_data *sd)
{
	unsigned char buf[16];

	nullpo_retr(0, sd);

	WBUFW(buf,0)=0x106;
	WBUFL(buf,2)=sd->status.account_id;
	if (sd->battle_status.max_hp > SHRT_MAX) { //To correctly display the %hp bar. [Skotlex]
		WBUFW(buf,6) = sd->battle_status.hp/(sd->battle_status.max_hp/100);
		WBUFW(buf,8) = 100;
	} else {
		WBUFW(buf,6) = sd->battle_status.hp;
		WBUFW(buf,8) = sd->battle_status.max_hp;
	}
	clif_send(buf,packet_len(0x106),&sd->bl,PARTY_AREA_WOS);
	return 0;
}

/*==========================================
 * Sends HP bar to a single fd. [Skotlex]
 *------------------------------------------*/
static void clif_hpmeter_single(int fd, struct map_session_data *sd)
{
	WFIFOHEAD(fd,packet_len(0x106));
	WFIFOW(fd,0) = 0x106;
	WFIFOL(fd,2) = sd->status.account_id;
	if (sd->battle_status.max_hp > SHRT_MAX) { //To correctly display the %hp bar. [Skotlex]
		WFIFOW(fd,6) = sd->battle_status.hp/(sd->battle_status.max_hp/100);
		WFIFOW(fd,8) = 100;
	} else {
		WFIFOW(fd,6) = sd->battle_status.hp;
		WFIFOW(fd,8) = sd->battle_status.max_hp;
	}
	WFIFOSET (fd, packet_len(0x106));
}

/*==========================================
 * GM�֏ꏊ��HP�ʒm
 *------------------------------------------*/
int clif_hpmeter(struct map_session_data *sd)
{
	struct map_session_data *sd2;
	unsigned char buf[16];
	int i, x0, y0, x1, y1;
	int level;

	nullpo_retr(0, sd);

	x0 = sd->bl.x - AREA_SIZE;
	y0 = sd->bl.y - AREA_SIZE;
	x1 = sd->bl.x + AREA_SIZE;
	y1 = sd->bl.y + AREA_SIZE;

	WBUFW(buf,0) = 0x106;
	WBUFL(buf,2) = sd->status.account_id;
	if (sd->battle_status.max_hp > SHRT_MAX) { //To correctly display the %hp bar. [Skotlex]
		WBUFW(buf,6) = sd->battle_status.hp/(sd->battle_status.max_hp/100);
		WBUFW(buf,8) = 100;
	} else {
		WBUFW(buf,6) = sd->battle_status.hp;
		WBUFW(buf,8) = sd->battle_status.max_hp;
	}
	for (i = 0; i < fd_max; i++) {
		if (session[i] && session[i]->func_parse == clif_parse &&	
			(sd2 = (struct map_session_data*)session[i]->session_data) &&
			sd != sd2 && sd2->state.auth) {
			if (sd2->bl.m != sd->bl.m || 
				sd2->bl.x < x0 || sd2->bl.y < y0 ||
				sd2->bl.x > x1 || sd2->bl.y > y1 ||
				(level = pc_isGM(sd2)) < battle_config.disp_hpmeter ||
				level < pc_isGM(sd))
				continue;
			WFIFOHEAD (i, packet_len(0x106));
			memcpy (WFIFOP(i,0), buf, packet_len(0x106));
			WFIFOSET (i, packet_len(0x106));
		}
	}

	return 0;
}

/*==========================================
 * �p�[�e�B�ꏊ�ړ��i���g�p�j
 *------------------------------------------*/
void clif_party_move(struct party* p, struct map_session_data* sd, int online)
{
	unsigned char buf[128];

	nullpo_retv(sd);
	nullpo_retv(p);

	WBUFW(buf, 0) = 0x104;
	WBUFL(buf, 2) = sd->status.account_id;
	WBUFL(buf, 6) = 0;
	WBUFW(buf,10) = sd->bl.x;
	WBUFW(buf,12) = sd->bl.y;
	WBUFB(buf,14) = !online;
	memcpy(WBUFP(buf,15),p->name, NAME_LENGTH);
	memcpy(WBUFP(buf,39),sd->status.name, NAME_LENGTH);
	mapindex_getmapname_ext(map[sd->bl.m].name, (char*)WBUFP(buf,63));
	clif_send(buf,packet_len(0x104),&sd->bl,PARTY);
}
/*==========================================
 * �U�����邽�߂Ɉړ����K�v
 *------------------------------------------*/
int clif_movetoattack(struct map_session_data *sd,struct block_list *bl)
{
	int fd;

	nullpo_retr(0, sd);
	nullpo_retr(0, bl);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x139));
	WFIFOW(fd, 0)=0x139;
	WFIFOL(fd, 2)=bl->id;
	WFIFOW(fd, 6)=bl->x;
	WFIFOW(fd, 8)=bl->y;
	WFIFOW(fd,10)=sd->bl.x;
	WFIFOW(fd,12)=sd->bl.y;
	WFIFOW(fd,14)=sd->battle_status.rhw.range;
	WFIFOSET(fd,packet_len(0x139));
	return 0;
}
/*==========================================
 * �����G�t�F�N�g
 *------------------------------------------*/
int clif_produceeffect(struct map_session_data* sd,int flag,int nameid)
{
	int fd;

	nullpo_retr(0, sd);

	fd = sd->fd;
	clif_solved_charname(fd, sd->status.char_id, sd->status.name);
	WFIFOHEAD(fd,packet_len(0x18f));
	WFIFOW(fd, 0)=0x18f;
	WFIFOW(fd, 2)=flag;
	WFIFOW(fd, 4)=(itemdb_viewid(nameid)||nameid);
	WFIFOSET(fd,packet_len(0x18f));
	return 0;
}

// pet
int clif_catch_process(struct map_session_data *sd)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x19e));
	WFIFOW(fd,0)=0x19e;
	WFIFOSET(fd,packet_len(0x19e));
	return 0;
}

int clif_pet_roulette(struct map_session_data *sd,int data)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x1a0));
	WFIFOW(fd,0)=0x1a0;
	WFIFOB(fd,2)=data;
	WFIFOSET(fd,packet_len(0x1a0));

	return 0;
}

/*==========================================
 * pet�����X�g�쐬
 *------------------------------------------*/
int clif_sendegg(struct map_session_data *sd)
{
	//R 01a6 <len>.w <index>.w*
	int i,n=0,fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	if (battle_config.pet_no_gvg && map_flag_gvg(sd->bl.m))
	{	//Disable pet hatching in GvG grounds during Guild Wars [Skotlex]
		clif_displaymessage(fd, "Pets are not allowed in Guild Wars.");
		return 0;
	}
	WFIFOHEAD(fd, MAX_INVENTORY * 2 + 4);
	WFIFOW(fd,0)=0x1a6;
	if(sd->status.pet_id <= 0) {
		for(i=0,n=0;i<MAX_INVENTORY;i++){
			if(sd->status.inventory[i].nameid<=0 || sd->inventory_data[i] == NULL ||
			   sd->inventory_data[i]->type!=IT_PETEGG ||
			   sd->status.inventory[i].amount<=0)
				continue;
			WFIFOW(fd,n*2+4)=i+2;
			n++;
		}
	}
	WFIFOW(fd,2)=4+n*2;
	WFIFOSET(fd,WFIFOW(fd,2));

	sd->menuskill_id = SA_TAMINGMONSTER;
	sd->menuskill_val = -1;
	return 0;
}

/*==========================================
 * Sends a specific pet data update.
 * type = 0 -> param = 0 (initial data)
 * type = 1 -> param = intimacy value
 * type = 2 -> param = hungry value
 * type = 3 -> param = accessory id
 * type = 4 -> param = performance number (1-3:normal, 4:special)
 * type = 5 -> param = hairstyle number
 * If sd is null, the update is sent to nearby objects, otherwise it is sent only to that player.
 *------------------------------------------*/
int clif_send_petdata(struct map_session_data* sd, struct pet_data* pd, int type, int param)
{
	uint8 buf[16];
	nullpo_retr(0, pd);

	WBUFW(buf,0) = 0x1a4;
	WBUFB(buf,2) = type;
	WBUFL(buf,3) = pd->bl.id;
	WBUFL(buf,7) = param;
	if (sd)
		clif_send(buf, packet_len(0x1a4), &sd->bl, SELF);
	else
		clif_send(buf, packet_len(0x1a4), &pd->bl, AREA);
	return 0;
}

int clif_send_petstatus(struct map_session_data *sd)
{
	int fd;
	struct s_pet *pet;

	nullpo_retr(0, sd);
	nullpo_retr(0, sd->pd);

	fd=sd->fd;
	pet = &sd->pd->pet;
	WFIFOHEAD(fd,packet_len(0x1a2));
	WFIFOW(fd,0)=0x1a2;
	memcpy(WFIFOP(fd,2),pet->name,NAME_LENGTH);
	WFIFOB(fd,26)=battle_config.pet_rename?0:pet->rename_flag;
	WFIFOW(fd,27)=pet->level;
	WFIFOW(fd,29)=pet->hungry;
	WFIFOW(fd,31)=pet->intimate;
	WFIFOW(fd,33)=pet->equip;
	WFIFOSET(fd,packet_len(0x1a2));

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_pet_emotion(struct pet_data *pd,int param)
{
	unsigned char buf[16];

	nullpo_retr(0, pd);

	memset(buf,0,packet_len(0x1aa));

	WBUFW(buf,0)=0x1aa;
	WBUFL(buf,2)=pd->bl.id;
	if(param >= 100 && pd->petDB->talk_convert_class) {
		if(pd->petDB->talk_convert_class < 0)
			return 0;
		else if(pd->petDB->talk_convert_class > 0) {
			param -= (pd->pet.class_ - 100)*100;
			param += (pd->petDB->talk_convert_class - 100)*100;
		}
	}
	WBUFL(buf,6)=param;

	clif_send(buf,packet_len(0x1aa),&pd->bl,AREA);

	return 0;
}

int clif_pet_food(struct map_session_data *sd,int foodid,int fail)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x1a3));
	WFIFOW(fd,0)=0x1a3;
	WFIFOB(fd,2)=fail;
	WFIFOW(fd,3)=foodid;
	WFIFOSET(fd,packet_len(0x1a3));

	return 0;
}

/*==========================================
 * �I�[�g�X�y�� ���X�g���M
 *------------------------------------------*/
int clif_autospell(struct map_session_data *sd,int skilllv)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x1cd));
	WFIFOW(fd, 0)=0x1cd;

	if(skilllv>0 && pc_checkskill(sd,MG_NAPALMBEAT)>0)
		WFIFOL(fd,2)= MG_NAPALMBEAT;
	else
		WFIFOL(fd,2)= 0x00000000;
	if(skilllv>1 && pc_checkskill(sd,MG_COLDBOLT)>0)
		WFIFOL(fd,6)= MG_COLDBOLT;
	else
		WFIFOL(fd,6)= 0x00000000;
	if(skilllv>1 && pc_checkskill(sd,MG_FIREBOLT)>0)
		WFIFOL(fd,10)= MG_FIREBOLT;
	else
		WFIFOL(fd,10)= 0x00000000;
	if(skilllv>1 && pc_checkskill(sd,MG_LIGHTNINGBOLT)>0)
		WFIFOL(fd,14)= MG_LIGHTNINGBOLT;
	else
		WFIFOL(fd,14)= 0x00000000;
	if(skilllv>4 && pc_checkskill(sd,MG_SOULSTRIKE)>0)
		WFIFOL(fd,18)= MG_SOULSTRIKE;
	else
		WFIFOL(fd,18)= 0x00000000;
	if(skilllv>7 && pc_checkskill(sd,MG_FIREBALL)>0)
		WFIFOL(fd,22)= MG_FIREBALL;
	else
		WFIFOL(fd,22)= 0x00000000;
	if(skilllv>9 && pc_checkskill(sd,MG_FROSTDIVER)>0)
		WFIFOL(fd,26)= MG_FROSTDIVER;
	else
		WFIFOL(fd,26)= 0x00000000;

	WFIFOSET(fd,packet_len(0x1cd));
	sd->menuskill_id = SA_AUTOSPELL;
	sd->menuskill_val = skilllv;
	
	return 0;
}

/*==========================================
 * �f�B�{�[�V�����̐���
 *------------------------------------------*/
int clif_devotion(struct map_session_data *sd)
{
	unsigned char buf[56];
	int i,n;

	nullpo_retr(0, sd);

	WBUFW(buf,0)=0x1cf;
	WBUFL(buf,2)=sd->bl.id;
	for(i=0,n=0;i<5;i++) {
		if (!sd->devotion[i])
			continue;
		WBUFL(buf,6+4*n)=sd->devotion[i];
		n++;
	}
	for(;n<5;n++)
		WBUFL(buf,6+4*n)=0;
		
	WBUFB(buf,26)=8;
	WBUFB(buf,27)=0;

	clif_send(buf,packet_len(0x1cf),&sd->bl,AREA);
	return 0;
}

int clif_marionette(struct block_list *src, struct block_list *target)
{
	unsigned char buf[56];
	int n;

	WBUFW(buf,0)=0x1cf;
	WBUFL(buf,2)=src->id;
	for(n=0;n<5;n++)
		WBUFL(buf,6+4*n)=0;
	if (target) //The target goes on the second slot.
		WBUFL(buf,6+4) = target->id;
	WBUFB(buf,26)=8;
	WBUFB(buf,27)=0;

	clif_send(buf,packet_len(0x1cf),src,AREA);
	return 0;
}

/*==========================================
 * ����
 *------------------------------------------*/
int clif_spiritball(struct map_session_data *sd)
{
	unsigned char buf[16];

	nullpo_retr(0, sd);

	WBUFW(buf,0)=0x1d0;
	WBUFL(buf,2)=sd->bl.id;
	WBUFW(buf,6)=sd->spiritball;
	clif_send(buf,packet_len(0x1d0),&sd->bl,AREA);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_combo_delay(struct block_list *bl,int wait)
{
	unsigned char buf[32];

	nullpo_retr(0, bl);

	WBUFW(buf,0)=0x1d2;
	WBUFL(buf,2)=bl->id;
	WBUFL(buf,6)=wait;
	clif_send(buf,packet_len(0x1d2),bl,AREA);

	return 0;
}
/*==========================================
 *���n���
 *------------------------------------------*/
int clif_bladestop(struct block_list *src,struct block_list *dst,
	int _bool)
{
	unsigned char buf[32];

	nullpo_retr(0, src);
	nullpo_retr(0, dst);

	WBUFW(buf,0)=0x1d1;
	WBUFL(buf,2)=src->id;
	WBUFL(buf,6)=dst->id;
	WBUFL(buf,10)=_bool;

	clif_send(buf,packet_len(0x1d1),src,AREA);

	return 0;
}

/*==========================================
 * MVP�G�t�F�N�g
 *------------------------------------------*/
int clif_mvp_effect(struct map_session_data *sd)
{
	unsigned char buf[16];

	nullpo_retr(0, sd);

	WBUFW(buf,0)=0x10c;
	WBUFL(buf,2)=sd->bl.id;
	clif_send(buf,packet_len(0x10c),&sd->bl,AREA);
	return 0;
}
/*==========================================
 * MVP�A�C�e������
 *------------------------------------------*/
int clif_mvp_item(struct map_session_data *sd,int nameid)
{
	int view,fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x10a));
	WFIFOW(fd,0)=0x10a;
	if((view = itemdb_viewid(nameid)) > 0)
		WFIFOW(fd,2)=view;
	else
		WFIFOW(fd,2)=nameid;
	WFIFOSET(fd,packet_len(0x10a));
	return 0;
}
/*==========================================
 * MVP�o���l����
 *------------------------------------------*/
int clif_mvp_exp(struct map_session_data *sd,unsigned long exp)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x10b));
	WFIFOW(fd,0)=0x10b;
	WFIFOL(fd,2)=exp;
	WFIFOSET(fd,packet_len(0x10b));
	return 0;
}

/*==========================================
 * �M���h�쐬�ےʒm
 *------------------------------------------*/
int clif_guild_created(struct map_session_data *sd,int flag)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x167));
	WFIFOW(fd,0)=0x167;
	WFIFOB(fd,2)=flag;
	WFIFOSET(fd,packet_len(0x167));
	return 0;
}
/*==========================================
 * �M���h�����ʒm
 *------------------------------------------*/
int clif_guild_belonginfo(struct map_session_data *sd,struct guild *g)
{
	int ps,fd;

	nullpo_retr(0, sd);
	nullpo_retr(0, g);

	fd=sd->fd;
	ps=guild_getposition(sd,g);

	WFIFOHEAD(fd,packet_len(0x16c));
	memset(WFIFOP(fd,0),0,packet_len(0x16c));
	WFIFOW(fd,0)=0x16c;
	WFIFOL(fd,2)=g->guild_id;
	WFIFOL(fd,6)=g->emblem_id;
	WFIFOL(fd,10)=g->position[ps].mode;
	memcpy(WFIFOP(fd,19),g->name,NAME_LENGTH);
	WFIFOSET(fd,packet_len(0x16c));
	return 0;
}
/*==========================================
 * �M���h�����o���O�C���ʒm
 *------------------------------------------*/
int clif_guild_memberlogin_notice(struct guild *g,int idx,int flag)
{
	unsigned char buf[64];

	nullpo_retr(0, g);

	WBUFW(buf, 0)=0x16d;
	WBUFL(buf, 2)=g->member[idx].account_id;
	WBUFL(buf, 6)=g->member[idx].char_id;
	WBUFL(buf,10)=flag;
	if(g->member[idx].sd==NULL){
		struct map_session_data *sd=guild_getavailablesd(g);
		if(sd!=NULL)
			clif_send(buf,packet_len(0x16d),&sd->bl,GUILD);
	}else
		clif_send(buf,packet_len(0x16d),&g->member[idx].sd->bl,GUILD_WOS);
	return 0;
}

// Function `clif_guild_memberlogin_notice` sends info about
// logins and logouts of a guild member to the rest members.
// But at the 1st time (after a player login or map changing)
// the client won't show the message.
// So I suggest use this function for sending "first-time-info"
// to some player on entering the game or changing location. 
// At next time the client would always show the message.
// The function sends all the statuses in the single packet 
// to economize traffic. [LuzZza]
int clif_guild_send_onlineinfo(struct map_session_data *sd)
{
	struct guild *g;
	unsigned char buf[14*128];
	int i, count=0, p_len;
	
	nullpo_retr(0, sd);

	p_len = packet_len(0x16d);

	if(!(g = guild_search(sd->status.guild_id)))
		return 0;
	
	for(i=0; i<g->max_member; i++) {

		if(g->member[i].account_id > 0 &&
			g->member[i].account_id != sd->status.account_id) {

			WBUFW(buf,count*p_len) = 0x16d;
			WBUFL(buf,count*p_len+2) = g->member[i].account_id;
			WBUFL(buf,count*p_len+6) = g->member[i].char_id;
			WBUFL(buf,count*p_len+10) = g->member[i].online;
			count++;
		}
	}
	
	clif_send(buf, p_len*count, &sd->bl, SELF);

	return 0;
}

/*==========================================
 * �M���h�}�X�^�[�ʒm(14d�ւ̉���)
 *------------------------------------------*/
int clif_guild_masterormember(struct map_session_data *sd)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x14e));
	WFIFOW(fd,0) = 0x14e;
	WFIFOL(fd,2) = (sd->state.gmaster_flag) ? 0xd7 : 0x57;
	WFIFOSET(fd,packet_len(0x14e));
	return 0;
}
/*==========================================
 * Basic Info (Territories [Valaris])
 *------------------------------------------*/
int clif_guild_basicinfo(struct map_session_data *sd)
{
	int fd,i,t;
	struct guild *g;
	struct guild_castle *gc=NULL;

	nullpo_retr(0, sd);

	fd=sd->fd;
	g=guild_search(sd->status.guild_id);
	if(g==NULL)
		return 0;

	WFIFOHEAD(fd,packet_len(0x1b6));
	WFIFOW(fd, 0)=0x1b6;//0x150;
	WFIFOL(fd, 2)=g->guild_id;
	WFIFOL(fd, 6)=g->guild_lv;
	WFIFOL(fd,10)=g->connect_member;
	WFIFOL(fd,14)=g->max_member;
	WFIFOL(fd,18)=g->average_lv;
	WFIFOL(fd,22)=g->exp;
	WFIFOL(fd,26)=g->next_exp;
	WFIFOL(fd,30)=0;	// Tax Points
	WFIFOL(fd,34)=0;	// Tendency: (left) Vulgar [-100,100] Famed (right)
	WFIFOL(fd,38)=0;	// Tendency: (down) Wicked [-100,100] Righteous (up)
	WFIFOL(fd,42)=g->emblem_id;
	memcpy(WFIFOP(fd,46),g->name, NAME_LENGTH);
	memcpy(WFIFOP(fd,70),g->master, NAME_LENGTH);

	for(i = 0, t = 0; i < MAX_GUILDCASTLE; i++)
	{
		gc = guild_castle_search(i);
		if(gc && g->guild_id == gc->guild_id)
			t++;
	}
	strncpy((char*)WFIFOP(fd,94),msg_txt(300+t),20); // "'N' castles"

	WFIFOSET(fd,packet_len(WFIFOW(fd,0)));
	return 0;
}

/*==========================================
 * �M���h����/�G�Ώ��
 *------------------------------------------*/
int clif_guild_allianceinfo(struct map_session_data *sd)
{
	int fd,i,c;
	struct guild *g;

	nullpo_retr(0, sd);

	fd=sd->fd;
	g=guild_search(sd->status.guild_id);
	if(g==NULL)
		return 0;
	WFIFOHEAD(fd, MAX_GUILDALLIANCE * 32 + 4);
	WFIFOW(fd, 0)=0x14c;
	for(i=c=0;i<MAX_GUILDALLIANCE;i++){
		struct guild_alliance *a=&g->alliance[i];
		if(a->guild_id>0){
			WFIFOL(fd,c*32+4)=a->opposition;
			WFIFOL(fd,c*32+8)=a->guild_id;
			memcpy(WFIFOP(fd,c*32+12),a->name,NAME_LENGTH);
			c++;
		}
	}
	WFIFOW(fd, 2)=c*32+4;
	WFIFOSET(fd,WFIFOW(fd,2));
	return 0;
}

/*==========================================
 * �M���h�����o�[���X�g
 *------------------------------------------*/
int clif_guild_memberlist(struct map_session_data *sd)
{
	int fd;
	int i,c;
	struct guild *g;

	nullpo_retr(0, sd);

	fd=sd->fd;
	if (!fd)
		return 0;
	g=guild_search(sd->status.guild_id);
	if(g==NULL)
		return 0;

	WFIFOHEAD(fd, g->max_member * 104 + 4);
	WFIFOW(fd, 0)=0x154;
	for(i=0,c=0;i<g->max_member;i++){
		struct guild_member *m=&g->member[i];
		if(m->account_id==0)
			continue;
		WFIFOL(fd,c*104+ 4)=m->account_id;
		WFIFOL(fd,c*104+ 8)=m->char_id;
		WFIFOW(fd,c*104+12)=m->hair;
		WFIFOW(fd,c*104+14)=m->hair_color;
		WFIFOW(fd,c*104+16)=m->gender;
		WFIFOW(fd,c*104+18)=m->class_;
		WFIFOW(fd,c*104+20)=m->lv;
		WFIFOL(fd,c*104+22)=m->exp;
		WFIFOL(fd,c*104+26)=m->online;
		WFIFOL(fd,c*104+30)=m->position;
		memset(WFIFOP(fd,c*104+34),0,50);	// �����H
		memcpy(WFIFOP(fd,c*104+84),m->name,NAME_LENGTH);
		c++;
	}
	WFIFOW(fd, 2)=c*104+4;
	WFIFOSET(fd,WFIFOW(fd,2));
	return 0;
}
/*==========================================
 * �M���h��E�����X�g
 *------------------------------------------*/
int clif_guild_positionnamelist(struct map_session_data *sd)
{
	int i,fd;
	struct guild *g;

	nullpo_retr(0, sd);

	fd=sd->fd;
	g=guild_search(sd->status.guild_id);
	if(g==NULL)
		return 0;
	WFIFOHEAD(fd, MAX_GUILDPOSITION * 28 + 4);
	WFIFOW(fd, 0)=0x166;
	for(i=0;i<MAX_GUILDPOSITION;i++){
		WFIFOL(fd,i*28+4)=i;
		memcpy(WFIFOP(fd,i*28+8),g->position[i].name,NAME_LENGTH);
	}
	WFIFOW(fd,2)=i*28+4;
	WFIFOSET(fd,WFIFOW(fd,2));
	return 0;
}
/*==========================================
 * �M���h��E��񃊃X�g
 *------------------------------------------*/
int clif_guild_positioninfolist(struct map_session_data *sd)
{
	int i,fd;
	struct guild *g;

	nullpo_retr(0, sd);

	fd=sd->fd;
	g=guild_search(sd->status.guild_id);
	if(g==NULL)
		return 0;
	WFIFOHEAD(fd, MAX_GUILDPOSITION * 16 + 4);
	WFIFOW(fd, 0)=0x160;
	for(i=0;i<MAX_GUILDPOSITION;i++){
		struct guild_position *p=&g->position[i];
		WFIFOL(fd,i*16+ 4)=i;
		WFIFOL(fd,i*16+ 8)=p->mode;
		WFIFOL(fd,i*16+12)=i;
		WFIFOL(fd,i*16+16)=p->exp_mode;
	}
	WFIFOW(fd, 2)=i*16+4;
	WFIFOSET(fd,WFIFOW(fd,2));
	return 0;
}
/*==========================================
 * �M���h��E�ύX�ʒm
 *------------------------------------------*/
int clif_guild_positionchanged(struct guild *g,int idx)
{
	struct map_session_data *sd;
	unsigned char buf[128];

	nullpo_retr(0, g);

	WBUFW(buf, 0)=0x174;
	WBUFW(buf, 2)=44;
	WBUFL(buf, 4)=idx;
	WBUFL(buf, 8)=g->position[idx].mode;
	WBUFL(buf,12)=idx;
	WBUFL(buf,16)=g->position[idx].exp_mode;
	memcpy(WBUFP(buf,20),g->position[idx].name,NAME_LENGTH);
	if( (sd=guild_getavailablesd(g))!=NULL )
		clif_send(buf,WBUFW(buf,2),&sd->bl,GUILD);
	return 0;
}
/*==========================================
 * �M���h�����o�ύX�ʒm
 *------------------------------------------*/
int clif_guild_memberpositionchanged(struct guild *g,int idx)
{
	struct map_session_data *sd;
	unsigned char buf[64];

	nullpo_retr(0, g);

	WBUFW(buf, 0)=0x156;
	WBUFW(buf, 2)=16;
	WBUFL(buf, 4)=g->member[idx].account_id;
	WBUFL(buf, 8)=g->member[idx].char_id;
	WBUFL(buf,12)=g->member[idx].position;
	if( (sd=guild_getavailablesd(g))!=NULL )
		clif_send(buf,WBUFW(buf,2),&sd->bl,GUILD);
	return 0;
}
/*==========================================
 * �M���h�G���u�������M
 *------------------------------------------*/
int clif_guild_emblem(struct map_session_data *sd,struct guild *g)
{
	int fd;

	nullpo_retr(0, sd);
	nullpo_retr(0, g);

	fd=sd->fd;

	if(g->emblem_len<=0)
		return 0;
	WFIFOHEAD(fd,g->emblem_len+12);
	WFIFOW(fd,0)=0x152;
	WFIFOW(fd,2)=g->emblem_len+12;
	WFIFOL(fd,4)=g->guild_id;
	WFIFOL(fd,8)=g->emblem_id;
	memcpy(WFIFOP(fd,12),g->emblem_data,g->emblem_len);
	WFIFOSET(fd,WFIFOW(fd,2));
	return 0;
}

/*==========================================
 * Send guild skills
 *------------------------------------------*/
int clif_guild_skillinfo(struct map_session_data* sd)
{
	int fd;
	struct guild* g;
	int i,c;

	nullpo_retr(0, sd);

	fd = sd->fd;
	g = guild_search(sd->status.guild_id);
	if(g == NULL)
		return 0;

	WFIFOHEAD(fd, MAX_GUILDSKILL * 37 + 6);
	WFIFOW(fd,0) = 0x0162;
	WFIFOW(fd,4) = g->skill_point;
	for(i = 0, c = 0; i < MAX_GUILDSKILL; i++)
	{
		if(g->skill[i].id > 0 && guild_check_skill_require(g, g->skill[i].id))
		{
			int id = g->skill[i].id;
			WFIFOW(fd,c*37+ 6) = id; 
			WFIFOW(fd,c*37+ 8) = skill_get_inf(id);
			WFIFOW(fd,c*37+10) = 0;
			WFIFOW(fd,c*37+12) = g->skill[i].lv;
			WFIFOW(fd,c*37+14) = skill_get_sp(id, g->skill[i].lv);
			WFIFOW(fd,c*37+16) = skill_get_range(id, g->skill[i].lv);
			strncpy((char*)WFIFOP(fd,c*37+18), skill_get_name(id), NAME_LENGTH);
			WFIFOB(fd,c*37+42)= (g->skill[i].lv < guild_skill_get_max(id) && sd == g->member[0].sd) ? 1 : 0;
			c++;
		}
	}
	WFIFOW(fd,2) = c*37 + 6;
	WFIFOSET(fd,WFIFOW(fd,2));
	return 0;
}

/*==========================================
 * Sends guild notice to client
 * R 016f <str1z>.60B <str2z>.120B
 *------------------------------------------*/
int clif_guild_notice(struct map_session_data* sd, struct guild* g)
{
	int fd;

	nullpo_retr(0, sd);
	nullpo_retr(0, g);

	fd = sd->fd;

	if ( !session_isActive(fd) )
		return 0;
 
	if(g->mes1[0] == '\0' && g->mes2[0] == '\0')
		return 0;

	WFIFOHEAD(fd,packet_len(0x16f));
	WFIFOW(fd,0) = 0x16f;
	memcpy(WFIFOP(fd,2), g->mes1, 60);
	memcpy(WFIFOP(fd,62), g->mes2, 120);
	WFIFOSET(fd,packet_len(0x16f));
	return 0;
}

/*==========================================
 * �M���h�����o���U
 *------------------------------------------*/
int clif_guild_invite(struct map_session_data *sd,struct guild *g)
{
	int fd;

	nullpo_retr(0, sd);
	nullpo_retr(0, g);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x16a));
	WFIFOW(fd,0)=0x16a;
	WFIFOL(fd,2)=g->guild_id;
	memcpy(WFIFOP(fd,6),g->name,NAME_LENGTH);
	WFIFOSET(fd,packet_len(0x16a));
	return 0;
}
/*==========================================
 * �M���h�����o���U����
 *------------------------------------------*/
int clif_guild_inviteack(struct map_session_data *sd,int flag)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x169));
	WFIFOW(fd,0)=0x169;
	WFIFOB(fd,2)=flag;
	WFIFOSET(fd,packet_len(0x169));
	return 0;
}
/*==========================================
 * �M���h�����o�E�ޒʒm
 *------------------------------------------*/
int clif_guild_leave(struct map_session_data *sd,const char *name,const char *mes)
{
	unsigned char buf[128];

	nullpo_retr(0, sd);

	WBUFW(buf, 0)=0x15a;
	memcpy(WBUFP(buf, 2),name,NAME_LENGTH);
	memcpy(WBUFP(buf,26),mes,40);
	clif_send(buf,packet_len(0x15a),&sd->bl,GUILD);
	return 0;
}
/*==========================================
 * �M���h�����o�Ǖ��ʒm
 *------------------------------------------*/
int clif_guild_expulsion(struct map_session_data *sd,const char *name,const char *mes,
	int account_id)
{
	unsigned char buf[128];

	nullpo_retr(0, sd);

	WBUFW(buf, 0)=0x15c;
	memcpy(WBUFP(buf, 2),name,NAME_LENGTH);
	memcpy(WBUFP(buf,26),mes,40);
	memcpy(WBUFP(buf,66),"dummy",NAME_LENGTH);
	clif_send(buf,packet_len(0x15c),&sd->bl,GUILD);
	return 0;
}
/*==========================================
 * �M���h�Ǖ������o���X�g
 *------------------------------------------*/
int clif_guild_expulsionlist(struct map_session_data *sd)
{
	int fd;
	int i,c;
	struct guild *g;

	nullpo_retr(0, sd);

	fd=sd->fd;
	g=guild_search(sd->status.guild_id);
	if(g==NULL)
		return 0;
	WFIFOHEAD(fd,MAX_GUILDEXPULSION * 88 + 4);
	WFIFOW(fd,0)=0x163;
	for(i=c=0;i<MAX_GUILDEXPULSION;i++){
		struct guild_expulsion *e=&g->expulsion[i];
		if(e->account_id>0){
			memcpy(WFIFOP(fd,c*88+ 4),e->name,NAME_LENGTH);
			memcpy(WFIFOP(fd,c*88+28),e->acc,24);
			memcpy(WFIFOP(fd,c*88+52),e->mes,44);
			c++;
		}
	}
	WFIFOW(fd,2)=c*88+4;
	WFIFOSET(fd,WFIFOW(fd,2));
	return 0;
}

/*==========================================
 * �M���h��b
 *------------------------------------------*/
int clif_guild_message(struct guild *g,int account_id,const char *mes,int len)
{
	struct map_session_data *sd;
	unsigned char *buf;

	buf = (unsigned char*)aMallocA((len + 4)*sizeof(unsigned char));

	WBUFW(buf, 0) = 0x17f;
	WBUFW(buf, 2) = len + 4;
	memcpy(WBUFP(buf,4), mes, len);

	if ((sd = guild_getavailablesd(g)) != NULL)
		clif_send(buf, WBUFW(buf,2), &sd->bl, GUILD);

	if(buf) aFree(buf);

	return 0;
}
/*==========================================
 * �M���h�X�L������U��ʒm
 *------------------------------------------*/
int clif_guild_skillup(struct map_session_data *sd,int skill_num,int lv)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,11);
	WFIFOW(fd,0) = 0x10e;
	WFIFOW(fd,2) = skill_num;
	WFIFOW(fd,4) = lv;
	WFIFOW(fd,6) = skill_get_sp(skill_num,lv);
	WFIFOW(fd,8) = skill_get_range(skill_num,lv);
	WFIFOB(fd,10) = 1;
	WFIFOSET(fd,11);
	return 0;
}
/*==========================================
 * �M���h�����v��
 *------------------------------------------*/
int clif_guild_reqalliance(struct map_session_data *sd,int account_id,const char *name)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x171));
	WFIFOW(fd,0)=0x171;
	WFIFOL(fd,2)=account_id;
	memcpy(WFIFOP(fd,6),name,NAME_LENGTH);
	WFIFOSET(fd,packet_len(0x171));
	return 0;
}
/*==========================================
 * Reply to alliance request.
 * Flag values are:
 * 0: Already allied.
 * 1: You rejected the offer.
 * 2: You accepted the offer.
 * 3: They have too any alliances
 * 4: You have too many alliances.
 *------------------------------------------*/
int clif_guild_allianceack(struct map_session_data *sd,int flag)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x173));
	WFIFOW(fd,0)=0x173;
	WFIFOL(fd,2)=flag;
	WFIFOSET(fd,packet_len(0x173));
	return 0;
}
/*==========================================
 * �M���h�֌W�����ʒm
 *------------------------------------------*/
int clif_guild_delalliance(struct map_session_data *sd,int guild_id,int flag)
{
	int fd;

	nullpo_retr(0, sd);

	fd = sd->fd;
	if (fd <= 0)
		return 0;
	WFIFOHEAD(fd,packet_len(0x184));
	WFIFOW(fd,0)=0x184;
	WFIFOL(fd,2)=guild_id;
	WFIFOL(fd,6)=flag;
	WFIFOSET(fd,packet_len(0x184));
	return 0;
}
/*==========================================
 * �M���h�G�Ό���
 *------------------------------------------*/
int clif_guild_oppositionack(struct map_session_data *sd,int flag)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x181));
	WFIFOW(fd,0)=0x181;
	WFIFOB(fd,2)=flag;
	WFIFOSET(fd,packet_len(0x181));
	return 0;
}
/*==========================================
 * �M���h�֌W�ǉ�
 *------------------------------------------*/
/*int clif_guild_allianceadded(struct guild *g,int idx)
{
	unsigned char buf[64];
	WBUFW(fd,0)=0x185;
	WBUFL(fd,2)=g->alliance[idx].opposition;
	WBUFL(fd,6)=g->alliance[idx].guild_id;
	memcpy(WBUFP(fd,10),g->alliance[idx].name,NAME_LENGTH);
	clif_send(buf,packet_len(0x185),guild_getavailablesd(g),GUILD);
	return 0;
}*/

/*==========================================
 * �M���h���U�ʒm
 *------------------------------------------*/
int clif_guild_broken(struct map_session_data *sd,int flag)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x15e));
	WFIFOW(fd,0)=0x15e;
	WFIFOL(fd,2)=flag;
	WFIFOSET(fd,packet_len(0x15e));
	return 0;
}

/*==========================================
 * �G���[�V����
 *------------------------------------------*/
void clif_emotion(struct block_list *bl,int type)
{
	unsigned char buf[8];

	nullpo_retv(bl);

	WBUFW(buf,0)=0xc0;
	WBUFL(buf,2)=bl->id;
	WBUFB(buf,6)=type;
	clif_send(buf,packet_len(0xc0),bl,AREA);
}

/*==========================================
 * �g�[�L�[�{�b�N�X
 *------------------------------------------*/
void clif_talkiebox(struct block_list* bl, const char* talkie)
{
	unsigned char buf[86];

	nullpo_retv(bl);

	WBUFW(buf,0)=0x191;
	WBUFL(buf,2)=bl->id;
	memcpy(WBUFP(buf,6),talkie,MESSAGE_SIZE);
	clif_send(buf,packet_len(0x191),bl,AREA);
}

/*==========================================
 * �����G�t�F�N�g
 *------------------------------------------*/
void clif_wedding_effect(struct block_list *bl)
{
	unsigned char buf[6];

	nullpo_retv(bl);

	WBUFW(buf,0) = 0x1ea;
	WBUFL(buf,2) = bl->id;
	clif_send(buf, packet_len(0x1ea), bl, AREA);
}
/*==========================================
 * ���Ȃ��Ɉ��������g�p�����O����
 *------------------------------------------

void clif_callpartner(struct map_session_data *sd)
{
	unsigned char buf[26];
	char *p;

	nullpo_retv(sd);

	if(sd->status.partner_id){
		WBUFW(buf,0)=0x1e6;
		p = map_charid2nick(sd->status.partner_id);
		if(p){
			memcpy(WBUFP(buf,2),p,NAME_LENGTH);
		}else{
			map_reqchariddb(sd,sd->status.partner_id);
			chrif_searchcharid(sd->status.partner_id);
			WBUFB(buf,2) = 0;
		}
		clif_send(buf,packet_len(0x1e6),&sd->bl,AREA);
	}
	return;
}
*/
/*==========================================
 * Adopt baby [Celest]
 *------------------------------------------*/
void clif_adopt_process(struct map_session_data *sd)
{
	int fd;
	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x1f8));
	WFIFOW(fd,0)=0x1f8;
	WFIFOSET(fd,packet_len(0x1f8));
}
/*==========================================
 * Marry [DracoRPG]
 *------------------------------------------*/
void clif_marriage_process(struct map_session_data *sd)
{
	int fd;
	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x1e4));
	WFIFOW(fd,0)=0x1e4;
	WFIFOSET(fd,packet_len(0x1e4));
}


/*==========================================
 * Notice of divorce
 *------------------------------------------*/
void clif_divorced(struct map_session_data* sd, const char* name)
{
	int fd;
	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x205));
	WFIFOW(fd,0)=0x205;
	memcpy(WFIFOP(fd,2), name, NAME_LENGTH);
	WFIFOSET(fd, packet_len(0x205));
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_ReqAdopt(int fd, struct map_session_data *sd)
{
	nullpo_retv(sd);

	WFIFOHEAD(fd,packet_len(0x1f6));
	WFIFOW(fd,0)=0x1f6;
	WFIFOSET(fd, packet_len(0x1f6));
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_ReqMarriage(int fd, struct map_session_data *sd)
{
	nullpo_retv(sd);

	WFIFOHEAD(fd,packet_len(0x1e2));
	WFIFOW(fd,0)=0x1e2;
	WFIFOSET(fd, packet_len(0x1e2));
}

/*==========================================
 *
 *------------------------------------------*/
int clif_disp_onlyself(struct map_session_data *sd, const char *mes, int len)
{
	int fd;
	nullpo_retr(0, sd);
	fd = sd->fd;
	if (!fd || !len) return 0; //Disconnected player.
	WFIFOHEAD(fd, len+5);
	WFIFOW(fd, 0) = 0x17f;
	WFIFOW(fd, 2) = len + 5;
	memcpy(WFIFOP(fd,4), mes, len);
	WFIFOSET(fd, WFIFOW(fd,2));
	return 1;
}

/*==========================================
 * Displays a message using the guild-chat colors to the specified targets. [Skotlex]
 *------------------------------------------*/
void clif_disp_message(struct block_list* src, const char* mes, int len, enum send_target target)
{
	unsigned char buf[1024];
	if (!len) return;
	WBUFW(buf, 0) = 0x17f;
	WBUFW(buf, 2) = len + 5;
	memcpy(WBUFP(buf,4), mes, len);
	clif_send(buf, WBUFW(buf,2), src, target);
	return;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_GM_kickack(struct map_session_data *sd, int id)
{
	int fd;

	nullpo_retr(0, sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0xcd));
	WFIFOW(fd,0) = 0xcd;
	WFIFOL(fd,2) = id;
	WFIFOSET(fd, packet_len(0xcd));
	return 0;
}

void clif_parse_QuitGame(int fd,struct map_session_data *sd);

int clif_GM_kick(struct map_session_data *sd,struct map_session_data *tsd,int type)
{
	int fd = tsd->fd;
	WFIFOHEAD(fd,packet_len(0x18b));
	if(type)
		clif_GM_kickack(sd,tsd->status.account_id);
	if (!fd) {
		map_quit(tsd);
		return 0;
	}

	WFIFOW(fd,0) = 0x18b;
	WFIFOW(fd,2) = 0;
	WFIFOSET(fd,packet_len(0x18b));
	clif_setwaitclose(fd);
	return 0;
}

int clif_GM_silence(struct map_session_data *sd, struct map_session_data *tsd, int type)
{
	int fd;
	
	nullpo_retr(0, sd);
	nullpo_retr(0, tsd);

	fd = tsd->fd;
	if (fd <= 0)
		return 0;
	WFIFOHEAD(fd,packet_len(0x14b));
	WFIFOW(fd,0) = 0x14b;
	WFIFOB(fd,2) = 0;
	memcpy(WFIFOP(fd,3), sd->status.name, NAME_LENGTH);
	WFIFOSET(fd, packet_len(0x14b));

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_timedout(struct map_session_data *sd)
{
	nullpo_retr(0, sd);

	ShowInfo("%sCharacter with Account ID '"CL_WHITE"%d"CL_RESET"' timed out.\n", (pc_isGM(sd))?"GM ":"", sd->bl.id);
	clif_authfail_fd(sd->fd,3); // Even if player is not on we still send anyway
	clif_setwaitclose(sd->fd); // Set session to EOF
	return 0;
}

/*==========================================
 * Wis���ۋ�����
 *------------------------------------------*/
int clif_wisexin(struct map_session_data *sd,int type,int flag)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xd1));
	WFIFOW(fd,0)=0xd1;
	WFIFOB(fd,2)=type;
	WFIFOB(fd,3)=flag;
	WFIFOSET(fd,packet_len(0xd1));

	return 0;
}
/*==========================================
 * Wis�S���ۋ�����
 *------------------------------------------*/
int clif_wisall(struct map_session_data *sd,int type,int flag)
{
	int fd;

	nullpo_retr(0, sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xd2));
	WFIFOW(fd,0)=0xd2;
	WFIFOB(fd,2)=type;
	WFIFOB(fd,3)=flag;
	WFIFOSET(fd,packet_len(0xd2));

	return 0;
}
/*==========================================
 * �T�E���h�G�t�F�N�g
 *------------------------------------------*/
void clif_soundeffect(struct map_session_data* sd, struct block_list* bl, const char* name, int type)
{
	int fd;

	nullpo_retv(sd);
	nullpo_retv(bl);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0x1d3));
	WFIFOW(fd,0) = 0x1d3;
	safestrncpy((char*)WFIFOP(fd,2), name, NAME_LENGTH);
	WFIFOB(fd,26) = type;
	WFIFOL(fd,27) = 0;
	WFIFOL(fd,31) = bl->id;
	WFIFOSET(fd,packet_len(0x1d3));

	return;
}

int clif_soundeffectall(struct block_list* bl, const char* name, int type, enum send_target coverage)
{
	unsigned char buf[40];

	nullpo_retr(0, bl);

	WBUFW(buf,0) = 0x1d3;
	safestrncpy((char*)WBUFP(buf,2), name, NAME_LENGTH);
	WBUFB(buf,26) = type;
	WBUFL(buf,27) = 0;
	WBUFL(buf,31) = bl->id;
	clif_send(buf, packet_len(0x1d3), bl, coverage);

	return 0;
}

// displaying special effects (npcs, weather, etc) [Valaris]
int clif_specialeffect(struct block_list* bl, int type, enum send_target target)
{
	unsigned char buf[24];

	nullpo_retr(0, bl);

	memset(buf, 0, packet_len(0x1f3));

	WBUFW(buf,0) = 0x1f3;
	WBUFL(buf,2) = bl->id;
	WBUFL(buf,6) = type;

	clif_send(buf, packet_len(0x1f3), bl, target);

	if (disguised(bl)) {
		WBUFL(buf,2) = -bl->id;
		clif_send(buf, packet_len(0x1f3), bl, SELF);
	}
	return 0;
}

// refresh the client's screen, getting rid of any effects
int clif_refresh(struct map_session_data *sd)
{
	nullpo_retr(-1, sd);
	clif_changemap(sd,sd->mapindex,sd->bl.x,sd->bl.y);
	clif_inventorylist(sd);
	if(pc_iscarton(sd)) {
		clif_cartlist(sd);
		clif_updatestatus(sd,SP_CARTINFO);
	}
	clif_updatestatus(sd,SP_MAXWEIGHT);
	clif_updatestatus(sd,SP_WEIGHT);
	clif_updatestatus(sd,SP_STR);
	clif_updatestatus(sd,SP_AGI);
	clif_updatestatus(sd,SP_VIT);
	clif_updatestatus(sd,SP_INT);
	clif_updatestatus(sd,SP_DEX);
	clif_updatestatus(sd,SP_LUK);
	if (sd->spiritball)
		clif_spiritball_single(sd->fd, sd);
	if (sd->vd.cloth_color)
		clif_refreshlook(&sd->bl,sd->bl.id,LOOK_CLOTHES_COLOR,sd->vd.cloth_color,SELF);
	if(merc_is_hom_active(sd->hd))
		clif_send_homdata(sd,0,0);
	map_foreachinrange(clif_getareachar,&sd->bl,AREA_SIZE,BL_ALL,sd);
	clif_weather_check(sd);
	return 0;
}

// updates the object's (bl) name on client
int clif_charnameack (int fd, struct block_list *bl)
{
	unsigned char buf[103];
	int cmd = 0x95;

	nullpo_retr(0, bl);

	WBUFW(buf,0) = cmd;
	WBUFL(buf,2) = bl->id;

	switch(bl->type) {
	case BL_PC:
		{
			struct map_session_data *ssd = (struct map_session_data *)bl;
			struct party_data *p = NULL;
			struct guild *g = NULL;
			
			//Requesting your own "shadow" name. [Skotlex]
			if (ssd->fd == fd && ssd->disguise)
				WBUFL(buf,2) = -bl->id; 

			if (strlen(ssd->fakename)>1) {
				memcpy(WBUFP(buf,6), ssd->fakename, NAME_LENGTH);
				break;
			}
			memcpy(WBUFP(buf,6), ssd->status.name, NAME_LENGTH);
			
			if (ssd->status.party_id > 0)
				p = party_search(ssd->status.party_id);

			if (ssd->status.guild_id > 0)
				g = guild_search(ssd->status.guild_id);

			if (p == NULL && g == NULL)
				break;
			
			WBUFW(buf, 0) = cmd = 0x195;
			if (p)
				memcpy(WBUFP(buf,30), p->party.name, NAME_LENGTH);
			else
				WBUFB(buf,30) = 0;
			
			if (g)
			{
				int i, ps = -1;
				for(i = 0; i < g->max_member; i++) {
					if (g->member[i].account_id == ssd->status.account_id &&
						g->member[i].char_id == ssd->status.char_id )
						{
							ps = g->member[i].position;
							break;
						}
					}
				if (ps >= 0 && ps < MAX_GUILDPOSITION)
				{
					memcpy(WBUFP(buf,54), g->name,NAME_LENGTH);
					memcpy(WBUFP(buf,78), g->position[ps].name, NAME_LENGTH);
				} else { //Assume no guild.
					WBUFB(buf,54) = 0;
					WBUFB(buf,78) = 0;
				}
			} else {
				WBUFB(buf,54) = 0;
				WBUFB(buf,78) = 0;
			}
		}
		break;
	//[blackhole89]
	case BL_HOM:
		memcpy(WBUFP(buf,6), ((TBL_HOM*)bl)->homunculus.name, NAME_LENGTH);
		break;
	case BL_PET:
		memcpy(WBUFP(buf,6), ((TBL_PET*)bl)->pet.name, NAME_LENGTH);
		break;
	case BL_NPC:
		memcpy(WBUFP(buf,6), ((TBL_NPC*)bl)->name, NAME_LENGTH);
		break;
	case BL_MOB:
		{
			struct mob_data *md = (struct mob_data *)bl;
			nullpo_retr(0, md);

			memcpy(WBUFP(buf,6), md->name, NAME_LENGTH);
			if (md->guardian_data && md->guardian_data->guild_id) {
				WBUFW(buf, 0) = cmd = 0x195;
				WBUFB(buf,30) = 0;
				memcpy(WBUFP(buf,54), md->guardian_data->guild_name, NAME_LENGTH);
				memcpy(WBUFP(buf,78), md->guardian_data->castle->castle_name, NAME_LENGTH);
			} else if (battle_config.show_mob_info) {
				char mobhp[50], *str_p = mobhp;

				WBUFW(buf, 0) = cmd = 0x195;
				if (battle_config.show_mob_info&4)
					str_p += sprintf(str_p, "Lv. %d | ", md->level);
				if (battle_config.show_mob_info&1)
					str_p += sprintf(str_p, "HP: %u/%u | ", md->status.hp, md->status.max_hp);
				if (battle_config.show_mob_info&2)
					str_p += sprintf(str_p, "HP: %d%% | ", 
						md->status.max_hp > 10000?
						md->status.hp/(md->status.max_hp/100):
						100*md->status.hp/md->status.max_hp);
				//Even thought mobhp ain't a name, we send it as one so the client
				//can parse it. [Skotlex]
				if (str_p != mobhp) {
					*(str_p-3) = '\0'; //Remove trailing space + pipe.
					memcpy(WBUFP(buf,30), mobhp, NAME_LENGTH);
					WBUFB(buf,54) = 0;
					memcpy(WBUFP(buf,78), mobhp, NAME_LENGTH);
				}
			}
		}
		break;
	case BL_CHAT:	//FIXME: Clients DO request this... what should be done about it? The chat's title may not fit... [Skotlex]
//		memcpy(WBUFP(buf,6), (struct chat*)->title, NAME_LENGTH);
//		break;
		return 0;
	default:
		if (battle_config.error_log)
			ShowError("clif_parse_GetCharNameRequest : bad type %d(%d)\n", bl->type, bl->id);
		return 0;
	}

	// if no receipient specified just update nearby clients
	if (fd == 0)
		clif_send(buf, packet_len(cmd), bl, AREA);
	else {
		WFIFOHEAD(fd, packet_len(cmd));
		memcpy(WFIFOP(fd, 0), buf, packet_len(cmd));
		WFIFOSET(fd, packet_len(cmd));
	}

	return 0;
}

//Used to update when a char leaves a party/guild. [Skotlex]
//Needed because when you send a 0x95 packet, the client will not remove the cached party/guild info that is not sent.
int clif_charnameupdate (struct map_session_data *ssd)
{
	unsigned char buf[103];
	int cmd = 0x195;
	struct party_data *p = NULL;
	struct guild *g = NULL;

	nullpo_retr(0, ssd);

	if (strlen(ssd->fakename)>1)
		return 0; //No need to update as the party/guild was not displayed anyway.

	WBUFW(buf,0) = cmd;
	WBUFL(buf,2) = ssd->bl.id;

	memcpy(WBUFP(buf,6), ssd->status.name, NAME_LENGTH);
			
	if (ssd->status.party_id > 0)
		p = party_search(ssd->status.party_id);

	if (ssd->status.guild_id > 0)
		g = guild_search(ssd->status.guild_id);

	if (p)
		memcpy(WBUFP(buf,30), p->party.name, NAME_LENGTH);
	else
		WBUFB(buf,30) = 0;

	if (g)
	{
		int i, ps = -1;
		for(i = 0; i < g->max_member; i++) {
			if (g->member[i].account_id == ssd->status.account_id &&
				g->member[i].char_id == ssd->status.char_id )
				{
					ps = g->member[i].position;
					break;
				}
			}
		if (ps >= 0 && ps < MAX_GUILDPOSITION)
		{
			memcpy(WBUFP(buf,54), g->name,NAME_LENGTH);
			memcpy(WBUFP(buf,78), g->position[ps].name, NAME_LENGTH);
		} else { //Assume no guild.
			WBUFB(buf,54) = 0;
			WBUFB(buf,78) = 0;
		}
	} else {
		WBUFB(buf,54) = 0;
		WBUFB(buf,78) = 0;
	}

	// Update nearby clients
	clif_send(buf, packet_len(cmd), &ssd->bl, AREA);
	return 0;
}

int clif_slide(struct block_list *bl, int x, int y){
	unsigned char buf[10];

	nullpo_retr(0, bl);

	WBUFW(buf, 0) = 0x01ff;
	WBUFL(buf, 2) = bl->id;
	WBUFW(buf, 6) = x;
	WBUFW(buf, 8) = y;

	clif_send(buf, packet_len(0x1ff), bl, AREA);
	return 0;
}

/*------------------------------------------
 * @me command by lordalfa, rewritten implementation by Skotlex
 *------------------------------------------*/
int clif_disp_overhead(struct map_session_data *sd, const char* mes)
{
	unsigned char buf[256]; //This should be more than sufficient, the theorical max is CHAT_SIZE + 8 (pads and extra inserted crap)
	int len_mes = strlen(mes)+1; //Account for \0

	if (len_mes + 8 >= 256) {
		if (battle_config.error_log)
			ShowError("clif_disp_overhead: Message too long (length %d)\n", len_mes);
		len_mes = 247; //Trunk it to avoid problems.
	}
	// send message to others
	WBUFW(buf,0) = 0x8d;
	WBUFW(buf,2) = len_mes + 8; // len of message + 8 (command+len+id)
	WBUFL(buf,4) = sd->bl.id;
	memcpy(WBUFP(buf,8), mes, len_mes);
	clif_send(buf, WBUFW(buf,2), &sd->bl, AREA_CHAT_WOC);

	// send back message to the speaker
	WBUFW(buf,0) = 0x8e;
	WBUFW(buf, 2) = len_mes + 4;
	memcpy(WBUFP(buf,4), mes, len_mes);  
	clif_send(buf, WBUFW(buf,2), &sd->bl, SELF);

	return 0;
}

/*==========================
 * Minimap fix [Kevin]
 * Remove dot from minimap 
 *--------------------------*/
int clif_party_xy_remove(struct map_session_data *sd)
{
	unsigned char buf[16];
	nullpo_retr(0, sd);
	WBUFW(buf,0)=0x107;
	WBUFL(buf,2)=sd->status.account_id;
	WBUFW(buf,6)=-1;
	WBUFW(buf,8)=-1;
	clif_send(buf,packet_len(0x107),&sd->bl,PARTY_SAMEMAP_WOS);
	return 0;
}

//Displays gospel-buff information (thanks to Rayce):
//Type determines message based on this table:
/*
	0x15 End all negative status
	0x16 Immunity to all status
	0x17 MaxHP +100%
	0x18 MaxSP +100%
	0x19 All stats +20
	0x1c Enchant weapon with Holy element
	0x1d Enchant armor with Holy element
	0x1e DEF +25%
	0x1f ATK +100%
	0x20 HIT/Flee +50
	0x28 Full strip failed because of coating (unrelated to gospel, maybe for ST_FULLSTRIP)
*/
void clif_gospel_info(struct map_session_data *sd, int type)
{
	int fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x215));
	WFIFOW(fd,0)=0x215;
	WFIFOL(fd,2)=type;
	WFIFOSET(fd, packet_len(0x215));

}
/*==========================================
 * Info about Star Glaldiator save map [Komurka]
 * type: 1: Information, 0: Map registered
 *------------------------------------------*/
void clif_feel_info(struct map_session_data* sd, unsigned char feel_level, unsigned char type)
{
	int fd=sd->fd;

	WFIFOHEAD(fd,packet_len(0x20e));
	WFIFOW(fd,0) = 0x20e;
	mapindex_getmapname_ext(mapindex_id2name(sd->feel_map[feel_level].index), (char*)WFIFOP(fd,2));
	WFIFOL(fd,26) = sd->bl.id;
	WFIFOB(fd,30) = feel_level;
	WFIFOB(fd,31) = type?1:0;
	WFIFOSET(fd,packet_len(0x20e));
}

/*==========================================
 * Info about Star Glaldiator hate mob [Komurka]
 * type: 1: Register mob, 0: Information.
 *------------------------------------------*/
void clif_hate_info(struct map_session_data *sd, unsigned char hate_level,int class_, unsigned char type)
{
	int fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x20e));
	WFIFOW(fd,0)=0x20e;
	if (pcdb_checkid(class_))
		strncpy((char*)WFIFOP(fd,2),job_name(class_), NAME_LENGTH);
	else if (mobdb_checkid(class_))
		strncpy((char*)WFIFOP(fd,2),mob_db(class_)->jname, NAME_LENGTH);
	else //Really shouldn't happen...
		memset(WFIFOP(fd,2), 0, NAME_LENGTH);
	WFIFOL(fd,26)=sd->bl.id;
	WFIFOB(fd,30)=hate_level;
	WFIFOB(fd,31)=type?10:11; //Register/Info
	WFIFOSET(fd, packet_len(0x20e));
}

/*==========================================
 * Info about TaeKwon Do TK_MISSION mob [Skotlex]
 *------------------------------------------*/
void clif_mission_info(struct map_session_data *sd, int mob_id, unsigned char progress)
{
	int fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x20e));
	WFIFOW(fd,0)=0x20e;
	strncpy((char*)WFIFOP(fd,2),mob_db(mob_id)->jname, NAME_LENGTH);
	WFIFOL(fd,26)=mob_id;
	WFIFOB(fd,30)=progress; //Message to display
	WFIFOB(fd,31)=20;
	WFIFOSET(fd, packet_len(0x20e));
}

/*==========================================
 * Feel/Hate reset (thanks to Rayce) [Skotlex]
 *------------------------------------------*/
void clif_feel_hate_reset(struct map_session_data *sd)
{
	int fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x20e));
	WFIFOW(fd,0)=0x20e;
	memset(WFIFOP(fd,2), 0, NAME_LENGTH); //Blank name as all was reset.
	WFIFOL(fd,26)=sd->bl.id;
	WFIFOB(fd,30)=0; //Feel/hate level: irrelevant
	WFIFOB(fd,31)=30;
	WFIFOSET(fd, packet_len(0x20e));
}


// ---------------------
// clif_guess_PacketVer
// ---------------------
// Parses a WantToConnection packet to try to identify which is the packet version used. [Skotlex]
// error codes:
// 0 - Success
// 1 - Unknown packet_ver
// 2 - Invalid account_id
// 3 - Invalid char_id
// 4 - Invalid login_id1 (reserved)
// 5 - Invalid client_tick (reserved)
// 6 - Invalid sex
// Only the first 'invalid' error that appears is used.
static int clif_guess_PacketVer(int fd, int get_previous, int *error)
{
	static int err = 1;
	static int packet_ver = -1;
	int cmd, packet_len, value; //Value is used to temporarily store account/char_id/sex

	if (get_previous)
	{//For quick reruns, since the normal code flow is to fetch this once to identify the packet version, then again in the wanttoconnect function. [Skotlex]
		if( error )
			*error = err;
		return packet_ver;
	}

	//By default, start searching on the default one.
	err = 1;
	packet_ver = clif_config.packet_db_ver;
	cmd = RFIFOW(fd,0);
	packet_len = RFIFOREST(fd);

#define SET_ERROR(n) \
	if( err == 1 )\
		err = n;\
//define SET_ERROR

#define CHECK_PACKET_VER() \
	if( cmd != clif_config.connect_cmd[packet_ver] || packet_len != packet_db[packet_ver][cmd].len )\
		;/* not wanttoconnection or wrong length */\
	else if( (value=(int)RFIFOL(fd, packet_db[packet_ver][cmd].pos[0])) < START_ACCOUNT_NUM || value > max_account_id )\
	{ SET_ERROR(2); }/* invalid account_id */\
	else if( (value=(int)RFIFOL(fd, packet_db[packet_ver][cmd].pos[1])) <= 0 || value > max_char_id )\
	{ SET_ERROR(3); }/* invalid char_id */\
	/*                   RFIFOL(fd, packet_db[packet_ver][cmd].pos[2]) - don't care about login_id1 */\
	/*                   RFIFOL(fd, packet_db[packet_ver][cmd].pos[3]) - don't care about client_tick */\
	else if( (value=(int)RFIFOB(fd, packet_db[packet_ver][cmd].pos[4])) != 0 && value != 1 )\
	{ SET_ERROR(6); }/* invalid sex */\
	else\
	{\
		err = 0;\
		if( error )\
			*error = 0;\
		return packet_ver;\
	}\
//define CHECK_PACKET_VER

	CHECK_PACKET_VER();//Default packet version found.
	
	for (packet_ver = MAX_PACKET_VER; packet_ver > 0; packet_ver--)
	{	//Start guessing the version, giving priority to the newer ones. [Skotlex]
		CHECK_PACKET_VER();
	}
	if( error )
		*error = err;
	packet_ver = -1;
	return -1;
#undef SET_ERROR
#undef CHECK_PACKET_VER
}

// ------------
// clif_parse_*
// ------------
// �p�P�b�g�ǂݎ���ĐF�X����
/*==========================================
 *
 *------------------------------------------*/
void clif_parse_WantToConnection(int fd, TBL_PC* sd)
{
	int cmd, account_id, char_id, login_id1, sex;
	unsigned int client_tick; //The client tick is a tick, therefore it needs be unsigned. [Skotlex]
	int packet_ver;	// 5: old, 6: 7july04, 7: 13july04, 8: 26july04, 9: 9aug04/16aug04/17aug04, 10: 6sept04, 11: 21sept04, 12: 18oct04, 13: 25oct04 (by [Yor])

	if (sd) {
		if (battle_config.error_log)
			ShowError("clif_parse_WantToConnection : invalid request (character already logged in)?\n");
		return;
	}

	// Only valid packet version get here
	packet_ver = clif_guess_PacketVer(fd, 1, NULL);

	cmd = RFIFOW(fd,0);
	account_id  = RFIFOL(fd, packet_db[packet_ver][cmd].pos[0]);
	char_id     = RFIFOL(fd, packet_db[packet_ver][cmd].pos[1]);
	login_id1   = RFIFOL(fd, packet_db[packet_ver][cmd].pos[2]);
	client_tick = RFIFOL(fd, packet_db[packet_ver][cmd].pos[3]);
	sex         = RFIFOB(fd, packet_db[packet_ver][cmd].pos[4]);

	if( packet_ver < 5 || // reject really old client versions
			(packet_ver <= 9 && (battle_config.packet_ver_flag & 1) == 0) || // older than 6sept04
			(packet_ver > 9 && (battle_config.packet_ver_flag & 1<<(packet_ver-9)) == 0)) // version not allowed
	{// packet version rejected
		ShowInfo("Rejected connection attempt, forbidden packet version (AID/CID: '"CL_WHITE"%d/%d"CL_RESET"', Packet Ver: '"CL_WHITE"%d"CL_RESET"', IP: '"CL_WHITE"%s"CL_RESET"').\n", account_id, char_id, packet_ver, ip2str(session[fd]->client_addr, NULL));
		WFIFOHEAD(fd,packet_len(0x6a));
		WFIFOW(fd,0) = 0x6a;
		WFIFOB(fd,2) = 5; // Your Game's EXE file is not the latest version
		WFIFOSET(fd,packet_len(0x6a));
		clif_setwaitclose(fd);
		return;
	} else
	{// packet version accepted
		struct block_list* bl;
		if( (bl=map_id2bl(account_id)) != NULL && bl->type != BL_PC )
		{// non-player object already has that id
			ShowError("clif_parse_WantToConnection: a non-player object already has id %d, please increase the starting account number\n", account_id);
			WFIFOHEAD(fd,packet_len(0x6a));
			WFIFOW(fd,0) = 0x6a;
			WFIFOB(fd,2) = 3; // Rejected by server
			WFIFOSET(fd,packet_len(0x6a));
			clif_setwaitclose(fd);
			return;
		}
	}

	CREATE(sd, TBL_PC, 1);
	sd->fd = fd;
	sd->packet_ver = packet_ver;
	session[fd]->session_data = sd;

	pc_setnewpc(sd, account_id, char_id, login_id1, client_tick, sex, fd);
	WFIFOHEAD(fd,4);
	WFIFOL(fd,0) = sd->bl.id;
	WFIFOSET(fd,4);
	chrif_authreq(sd);
	return;
}

/*==========================================
 * 007d �N���C�A���g���}�b�v�ǂݍ��݊���
 * map�N�����ɕK�v�ȃf�[�^��S�đ������
 *------------------------------------------*/
void clif_parse_LoadEndAck(int fd,struct map_session_data *sd)
{
	if(sd->bl.prev != NULL)
		return;
	
	if (!sd->state.auth)
	{	//Character loading is not complete yet!
		//Let pc_reg_received reinvoke this when ready.
		sd->state.connect_new = 0;
		return;
	}

	if (sd->state.rewarp)
  	{	//Rewarp player.
		sd->state.rewarp = 0;
		clif_changemap(sd, sd->mapindex, sd->bl.x, sd->bl.y);
		return;
	}

	// look
#if PACKETVER < 4
	clif_changelook(&sd->bl,LOOK_WEAPON,sd->status.weapon);
	clif_changelook(&sd->bl,LOOK_SHIELD,sd->status.shield);
#else
	clif_changelook(&sd->bl,LOOK_WEAPON,0);
#endif

	if(sd->vd.cloth_color)
		clif_refreshlook(&sd->bl,sd->bl.id,LOOK_CLOTHES_COLOR,sd->vd.cloth_color,SELF);

	// item
	pc_checkitem(sd);
	clif_inventorylist(sd);
	
	// cart
	if(pc_iscarton(sd)) {
		clif_cartlist(sd);
		clif_updatestatus(sd,SP_CARTINFO);
	}

	// weight max, now
	clif_updatestatus(sd,SP_MAXWEIGHT);
	clif_updatestatus(sd,SP_WEIGHT);

	// guild
	// (needs to go before clif_spawn() to show guild emblems correctly)
	if(sd->status.guild_id)
		guild_send_memberinfoshort(sd,1);

	if(battle_config.pc_invincible_time > 0) {
		if(map_flag_gvg(sd->bl.m))
			pc_setinvincibletimer(sd,battle_config.pc_invincible_time<<1);
		else
			pc_setinvincibletimer(sd,battle_config.pc_invincible_time);
	}

	map_addblock(&sd->bl);
	clif_spawn(&sd->bl);

	// Party
	// (needs to go after clif_spawn() to show hp bars correctly)
	if(sd->status.party_id) {
		party_send_movemap(sd);
		clif_party_hp(sd); // Show hp after displacement [LuzZza]
	}

	if(map[sd->bl.m].flag.pvp) {
		if(!battle_config.pk_mode) { // remove pvp stuff for pk_mode [Valaris]
			if (!map[sd->bl.m].flag.pvp_nocalcrank)
				sd->pvp_timer = add_timer(gettick()+200, pc_calc_pvprank_timer, sd->bl.id, 0);
			sd->pvp_rank = 0;
			sd->pvp_lastusers = 0;
			sd->pvp_point = 5;
			sd->pvp_won = 0;
			sd->pvp_lost = 0;
		}
		clif_set0199(fd,1);
	} else
	// set flag, if it's a duel [LuzZza]
	if(sd->duel_group)
		clif_set0199(fd,1);

	if (map[sd->bl.m].flag.gvg_dungeon)
	{
		clif_set0199(fd,1); //TODO: Figure out the real packet to send here.
		if (!sd->pvp_point)
		{
			sd->pvp_point = 5; //Need to die twice to be warped out.
			sd->pvp_won = 0;
			sd->pvp_lost = 0;
		}
	}

	if(map_flag_gvg(sd->bl.m))
		clif_set0199(fd,3);

	map_foreachinarea(clif_getareachar, sd->bl.m,
		sd->bl.x-AREA_SIZE, sd->bl.y-AREA_SIZE, sd->bl.x+AREA_SIZE, sd->bl.y+AREA_SIZE,
		BL_ALL, sd);

	// pet
	if(sd->pd) {
		map_addblock(&sd->pd->bl);
		clif_spawn(&sd->pd->bl);
		clif_send_petdata(sd,sd->pd,0,0);
		clif_send_petstatus(sd);
//		skill_unit_move(&sd->pd->bl,gettick(),1);
	}

	//homunculus [blackhole89]
	if(merc_is_hom_active(sd->hd)) {
		map_addblock(&sd->hd->bl);
		clif_spawn(&sd->hd->bl);
		clif_hominfo(sd,sd->hd,1);
		clif_hominfo(sd,sd->hd,0); //for some reason, at least older clients want this sent twice
		clif_send_homdata(sd,0,0);
		clif_homskillinfoblock(sd);
		if (battle_config.hom_setting&0x8)
			status_calc_bl(&sd->hd->bl, SCB_SPEED); //Homunc mimic their master's speed on each map change
		if (!(battle_config.hom_setting&0x2))
			skill_unit_move(&sd->hd->bl,gettick(),1); // apply land skills immediately
	}

	if(sd->state.connect_new) {
		int lv;
		sd->state.connect_new = 0;
		clif_skillinfoblock(sd);
		clif_hotkeys_send(sd);
		clif_updatestatus(sd,SP_NEXTBASEEXP);
		clif_updatestatus(sd,SP_NEXTJOBEXP);
		clif_updatestatus(sd,SP_SKILLPOINT);
		clif_initialstatus(sd);

		if (sd->sc.option&OPTION_FALCON)
			clif_status_load(&sd->bl, SI_FALCON, 1);

		if (sd->sc.option&OPTION_RIDING)
			clif_status_load(&sd->bl, SI_RIDING, 1);

		if(sd->status.manner < 0)
			sc_start(&sd->bl,SC_NOCHAT,100,0,0);

		//Auron reported that This skill only triggers when you logon on the map o.O [Skotlex]
		if ((lv = pc_checkskill(sd,SG_KNOWLEDGE)) > 0) {
			if(sd->bl.m == sd->feel_map[0].m
				|| sd->bl.m == sd->feel_map[1].m
				|| sd->bl.m == sd->feel_map[2].m)
				sc_start(&sd->bl, SC_KNOWLEDGE, 100, lv, skill_get_time(SG_KNOWLEDGE, lv));
		}

		if(sd->pd && sd->pd->pet.intimate > 900)
			clif_pet_emotion(sd->pd,(sd->pd->pet.class_ - 100)*100 + 50 + pet_hungry_val(sd->pd));

		if(merc_is_hom_active(sd->hd))
			merc_hom_init_timers(sd->hd);

		if (night_flag && map[sd->bl.m].flag.nightenabled)
		{
			sd->state.night = 1;
			clif_status_load(&sd->bl, SI_NIGHT, 1);
		}

		// Notify everyone that this char logged in [Skotlex].
		clif_foreachclient(clif_friendslist_toggle_sub, sd->status.account_id, sd->status.char_id, 1);

		//Login Event
		npc_script_event(sd, NPCE_LOGIN);
	} else {
		//For some reason the client "loses" these on map-change.
		clif_updatestatus(sd,SP_STR);
		clif_updatestatus(sd,SP_AGI);
		clif_updatestatus(sd,SP_VIT);
		clif_updatestatus(sd,SP_INT);
		clif_updatestatus(sd,SP_DEX);
		clif_updatestatus(sd,SP_LUK);

		sd->state.using_fake_npc = 0;

		//New 'night' effect by dynamix [Skotlex]
		if (night_flag && map[sd->bl.m].flag.nightenabled)
		{	//Display night.
			if (!sd->state.night) {
				sd->state.night = 1;
				clif_status_load(&sd->bl, SI_NIGHT, 1);
			}
		} else if (sd->state.night) { //Clear night display.
			sd->state.night = 0;
			clif_status_load(&sd->bl, SI_NIGHT, 0);
		}

		if(sd->npc_id)
			npc_event_dequeue(sd);
	}

	// Lance
	if(sd->state.event_loadmap && map[sd->bl.m].flag.loadevent){
		pc_setregstr(sd, add_str("@maploaded$"), map[sd->bl.m].name);
		npc_script_event(sd, NPCE_LOADMAP);
	}

	if (pc_checkskill(sd, SG_DEVIL) && !pc_nextjobexp(sd))
		clif_status_load(&sd->bl, SI_DEVIL, 1);  //blindness [Komurka]

	if (sd->sc.opt2) //Client loses these on warp.
		clif_changeoption(&sd->bl);

	clif_weather_check(sd);
	
	// For automatic triggering of NPCs after map loading (so you don't need to walk 1 step first)
	if (map_getcell(sd->bl.m,sd->bl.x,sd->bl.y,CELL_CHKNPC))
		npc_touch_areanpc(sd,sd->bl.m,sd->bl.x,sd->bl.y);
	else
		sd->areanpc_id = 0;

  	// If player is dead, and is spawned (such as @refresh) send death packet. [Valaris]
	if(pc_isdead(sd))
		clif_clearunit_area(&sd->bl, 1);
// Uncomment if you want to make player face in the same direction he was facing right before warping. [Skotlex]
//	else
//		clif_changed_dir(&sd->bl, SELF);

//	Trigger skill effects if you appear standing on them
	if(!battle_config.pc_invincible_time)
		skill_unit_move(&sd->bl,gettick(),1);
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_TickSend(int fd, struct map_session_data *sd)
{
	sd->client_tick = RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]);

	WFIFOHEAD(fd, packet_len(0x7f));
	WFIFOW(fd,0)=0x7f;
	WFIFOL(fd,2)=gettick();
	WFIFOSET(fd,packet_len(0x7f));
	// removed until the socket problems are fixed. [FlavioJS]
	//flush_fifo(fd); // try to send immediatly so the client gets more accurate "pings"
	return;
}

void clif_hotkeys_send(struct map_session_data *sd) {
#ifdef HOTKEY_SAVING
	const int fd = sd->fd;
	int i;
	if (!fd) return;
	WFIFOHEAD(fd, 2+MAX_HOTKEYS*7);
	WFIFOW(fd, 0) = 0x02b9;
	for(i = 0; i < MAX_HOTKEYS; i++) {
		WFIFOB(fd, 2 + 0 + i * 7) = sd->status.hotkeys[i].type; // type: 0: item, 1: skill
		WFIFOL(fd, 2 + 1 + i * 7) = sd->status.hotkeys[i].id; // item or skill ID
		WFIFOW(fd, 2 + 5 + i * 7) = sd->status.hotkeys[i].lv; // skill level
	}
	WFIFOSET(fd, packet_len(0x02b9));
#endif
}

void clif_parse_Hotkey(int fd, struct map_session_data *sd) {
#ifdef HOTKEY_SAVING
	unsigned short idx;
	int cmd;

	cmd = RFIFOW(fd, 0);
	idx = RFIFOW(fd, packet_db[sd->packet_ver][cmd].pos[0]);
	if (idx >= MAX_HOTKEYS) return;

	sd->status.hotkeys[idx].type = RFIFOB(fd, packet_db[sd->packet_ver][cmd].pos[1]);
	sd->status.hotkeys[idx].id = RFIFOL(fd, packet_db[sd->packet_ver][cmd].pos[2]);
	sd->status.hotkeys[idx].lv = RFIFOW(fd, packet_db[sd->packet_ver][cmd].pos[3]);
	return;
#endif
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_WalkToXY(int fd, struct map_session_data *sd)
{
	int x, y;
	int cmd;

	if (pc_isdead(sd)) {
		clif_clearunit_area(&sd->bl, 1);
		return;
	}

	if (sd->sc.opt1 && sd->sc.opt1 == OPT1_STONEWAIT)
		; //You CAN walk on this OPT1 value.
	else if (pc_cant_act(sd))
		return;

	if(sd->sc.count && sd->sc.data[SC_RUN].timer != -1)
		return;

	pc_delinvincibletimer(sd);

	cmd = RFIFOW(fd,0);
	x = RFIFOB(fd,packet_db[sd->packet_ver][cmd].pos[0]) * 4 +
		(RFIFOB(fd,packet_db[sd->packet_ver][cmd].pos[0] + 1) >> 6);
	y = ((RFIFOB(fd,packet_db[sd->packet_ver][cmd].pos[0]+1) & 0x3f) << 4) +
		(RFIFOB(fd,packet_db[sd->packet_ver][cmd].pos[0] + 2) >> 4);
	//Set last idle time... [Skotlex]
	sd->idletime = last_tick;
	
	unit_walktoxy(&sd->bl, x, y, 4);
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_QuitGame(int fd, struct map_session_data *sd)
{
	WFIFOHEAD(fd,packet_len(0x18b));
	WFIFOW(fd,0) = 0x18b;

	/*	Rovert's prevent logout option fixed [Valaris]	*/
	if (sd->sc.data[SC_CLOAKING].timer==-1 && sd->sc.data[SC_HIDING].timer==-1 &&
		(!battle_config.prevent_logout || DIFF_TICK(gettick(), sd->canlog_tick) > battle_config.prevent_logout)
	) {
		clif_setwaitclose(fd);
		WFIFOW(fd,2)=0;
	} else {
		WFIFOW(fd,2)=1;
	}
	WFIFOSET(fd,packet_len(0x18b));
}


/*==========================================
 *
 *------------------------------------------*/
void check_fake_id(int fd, struct map_session_data *sd, int target_id)
{
	// if player asks for the fake player (only bot and modified client can see a hiden player)
/*	if (target_id == server_char_id) {
		char message_to_gm[strlen(msg_txt(536)) + strlen(msg_txt(540)) + strlen(msg_txt(507)) + strlen(msg_txt(508))];
		sprintf(message_to_gm, msg_txt(536), sd->status.name, sd->status.account_id); // Character '%s' (account: %d) try to use a bot (it tries to detect a fake player).
		intif_wis_message_to_gm(wisp_server_name, battle_config.hack_info_GM_level, message_to_gm);
		// if we block people
		if (battle_config.ban_bot < 0) {
			chrif_char_ask_name(-1, sd->status.name, 1, 0, 0, 0, 0, 0, 0); // type: 1 - block
			clif_setwaitclose(sd->fd); // forced to disconnect because of the hack
			// message about the ban
			sprintf(message_to_gm, msg_txt(540)); //  This player has been definitivly blocked.
		// if we ban people
		} else if (battle_config.ban_bot > 0) {
			chrif_char_ask_name(-1, sd->status.name, 2, 0, 0, 0, 0, battle_config.ban_bot, 0); // type: 2 - ban (year, month, day, hour, minute, second)
			clif_setwaitclose(sd->fd); // forced to disconnect because of the hack
			// message about the ban
			sprintf(message_to_gm, msg_txt(507), battle_config.ban_bot); //  This player has been banned for %d minute(s).
		} else { // impossible to display: we don't send fake player if battle_config.ban_bot is == 0
			// message about the ban
			sprintf(message_to_gm, msg_txt(508)); //  This player hasn't been banned (Ban option is disabled).
		}
		intif_wis_message_to_gm(wisp_server_name, battle_config.hack_info_GM_level, message_to_gm);
		// send this info cause the bot ask until get an answer, damn spam
		memset(WPACKETP(0), 0, packet_len(0x95));
		WPACKETW(0) = 0x95;
		WPACKETL(2) = server_char_id;
		strncpy(WPACKETP(6), sd->status.name, 24);
		SENDPACKET(fd, packet_len(0x95));
		// take fake player out of screen
		WPACKETW(0) = 0x80;
		WPACKETL(2) = server_char_id;
		WPACKETB(6) = 0;
		SENDPACKET(fd, packet_len(0x80));
		// take fake mob out of screen
		WPACKETW(0) = 0x80;
		WPACKETL(2) = server_fake_mob_id;
		WPACKETB(6) = 0;
		SENDPACKET(fd, packet_len(0x80));
	}

	// if player asks for the fake mob (only bot and modified client can see a hiden mob)
	if (target_id == server_fake_mob_id) {
		int fake_mob;
		char message_to_gm[strlen(msg_txt(537)) + strlen(msg_txt(540)) + strlen(msg_txt(507)) + strlen(msg_txt(508))];
		sprintf(message_to_gm, msg_txt(537), sd->status.name, sd->status.account_id); // Character '%s' (account: %d) try to use a bot (it tries to detect a fake mob).
		intif_wis_message_to_gm(wisp_server_name, battle_config.hack_info_GM_level, message_to_gm);
		// if we block people
		if (battle_config.ban_bot < 0) {
			chrif_char_ask_name(-1, sd->status.name, 1, 0, 0, 0, 0, 0, 0); // type: 1 - block
			clif_setwaitclose(sd->fd); // forced to disconnect because of the hack
			// message about the ban
			sprintf(message_to_gm, msg_txt(540)); //  This player has been definitivly blocked.
		// if we ban people
		} else if (battle_config.ban_bot > 0) {
			chrif_char_ask_name(-1, sd->status.name, 2, 0, 0, 0, 0, battle_config.ban_bot, 0); // type: 2 - ban (year, month, day, hour, minute, second)
			clif_setwaitclose(sd->fd); // forced to disconnect because of the hack
			// message about the ban
			sprintf(message_to_gm, msg_txt(507), battle_config.ban_bot); //  This player has been banned for %d minute(s).
		} else { // impossible to display: we don't send fake player if battle_config.ban_bot is == 0
			// message about the ban
			sprintf(message_to_gm, msg_txt(508)); //  This player hasn't been banned (Ban option is disabled).
		}
		intif_wis_message_to_gm(wisp_server_name, battle_config.hack_info_GM_level, message_to_gm);
		// send this info cause the bot ask until get an answer, damn spam
		memset(WPACKETP(0), 0, packet_len(0x95));
		WPACKETW(0) = 0x95;
		WPACKETL(2) = server_fake_mob_id;
		fake_mob = fake_mob_list[(sd->bl.m + sd->fd + sd->status.char_id) % (sizeof(fake_mob_list) / sizeof(fake_mob_list[0]))]; // never same mob
		if (!mobdb_checkid(fake_mob))
			fake_mob = 1002; // poring (default)
		strncpy(WPACKETP(6), mob_db[fake_mob].name, 24);
		SENDPACKET(fd, packet_len(0x95));
		// take fake mob out of screen
		WPACKETW(0) = 0x80;
		WPACKETL(2) = server_fake_mob_id;
		WPACKETB(6) = 0;
		SENDPACKET(fd, packet_len(0x80));
		// take fake player out of screen
		WPACKETW(0) = 0x80;
		WPACKETL(2) = server_char_id;
		WPACKETB(6) = 0;
		SENDPACKET(fd, packet_len(0x80));
	}
*/
	return;
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_GetCharNameRequest(int fd, struct map_session_data *sd)
{
	int account_id;
	struct block_list* bl;
	struct status_change *sc;
	
	account_id = RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]);

	if(account_id<0 && -account_id == sd->bl.id) // for disguises [Valaris]
		account_id= sd->bl.id;

	bl = map_id2bl(account_id);
	//Is this possible? Lagged clients could request names of already gone mobs/players. [Skotlex]
	if (!bl) return;

	sc = status_get_sc(bl);
	if (sc && sc->option&OPTION_INVISIBLE && !disguised(bl) &&
		bl->type != BL_NPC && //Skip hidden NPCs which can be seen using Maya Purple
		pc_isGM(sd) < battle_config.hack_info_GM_level
	) {
		//GM characters (with client side GM enabled are able to see invisible stuff) [Lance]
		//Asked name of invisible player, this shouldn't be possible!
		//Possible bot? Thanks to veider and qspirit
		//FIXME: Still isn't perfected as clients keep asking for this on legitimate situations.
		char gm_msg[256];
		sprintf(gm_msg, "Hack on NameRequest: character '%s' (account: %d) requested the name of an invisible target.", sd->status.name, sd->status.account_id);
		ShowWarning(gm_msg);
		 // information is sended to all online GM
		intif_wis_message_to_gm(wisp_server_name, battle_config.hack_info_GM_level, gm_msg);
		return;
	}
	clif_charnameack(fd, bl);
}

/*==========================================
 * Validates and processes global messages
 * S 008c/00f3 <packet len>.w <text>.?B (<name> : <message>)
 *------------------------------------------*/
void clif_parse_GlobalMessage(int fd, struct map_session_data* sd)
{
	const char *text, *name, *message;
	unsigned int packetlen, textlen, namelen, messagelen;

	packetlen = RFIFOW(fd,2);
	// basic structure checks
	if( packetlen > RFIFOREST(fd) )
	{	// there has to be enough data to read
		ShowWarning("clif_parse_GlobalMessage: Received malformed packet from player '%s' (length is incorrect)!", sd->status.name);
		return;
	}
	if( packetlen < 4 + 1 )
	{	// 4-byte header and at least an empty string is expected
		ShowWarning("clif_parse_GlobalMessage: Received malformed packet from player '%s' (no message data)!", sd->status.name);
		return;
	}

	text = (char*)RFIFOP(fd,4);
	textlen = packetlen - 4;

	name = text;
	namelen = strnlen(sd->status.name, NAME_LENGTH - 1);
	// verify <name> part of the packet
	if( strncmp(name, sd->status.name, namelen) || // the text must start with the speaker's name
		name[namelen] != ' ' || name[namelen+1] != ':' || name[namelen+2] != ' ' ) // followed by ' : '
	{
		//Hacked message, or infamous "client desynch" issue where they pick one char while loading another.
		ShowWarning("clif_parse_GlobalMessage: Player '%s' sent a message using an incorrect name! Forcing a relog...", sd->status.name);
		clif_setwaitclose(fd); // Just kick them out to correct it.
		return;
	}

	message = text + namelen + 3;
	messagelen = textlen - namelen - 3 - 1; // this should be the message length
	// verify <message> part of the packet
	if( message[messagelen] != '\0' )
	{	// message must be zero-terminated
		ShowWarning("clif_parse_GlobalMessage: Player '%s' sent an unterminated string!", sd->status.name);
		return;		
	}
	if( messagelen != strnlen(message, messagelen+1) )
	{	// the declared length must match real length
		ShowWarning("clif_parse_GlobalMessage: Received malformed packet from player '%s' (length is incorrect)!", sd->status.name);
		return;		
	}
	if( messagelen > CHATBOX_SIZE )
	{	// messages mustn't be too long
		int i;
		// special case here - allow some more freedom for frost joke & dazzler 
		// TODO:? You could use a state flag when FrostJoke/Scream is used, and unset it once the skill triggers. [Skotlex]
		ARR_FIND( 0, MAX_SKILLTIMERSKILL, i, sd->ud.skilltimerskill[i] == 0 || sd->ud.skilltimerskill[i]->skill_id == BA_FROSTJOKE || sd->ud.skilltimerskill[i]->skill_id == DC_SCREAM );

		if( i == MAX_SKILLTIMERSKILL || !sd->ud.skilltimerskill[i])
		{	// normal message, too long
			ShowWarning("clif_parse_GlobalMessage: Player '%s' sent a message too long ('%.*s')!", sd->status.name, CHATBOX_SIZE, message);
			return;
		}
		if( messagelen > 255 )
		{	// frost joke/dazzler, but still too long
			ShowWarning("clif_parse_GlobalMessage: Player '%s' sent a message too long ('%.*s')!", sd->status.name, 255, message);
			return;
		}
	}

	if( is_atcommand(fd, sd, text) != AtCommand_None || is_charcommand(fd, sd, text) != CharCommand_None )
		return;

	if( /* sd->sc.data[SC_BERSERK].timer != -1 || */ (sd->sc.data[SC_NOCHAT].timer != -1 && sd->sc.data[SC_NOCHAT].val1&MANNER_NOCHAT) )
		return;

	if( battle_config.min_chat_delay )
	{	//[Skotlex]
		if (DIFF_TICK(sd->cantalk_tick, gettick()) > 0)
			return;
		sd->cantalk_tick = gettick() + battle_config.min_chat_delay;
	}

	// send message to others
	WFIFOHEAD(fd, 8 + textlen);
	WFIFOW(fd,0) = 0x8d;
	WFIFOW(fd,2) = 8 + textlen;
	WFIFOL(fd,4) = sd->bl.id;
	safestrncpy((char*)WFIFOP(fd,8), text, textlen);
	clif_send(WFIFOP(fd,0), WFIFOW(fd,2), &sd->bl, sd->chatID ? CHAT_WOS : AREA_CHAT_WOC);

	// send back message to the speaker
	memcpy(WFIFOP(fd,0), RFIFOP(fd,0), packetlen);
	WFIFOW(fd,0) = 0x8e;
	WFIFOSET(fd, WFIFOW(fd,2));

#ifdef PCRE_SUPPORT
	// trigger listening mobs/npcs
	map_foreachinrange(npc_chat_sub, &sd->bl, AREA_SIZE, BL_NPC, text, textlen, &sd->bl);
	map_foreachinrange(mob_chat_sub, &sd->bl, AREA_SIZE, BL_MOB, text, textlen, &sd->bl);
#endif

	// check for special supernovice phrase
	if( (sd->class_&MAPID_UPPERMASK) == MAPID_SUPER_NOVICE )
	{
		int next = pc_nextbaseexp(sd);
		if( next > 0 && (sd->status.base_exp * 1000 / next)% 100 == 0 ) { // 0%, 10%, 20%, ...
			switch (sd->state.snovice_call_flag) {
			case 0:
					if( strstr(message, msg_txt(504)) ) // "Guardian Angel, can you hear my voice? ^^;"
						sd->state.snovice_call_flag++;
			break;
			case 1: {
					char buf[256];
					sprintf(buf, msg_txt(505), sd->status.name);
					if( strstr(message, buf) ) // "My name is %s, and I'm a Super Novice~"
						sd->state.snovice_call_flag++;
					}
			break;
			case 2:
					if( strstr(message, msg_txt(506)) ) // "Please help me~ T.T"
						sd->state.snovice_call_flag++;
			break;
			case 3:
					if( skillnotok(MO_EXPLOSIONSPIRITS,sd) )
						break; //Do not override the noskill mapflag. [Skotlex]
					clif_skill_nodamage(&sd->bl,&sd->bl,MO_EXPLOSIONSPIRITS,-1,
						sc_start(&sd->bl,SkillStatusChangeTable(MO_EXPLOSIONSPIRITS),100,
						17,skill_get_time(MO_EXPLOSIONSPIRITS,1))); //Lv17-> +50 critical (noted by Poki) [Skotlex]
					sd->state.snovice_call_flag = 0;
			break;
			}
		}
	}

	// Chat logging type 'O' / Global Chat
	if( log_config.chat&1 || (log_config.chat&2 && !(agit_flag && log_config.chat&64)) )
		log_chat("O", 0, sd->status.char_id, sd->status.account_id, mapindex_id2name(sd->mapindex), sd->bl.x, sd->bl.y, NULL, message);

	return;
}

int clif_message(struct block_list *bl, const char* msg)
{
	unsigned short msg_len = strlen(msg) + 1;
	unsigned char buf[256];

	nullpo_retr(0, bl);

	WBUFW(buf, 0) = 0x8d;
	WBUFW(buf, 2) = msg_len + 8;
	WBUFL(buf, 4) = bl->id;
	memcpy(WBUFP(buf, 8), msg, msg_len);

	clif_send(buf, WBUFW(buf,2), bl, AREA_CHAT_WOC);	// by Gengar

	return 0;
}

/*==========================================
 * /mm /mapmove (as @rura GM command)
 *------------------------------------------*/
void clif_parse_MapMove(int fd, struct map_session_data *sd)
{
	char output[MAP_NAME_LENGTH_EXT+15]; // Max length of a short: ' -6XXXX' -> 7 digits
	char message[MAP_NAME_LENGTH_EXT+15+5]; // "/mm "+output
	char* map_name;

	if (battle_config.atc_gmonly && !pc_isGM(sd))
		return;
	if(pc_isGM(sd) < get_atcommand_level(AtCommand_MapMove))
		return;

	map_name = (char*)RFIFOP(fd,2);
	map_name[MAP_NAME_LENGTH_EXT-1]='\0';
	sprintf(output, "%s %d %d", map_name, RFIFOW(fd,18), RFIFOW(fd,20));
	atcommand_rura(fd, sd, "@rura", output);
	if(log_config.gm && get_atcommand_level(AtCommand_MapMove) >= log_config.gm)
	{
		sprintf(message, "/mm %s", output);
		log_atcommand(sd, message);
	}
	return;
}

/*==========================================
 *
 *------------------------------------------*/
void clif_changed_dir(struct block_list *bl, int type)
{
	unsigned char buf[64];

	WBUFW(buf,0) = 0x9c;
	WBUFL(buf,2) = bl->id;
	WBUFW(buf,6) = bl->type==BL_PC?((TBL_PC*)bl)->head_dir:0;
	WBUFB(buf,8) = unit_getdir(bl);

	clif_send(buf, packet_len(0x9c), bl, type);
	if (disguised(bl)) {
		WBUFL(buf,2) = -bl->id;
		WBUFW(buf,6) = 0;
		clif_send(buf, packet_len(0x9c), bl, SELF);
	}

	return;
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_ChangeDir(int fd, struct map_session_data *sd)
{
	unsigned char headdir, dir;

	headdir = RFIFOB(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]);
	dir = RFIFOB(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[1]);
	pc_setdir(sd, dir, headdir);

	clif_changed_dir(&sd->bl, AREA_WOS);
	return;
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_Emotion(int fd, struct map_session_data *sd)
{
	unsigned char buf[64];

	if (battle_config.basic_skill_check == 0 || pc_checkskill(sd, NV_BASIC) >= 2) {
		if (RFIFOB(fd,2) == 34) {// prevent use of the mute emote [Valaris]
			clif_skill_fail(sd, 1, 0, 1);
			return;
		}
		// fix flood of emotion icon (ro-proxy): flood only the hacker player
		if (sd->emotionlasttime >= time(NULL)) {
			sd->emotionlasttime = time(NULL) + 1; // not more than 1 per second (using /commands the client can spam it)
			clif_skill_fail(sd, 1, 0, 1);
			return;
		}
		sd->emotionlasttime = time(NULL) + 1; // not more than 1 per second (using /commands the client can spam it)
		
		WBUFW(buf,0) = 0xc0;
		WBUFL(buf,2) = sd->bl.id;
		WBUFB(buf,6) = RFIFOB(fd,2);
		clif_send(buf, packet_len(0xc0), &sd->bl, AREA);
	} else
		clif_skill_fail(sd, 1, 0, 1);
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_HowManyConnections(int fd, struct map_session_data *sd)
{
	WFIFOHEAD(fd,packet_len(0xc2));
	WFIFOW(fd,0) = 0xc2;
	WFIFOL(fd,2) = map_getusers();
	WFIFOSET(fd,packet_len(0xc2));
}

void clif_parse_ActionRequest_sub(struct map_session_data *sd, int action_type, int target_id, unsigned int tick)
{
	if (pc_isdead(sd)) {
		clif_clearunit_area(&sd->bl, 1);
		return;
	}

	if (sd->sc.count &&
		(sd->sc.data[SC_TRICKDEAD].timer != -1 ||
	 	sd->sc.data[SC_AUTOCOUNTER].timer != -1 ||
		sd->sc.data[SC_BLADESTOP].timer != -1))
		return;

	pc_stop_walking(sd, 1);
	pc_stop_attack(sd);

	if(target_id<0 && -target_id == sd->bl.id) // for disguises [Valaris]
		target_id = sd->bl.id;

	switch(action_type)
	{
	case 0x00: // once attack
	case 0x07: // continuous attack

		if (pc_cant_act(sd) || sd->sc.option&OPTION_HIDE)
			return;

		if(sd->sc.option&(OPTION_WEDDING|OPTION_XMAS|OPTION_SUMMER))
			return;

		if (!battle_config.sdelay_attack_enable && pc_checkskill(sd, SA_FREECAST) <= 0) {
			if (DIFF_TICK(tick, sd->ud.canact_tick) < 0) {
				clif_skill_fail(sd, 1, 4, 0);
				return;
			}
		}

		pc_delinvincibletimer(sd);
		sd->idletime = last_tick;
		unit_attack(&sd->bl, target_id, action_type != 0);
	break;
	case 0x02: // sitdown
		if (battle_config.basic_skill_check && pc_checkskill(sd, NV_BASIC) < 3) {
			clif_skill_fail(sd, 1, 0, 2);
			break;
		}

		if(pc_issit(sd)) {
			//Bugged client? Just refresh them.
			clif_sitting(&sd->bl);
			return;
		}

		if (sd->ud.skilltimer != -1 || sd->sc.opt1)
			break;

		if (sd->sc.count && (
			sd->sc.data[SC_DANCING].timer != -1 ||
			(sd->sc.data[SC_GRAVITATION].timer != -1 && sd->sc.data[SC_GRAVITATION].val3 == BCT_SELF)
		)) //No sitting during these states either.
			break;

		pc_setsit(sd);
		skill_sit(sd,1);
		clif_sitting(&sd->bl);
	break;
	case 0x03: // standup
		if (!pc_issit(sd)) {
			//Bugged client? Just refresh them.
			clif_standing(&sd->bl);
			return;
		}
		pc_setstand(sd);
		skill_sit(sd,0); 
		clif_standing(&sd->bl);
	break;
	}
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_ActionRequest(int fd, struct map_session_data *sd)
{
	clif_parse_ActionRequest_sub(sd,
		RFIFOB(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[1]),
		RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]),
		gettick()
	);
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_Restart(int fd, struct map_session_data *sd)
{
	switch(RFIFOB(fd,2)) {
	case 0x00:
		if (!pc_isdead(sd))
			break;
		pc_setstand(sd);
		pc_setrestartvalue(sd, 3);
		if (pc_setpos(sd, sd->status.save_point.map, sd->status.save_point.x, sd->status.save_point.y, 2))
			clif_resurrection(&sd->bl, 1); //If warping fails, send a normal stand up packet.
		break;
	case 0x01:
		/*	Rovert's Prevent logout option - Fixed [Valaris]	*/
		if (!battle_config.prevent_logout || DIFF_TICK(gettick(), sd->canlog_tick) > battle_config.prevent_logout)
		{	//Send to char-server for character selection.
			chrif_charselectreq(sd, session[fd]->client_addr);
		} else {
			WFIFOHEAD(fd,packet_len(0x18b));
			WFIFOW(fd,0)=0x18b;
			WFIFOW(fd,2)=1;

			WFIFOSET(fd,packet_len(0x018b));
		}
		break;
	}
}

/*==========================================
 * Transmission of a wisp (S 0096 <len>.w <nick>.24B <message>.?B)
 *------------------------------------------*/
void clif_parse_Wis(int fd, struct map_session_data* sd)
{
	char *command, *msg;
	struct map_session_data *dstsd;
	int i=0;
	struct npc_data *npc;
	char split_data[10][50];
	char target[NAME_LENGTH+1];
	char output[256];
	unsigned int len;

	len = RFIFOW(fd,2); //Packet length
	if (len < 28)
	{	//Invalid packet, specified size is less than minimum! [Skotlex]
		ShowWarning("Hack on Whisper: %s (AID/CID: %d:%d)!\n", sd->status.name, sd->status.account_id, sd->status.char_id);
		clif_setwaitclose(fd);
		return;
	}
	if (len == 28) return; //Not sure if client really lets you send a blank line.
	len-=28; //Message length
	// 24+3+(RFIFOW(fd,2)-28)+1 <- last 1 is '\0'
	command = (char*)aMallocA((NAME_LENGTH+4+len) * sizeof(char));
	//No need for out of memory checks, malloc.c aborts the map when that happens.
	msg = command;	
	msg+= sprintf(command, "%s : ", sd->status.name);
	memcpy(msg, RFIFOP(fd, 28), len);
	mes_len_check(msg, len, CHATBOX_SIZE);

	if ((is_charcommand(fd, sd, command) != CharCommand_None) || (is_atcommand(fd, sd, command) != AtCommand_None)) {
		aFree(command);
		return;
	}
	if (sd->sc.count &&
		(/*sd->sc.data[SC_BERSERK].timer!=-1 || */
		(sd->sc.data[SC_NOCHAT].timer != -1 && sd->sc.data[SC_NOCHAT].val1&MANNER_NOCHAT))) {
		aFree(command);
		return;
	}

	if (battle_config.min_chat_delay)
	{	//[Skotlex]
		if (DIFF_TICK(sd->cantalk_tick, gettick()) > 0) {
			aFree(command);
			return;
		}
		sd->cantalk_tick = gettick() + battle_config.min_chat_delay;
	}

	memcpy(&target,RFIFOP(fd, 4),NAME_LENGTH);
	target[NAME_LENGTH]='\0';
	
	// Chat logging type 'W' / Whisper
	if( log_config.chat&1 || (log_config.chat&4 && !(agit_flag && log_config.chat&64)) )
		log_chat("W", 0, sd->status.char_id, sd->status.account_id, mapindex_id2name(sd->mapindex), sd->bl.x, sd->bl.y, target, msg);

	//-------------------------------------------------------//
	//   Lordalfa - Paperboy - To whisper NPC commands       //
	//-------------------------------------------------------//
	if (target[0] && (strncasecmp(target,"NPC:",4) == 0) && (strlen(target) >4))   {
		char *str= target+4; //Skip the NPC: string part.
		if ((npc = npc_name2id(str)))	
		{
			char *split;
			str = msg;
			for( i=0; i < 10; ++i )
			{// Splits the message using '#' as separators
				split = strchr(str,'#');
				if( split == NULL )
				{	// use the remaining string
					strncpy(split_data[i], str, sizeof(split_data[0])/sizeof(char));
					split_data[i][sizeof(split_data[0])/sizeof(char)-1] = '\0';
					for( ++i; i < 10; ++i )
						split_data[i][0] = '\0';
					break;
				}
				*split = '\0';
				strncpy(split_data[i], str, sizeof(split_data[0])/sizeof(char));
				split_data[i][sizeof(split_data[0])/sizeof(char)-1] = '\0';
				str = split+1;
			}
			
			for (i=0;i<10;i++)
			{
				sprintf(output, "@whispervar%d$", i);
				set_var(sd,output,(char *) split_data[i]);
			}
			
			sprintf(output, "%s::OnWhisperGlobal", npc->name);
			npc_event(sd,output,0); // Calls the NPC label 

			aFree(command);
			return;     
		}
	}
	
	// Main chat [LuzZza]
	if(strcmpi(target, main_chat_nick) == 0) {
		if(!sd->state.mainchat)
			clif_displaymessage(fd, msg_txt(388)); // You should enable main chat with "@main on" command.
		else {
			snprintf(output, sizeof(output)/sizeof(char), msg_txt(386), sd->status.name, msg);
			intif_announce(output, strlen(output) + 1, 0xFE000000, 0);
		}
		aFree(command);
		return;
	}

	// searching destination character
	dstsd = map_nick2sd(target);
	// player is not on this map-server
	// At this point, don't send wisp/page if it's not exactly the same name, because (example)
	// if there are 'Test' player on an other map-server and 'test' player on this map-server,
	// and if we ask for 'Test', we must not contact 'test' player
	// so, we send information to inter-server, which is the only one which decide (and copy correct name).
	if (dstsd == NULL || strcmp(dstsd->status.name, target) != 0)
	{	// send message to inter-server
		intif_wis_message(sd, target, msg, len);
		aFree(command);
		return;
	}
	// player is on this map-server
	if (dstsd->fd == fd) {
		// if you send to your self, don't send anything to others
		// but, normally, it's impossible!
		clif_wis_message(fd, wisp_server_name,
			"You can not page yourself. Sorry.",
			strlen("You can not page yourself. Sorry.") + 1);
		aFree(command);
		return;
	}
	// otherwise, send message and answer immediately
	if (dstsd->state.ignoreAll) {
		if (dstsd->sc.option & OPTION_INVISIBLE && pc_isGM(sd) < pc_isGM(dstsd))
			clif_wis_end(fd, 1); // 1: target character is not loged in
		else
			clif_wis_end(fd, 3); // 3: everyone ignored by target
		aFree(command);
		return;
	}
	// if player ignore the source character
	for(i = 0; i < MAX_IGNORE_LIST &&
		dstsd->ignore[i].name[0] != '\0' &&
		strcmp(dstsd->ignore[i].name, sd->status.name) != 0
		; i++);

	if(i < MAX_IGNORE_LIST && dstsd->ignore[i].name[0] != '\0')
	{	//Ignored
		clif_wis_end(fd, 2);	// 2: ignored by target
		aFree(command);
		return;
	}

	// if source player not found in ignore list
	if(dstsd->away_message[0] != '\0') { // Send away automessage [LuzZza]
		//(Automessage has been sent)
		sprintf(output, "%s %s", msg, msg_txt(543));
		clif_wis_message(dstsd->fd, sd->status.name, output, strlen(output) + 1);
		clif_wis_end(fd, 0); // 0: success to send wisper
		if(dstsd->state.autotrade)
			//"Away [AT] - \"%s\""
			sprintf(output, msg_txt(544), dstsd->away_message);
		else
			//"Away - \"%s\""
			sprintf(output, msg_txt(545), dstsd->away_message);
		aFree(command);
		clif_wis_message(fd, dstsd->status.name, output, strlen(output) + 1);
		return;
	}
	// Normal message
	clif_wis_message(dstsd->fd, sd->status.name, msg, len);
	clif_wis_end(fd, 0); // 0: success to send wisper
	aFree(command);
	return;
}

/*==========================================
 * /b
 *------------------------------------------*/
void clif_parse_GMmessage(int fd, struct map_session_data *sd)
{
	char* mes;
	int size, lv;

	if (battle_config.atc_gmonly && !pc_isGM(sd))
		return;
	if (pc_isGM(sd) < (lv=get_atcommand_level(AtCommand_Broadcast)))
		return;

	size = RFIFOW(fd,2)-4;
	mes = (char*)RFIFOP(fd,4);
	mes_len_check(mes, size, CHAT_SIZE);

	intif_GMmessage(mes, size, 0);
	if(log_config.gm && lv >= log_config.gm) {
		char message[CHAT_SIZE+4];
		sprintf(message, "/b %s", mes);
		log_atcommand(sd, message);
	}
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_TakeItem(int fd, struct map_session_data *sd)
{
	struct flooritem_data *fitem;
	int map_object_id;

	map_object_id = RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]);
	
	fitem = (struct flooritem_data*)map_id2bl(map_object_id);

	do {
		if (pc_isdead(sd)) {
			clif_clearunit_area(&sd->bl, 1);
			break;
		}

		if (fitem == NULL || fitem->bl.type != BL_ITEM || fitem->bl.m != sd->bl.m)
			break;
	
		if (pc_cant_act(sd))
			break;

  		//Disable cloaking/chasewalking characters from looting [Skotlex]
		if(pc_iscloaking(sd) || pc_ischasewalk(sd))
			break;
		
		if(sd->sc.count && (
			sd->sc.data[SC_TRICKDEAD].timer != -1 ||
			sd->sc.data[SC_BLADESTOP].timer != -1 ||
			(sd->sc.data[SC_NOCHAT].timer!=-1 && sd->sc.data[SC_NOCHAT].val1&MANNER_NOITEM))
		)
			break;

		if (!pc_takeitem(sd, fitem))
			break;

		return;
	} while (0);
	// Client REQUIRES a fail packet or you can no longer pick items.
	clif_additem(sd,0,0,6);
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_DropItem(int fd, struct map_session_data *sd)
{
	int item_index, item_amount;

	if (pc_isdead(sd)) {
		clif_clearunit_area(&sd->bl, 1);
		return;
	}

	if (pc_cant_act(sd))
		return;

	if (sd->sc.count && (
		sd->sc.data[SC_AUTOCOUNTER].timer != -1 ||
		sd->sc.data[SC_BLADESTOP].timer != -1 ||
		(sd->sc.data[SC_NOCHAT].timer!=-1 && sd->sc.data[SC_NOCHAT].val1&MANNER_NOITEM)
	))
		return;

	item_index = RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0])-2;
	item_amount = RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[1]);
	if (!pc_dropitem(sd, item_index, item_amount))
	//Because the client does not likes being ignored.
		clif_delitem(sd, item_index,0);

}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_UseItem(int fd, struct map_session_data *sd)
{
	int n;

	if (pc_isdead(sd)) {
		clif_clearunit_area(&sd->bl, 1);
		return;
	}

	if (sd->sc.opt1 > 0 && sd->sc.opt1 != OPT1_STONEWAIT)
		return;
	
	//This flag enables you to use items while in an NPC. [Skotlex]
	if (sd->npc_id) {
		if (sd->npc_id != sd->npc_item_flag)
			return;
	} else
	if (pc_istrading(sd))
		return;
	
	pc_delinvincibletimer(sd);

	//Whether the item is used or not is irrelevant, the char ain't idle. [Skotlex]
	sd->idletime = last_tick;
	n = RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0])-2;
	
	if(n <0 || n >= MAX_INVENTORY)
		return;
	if (!pc_useitem(sd,n))
		clif_useitemack(sd,n,0,0); //Send an empty ack packet or the client gets stuck.
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_EquipItem(int fd,struct map_session_data *sd)
{
	int index;

	if(pc_isdead(sd)) {
		clif_clearunit_area(&sd->bl,1);
		return;
	}
	index = RFIFOW(fd,2)-2; 
	if (index < 0 || index >= MAX_INVENTORY)
		return; //Out of bounds check.
	
	if(sd->npc_id) {
		if (sd->npc_id != sd->npc_item_flag)
			return;
	} else if (sd->state.storage_flag || sd->sc.opt1)
		; //You can equip/unequip stuff while storage is open/under status changes
	else if (pc_cant_act(sd))
		return;

	if(sd->sc.data[SC_BLADESTOP].timer!=-1 || sd->sc.data[SC_BERSERK].timer!=-1 )
		return;

	if(!sd->status.inventory[index].identify) {
		clif_equipitemack(sd,index,0,0);	// fail
		return;
	}

	if(!sd->inventory_data[index])
		return;

	if(sd->inventory_data[index]->type == IT_PETARMOR){
		pet_equipitem(sd,index);
		return;
	}
	
	//Client doesn't send the position for ammo.
	if(sd->inventory_data[index]->type == IT_AMMO)
		pc_equipitem(sd,index,EQP_AMMO);
	else
		pc_equipitem(sd,index,RFIFOW(fd,4));
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_UnequipItem(int fd,struct map_session_data *sd)
{
	int index;

	if(pc_isdead(sd)) {
		clif_clearunit_area(&sd->bl,1);
		return;
	}

	if (sd->state.storage_flag)
		; //You can equip/unequip stuff while storage is open.
	else if (pc_cant_act(sd))
		return;

	index = RFIFOW(fd,2)-2;

	pc_unequipitem(sd,index,1);
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_NpcClicked(int fd,struct map_session_data *sd)
{
	struct block_list *bl;

	if(pc_isdead(sd)) {
		clif_clearunit_area(&sd->bl,1);
		return;
	}

	if (pc_cant_act(sd))
		return;

	bl = map_id2bl(RFIFOL(fd,2));
	if (!bl) return;
	switch (bl->type) {
		case BL_MOB:
			if (!((TBL_MOB *)bl)->nd || !mob_script_callback((TBL_MOB *)bl, &sd->bl, CALLBACK_NPCCLICK))
				clif_parse_ActionRequest_sub(sd, 0x07, bl->id, gettick());
			break;
		case BL_PC:
			clif_parse_ActionRequest_sub(sd, 0x07, bl->id, gettick());
			break;
		case BL_NPC:
			npc_click(sd,(TBL_NPC*)bl);
			break;
	}
	return;
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_NpcBuySellSelected(int fd,struct map_session_data *sd)
{
	if (sd->state.trading)
		return;
	npc_buysellsel(sd,RFIFOL(fd,2),RFIFOB(fd,6));
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_NpcBuyListSend(int fd,struct map_session_data *sd)
{
	int fail=0,n;
	unsigned short *item_list;

	n = (RFIFOW(fd,2)-4) /4;
	item_list = (unsigned short*)RFIFOP(fd,4);

	if (sd->state.trading|| !sd->npc_shopid)
		fail = 1;
	else
		fail = npc_buylist(sd,n,item_list);

	sd->npc_shopid = 0; //Clear shop data.
	WFIFOHEAD(fd,packet_len(0xca));
	WFIFOW(fd,0)=0xca;
	WFIFOB(fd,2)=fail;
	WFIFOSET(fd,packet_len(0xca));
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_NpcSellListSend(int fd,struct map_session_data *sd)
{
	int fail=0,n;
	unsigned short *item_list;

	n = (RFIFOW(fd,2)-4) /4;
	item_list = (unsigned short*)RFIFOP(fd,4);

	if (sd->state.trading || !sd->npc_shopid)
		fail = 1;
	else
		fail = npc_selllist(sd,n,item_list);
	
	sd->npc_shopid = 0; //Clear shop data.

	WFIFOHEAD(fd,packet_len(0xcb));
	WFIFOW(fd,0)=0xcb;
	WFIFOB(fd,2)=fail;
	WFIFOSET(fd,packet_len(0xcb));
}

/*==========================================
 * Chatroom creation request
 * S 00d5 <len>.w <limit>.w <pub>.B <passwd>.8B <title>.?B
 *------------------------------------------*/
void clif_parse_CreateChatRoom(int fd, struct map_session_data* sd)
{
	int len = RFIFOW(fd,2)-15;
	int limit = RFIFOW(fd,4);
	bool pub = (RFIFOB(fd,6) != 0);
	const char* password = (char*)RFIFOP(fd,7); //not zero-terminated
	const char* title = (char*)RFIFOP(fd,15); // not zero-terminated
	char s_title[CHATROOM_TITLE_SIZE];
	char s_password[CHATROOM_PASS_SIZE];

	if (sd->sc.data[SC_NOCHAT].timer!=-1 && sd->sc.data[SC_NOCHAT].val1&MANNER_NOROOM)
		return;
	if(battle_config.basic_skill_check && pc_checkskill(sd,NV_BASIC) < 4) {
		clif_skill_fail(sd,1,0,3);
		return;
	}

	safestrncpy(s_title, title, min(len+1,CHATROOM_TITLE_SIZE));
	safestrncpy(s_password, password, CHATROOM_PASS_SIZE);

	chat_createpcchat(sd, s_title, s_password, limit, pub);
}

/*==========================================
 * Chatroom join request
 * S 00d9 <chat ID>.l <passwd>.8B
 *------------------------------------------*/
void clif_parse_ChatAddMember(int fd, struct map_session_data* sd)
{
	int chatid = RFIFOL(fd,2);
	const char* password = (char*)RFIFOP(fd,6); // not zero-terminated

	chat_joinchat(sd,chatid,password);
}

/*==========================================
 * Chatroom properties adjustment request
 * S 00de <len>.w <limit>.w <pub>.B <passwd>.8B <title>.?B
 *------------------------------------------*/
void clif_parse_ChatRoomStatusChange(int fd, struct map_session_data* sd)
{
	int len = RFIFOW(fd,2)-15;
	int limit = RFIFOW(fd,4);
	bool pub = (RFIFOB(fd,6) != 0);
	const char* password = (char*)RFIFOP(fd,7); // not zero-terminated
	const char* title = (char*)RFIFOP(fd,15); // not zero-terminated

	char s_title[CHATROOM_TITLE_SIZE];
	char s_password[CHATROOM_PASS_SIZE];
	safestrncpy(s_title, title, min(len+1,CHATROOM_TITLE_SIZE));
	safestrncpy(s_password, password, CHATROOM_PASS_SIZE);

	chat_changechatstatus(sd, s_title, s_password, limit, pub);
}

/*==========================================
 * S 00e0 ?.l <nick>.24B
 *------------------------------------------*/
void clif_parse_ChangeChatOwner(int fd, struct map_session_data* sd)
{
	//TODO: the first argument seems to be the destination position (always 0) [ultramage]
	chat_changechatowner(sd,(char*)RFIFOP(fd,6));
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_KickFromChat(int fd,struct map_session_data *sd)
{
	chat_kickchat(sd,(char*)RFIFOP(fd,2));
}

/*==========================================
 * Request to leave the current chatroom
 * S 00e3
 *------------------------------------------*/
void clif_parse_ChatLeave(int fd, struct map_session_data* sd)
{
	chat_leavechat(sd,0);
}

//Handles notifying asker and rejecter of what has just ocurred.
//Type is used to determine the correct msg_txt to use:
//0: 
static void clif_noask_sub(struct map_session_data *src, struct map_session_data *target, int type)
{
	char *msg, output[256];
	// Your request has been rejected by autoreject option.
	msg = msg_txt(392);
	clif_disp_onlyself(src, msg, strlen(msg));
	//Notice that a request was rejected.
	snprintf(output, 256, msg_txt(393+type), src->status.name, 256);
	clif_disp_onlyself(target, output, strlen(output));
}

/*==========================================
 * ����v���𑊎�ɑ���
 *------------------------------------------*/
void clif_parse_TradeRequest(int fd,struct map_session_data *sd)
{
	struct map_session_data *t_sd;
	
	t_sd = map_id2sd(RFIFOL(fd,2));

	if(!sd->chatID && pc_cant_act(sd))
		return; //You can trade while in a chatroom.

	// @noask [LuzZza]
	if(t_sd && t_sd->state.noask) {
		clif_noask_sub(sd, t_sd, 0);
		return;
	}

	if(battle_config.basic_skill_check == 0 || pc_checkskill(sd,NV_BASIC) >= 1){
		trade_traderequest(sd,t_sd);
	} else
		clif_skill_fail(sd,1,0,0);
}

/*==========================================
 * ����v��
 *------------------------------------------*/
void clif_parse_TradeAck(int fd,struct map_session_data *sd)
{
	trade_tradeack(sd,RFIFOB(fd,2));
}

/*==========================================
 * �A�C�e���ǉ�
 *------------------------------------------*/
void clif_parse_TradeAddItem(int fd,struct map_session_data *sd)
{
	int index = RFIFOW(fd,2);
	int amount = RFIFOL(fd,4);

	if( index == 0 )
		trade_tradeaddzeny(sd, amount);
	else
		trade_tradeadditem(sd, index, amount);
}

/*==========================================
 * �A�C�e���ǉ�����(ok����)
 *------------------------------------------*/
void clif_parse_TradeOk(int fd,struct map_session_data *sd)
{
	trade_tradeok(sd);
}

/*==========================================
 * ����L�����Z��
 *------------------------------------------*/
void clif_parse_TradeCancel(int fd,struct map_session_data *sd)
{
	trade_tradecancel(sd);
}

/*==========================================
 * �������(trade����)
 *------------------------------------------*/
void clif_parse_TradeCommit(int fd,struct map_session_data *sd)
{
	trade_tradecommit(sd);
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_StopAttack(int fd,struct map_session_data *sd)
{
	pc_stop_attack(sd);
}

/*==========================================
 * �J�[�g�փA�C�e�����ڂ�
 *------------------------------------------*/
void clif_parse_PutItemToCart(int fd,struct map_session_data *sd)
{
	if (pc_istrading(sd))
		return;
	if (!pc_iscarton(sd))
		return;
	pc_putitemtocart(sd,RFIFOW(fd,2)-2,RFIFOL(fd,4));
}
/*==========================================
 * �J�[�g����A�C�e�����o��
 *------------------------------------------*/
void clif_parse_GetItemFromCart(int fd,struct map_session_data *sd)
{
	if (!pc_iscarton(sd))
		return;
	pc_getitemfromcart(sd,RFIFOW(fd,2)-2,RFIFOL(fd,4));
}

/*==========================================
 * �t���i(��,�y�R,�J�[�g)���͂���
 *------------------------------------------*/
void clif_parse_RemoveOption(int fd,struct map_session_data *sd)
{
	//Can only remove Cart/Riding/Falcon.
	pc_setoption(sd,sd->sc.option&~(OPTION_CART|OPTION_RIDING|OPTION_FALCON));
}

/*==========================================
 * �`�F���W�J�[�g
 *------------------------------------------*/
void clif_parse_ChangeCart(int fd,struct map_session_data *sd)
{
	int type;

	type = (int)RFIFOW(fd,2);

	if( (type == 5 && sd->status.base_level <= 90) ||
		(type == 4 && sd->status.base_level <= 80) ||
		(type == 3 && sd->status.base_level <= 65) ||
		(type == 2 && sd->status.base_level <= 40) ||
		pc_setcart(sd,type) )
	{
		LOG_SUSPICIOUS(sd,"clif_parse_ChangeCart");
	}
}

/*==========================================
 * �X�e�[�^�X�A�b�v
 *------------------------------------------*/
void clif_parse_StatusUp(int fd,struct map_session_data *sd)
{
	pc_statusup(sd,RFIFOW(fd,2));
}

/*==========================================
 * �X�L�����x���A�b�v
 *------------------------------------------*/
void clif_parse_SkillUp(int fd,struct map_session_data *sd)
{
	pc_skillup(sd,RFIFOW(fd,2));
}

static void clif_parse_UseSkillToId_homun(struct homun_data *hd, struct map_session_data *sd, unsigned int tick, int skillnum, int skilllv, int target_id)
{
	int lv;

	if (!hd)
		return;

	if (skillnotok_hom(skillnum, hd))	//[orn]
		return;

	if (hd->bl.id != target_id &&
		skill_get_inf(skillnum)&INF_SELF_SKILL)
		target_id = hd->bl.id; //What good is it to mess up the target in self skills? Wished I knew... [Skotlex]
	
	if (hd->ud.skilltimer != -1) {
		if (skillnum != SA_CASTCANCEL)
			return;
	} else if (DIFF_TICK(tick, hd->ud.canact_tick) < 0)
		return;

	lv = merc_hom_checkskill(hd, skillnum);
	if (skilllv > lv)
		skilllv = lv;
			
	if (skilllv)
		unit_skilluse_id(&hd->bl, target_id, skillnum, skilllv);
}

/*==========================================
 * �X�L���g�p�iID�w��j
 *------------------------------------------*/
void clif_parse_UseSkillToId(int fd, struct map_session_data *sd)
{
	int skillnum, skilllv, tmp, target_id;
	unsigned int tick = gettick();

	skilllv = RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]);
	if (skilllv < 1) skilllv = 1; //No clue, I have seen the client do this with guild skills :/ [Skotlex]
	skillnum = RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[1]);
	target_id = RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[2]);

	//Whether skill fails or not is irrelevant, the char ain't idle. [Skotlex]
	sd->idletime = last_tick;

	tmp = skill_get_inf(skillnum);
	if (tmp&INF_GROUND_SKILL || !tmp)
		return; //Using a ground/passive skill on a target? WRONG.

	if (skillnum >= HM_SKILLBASE && skillnum <= HM_SKILLBASE+MAX_HOMUNSKILL) {
		clif_parse_UseSkillToId_homun(sd->hd, sd, tick, skillnum, skilllv, target_id);
		return;
	}

	if (pc_cant_act(sd))
		return;
	if (pc_issit(sd))
		return;

	if (skillnotok(skillnum, sd))
		return;

	if (sd->bl.id != target_id && !sd->state.skill_flag && tmp&INF_SELF_SKILL)
		target_id = sd->bl.id; //What good is it to mess up the target in self skills? Wished I knew... [Skotlex]
	
	if (sd->ud.skilltimer != -1) {
		if (skillnum != SA_CASTCANCEL)
			return;
	} else if (DIFF_TICK(tick, sd->ud.canact_tick) < 0) {
		clif_skill_fail(sd, skillnum, 4, 0);
		return;
	}

	if(sd->sc.option&(OPTION_WEDDING|OPTION_XMAS|OPTION_SUMMER))
		return;
	
	if(target_id<0 && -target_id == sd->bl.id) // for disguises [Valaris]
		target_id = sd->bl.id;
	
	if(sd->menuskill_id)
	{
		if (sd->menuskill_id == SA_TAMINGMONSTER)
			sd->menuskill_id = sd->menuskill_val = 0; //Cancel pet capture.
		else
		if (sd->menuskill_id != SA_AUTOSPELL)
			return; //Can't use skills while a menu is open.
	}
	if (sd->skillitem == skillnum) {
		if (skilllv != sd->skillitemlv)
			skilllv = sd->skillitemlv;
		unit_skilluse_id(&sd->bl, target_id, skillnum, skilllv);
		return;
	}

	sd->skillitem = sd->skillitemlv = 0;
	if (skillnum == MO_EXTREMITYFIST) {
		if ((sd->sc.data[SC_COMBO].timer == -1 ||
			(sd->sc.data[SC_COMBO].val1 != MO_COMBOFINISH &&
			sd->sc.data[SC_COMBO].val1 != CH_TIGERFIST &&
			sd->sc.data[SC_COMBO].val1 != CH_CHAINCRUSH))) {
			if (!sd->state.skill_flag ) {
				sd->state.skill_flag = 1;
				clif_skillinfo(sd, MO_EXTREMITYFIST, INF_ATTACK_SKILL, -1);
				return;
			} else if (sd->bl.id == target_id) {
				clif_skillinfo(sd, MO_EXTREMITYFIST, INF_ATTACK_SKILL, -1);
				return;
			}
		}
	}
	if (skillnum == TK_JUMPKICK) {
		if (sd->sc.data[SC_COMBO].timer == -1 ||
			sd->sc.data[SC_COMBO].val1 != TK_JUMPKICK) {
			if (!sd->state.skill_flag ) {
				sd->state.skill_flag = 1;
				clif_skillinfo(sd, TK_JUMPKICK, INF_ATTACK_SKILL, -1);
				return;
			} else if (sd->bl.id == target_id) {
				clif_skillinfo(sd, TK_JUMPKICK, INF_ATTACK_SKILL, -1);
				return;
			}
		}
	}

	if (skillnum >= GD_SKILLBASE) {
		if (sd->state.gmaster_flag)
			skilllv = guild_checkskill(sd->state.gmaster_flag, skillnum);
		else
			skilllv = 0;
	} else {
		tmp = pc_checkskill(sd, skillnum);
		if (skilllv > tmp)
			skilllv = tmp;
	}

	pc_delinvincibletimer(sd);
	
	if (skilllv)
		unit_skilluse_id(&sd->bl, target_id, skillnum, skilllv);

	if (sd->state.skill_flag)
		sd->state.skill_flag = 0;
}

/*==========================================
 * �X�L���g�p�i�ꏊ�w��j
 *------------------------------------------*/
void clif_parse_UseSkillToPosSub(int fd, struct map_session_data *sd, int skilllv, int skillnum, int x, int y, int skillmoreinfo)
{
	int lv;
	unsigned int tick = gettick();

	//Whether skill fails or not is irrelevant, the char ain't idle. [Skotlex]
	sd->idletime = last_tick;

	if (skillnotok(skillnum, sd))
		return;

	if (!(skill_get_inf(skillnum)&INF_GROUND_SKILL))
		return; //Using a target skill on the ground? WRONG.

	if (skillmoreinfo != -1) {
		if (pc_issit(sd)) {
			clif_skill_fail(sd, skillnum, 0, 0);
			return;
		}
		//You can't use Graffiti/TalkieBox AND have a vending open, so this is safe.
		memcpy(sd->message, RFIFOP(fd,skillmoreinfo), MESSAGE_SIZE);
		sd->message[MESSAGE_SIZE-1] = '\0'; //Overflow protection [Skotlex]
	}

	if (sd->ud.skilltimer != -1)
		return;
	if (DIFF_TICK(tick, sd->ud.canact_tick) < 0)
	{
		clif_skill_fail(sd, skillnum, 4, 0);
		return;
	}

	if(sd->sc.option&(OPTION_WEDDING|OPTION_XMAS|OPTION_SUMMER))
		return;
	
	if(sd->menuskill_id)
	{
		if (sd->menuskill_id == SA_TAMINGMONSTER)
			sd->menuskill_id = sd->menuskill_val = 0; //Cancel pet capture.
		else
		if (sd->menuskill_id != SA_AUTOSPELL)
			return; //Can't use skills while a menu is open.
	}

	pc_delinvincibletimer(sd);

	if (sd->skillitem == skillnum) {
		if (skilllv != sd->skillitemlv)
			skilllv = sd->skillitemlv;
		unit_skilluse_pos(&sd->bl, x, y, skillnum, skilllv);
	} else {
		sd->skillitem = sd->skillitemlv = 0;
		if ((lv = pc_checkskill(sd, skillnum)) > 0) {
			if (skilllv > lv)
				skilllv = lv;
			unit_skilluse_pos(&sd->bl, x, y, skillnum,skilllv);
		}
	}
}


void clif_parse_UseSkillToPos(int fd, struct map_session_data *sd)
{
	if (pc_cant_act(sd))
		return;
	if (pc_issit(sd))
		return;

	clif_parse_UseSkillToPosSub(fd, sd,
		RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]), //skill lv
		RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[1]), //skill num
		RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[2]), //pos x
		RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[3]), //pos y
		-1	//Skill more info.
	);
}

void clif_parse_UseSkillToPosMoreInfo(int fd, struct map_session_data *sd)
{
	if (pc_cant_act(sd))
		return;
	if (pc_issit(sd))
		return;
	
	clif_parse_UseSkillToPosSub(fd, sd,
		RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]), //Skill lv
		RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[1]), //Skill num
		RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[2]), //pos x
		RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[3]), //pos y
		packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[4] //skill more info
	);
}
/*==========================================
 * �X�L���g�p�imap�w��j
 *------------------------------------------*/
void clif_parse_UseSkillMap(int fd, struct map_session_data* sd)
{
	int skill_num = RFIFOW(fd,2);
	char map_name[MAP_NAME_LENGTH];
	mapindex_getmapname((char*)RFIFOP(fd,4), map_name);

	if(skill_num != sd->menuskill_id) 
		return;

	if (pc_cant_act(sd))
	{
		sd->menuskill_id = sd->menuskill_val = 0;
		return;
	}

	pc_delinvincibletimer(sd);
	skill_castend_map(sd,skill_num,map_name);
}
/*==========================================
 * �����v��
 *------------------------------------------*/
void clif_parse_RequestMemo(int fd,struct map_session_data *sd)
{
	if (!pc_isdead(sd))
		pc_memo(sd,-1);
}
/*==========================================
 * �A�C�e������
 *------------------------------------------*/
void clif_parse_ProduceMix(int fd,struct map_session_data *sd)
{
	if (sd->menuskill_id !=	AM_PHARMACY)
		return;

	if (pc_istrading(sd)) {
		//Make it fail to avoid shop exploits where you sell something different than you see.
		clif_skill_fail(sd,sd->ud.skillid,0,0);
		sd->menuskill_val = sd->menuskill_id = 0;
		return;
	}
	skill_produce_mix(sd,0,RFIFOW(fd,2),RFIFOW(fd,4),RFIFOW(fd,6),RFIFOW(fd,8), 1);
	sd->menuskill_val = sd->menuskill_id = 0;
}
/*==========================================
 * ����C��
 *------------------------------------------*/
void clif_parse_RepairItem(int fd, struct map_session_data *sd)
{
	if (sd->menuskill_id != BS_REPAIRWEAPON)
		return;
	if (pc_istrading(sd)) {
		//Make it fail to avoid shop exploits where you sell something different than you see.
		clif_skill_fail(sd,sd->ud.skillid,0,0);
		sd->menuskill_val = sd->menuskill_id = 0;
		return;
	}
	skill_repairweapon(sd,RFIFOW(fd,2));
	sd->menuskill_val = sd->menuskill_id = 0;
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_WeaponRefine(int fd, struct map_session_data *sd)
{
	int idx;

	if (sd->menuskill_id != WS_WEAPONREFINE) //Packet exploit?
		return;
	if (pc_istrading(sd)) {
		//Make it fail to avoid shop exploits where you sell something different than you see.
		clif_skill_fail(sd,sd->ud.skillid,0,0);
		sd->menuskill_val = sd->menuskill_id = 0;
		return;
	}
	idx = RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]);
	skill_weaponrefine(sd, idx-2);
	sd->menuskill_val = sd->menuskill_id = 0;
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_NpcSelectMenu(int fd,struct map_session_data *sd)
{
	uint8 select;

	select = RFIFOB(fd,6);
	if((select > sd->npc_menu && select != 0xff) || !select){
		ShowWarning("Hack on NPC Select Menu: %s (AID: %d)!\n",sd->status.name,sd->bl.id);
		clif_GM_kick(sd,sd,0);
	} else {
		sd->npc_menu=select;
		npc_scriptcont(sd,RFIFOL(fd,2));
	}
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_NpcNextClicked(int fd,struct map_session_data *sd)
{
	npc_scriptcont(sd,RFIFOL(fd,2));
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_NpcAmountInput(int fd,struct map_session_data *sd)
{
	sd->npc_amount=(int)RFIFOL(fd,6);
	npc_scriptcont(sd,RFIFOL(fd,2));
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_NpcStringInput(int fd,struct map_session_data *sd)
{
	short message_len;
	message_len = RFIFOW(fd,2)-7;

	if(message_len < 1)
		return; //Blank message?

	if(message_len >= sizeof(sd->npc_str)){
		ShowWarning("clif: input string too long !\n");
		message_len = sizeof(sd->npc_str);
	}

	// Exploit prevention if crafted packets (without null) is being sent. [Lance]
	memcpy(sd->npc_str,RFIFOP(fd,8),message_len); 
	sd->npc_str[message_len-1]=0;
	npc_scriptcont(sd,RFIFOL(fd,4));
}

/*==========================================
 *
 *------------------------------------------*/
void clif_parse_NpcCloseClicked(int fd,struct map_session_data *sd)
{
	if (!sd->npc_id) //Avoid parsing anything when the script was done with. [Skotlex]
		return; 
	npc_scriptcont(sd,RFIFOL(fd,2));
}

/*==========================================
 * �A�C�e���Ӓ�
 *------------------------------------------*/
void clif_parse_ItemIdentify(int fd,struct map_session_data *sd)
{
	if (sd->menuskill_id != MC_IDENTIFY)
		return;
	skill_identify(sd,RFIFOW(fd,2)-2);
	sd->menuskill_val = sd->menuskill_id = 0;
}
/*==========================================
 * ��쐬
 *------------------------------------------*/
void clif_parse_SelectArrow(int fd,struct map_session_data *sd)
{
	if (sd->menuskill_id != AC_MAKINGARROW)
		return;
	if (pc_istrading(sd)) {
	//Make it fail to avoid shop exploits where you sell something different than you see.
		clif_skill_fail(sd,sd->ud.skillid,0,0);
		sd->menuskill_val = sd->menuskill_id = 0;
		return;
	}
	skill_arrow_create(sd,RFIFOW(fd,2));
	sd->menuskill_val = sd->menuskill_id = 0;
}
/*==========================================
 * �I�[�g�X�y����M
 *------------------------------------------*/
void clif_parse_AutoSpell(int fd,struct map_session_data *sd)
{
	if (sd->menuskill_id != SA_AUTOSPELL)
		return;
	skill_autospell(sd,RFIFOW(fd,2));
	sd->menuskill_val = sd->menuskill_id = 0;
}
/*==========================================
 * �J�[�h�g�p
 *------------------------------------------*/
void clif_parse_UseCard(int fd,struct map_session_data *sd)
{
	if (sd->state.trading != 0)
		return;
	clif_use_card(sd,RFIFOW(fd,2)-2);
}
/*==========================================
 * �J�[�h�}�������I��
 *------------------------------------------*/
void clif_parse_InsertCard(int fd,struct map_session_data *sd)
{
	if (sd->state.trading != 0)
		return;
	pc_insert_card(sd,RFIFOW(fd,2)-2,RFIFOW(fd,4)-2);
}

/*==========================================
 * 0193 �L����ID���O����
 *------------------------------------------*/
void clif_parse_SolveCharName(int fd, struct map_session_data *sd)
{
	int charid;

	charid = RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]);
	map_reqnickdb(sd, charid);
}

/*==========================================
 * 0197 /resetskill /resetstate
 *------------------------------------------*/
void clif_parse_ResetChar(int fd, struct map_session_data *sd)
{
	if (battle_config.atc_gmonly && !pc_isGM(sd))
		return;
	if (pc_isGM(sd) < get_atcommand_level(AtCommand_ResetState))
		return;

	if (RFIFOW(fd,2))
		pc_resetskill(sd,1);
	else
		pc_resetstate(sd);

	if(log_config.gm && get_atcommand_level(AtCommand_ResetState >= log_config.gm))
		log_atcommand(sd, RFIFOW(fd,2) ? "/resetskill" : "/resetstate");
}

/*==========================================
 * 019c /lb��
 *------------------------------------------*/
void clif_parse_LGMmessage(int fd, struct map_session_data *sd)
{
	unsigned char buf[CHAT_SIZE+4];
	char *mes;
	int len, lv;

	if (battle_config.atc_gmonly && !pc_isGM(sd))
		return;
	if (pc_isGM(sd) < (lv=get_atcommand_level(AtCommand_LocalBroadcast)))
		return;

	len = RFIFOW(fd,2) - 4;
	mes = (char*)RFIFOP(fd,4);
	mes_len_check(mes, len, CHAT_SIZE);

	WBUFW(buf,0) = 0x9a;
	WBUFW(buf,2) = len+4;
	memcpy(WBUFP(buf,4), mes, len);
	clif_send(buf, WBUFW(buf,2), &sd->bl, ALL_SAMEMAP);
	if(log_config.gm && lv >= log_config.gm) {
		char message[CHAT_SIZE+5];
		sprintf(message, "/lb %s", mes);
		log_atcommand(sd, message);
	}
}

/*==========================================
 * �J�v���q�ɂ֓����
 *------------------------------------------*/
void clif_parse_MoveToKafra(int fd, struct map_session_data *sd)
{
	int item_index, item_amount;

	if (pc_istrading(sd))
		return;
	
	item_index = RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0])-2;
	item_amount = RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[1]);
	if (item_index < 0 || item_index >= MAX_INVENTORY || item_amount < 1)
		return;

	if (sd->state.storage_flag == 1)
		storage_storageadd(sd, item_index, item_amount);
	else if (sd->state.storage_flag == 2)
		storage_guild_storageadd(sd, item_index, item_amount);
}

/*==========================================
 * �J�v���q�ɂ���o��
 *------------------------------------------*/
void clif_parse_MoveFromKafra(int fd,struct map_session_data *sd)
{
	int item_index, item_amount;

	item_index = RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0])-1;
	item_amount = RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[1]);

	if (sd->state.storage_flag == 1)
		storage_storageget(sd, item_index, item_amount);
	else if(sd->state.storage_flag == 2)
		storage_guild_storageget(sd, item_index, item_amount);
}

/*==========================================
 * �J�v���q�ɂփJ�[�g��������
 *------------------------------------------*/
void clif_parse_MoveToKafraFromCart(int fd, struct map_session_data *sd)
{
	if(sd->vender_id)	
		return;
	if (!pc_iscarton(sd))
		return;
	if (sd->state.storage_flag == 1)
		storage_storageaddfromcart(sd, RFIFOW(fd,2) - 2, RFIFOL(fd,4));
	else	if (sd->state.storage_flag == 2)
		storage_guild_storageaddfromcart(sd, RFIFOW(fd,2) - 2, RFIFOL(fd,4));
}

/*==========================================
 * �J�v���q�ɂ���o��
 *------------------------------------------*/
void clif_parse_MoveFromKafraToCart(int fd, struct map_session_data *sd)
{
	if (sd->vender_id)
		return;
	if (!pc_iscarton(sd))
		return;
	if (sd->state.storage_flag == 1)
		storage_storagegettocart(sd, RFIFOW(fd,2)-1, RFIFOL(fd,4));
	else if (sd->state.storage_flag == 2)
		storage_guild_storagegettocart(sd, RFIFOW(fd,2)-1, RFIFOL(fd,4));
}

/*==========================================
 * �J�v���q�ɂ����
 *------------------------------------------*/
void clif_parse_CloseKafra(int fd, struct map_session_data *sd)
{
	if (sd->state.storage_flag == 1)
		storage_storageclose(sd);
	else if (sd->state.storage_flag == 2)
		storage_guild_storageclose(sd);
}

/*==========================================
 * Kafra storage protection password system
 *------------------------------------------*/
void clif_parse_StoragePassword(int fd, struct map_session_data *sd)
{
	//TODO
}


/*==========================================
 * �p�[�e�B�����
 *------------------------------------------*/
void clif_parse_CreateParty(int fd, struct map_session_data *sd)
{
	if(map[sd->bl.m].flag.partylock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(227));
		return;
	}
	if (battle_config.basic_skill_check == 0 || pc_checkskill(sd,NV_BASIC) >= 7) {
		party_create(sd,(char*)RFIFOP(fd,2),0,0);
	} else
		clif_skill_fail(sd,1,0,4);
}

/*==========================================
 * �p�[�e�B�����
 *------------------------------------------*/
void clif_parse_CreateParty2(int fd, struct map_session_data *sd)
{
	if(map[sd->bl.m].flag.partylock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(227));
		return;
	}
	if (battle_config.basic_skill_check == 0 || pc_checkskill(sd,NV_BASIC) >= 7)
		party_create(sd,(char*)RFIFOP(fd,2),RFIFOB(fd,26),RFIFOB(fd,27));
	else
		clif_skill_fail(sd,1,0,4);
}

/*==========================================
 * �p�[�e�B�Ɋ��U
 *------------------------------------------*/
void clif_parse_PartyInvite(int fd, struct map_session_data *sd)
{
	struct map_session_data *t_sd;
	
	if(map[sd->bl.m].flag.partylock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(227));
		return;
	}

	t_sd = map_id2sd(RFIFOL(fd,2));

	// @noask [LuzZza]
	if(t_sd && t_sd->state.noask) {
		clif_noask_sub(sd, t_sd, 1);
		return;
	}
	
	party_invite(sd, t_sd);
}

void clif_parse_PartyInvite2(int fd, struct map_session_data *sd)
{
	struct map_session_data *t_sd;
	char *name = RFIFOP(fd,2);
	name[NAME_LENGTH]='\0';

	if(map[sd->bl.m].flag.partylock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(227));
		return;
	}

	t_sd = map_nick2sd(name);

	// @noask [LuzZza]
	if(t_sd && t_sd->state.noask) {
		clif_noask_sub(sd, t_sd, 1);
		return;
	}
	
	party_invite(sd, t_sd);
}
/*==========================================
 * �p�[�e�B���U�ԓ�
 *------------------------------------------*/
void clif_parse_ReplyPartyInvite(int fd,struct map_session_data *sd)
{
	if(battle_config.basic_skill_check == 0 || pc_checkskill(sd,NV_BASIC) >= 5){
		party_reply_invite(sd,RFIFOL(fd,2),RFIFOL(fd,6));
	} else {
		party_reply_invite(sd,RFIFOL(fd,2),-1);
		clif_skill_fail(sd,1,0,4);
	}
}

void clif_parse_ReplyPartyInvite2(int fd,struct map_session_data *sd)
{
	if(battle_config.basic_skill_check == 0 || pc_checkskill(sd,NV_BASIC) >= 5){
		party_reply_invite(sd,RFIFOL(fd,2),RFIFOB(fd,6));
	} else {
		party_reply_invite(sd,RFIFOL(fd,2),-1);
		clif_skill_fail(sd,1,0,4);
	}
}

/*==========================================
 * �p�[�e�B�E�ޗv��
 *------------------------------------------*/
void clif_parse_LeaveParty(int fd, struct map_session_data *sd)
{
	if(map[sd->bl.m].flag.partylock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(227));
		return;
	}
	party_leave(sd);
}

/*==========================================
 * �p�[�e�B�����v��
 *------------------------------------------*/
void clif_parse_RemovePartyMember(int fd, struct map_session_data *sd)
{
	if(map[sd->bl.m].flag.partylock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(227));
		return;
	}
	party_removemember(sd,RFIFOL(fd,2),(char*)RFIFOP(fd,6));
}

/*==========================================
 * �p�[�e�B�ݒ�ύX�v��
 *------------------------------------------*/
void clif_parse_PartyChangeOption(int fd, struct map_session_data *sd)
{
	struct party_data *p;

	if(!sd->status.party_id)
		return;

	p = party_search(sd->status.party_id);
	if (!p) return;
	//The client no longer can change the item-field, therefore it always
	//comes as zero. Here, resend the item data as it is.
// party_changeoption(sd, RFIFOW(fd,2), RFIFOW(fd,4));
	party_changeoption(sd, RFIFOW(fd,2), p->party.item);
}

/*==========================================
 * �p�[�e�B���b�Z�[�W���M�v��
 *------------------------------------------*/
void clif_parse_PartyMessage(int fd, struct map_session_data* sd)
{
	char* message;
	int len;

	len = RFIFOW(fd,2) - 4;
	message = (char*)RFIFOP(fd,4);
	mes_len_check(message, len, CHAT_SIZE);

	if (is_charcommand(fd, sd, message) != CharCommand_None || is_atcommand(fd, sd, message) != AtCommand_None)
		return;

	if (/* sd->sc.data[SC_BERSERK].timer!=-1 || */(sd->sc.data[SC_NOCHAT].timer!=-1 && sd->sc.data[SC_NOCHAT].val1&MANNER_NOCHAT))
		return;

	if (battle_config.min_chat_delay)
	{	//[Skotlex]
		if (DIFF_TICK(sd->cantalk_tick, gettick()) > 0)
			return;
		sd->cantalk_tick = gettick() + battle_config.min_chat_delay;
	}

	party_send_message(sd, message, len);
}

/*==========================================
 * �I�X��
 *------------------------------------------*/
void clif_parse_CloseVending(int fd, struct map_session_data* sd)
{
	vending_closevending(sd);
}

/*==========================================
 * �I�X�A�C�e�����X�g�v��
 *------------------------------------------*/
void clif_parse_VendingListReq(int fd, struct map_session_data* sd)
{
	vending_vendinglistreq(sd,RFIFOL(fd,2));

	if( sd->npc_id )
		npc_event_dequeue(sd);
}

/*==========================================
 * Shop item(s) purchase request
 * S 0134 <len>.w <ID>.l {<amount>.w <index>.w}.4B*
 *------------------------------------------*/
void clif_parse_PurchaseReq(int fd, struct map_session_data* sd)
{
	int len = (int)RFIFOW(fd,2) - 8;
	int id = (int)RFIFOL(fd,4);
	const uint8* data = (uint8*)RFIFOP(fd,8);

	vending_purchasereq(sd, id, data, len/4);
}

/*==========================================
 * Confirm or cancel the shop preparation window
 * S 01b2 <len>.w <message>.80B <flag>.B {<index>.w <amount>.w <value>.l}.8B*
 * flag: 0=cancel, 1=confirm
 *------------------------------------------*/
void clif_parse_OpenVending(int fd, struct map_session_data* sd)
{
	short len = (short)RFIFOW(fd,2) - 85;
	const char* message = (char*)RFIFOP(fd,4);
	bool flag = (bool)RFIFOB(fd,84);
	const uint8* data = (uint8*)RFIFOP(fd,85);

	if (sd->sc.data[SC_NOCHAT].timer!=-1 && sd->sc.data[SC_NOCHAT].val1&MANNER_NOROOM)
		return;
	if (map[sd->bl.m].flag.novending) {
		clif_displaymessage (sd->fd, msg_txt(276)); // "You can't open shop on this map"
		return;
	}
	if( message[0] == '\0' ) // invalid input
		return;

	vending_openvending(sd, message, flag, data, len/8);
}

/*==========================================
 * �M���h�����
 *------------------------------------------*/
void clif_parse_CreateGuild(int fd,struct map_session_data *sd)
{
	if(map[sd->bl.m].flag.guildlock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(228));
		return;
	}
	guild_create(sd, (char*)RFIFOP(fd,6));
}

/*==========================================
 * �M���h�}�X�^�[���ǂ����m�F
 *------------------------------------------*/
void clif_parse_GuildCheckMaster(int fd, struct map_session_data *sd)
{
	clif_guild_masterormember(sd);
}

/*==========================================
 * �M���h���v��
 *------------------------------------------*/
void clif_parse_GuildRequestInfo(int fd, struct map_session_data *sd)
{
	if (!sd->status.guild_id)
		return;
	switch(RFIFOL(fd,2)){
	case 0:	// �M���h��{���A�����G�Ώ��
		clif_guild_basicinfo(sd);
		clif_guild_allianceinfo(sd);
		break;
	case 1:	// �����o�[���X�g�A��E�����X�g
		clif_guild_positionnamelist(sd);
		clif_guild_memberlist(sd);
		break;
	case 2:	// ��E�����X�g�A��E��񃊃X�g
		clif_guild_positionnamelist(sd);
		clif_guild_positioninfolist(sd);
		break;
	case 3:	// �X�L�����X�g
		clif_guild_skillinfo(sd);
		break;
	case 4:	// �Ǖ����X�g
		clif_guild_expulsionlist(sd);
		break;
	default:
		if (battle_config.error_log)
			ShowError("clif: guild request info: unknown type %d\n", RFIFOL(fd,2));
		break;
	}
}

/*==========================================
 * �M���h��E�ύX
 *------------------------------------------*/
void clif_parse_GuildChangePositionInfo(int fd, struct map_session_data *sd)
{
	int i;

	if(!sd->state.gmaster_flag)
		return;

	for(i = 4; i < RFIFOW(fd,2); i += 40 ){
		guild_change_position(sd->status.guild_id, RFIFOL(fd,i), RFIFOL(fd,i+4), RFIFOL(fd,i+12), (char*)RFIFOP(fd,i+16));
	}
}

/*==========================================
 * �M���h�����o��E�ύX
 *------------------------------------------*/
void clif_parse_GuildChangeMemberPosition(int fd, struct map_session_data *sd)
{
	int i;
	
	if(!sd->state.gmaster_flag)
		return;

	for(i=4;i<RFIFOW(fd,2);i+=12){
		guild_change_memberposition(sd->status.guild_id,
			RFIFOL(fd,i),RFIFOL(fd,i+4),RFIFOL(fd,i+8));
	}
}

/*==========================================
 * �M���h�G���u�����v��
 *------------------------------------------*/
void clif_parse_GuildRequestEmblem(int fd,struct map_session_data *sd)
{
	struct guild* g = guild_search(RFIFOL(fd,2));
	if(g!=NULL)
		clif_guild_emblem(sd,g);
}

/*==========================================
 * �M���h�G���u�����ύX
 *------------------------------------------*/
void clif_parse_GuildChangeEmblem(int fd,struct map_session_data *sd)
{
	if(!sd->state.gmaster_flag)
		return;

	guild_change_emblem(sd,RFIFOW(fd,2)-4,(char*)RFIFOP(fd,4));
}

/*==========================================
 * Guild notice update request
 * S 016E <guildID>.l <msg1>.60B <msg2>.120B
 *------------------------------------------*/
void clif_parse_GuildChangeNotice(int fd, struct map_session_data* sd)
{
	int guild_id = RFIFOL(fd,2);
	char* msg1 = (char*)RFIFOP(fd,6);
	char* msg2 = (char*)RFIFOP(fd,66);

	if(!sd->state.gmaster_flag)
		return;

	// compensate for some client defects when using multilanguage mode
	if (msg1[0] == '|' && msg1[3] == '|') msg1+= 3; // skip duplicate marker
	if (msg2[0] == '|' && msg2[3] == '|') msg2+= 3; // skip duplicate marker
	if (msg2[0] == '|') msg2[strnlen(msg2, 120)-1] = '\0'; // delete extra space at the end of string

	guild_change_notice(sd, guild_id, msg1, msg2);
}

/*==========================================
 * �M���h���U
 *------------------------------------------*/
void clif_parse_GuildInvite(int fd,struct map_session_data *sd)
{
	struct map_session_data *t_sd;
	
	if(map[sd->bl.m].flag.guildlock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(228));
		return;
	}

	t_sd = map_id2sd(RFIFOL(fd,2));

	// @noask [LuzZza]
	if(t_sd && t_sd->state.noask) {
		clif_noask_sub(sd, t_sd, 2);
		return;
	}

	guild_invite(sd,t_sd);
}

/*==========================================
 * �M���h���U�ԐM
 *------------------------------------------*/
void clif_parse_GuildReplyInvite(int fd,struct map_session_data *sd)
{
	guild_reply_invite(sd,RFIFOL(fd,2),RFIFOB(fd,6));
}

/*==========================================
 * �M���h�E��
 *------------------------------------------*/
void clif_parse_GuildLeave(int fd,struct map_session_data *sd)
{
	if(map[sd->bl.m].flag.guildlock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(228));
		return;
	}
	guild_leave(sd,RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),(char*)RFIFOP(fd,14));
}

/*==========================================
 * �M���h�Ǖ�
 *------------------------------------------*/
void clif_parse_GuildExpulsion(int fd,struct map_session_data *sd)
{
	if(map[sd->bl.m].flag.guildlock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(228));
		return;
	}
	guild_expulsion(sd,RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),(char*)RFIFOP(fd,14));
}

/*==========================================
 * �M���h��b
 *------------------------------------------*/
void clif_parse_GuildMessage(int fd, struct map_session_data* sd)
{
	char* message;
	int len;

	len = RFIFOW(fd,2) - 4;
	message = (char*)RFIFOP(fd,4);
	mes_len_check(message, len, CHAT_SIZE);

	if (is_charcommand(fd, sd, message) != CharCommand_None || is_atcommand(fd, sd, message) != AtCommand_None)
		return;

	if (/* sd->sc.data[SC_BERSERK].timer!=-1 || */(sd->sc.data[SC_NOCHAT].timer!=-1 && sd->sc.data[SC_NOCHAT].val1&MANNER_NOCHAT))
		return;

	if (battle_config.min_chat_delay)
	{	//[Skotlex]
		if (DIFF_TICK(sd->cantalk_tick, gettick()) > 0)
			return;
		sd->cantalk_tick = gettick() + battle_config.min_chat_delay;
	}

	guild_send_message(sd, message, len);
}

/*==========================================
 * �M���h�����v��
 *------------------------------------------*/
void clif_parse_GuildRequestAlliance(int fd, struct map_session_data *sd)
{
	struct map_session_data *t_sd;
	
	if(!sd->state.gmaster_flag)
		return;

	if(map[sd->bl.m].flag.guildlock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(228));
		return;
	}

	t_sd = map_id2sd(RFIFOL(fd,2));

	// @noask [LuzZza]
	if(t_sd && t_sd->state.noask) {
		clif_noask_sub(sd, t_sd, 3);
		return;
	}
	
	guild_reqalliance(sd,t_sd);
}

/*==========================================
 * �M���h�����v���ԐM
 *------------------------------------------*/
void clif_parse_GuildReplyAlliance(int fd, struct map_session_data *sd)
{
	guild_reply_reqalliance(sd,RFIFOL(fd,2),RFIFOL(fd,6));
}

/*==========================================
 * �M���h�֌W����
 *------------------------------------------*/
void clif_parse_GuildDelAlliance(int fd, struct map_session_data *sd)
{
	if(!sd->state.gmaster_flag)
		return;

	if(map[sd->bl.m].flag.guildlock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(228));
		return;
	}
	guild_delalliance(sd,RFIFOL(fd,2),RFIFOL(fd,6));
}

/*==========================================
 * �M���h�G��
 *------------------------------------------*/
void clif_parse_GuildOpposition(int fd, struct map_session_data *sd)
{
	struct map_session_data *t_sd;

	if(!sd->state.gmaster_flag)
		return;

	if(map[sd->bl.m].flag.guildlock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(228));
		return;
	}

	t_sd = map_id2sd(RFIFOL(fd,2));

	// @noask [LuzZza]
	if(t_sd && t_sd->state.noask) {
		clif_noask_sub(sd, t_sd, 4);
		return;
	}
	
	guild_opposition(sd,t_sd);
}

/*==========================================
 * �M���h���U
 *------------------------------------------*/
void clif_parse_GuildBreak(int fd, struct map_session_data *sd)
{
	if(map[sd->bl.m].flag.guildlock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(228));
		return;
	}
	guild_break(sd,(char*)RFIFOP(fd,2));
}

// pet
void clif_parse_PetMenu(int fd, struct map_session_data *sd)
{
	pet_menu(sd,RFIFOB(fd,2));
}

void clif_parse_CatchPet(int fd, struct map_session_data *sd)
{
	pet_catch_process2(sd,RFIFOL(fd,2));
}

void clif_parse_SelectEgg(int fd, struct map_session_data *sd)
{
	if (sd->menuskill_id != SA_TAMINGMONSTER || sd->menuskill_val != -1)
		return;
	pet_select_egg(sd,RFIFOW(fd,2)-2);
	sd->menuskill_val = sd->menuskill_id = 0;
}

void clif_parse_SendEmotion(int fd, struct map_session_data *sd)
{
	if(sd->pd)
		clif_pet_emotion(sd->pd,RFIFOL(fd,2));
}

void clif_parse_ChangePetName(int fd, struct map_session_data *sd)
{
	pet_change_name(sd,(char*)RFIFOP(fd,2));
}

// Kick (right click menu for GM "(name) force to quit")
void clif_parse_GMKick(int fd, struct map_session_data *sd)
{
	struct block_list *target;
	int tid,lv;

	if (battle_config.atc_gmonly && !pc_isGM(sd))
		return;

	if (pc_isGM(sd) < (lv=get_atcommand_level(AtCommand_Kick)))
		return;

	tid = RFIFOL(fd,2);
	target = map_id2bl(tid);
	if (!target) {
		clif_GM_kickack(sd, 0);
		return;
	}

	switch (target->type) {
	case BL_PC:
	{
		struct map_session_data *tsd = (struct map_session_data *)target;
		if (pc_isGM(sd) <= pc_isGM(tsd)) {
			clif_GM_kickack(sd, 0);
			return;
		}
		clif_GM_kick(sd, tsd, 1);
		if(log_config.gm && lv >= log_config.gm) {
			char message[NAME_LENGTH+6];
			sprintf(message, "/kick %d", tsd->status.char_id);
			log_atcommand(sd, message);
		}
		break;
	}
	case BL_MOB:
		status_percent_damage(&sd->bl, target, 100, 0);
		if(log_config.gm && lv >= log_config.gm) {
			char message[NAME_LENGTH+16];
			sprintf(message, "/kick %s (%d)", status_get_name(target), status_get_class(target));
			log_atcommand(sd, message);
		}
		break;
	default:
		clif_GM_kickack(sd, 0);
	}
}

/*==========================================
 * /shift
 *------------------------------------------*/
void clif_parse_Shift(int fd, struct map_session_data *sd)
{	
	char *player_name;
	int lv;

	if (battle_config.atc_gmonly && !pc_isGM(sd))
		return;
	if (pc_isGM(sd) < (lv=get_atcommand_level(AtCommand_JumpTo)))
		return;

	player_name = (char*)RFIFOP(fd,2);
	player_name[NAME_LENGTH-1] = '\0';
	atcommand_jumpto(fd, sd, "@jumpto", player_name); // as @jumpto
	if(log_config.gm && lv >= log_config.gm) {
		char message[NAME_LENGTH+7];
		sprintf(message, "/shift %s", player_name);
		log_atcommand(sd, message);
	}
	return;
}

/*==========================================
 * /recall
 *------------------------------------------*/
void clif_parse_Recall(int fd, struct map_session_data *sd)
{
	char *player_name;
	int lv;

	if (battle_config.atc_gmonly && !pc_isGM(sd))
		return;

	if (pc_isGM(sd) < (lv=get_atcommand_level(AtCommand_Recall)))
		return;

	player_name = (char*)RFIFOP(fd,2);
	player_name[NAME_LENGTH-1] = '\0';
	atcommand_recall(fd, sd, "@recall", player_name); // as @recall
	if(log_config.gm && lv >= log_config.gm) {
		char message[NAME_LENGTH+8];
		sprintf(message, "/recall %s", player_name);
		log_atcommand(sd, message);
	}
	return;
}

/*==========================================
 * /monster /item 
 *------------------------------------------*/
void clif_parse_GM_Monster_Item(int fd, struct map_session_data *sd)
{
	char *monster_item_name;
	char message[NAME_LENGTH+10]; //For logging.
	int level;

	if (battle_config.atc_gmonly && !pc_isGM(sd))
		return;

	monster_item_name = (char*)RFIFOP(fd,2);
	monster_item_name[NAME_LENGTH-1] = '\0';

	if (mobdb_searchname(monster_item_name)) {
		if (pc_isGM(sd) < (level=get_atcommand_level(AtCommand_Spawn)))
			return;
		atcommand_monster(fd, sd, "@spawn", monster_item_name); // as @spawn
		if(log_config.gm && level >= log_config.gm)
		{	//Log action. [Skotlex]
			snprintf(message, sizeof(message)-1, "@spawn %s", monster_item_name);
			log_atcommand(sd, message);
		}
		return;
	}
	if (itemdb_searchname(monster_item_name) == NULL)
		return;
	if (pc_isGM(sd) < (level = get_atcommand_level(AtCommand_Item)))
		return;
	atcommand_item(fd, sd, "@item", monster_item_name); // as @item
	if(log_config.gm && level >= log_config.gm)
	{	//Log action. [Skotlex]
		sprintf(message, "@item %s", monster_item_name);
		log_atcommand(sd, message);
	}
}

/*==========================================
 * /hide
 *------------------------------------------*/
void clif_parse_GMHide(int fd, struct map_session_data *sd)
{
	if (battle_config.atc_gmonly && !pc_isGM(sd))
		return;
	if (pc_isGM(sd) < get_atcommand_level(AtCommand_Hide))
		return;

	if (sd->sc.option & OPTION_INVISIBLE) {
		sd->sc.option &= ~OPTION_INVISIBLE;
		if (sd->disguise)
			status_set_viewdata(&sd->bl, sd->disguise);
		else
			status_set_viewdata(&sd->bl, sd->status.class_);
		clif_displaymessage(fd, "Invisible: Off.");
	} else {
		sd->sc.option |= OPTION_INVISIBLE;
		sd->vd.class_ = INVISIBLE_CLASS;
		clif_displaymessage(fd, "Invisible: On.");
		if(log_config.gm && get_atcommand_level(AtCommand_Hide) >= log_config.gm)
			log_atcommand(sd, "/hide");
	}
	clif_changeoption(&sd->bl);
}

/*==========================================
 * GM�ɂ��`���b�g�֎~���ԕt�^
 *------------------------------------------*/
void clif_parse_GMReqNoChat(int fd,struct map_session_data *sd)
{
	int type, limit, level;
	struct block_list *bl;
	struct map_session_data *dstsd;

	bl = map_id2bl(RFIFOL(fd,2));
	if (!bl || bl->type != BL_PC)
		return;
	dstsd =(struct map_session_data *)bl;

	type = RFIFOB(fd,6);
	limit = RFIFOW(fd,7);
	if (type == 0)
		limit = 0 - limit;

	//If type is 2 and the ids don't match, this is a crafted hacked packet!
	//Disabled because clients keep self-muting when you give players public @ commands... [Skotlex]
	if (type == 2/* && sd->bl.id != dstsd->bl.id*/)
		return;
	
	if (
		((level = pc_isGM(sd)) > pc_isGM(dstsd) && level >= get_atcommand_level(AtCommand_Mute))
		|| (type == 2 && !level))
	{
		clif_GM_silence(sd, dstsd, ((type == 2) ? 1 : type));
		dstsd->status.manner -= limit;
		if(dstsd->status.manner < 0)
			sc_start(bl,SC_NOCHAT,100,0,0);
		else
		{
			dstsd->status.manner = 0;
			status_change_end(bl,SC_NOCHAT,-1);
		}
	}

	return;
}
/*==========================================
 * GM�ɂ��`���b�g�֎~���ԎQ�Ɓi�H�j
 *------------------------------------------*/
void clif_parse_GMReqNoChatCount(int fd, struct map_session_data *sd)
{
	int tid;
	tid = RFIFOL(fd,2);

	WFIFOHEAD(fd,packet_len(0x1e0));
	WFIFOW(fd,0) = 0x1e0;
	WFIFOL(fd,2) = tid;
	sprintf((char*)WFIFOP(fd,6),"%d",tid);
	WFIFOSET(fd, packet_len(0x1e0));

	return;
}

static int pstrcmp(const void *a, const void *b)
{
	char *name1 = (char *)a;
	char *name2 = (char *)b;
	if (name1[0] && name2[0])
		return strcmp(name1, name2);
	//Since names are sorted in ascending order, send blank entries to the bottom.
	if (name1[0])
		return -1;
	if (name2[0])
		return 1;
	return 0;
}

void clif_parse_PMIgnore(int fd, struct map_session_data *sd)
{
	char output[512];
	char *nick; // S 00cf <nick>.24B <type>.B: 00 (/ex nick) deny speech from nick, 01 (/in nick) allow speech from nick
	int i;

	memset(output, '\0', sizeof(output));

	nick = (char*)RFIFOP(fd,2); // speed up
	nick[NAME_LENGTH-1] = '\0'; // to be sure that the player name have at maximum 23 characters

	WFIFOHEAD(fd,packet_len(0xd1));
	WFIFOW(fd,0) = 0x0d1; // R 00d1 <type>.B <result>.B: type: 0: deny, 1: allow, result: 0: success, 1: fail 2: list full
	WFIFOB(fd,2) = RFIFOB(fd,26);
	
	if (RFIFOB(fd,26) == 0)
	{	// Add name to ignore list (block)

		// Bot-check...
		if (strcmp(wisp_server_name, nick) == 0)
		{	// to find possible bot users who automaticaly ignore people
			sprintf(output, "Character '%s' (account: %d) has tried to block wisps from '%s' (wisp name of the server). Bot user?", sd->status.name, sd->status.account_id, wisp_server_name);
			intif_wis_message_to_gm(wisp_server_name, battle_config.hack_info_GM_level, output);
			WFIFOB(fd,3) = 1; // fail
			WFIFOSET(fd, packet_len(0x0d1));
			return;
		}

		// try to find a free spot, while checking for duplicates at the same time
		for(i = 0; i < MAX_IGNORE_LIST && sd->ignore[i].name[0] != '\0' && strcmp(sd->ignore[i].name, nick) != 0; i++);

		if (i == MAX_IGNORE_LIST) { // no space for new entry
			WFIFOB(fd,3) = 2; // fail
			WFIFOSET(fd, packet_len(0x0d1));
			return;
		}
		if(sd->ignore[i].name[0] != '\0') { // name already exists
			WFIFOB(fd,3) = 0; // Aegis reports success.
			WFIFOSET(fd, packet_len(0x0d1));
			return;
		}
		//Insert in position i
		memcpy(sd->ignore[i].name, nick, NAME_LENGTH);
		WFIFOB(fd,3) = 0; // success
		WFIFOSET(fd, packet_len(0x0d1));

		//Sort the ignore list.
		//FIXME: why not just use a simple shift-and-insert scheme instead? [ultramage]
		qsort (sd->ignore[0].name, MAX_IGNORE_LIST, sizeof(sd->ignore[0].name), pstrcmp);
	}
	else
	{	// Remove name from ignore list (unblock)
		
		for(i = 0; i < MAX_IGNORE_LIST && sd->ignore[i].name[0] != '\0' && strcmp(sd->ignore[i].name, nick) != 0; i++);

		if (i == MAX_IGNORE_LIST || sd->ignore[i].name[i] == '\0') { //Not found
			WFIFOB(fd,3) = 1; // fail
			WFIFOSET(fd, packet_len(0x0d1));
			return;
		}
		//Move everything one place down to overwrite removed entry.
		memmove(sd->ignore[i].name, sd->ignore[i+1].name, (MAX_IGNORE_LIST-i-1)*sizeof(sd->ignore[0].name));
		memset(sd->ignore[MAX_IGNORE_LIST-1].name, 0, sizeof(sd->ignore[0].name));
		WFIFOB(fd,3) = 0; // success
		WFIFOSET(fd, packet_len(0x0d1));
	}

	//for(i = 0; i < MAX_IGNORE_LIST && sd->ignore[i].name[0] != '\0'; i++)
	//	ShowDebug("Ignored player: '%s'\n", sd->ignore[i].name);
	return;
}

void clif_parse_PMIgnoreAll(int fd, struct map_session_data *sd)
{
	//printf("Ignore all: state: %d\n", RFIFOB(fd,2));
	// R 00d2 <type>.B <fail>.B: type: 0: deny, 1: allow, fail: 0: success, 1: fail
	// S 00d0 <type>len.B: 00 (/exall) deny all speech, 01 (/inall) allow all speech
	WFIFOHEAD(fd,packet_len(0xd2));
	WFIFOW(fd,0) = 0x0d2;
	WFIFOB(fd,2) = RFIFOB(fd,2);
	if (RFIFOB(fd,2) == 0) { //Deny all
		if (sd->state.ignoreAll) {
			WFIFOB(fd,3) = 1; // fail
			WFIFOSET(fd, packet_len(0x0d2));
			return;
		}
		sd->state.ignoreAll = 1;
		WFIFOB(fd,3) = 0; // success
		WFIFOSET(fd, packet_len(0x0d2));
		return;
	}
	//Unblock everyone
	if (!sd->state.ignoreAll) {
		if (sd->ignore[0].name[0] != '\0')
		{  //Wipe the ignore list.
			memset(sd->ignore, 0, sizeof(sd->ignore));
			WFIFOB(fd,3) = 0;
			WFIFOSET(fd, packet_len(0x0d2));
			return;
		}
		WFIFOB(fd,3) = 1; // fail
		WFIFOSET(fd, packet_len(0x0d2));
		return;
	}
	sd->state.ignoreAll = 0;
	WFIFOB(fd,3) = 0; // success
	WFIFOSET(fd, packet_len(0x0d2));
	return;
}

/*==========================================
 * Wis���ۃ��X�g
 *------------------------------------------*/
void clif_parse_PMIgnoreList(int fd,struct map_session_data *sd)
{
	int i;

	WFIFOHEAD(fd, 4 + (NAME_LENGTH * MAX_IGNORE_LIST));
	WFIFOW(fd,0) = 0xd4;

	for(i = 0; i < MAX_IGNORE_LIST && sd->ignore[i].name[0] != '\0'; i++)
		memcpy(WFIFOP(fd, 4 + i * NAME_LENGTH),sd->ignore[i].name, NAME_LENGTH);

	WFIFOW(fd,2) = 4 + i * NAME_LENGTH;
	WFIFOSET(fd, WFIFOW(fd,2));
	return;
}

/*==========================================
 * �X�p�m�r��/doridori�ɂ��SPR2�{
 *------------------------------------------*/
void clif_parse_NoviceDoriDori(int fd, struct map_session_data *sd)
{
	if (sd->state.doridori) return;

	switch (sd->class_&MAPID_UPPERMASK)
	{
		case MAPID_SOUL_LINKER:
		case MAPID_STAR_GLADIATOR:
		case MAPID_TAEKWON:
			if (!sd->state.rest)
				break;
		case MAPID_SUPER_NOVICE:
			sd->state.doridori=1;
			break;	
	}
	return;
}
/*==========================================
 * �X�p�m�r�̔����g��
 *------------------------------------------*/
void clif_parse_NoviceExplosionSpirits(int fd, struct map_session_data *sd)
{
	if(sd){
		int nextbaseexp=pc_nextbaseexp(sd);
		if (battle_config.etc_log){
			if(nextbaseexp != 0)
				ShowInfo("SuperNovice explosionspirits!! %d %d %d %d\n",sd->bl.id,sd->status.class_,sd->status.base_exp,(int)((double)1000*sd->status.base_exp/nextbaseexp));
			else
				ShowInfo("SuperNovice explosionspirits!! %d %d %d 000\n",sd->bl.id,sd->status.class_,sd->status.base_exp);
		}
		if((sd->class_&MAPID_UPPERMASK) == MAPID_SUPER_NOVICE && sd->status.base_exp > 0 && nextbaseexp > 0 && (int)((double)1000*sd->status.base_exp/nextbaseexp)%100==0){
			clif_skill_nodamage(&sd->bl,&sd->bl,MO_EXPLOSIONSPIRITS,5,
				sc_start(&sd->bl,SkillStatusChangeTable(MO_EXPLOSIONSPIRITS),100,
					5,skill_get_time(MO_EXPLOSIONSPIRITS,5)));
		}
	}
	return;
}

// random notes:
// 0x214: monster/player info ?

/*==========================================
 * Friends List
 *------------------------------------------*/
void clif_friendslist_toggle(struct map_session_data *sd,int account_id, int char_id, int online)
{	//Toggles a single friend online/offline [Skotlex]
	int i, fd = sd->fd;

	//Seek friend.
	for (i = 0; i < MAX_FRIENDS && sd->status.friends[i].char_id &&
		(sd->status.friends[i].char_id != char_id || sd->status.friends[i].account_id != account_id); i++);

	if(i == MAX_FRIENDS || sd->status.friends[i].char_id == 0)
		return; //Not found

	WFIFOHEAD(fd,packet_len(0x206));
	WFIFOW(fd, 0) = 0x206;
	WFIFOL(fd, 2) = sd->status.friends[i].account_id;
	WFIFOL(fd, 6) = sd->status.friends[i].char_id;
	WFIFOB(fd,10) = !online; //Yeah, a 1 here means "logged off", go figure... 
	WFIFOSET(fd, packet_len(0x206));
}

//Subfunction called from clif_foreachclient to toggle friends on/off [Skotlex]
int clif_friendslist_toggle_sub(struct map_session_data *sd,va_list ap)
{
	int account_id, char_id, online;
	account_id = va_arg(ap, int);
	char_id = va_arg(ap, int);
	online = va_arg(ap, int);
	clif_friendslist_toggle(sd, account_id, char_id, online);
	return 0;
}

//For sending the whole friends list.
void clif_friendslist_send(struct map_session_data *sd)
{
	int i = 0, n, fd = sd->fd;
	
	// Send friends list
	WFIFOHEAD(fd, MAX_FRIENDS * 32 + 4);
	WFIFOW(fd, 0) = 0x201;
	for(i = 0; i < MAX_FRIENDS && sd->status.friends[i].char_id; i++)
	{
		WFIFOL(fd, 4 + 32 * i + 0) = sd->status.friends[i].account_id;
		WFIFOL(fd, 4 + 32 * i + 4) = sd->status.friends[i].char_id;
		memcpy(WFIFOP(fd, 4 + 32 * i + 8), &sd->status.friends[i].name, NAME_LENGTH);
	}

	if (i) {
		WFIFOW(fd,2) = 4 + 32 * i;
		WFIFOSET(fd, WFIFOW(fd,2));
	}
	
	for (n = 0; n < i; n++)
	{	//Sending the online players
		if (map_charid2sd(sd->status.friends[n].char_id))
			clif_friendslist_toggle(sd, sd->status.friends[n].account_id, sd->status.friends[n].char_id, 1);
	}
}

/// Reply for add friend request: (success => type 0)
/// type=0 : MsgStringTable[821]="You have become friends with (%s)."
/// type=1 : MsgStringTable[822]="(%s) does not want to be friends with you."
/// type=2 : MsgStringTable[819]="Your Friend List is full."
/// type=3 : MsgStringTable[820]="(%s)'s Friend List is full."
void clif_friendslist_reqack(struct map_session_data *sd, struct map_session_data *f_sd, int type)
{
	int fd;
	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0x209));
	WFIFOW(fd,0) = 0x209;
	WFIFOW(fd,2) = type;
	if (f_sd)
	{
		WFIFOW(fd,4) = f_sd->status.account_id;
		WFIFOW(fd,8) = f_sd->status.char_id;
		memcpy(WFIFOP(fd, 12), f_sd->status.name,NAME_LENGTH);
	}
	WFIFOSET(fd, packet_len(0x209));
}

void clif_parse_FriendsListAdd(int fd, struct map_session_data *sd)
{
	struct map_session_data *f_sd;
	int i, f_fd;

	f_sd = map_nick2sd((char*)RFIFOP(fd,2));

	// Friend doesn't exist (no player with this name)
	if (f_sd == NULL) {
		clif_displaymessage(fd, msg_txt(3));
		return;
	}

	// @noask [LuzZza]
	if(f_sd->state.noask) {
		clif_noask_sub(sd, f_sd, 5);
		return;
	}

	// Friend already exists
	for (i = 0; i < MAX_FRIENDS && sd->status.friends[i].char_id != 0; i++) {
		if (sd->status.friends[i].char_id == f_sd->status.char_id) {
			clif_displaymessage(fd, "Friend already exists.");
			return;
		}
	}

	if (i == MAX_FRIENDS) {
		//No space, list full.
		clif_friendslist_reqack(sd, f_sd, 2);
		return;
	}
		
	f_fd = f_sd->fd;
	WFIFOHEAD(f_fd,packet_len(0x207));
	WFIFOW(f_fd,0) = 0x207;
	WFIFOL(f_fd,2) = sd->status.account_id;
	WFIFOL(f_fd,6) = sd->status.char_id;
	memcpy(WFIFOP(f_fd,10), sd->status.name, NAME_LENGTH);
	WFIFOSET(f_fd, packet_len(0x207));

	return;
}

void clif_parse_FriendsListReply(int fd, struct map_session_data *sd)
{
	//<W: id> <L: Player 1 chara ID> <L: Player 1 AID> <B: Response>
	struct map_session_data *f_sd;
	int char_id, account_id;
	char reply;

	account_id = RFIFOL(fd,2);
	char_id = RFIFOL(fd,6);
	reply = RFIFOB(fd,10);
	//printf ("reply: %d %d %d\n", char_id, id, reply);

	f_sd = map_id2sd(account_id); //The account id is the same as the bl.id of players.
	if (f_sd == NULL)
		return;
		
	if (reply == 0)
		clif_friendslist_reqack(f_sd, sd, 1);
	else {
		int i;
		// Find an empty slot
		for (i = 0; i < MAX_FRIENDS; i++)
			if (f_sd->status.friends[i].char_id == 0)
				break;
		if (i == MAX_FRIENDS) {
			clif_friendslist_reqack(f_sd, sd, 2);
			return;
		}

		f_sd->status.friends[i].account_id = sd->status.account_id;
		f_sd->status.friends[i].char_id = sd->status.char_id;
		memcpy(f_sd->status.friends[i].name, sd->status.name, NAME_LENGTH);
		clif_friendslist_reqack(f_sd, sd, 0);

		if (battle_config.friend_auto_add) {
			// Also add f_sd to sd's friendlist.
			for (i = 0; i < MAX_FRIENDS; i++) {
				if (sd->status.friends[i].char_id == f_sd->status.char_id)
					return; //No need to add anything.
				if (sd->status.friends[i].char_id == 0)
					break;
			}
			if (i == MAX_FRIENDS) {
				clif_friendslist_reqack(sd, f_sd, 2);
				return;
			}

			sd->status.friends[i].account_id = f_sd->status.account_id;
			sd->status.friends[i].char_id = f_sd->status.char_id;
			memcpy(sd->status.friends[i].name, f_sd->status.name, NAME_LENGTH);
			clif_friendslist_reqack(sd, f_sd, 0);
		}
	}

	return;
}

void clif_parse_FriendsListRemove(int fd, struct map_session_data *sd)
{
	// 0x203 </o> <ID to be removed W 4B>
	int account_id, char_id;
	int i, j;

	account_id = RFIFOL(fd,2);
	char_id = RFIFOL(fd,6);

	// Search friend
	for (i = 0; i < MAX_FRIENDS &&
		(sd->status.friends[i].char_id != char_id || sd->status.friends[i].account_id != account_id); i++);

	if (i == MAX_FRIENDS) {
		clif_displaymessage(fd, "Name not found in list.");
		return;
	}
		
	// move all chars down
	for(j = i + 1; j < MAX_FRIENDS; j++)
		memcpy(&sd->status.friends[j-1], &sd->status.friends[j], sizeof(sd->status.friends[0]));

	memset(&sd->status.friends[MAX_FRIENDS-1], 0, sizeof(sd->status.friends[MAX_FRIENDS-1]));
	clif_displaymessage(fd, "Friend removed");
	
	WFIFOHEAD(fd,packet_len(0x20a));
	WFIFOW(fd,0) = 0x20a;
	WFIFOL(fd,2) = account_id;
	WFIFOL(fd,6) = char_id;
	WFIFOSET(fd, packet_len(0x20a));
//	clif_friendslist_send(sd); //This is not needed anymore.

	return;
}

/*==========================================
 * /killall
 *------------------------------------------*/
void clif_parse_GMKillAll(int fd, struct map_session_data* sd)
{
	char message[50];

	strncpy(message,sd->status.name, NAME_LENGTH);
	is_atcommand(fd, sd, strcat(message," : @kickall"));

	return;
}

/*==========================================
 * /pvpinfo
 *------------------------------------------*/
void clif_parse_PVPInfo(int fd,struct map_session_data *sd)
{
	WFIFOHEAD(fd,packet_len(0x210));
	WFIFOW(fd,0) = 0x210;
	//WFIFOL(fd,2) = 0;	// not sure what for yet
	//WFIFOL(fd,6) = 0;
	WFIFOL(fd,10) = sd->pvp_won;	// times won
	WFIFOL(fd,14) = sd->pvp_lost;	// times lost
	WFIFOL(fd,18) = sd->pvp_point;
	WFIFOSET(fd, packet_len(0x210));

	return;
}

/*==========================================
 * /blacksmith
 *------------------------------------------*/
void clif_parse_Blacksmith(int fd,struct map_session_data *sd)
{
	int i;
	const char* name;

	WFIFOHEAD(fd,packet_len(0x219));
	WFIFOW(fd,0) = 0x219;
	//Packet size limits this list to 10 elements. [Skotlex]
	for (i = 0; i < 10 && i < MAX_FAME_LIST; i++) {
		if (smith_fame_list[i].id > 0) {
			if (strcmp(smith_fame_list[i].name, "-") == 0 &&
				(name = map_charid2nick(smith_fame_list[i].id)) != NULL)
			{
				strncpy((char *)(WFIFOP(fd, 2 + 24 * i)), name, NAME_LENGTH);
			} else
				strncpy((char *)(WFIFOP(fd, 2 + 24 * i)), smith_fame_list[i].name, NAME_LENGTH);
		} else
			strncpy((char *)(WFIFOP(fd, 2 + 24 * i)), "None", 5);
		WFIFOL(fd, 242 + i * 4) = smith_fame_list[i].fame;
	}
	for(;i < 10; i++) { //In case the MAX is less than 10.
		strncpy((char *)(WFIFOP(fd, 2 + 24 * i)), "Unavailable", 12);
		WFIFOL(fd, 242 + i * 4) = 0;
	}

	WFIFOSET(fd, packet_len(0x219));
}

int clif_fame_blacksmith(struct map_session_data *sd, int points)
{
	int fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0x21b));
	WFIFOW(fd,0) = 0x21b;
	WFIFOL(fd,2) = points;
	WFIFOL(fd,6) = sd->status.fame;
	WFIFOSET(fd, packet_len(0x21b));

	return 0;
}

/*==========================================
 * /alchemist
 *------------------------------------------*/
void clif_parse_Alchemist(int fd,struct map_session_data *sd)
{
	int i;
	const char* name;

	WFIFOHEAD(fd,packet_len(0x21a));
	WFIFOW(fd,0) = 0x21a;
	//Packet size limits this list to 10 elements. [Skotlex]
	for (i = 0; i < 10 && i < MAX_FAME_LIST; i++) {
		if (chemist_fame_list[i].id > 0) {
			if (strcmp(chemist_fame_list[i].name, "-") == 0 &&
				(name = map_charid2nick(chemist_fame_list[i].id)) != NULL)
			{
				memcpy(WFIFOP(fd, 2 + 24 * i), name, NAME_LENGTH);
			} else
				memcpy(WFIFOP(fd, 2 + 24 * i), chemist_fame_list[i].name, NAME_LENGTH);
		} else
			memcpy(WFIFOP(fd, 2 + 24 * i), "None", NAME_LENGTH);
		WFIFOL(fd, 242 + i * 4) = chemist_fame_list[i].fame;
	}
	for(;i < 10; i++) { //In case the MAX is less than 10.
		memcpy(WFIFOP(fd, 2 + 24 * i), "Unavailable", NAME_LENGTH);
		WFIFOL(fd, 242 + i * 4) = 0;
	}

	WFIFOSET(fd, packet_len(0x21a));
}

int clif_fame_alchemist(struct map_session_data *sd, int points)
{
	int fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0x21c));
	WFIFOW(fd,0) = 0x21c;
	WFIFOL(fd,2) = points;
	WFIFOL(fd,6) = sd->status.fame;
	WFIFOSET(fd, packet_len(0x21c));
	
	return 0;
}

/*==========================================
 * /taekwon
 *------------------------------------------*/
void clif_parse_Taekwon(int fd,struct map_session_data *sd)
{
	int i;
	const char* name;

	WFIFOHEAD(fd,packet_len(0x226));
	WFIFOW(fd,0) = 0x226;
	//Packet size limits this list to 10 elements. [Skotlex]
	for (i = 0; i < 10 && i < MAX_FAME_LIST; i++) {
		if (taekwon_fame_list[i].id > 0) {
			if (strcmp(taekwon_fame_list[i].name, "-") == 0 &&
				(name = map_charid2nick(taekwon_fame_list[i].id)) != NULL)
			{
				memcpy(WFIFOP(fd, 2 + 24 * i), name, NAME_LENGTH);
			} else
				memcpy(WFIFOP(fd, 2 + 24 * i), taekwon_fame_list[i].name, NAME_LENGTH);
		} else
			memcpy(WFIFOP(fd, 2 + 24 * i), "None", NAME_LENGTH);
		WFIFOL(fd, 242 + i * 4) = taekwon_fame_list[i].fame;
	}
	for(;i < 10; i++) { //In case the MAX is less than 10.
		memcpy(WFIFOP(fd, 2 + 24 * i), "Unavailable", NAME_LENGTH);
		WFIFOL(fd, 242 + i * 4) = 0;
	}
	WFIFOSET(fd, packet_len(0x226));
}

int clif_fame_taekwon(struct map_session_data *sd, int points)
{
	int fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0x224));
	WFIFOW(fd,0) = 0x224;
	WFIFOL(fd,2) = points;
	WFIFOL(fd,6) = sd->status.fame;
	WFIFOSET(fd, packet_len(0x224));
	
	return 0;
}

/*==========================================
 * PK Ranking table?
 *------------------------------------------*/
void clif_parse_RankingPk(int fd,struct map_session_data *sd)
{
	int i;

	WFIFOHEAD(fd,packet_len(0x238));
	WFIFOW(fd,0) = 0x238;
	for(i=0;i<10;i++){
		memcpy(WFIFOP(fd,i*24+2), "Unknown", NAME_LENGTH);
		WFIFOL(fd,i*4+242) = 0;
	}
	WFIFOSET(fd, packet_len(0x238));
	return;
}

/*==========================================
 * SG Feel save OK [Komurka]
 *------------------------------------------*/
void clif_parse_FeelSaveOk(int fd,struct map_session_data *sd)
{
	char feel_var[3][NAME_LENGTH] = {"PC_FEEL_SUN","PC_FEEL_MOON","PC_FEEL_STAR"};
	int i;
	if (sd->menuskill_id != SG_FEEL)
		return;
	i = sd->menuskill_val-1;
	if (i<0 || i > 2) return; //Bug?

	sd->feel_map[i].index = map[sd->bl.m].index;
	sd->feel_map[i].m = sd->bl.m;
	pc_setglobalreg(sd,feel_var[i],map[sd->bl.m].index);

//Are these really needed? Shouldn't they show up automatically from the feel save packet?
//	clif_misceffect2(&sd->bl, 0x1b0);
//	clif_misceffect2(&sd->bl, 0x21f);
	clif_feel_info(sd, i, 0);
	sd->menuskill_val = sd->menuskill_id = 0;
}

/*==========================================
 * Question about Star Glaldiator save map [Komurka]
 *------------------------------------------*/
void clif_parse_ReqFeel(int fd, struct map_session_data *sd, int skilllv)
{
	WFIFOHEAD(fd,packet_len(0x253));
	WFIFOW(fd,0)=0x253;
	WFIFOSET(fd, packet_len(0x253));
	sd->menuskill_id = SG_FEEL;
	sd->menuskill_val = skilllv;
}

void clif_parse_AdoptRequest(int fd,struct map_session_data *sd)
{
	//TODO: add somewhere the adopt code, checks for exploits, etc, etc.
	//Missing packets are the client's reply packets to the adopt request one. 
	//[Skotlex]
	int account_id;
	struct map_session_data *sd2;
	
	account_id = RFIFOL(fd,2);
	sd2 = map_id2sd(account_id);
	if(sd2 && sd2->fd && sd2 != sd && sd2->status.party_id == sd->status.party_id) {	//FIXME: No checks whatsoever are in place yet!
		fd = sd2->fd;
		WFIFOHEAD(fd,packet_len(0x1f9));
		WFIFOW(fd,0)=0x1f9;
		WFIFOSET(fd, packet_len(0x1f9));
	}
}

/*==========================================
 * Homunculus packets
 *------------------------------------------*/
void clif_parse_ChangeHomunculusName(int fd, struct map_session_data *sd)
{
	merc_hom_change_name(sd,(char*)RFIFOP(fd,2));
}

void clif_parse_HomMoveToMaster(int fd, struct map_session_data *sd)
{	//[orn]
	nullpo_retv(sd);

	if(!merc_is_hom_active(sd->hd))
		return;

	unit_walktoxy(&sd->hd->bl, sd->bl.x,sd->bl.y-1, 4);
}

void clif_parse_HomMoveTo(int fd,struct map_session_data *sd)
{	//[orn]
	int x,y,cmd;
	nullpo_retv(sd);

	if(!merc_is_hom_active(sd->hd))
		return;

	cmd = RFIFOW(fd,0);
	x = RFIFOB(fd,packet_db[sd->packet_ver][cmd].pos[0]) * 4 +
		(RFIFOB(fd,packet_db[sd->packet_ver][cmd].pos[0] + 1) >> 6);
	y = ((RFIFOB(fd,packet_db[sd->packet_ver][cmd].pos[0]+1) & 0x3f) << 4) +
		(RFIFOB(fd,packet_db[sd->packet_ver][cmd].pos[0] + 2) >> 4);

	unit_walktoxy(&(sd->hd->bl),x,y,4);
}

void clif_parse_HomAttack(int fd,struct map_session_data *sd)
{	//[orn]
	if(!merc_is_hom_active(sd->hd))
		return;
	
	unit_attack(&sd->hd->bl,RFIFOL(fd,6),0) ;
}

void clif_parse_HomMenu(int fd, struct map_session_data *sd)
{	//[orn]
	int cmd;

	cmd = RFIFOW(fd,0);

	if(!merc_is_hom_active(sd->hd))
		return;

	merc_menu(sd,RFIFOB(fd,packet_db[sd->packet_ver][cmd].pos[0]));
}

void clif_parse_AutoRevive(int fd, struct map_session_data *sd)
{
	int item_position;

	nullpo_retv(sd);
	item_position = pc_search_inventory(sd, 7621);

	if (item_position < 0)
		return;

	if (!status_revive(&sd->bl, 100, 100))
		return;
	
	clif_skill_nodamage(&sd->bl,&sd->bl,ALL_RESURRECTION,4,1);
	pc_delitem(sd, item_position, 1, 0);
}

/*==========================================
 * �p�P�b�g�f�o�b�O
 *------------------------------------------*/
void clif_parse_debug(int fd,struct map_session_data *sd)
{
	int i, cmd, len;

	cmd = RFIFOW(fd,0);
	len = sd?packet_db[sd->packet_ver][cmd].len:RFIFOREST(fd); //With no session, just read the remaining in the buffer.
	ShowDebug("packet debug 0x%4X\n",cmd);
	printf("---- 00-01-02-03-04-05-06-07-08-09-0A-0B-0C-0D-0E-0F");
	for(i=0;i<len;i++){
		if((i&15)==0)
			printf("\n%04X ",i);
		printf("%02X ",RFIFOB(fd,i));
	}
	printf("\n");
}

/*==========================================
 * Main client packet processing function
 *------------------------------------------*/
int clif_parse(int fd)
{
	int cmd, packet_ver, packet_len, err;
	TBL_PC* sd;
	int pnum;

	//TODO apply deplays or disconnect based on packet throughput [FlavioJS]
	// Note: "click masters" can do 80+ clicks in 10 seconds

	for( pnum = 0; pnum < 3; ++pnum )// Limit max packets per cycle to 3 (delay packet spammers) [FlavioJS]
	{ // begin main client packet processing loop

	sd = (TBL_PC *)session[fd]->session_data;
	if (session[fd]->eof) {
		if (sd) {
			if (sd->state.autotrade) {
				//Disassociate character from the socket connection.
				session[fd]->session_data = NULL;
				sd->fd = 0;
				ShowInfo("%sCharacter '"CL_WHITE"%s"CL_RESET"' logged off (using @autotrade).\n", (pc_isGM(sd))?"GM ":"", sd->status.name);
			} else
			if (sd->state.auth) {
				 // Player logout display [Valaris]
				ShowInfo("%sCharacter '"CL_WHITE"%s"CL_RESET"' logged off.\n", (pc_isGM(sd))?"GM ":"", sd->status.name);
				clif_quitsave(fd, sd);
			} else {
				ShowInfo("Player AID:%d/CID:%d (not authenticated) logged off.\n", sd->bl.id, sd->status.char_id);
				map_quit(sd);
			}
		} else {
			uint32 ip = session[fd]->client_addr;
			ShowInfo("Closed connection from '"CL_WHITE"%d.%d.%d.%d"CL_RESET"'.\n", CONVIP(ip));
		}
		do_close(fd);
		return 0;
	}

	if (RFIFOREST(fd) < 2)
		return 0;

	cmd = RFIFOW(fd,0);

	// identify client's packet version
	if (sd) {
		packet_ver = sd->packet_ver;
	} else {
		// check authentification packet to know packet version
		packet_ver = clif_guess_PacketVer(fd, 0, &err);
		if( err )
		{// failed to identify packet version
			ShowInfo("clif_parse: Disconnecting session #%d with unknown packet version%s.\n", fd, (
				err == 1 ? "" :
				err == 2 ? ", possibly for having an invalid account_id" :
				err == 3 ? ", possibly for having an invalid char_id." :
				/* Uncomment when checks are added in clif_guess_PacketVer. [FlavioJS]
				err == 4 ? ", possibly for having an invalid login_id1." :
				err == 5 ? ", possibly for having an invalid client_tick." :
				*/
				err == 6 ? ", possibly for having an invalid sex." :
				". ERROR invalid error code"));
			WFIFOHEAD(fd,packet_len(0x6a));
			WFIFOW(fd,0) = 0x6a;
			WFIFOB(fd,2) = 3; // Rejected from Server
			WFIFOSET(fd,packet_len(0x6a));
			RFIFOSKIP(fd, RFIFOREST(fd));
			clif_setwaitclose(fd);
			return 0;
		}
	}

	// filter out invalid / unsupported packets
	if (cmd > MAX_PACKET_DB || packet_db[packet_ver][cmd].len == 0) {
		ShowWarning("clif_parse: Received unsupported packet (packet 0x%04x, %d bytes received), disconnecting session #%d.\n", cmd, RFIFOREST(fd), fd);
		set_eof(fd);
		return 0;
	}

	// determine real packet length
	packet_len = packet_db[packet_ver][cmd].len;
	if (packet_len == -1) { // variable-length packet
		if (RFIFOREST(fd) < 4)
			return 0;

		packet_len = RFIFOW(fd,2);
		if (packet_len < 4 || packet_len > 32768) {
			ShowWarning("clif_parse: Received packet 0x%04x specifies invalid packet_len (%d), disconnecting session #%d.\n", cmd, packet_len, fd);
			set_eof(fd);
			return 0;
		}
	}
	if ((int)RFIFOREST(fd) < packet_len)
		return 0; // not enough data received to form the packet

	if (sd && sd->state.waitingdisconnect == 1) {
		// �ؒf�҂��̏ꍇ�p�P�b�g���������Ȃ�
	} else if (packet_db[packet_ver][cmd].func) {
		if (sd && sd->bl.prev == NULL &&
			packet_db[packet_ver][cmd].func != clif_parse_LoadEndAck)
			; //Only valid packet when player is not on a map is the finish-loading packet.
		else
		if (sd
			|| packet_db[packet_ver][cmd].func == clif_parse_WantToConnection
			|| packet_db[packet_ver][cmd].func == clif_parse_debug
		)	//Only execute the function when there's an sd (except for debug/wanttoconnect packets)
			packet_db[packet_ver][cmd].func(fd, sd);
	}
#if DUMP_UNKNOWN_PACKET
	else if (battle_config.error_log)
	{
		int i;
		FILE *fp;
		char packet_txt[256] = "save/packet.txt";
		time_t now;
		dump = 1;

		if ((fp = fopen(packet_txt, "a")) == NULL) {
			ShowError("clif.c: can't write [%s] !!! data is lost !!!\n", packet_txt);
			return 1;
		} else {
			time(&now);
			if (sd && sd->state.auth) {
				fprintf(fp, "%sPlayer with account ID %d (character ID %d, player name %s) sent wrong packet:\n",
					asctime(localtime(&now)), sd->status.account_id, sd->status.char_id, sd->status.name);
			} else if (sd) // not authentified! (refused by char-server or disconnect before to be authentified)
				fprintf(fp, "%sPlayer with account ID %d sent wrong packet:\n", asctime(localtime(&now)), sd->bl.id);

			fprintf(fp, "\t---- 00-01-02-03-04-05-06-07-08-09-0A-0B-0C-0D-0E-0F");
			for(i = 0; i < packet_len; i++) {
				if ((i & 15) == 0)
					fprintf(fp, "\n\t%04X ", i);
				fprintf(fp, "%02X ", RFIFOB(fd,i));
			}
			fprintf(fp, "\n\n");
			fclose(fp);
		}
	}
#endif

	/* TODO: use utils.c :: dump()
	if (dump) {
		int i;
		ShowDebug("\nclif_parse: session #%d, packet 0x%04x, length %d, version %d\n", fd, cmd, packet_len, packet_ver);
		printf("---- 00-01-02-03-04-05-06-07-08-09-0A-0B-0C-0D-0E-0F");
		for(i = 0; i < packet_len; i++) {
			if ((i & 15) == 0)
				printf("\n%04X ",i);
			printf("%02X ", RFIFOB(fd,i));
		}
		printf("\n");
		if (sd && sd->state.auth) {
			if (sd->status.name != NULL)
				printf("\nAccount ID %d, character ID %d, player name %s.\n",
			       sd->status.account_id, sd->status.char_id, sd->status.name);
			else
				printf("\nAccount ID %d.\n", sd->bl.id);
		} else if (sd) // not authentified! (refused by char-server or disconnect before to be authentified)
			printf("\nAccount ID %d.\n", sd->bl.id);
	}*/

	RFIFOSKIP(fd, packet_len);

	}; // main loop end

	return 0;
}

/*==========================================
 * �p�P�b�g�f�[�^�x�[�X�ǂݍ���
 *------------------------------------------*/
static int packetdb_readdb(void)
{
	FILE *fp;
	char line[1024];
	int ln=0;
	int cmd,i,j,k,packet_ver;
	int max_cmd=-1;
	int skip_ver = 0;
	int warned = 0;
	char *str[64],*p,*str2[64],*p2,w1[64],w2[64];
	int packet_len_table[MAX_PACKET_DB] = {
	   10,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0,
	//#0x0040
	    0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0, 55, 17,  3, 37,  46, -1, 23, -1,  3,108,  3,  2,
#if PACKETVER < 2
	    3, 28, 19, 11,  3, -1,  9,  5,  52, 51, 56, 58, 41,  2,  6,  6,
#else	// 78-7b �T���ȍ~ lv99�G�t�F�N�g�p
	    3, 28, 19, 11,  3, -1,  9,  5,  54, 53, 58, 60, 41,  2,  6,  6,
#endif
	//#0x0080
	    7,  3,  2,  2,  2,  5, 16, 12,  10,  7, 29,  2, -1, -1, -1,  0, // 0x8b changed to 2 (was 23)
	    7, 22, 28,  2,  6, 30, -1, -1,   3, -1, -1,  5,  9, 17, 17,  6,
	   23,  6,  6, -1, -1, -1, -1,  8,   7,  6,  7,  4,  7,  0, -1,  6,
	    8,  8,  3,  3, -1,  6,  6, -1,   7,  6,  2,  5,  6, 44,  5,  3,
	//#0x00C0
	    7,  2,  6,  8,  6,  7, -1, -1,  -1, -1,  3,  3,  6,  3,  2, 27, // 0xcd change to 3 (was 6)
	    3,  4,  4,  2, -1, -1,  3, -1,   6, 14,  3, -1, 28, 29, -1, -1,
	   30, 30, 26,  2,  6, 26,  3,  3,   8, 19,  5,  2,  3,  2,  2,  2,
	    3,  2,  6,  8, 21,  8,  8,  2,   2, 26,  3, -1,  6, 27, 30, 10,
	//#0x0100
	    2,  6,  6, 30, 79, 31, 10, 10,  -1, -1,  4,  6,  6,  2, 11, -1,
	   10, 39,  4, 10, 31, 35, 10, 18,   2, 13, 15, 20, 68,  2,  3, 16,
	    6, 14, -1, -1, 21,  8,  8,  8,   8,  8,  2,  2,  3,  4,  2, -1,
	    6, 86,  6, -1, -1,  7, -1,  6,   3, 16,  4,  4,  4,  6, 24, 26,
	//#0x0140
	   22, 14,  6, 10, 23, 19,  6, 39,   8,  9,  6, 27, -1,  2,  6,  6,
	  110,  6, -1, -1, -1, -1, -1,  6,  -1, 54, 66, 54, 90, 42,  6, 42,
	   -1, -1, -1, -1, -1, 30, -1,  3,  14,  3, 30, 10, 43, 14,186,182,
	   14, 30, 10,  3, -1,  6,106, -1,   4,  5,  4, -1,  6,  7, -1, -1,
	//#0x0180
	    6,  3,106, 10, 10, 34,  0,  6,   8,  4,  4,  4, 29, -1, 10,  6,
#if PACKETVER < 1
	   90, 86, 24,  6, 30,102,  8,  4,   8,  4, 14, 10, -1,  6,  2,  6,
#else	// 196 comodo�ȍ~ ��ԕ\���A�C�R���p
	   90, 86, 24,  6, 30,102,  9,  4,   8,  4, 14, 10, -1,  6,  2,  6,
#endif
	    3,  3, 35,  5, 11, 26, -1,  4,   4,  6, 10, 12,  6, -1,  4,  4,
	   11,  7, -1, 67, 12, 18,114,  6,   3,  6, 26, 26, 26, 26,  2,  3,
	//#0x01C0,   Set 0x1d5=-1
	    2, 14, 10, -1, 22, 22,  4,  2,  13, 97,  3,  9,  9, 30,  6, 28,
	    8, 14, 10, 35,  6, -1,  4, 11,  54, 53, 60,  2, -1, 47, 33,  6,
	   30,  8, 34, 14,  2,  6, 26,  2,  28, 81,  6, 10, 26,  2, -1, -1,
	   -1, -1, 20, 10, 32,  9, 34, 14,   2,  6, 48, 56, -1,  4,  5, 10,
	//#0x0200
	   26, -1, 26, 10, 18, 26, 11, 34,  14, 36, 10,  0,  0, -1, 32, 10, // 0x20c change to 0 (was 19)
	   22,  0, 26, 26, 42,  6,  6,  2,   2,282,282, 10, 10, -1, -1, 66,
	   10, -1, -1,  8, 10,  2,282, 18,  18, 15, 58, 57, 64,  5, 71,  5,
	   12, 26,  9, 11, -1, -1, 10,  2, 282, 11,  4, 36, -1, -1,  4,  2,
	//#0x0240
	   -1, -1, -1, -1, -1,  3,  4,  8,  -1,  3, 70,  4,  8, 12,  4, 10,
	    3, 32, -1,  3,  3,  5,  5,  8,   2,  3, -1, -1,  4, -1,  4,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0,
	//#0x0280
	    0,  0,  0,  0,  0,  0,  0,  0,   0,  0, 18,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,   0,191,  0,  0,  0,  0,  0,  0,
	//#0x02C0
	    0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0,
	};
	struct {
		void (*func)(int, struct map_session_data *);
		char *name;
	} clif_parse_func[]={
		{clif_parse_WantToConnection,"wanttoconnection"},
		{clif_parse_LoadEndAck,"loadendack"},
		{clif_parse_TickSend,"ticksend"},
		{clif_parse_WalkToXY,"walktoxy"},
		{clif_parse_QuitGame,"quitgame"},
		{clif_parse_GetCharNameRequest,"getcharnamerequest"},
		{clif_parse_GlobalMessage,"globalmessage"},
		{clif_parse_MapMove,"mapmove"},
		{clif_parse_ChangeDir,"changedir"},
		{clif_parse_Emotion,"emotion"},
		{clif_parse_HowManyConnections,"howmanyconnections"},
		{clif_parse_ActionRequest,"actionrequest"},
		{clif_parse_Restart,"restart"},
		{clif_parse_Wis,"wis"},
		{clif_parse_GMmessage,"gmmessage"},
		{clif_parse_TakeItem,"takeitem"},
		{clif_parse_DropItem,"dropitem"},
		{clif_parse_UseItem,"useitem"},
		{clif_parse_EquipItem,"equipitem"},
		{clif_parse_UnequipItem,"unequipitem"},
		{clif_parse_NpcClicked,"npcclicked"},
		{clif_parse_NpcBuySellSelected,"npcbuysellselected"},
		{clif_parse_NpcBuyListSend,"npcbuylistsend"},
		{clif_parse_NpcSellListSend,"npcselllistsend"},
		{clif_parse_CreateChatRoom,"createchatroom"},
		{clif_parse_ChatAddMember,"chataddmember"},
		{clif_parse_ChatRoomStatusChange,"chatroomstatuschange"},
		{clif_parse_ChangeChatOwner,"changechatowner"},
		{clif_parse_KickFromChat,"kickfromchat"},
		{clif_parse_ChatLeave,"chatleave"},
		{clif_parse_TradeRequest,"traderequest"},
		{clif_parse_TradeAck,"tradeack"},
		{clif_parse_TradeAddItem,"tradeadditem"},
		{clif_parse_TradeOk,"tradeok"},
		{clif_parse_TradeCancel,"tradecancel"},
		{clif_parse_TradeCommit,"tradecommit"},
		{clif_parse_StopAttack,"stopattack"},
		{clif_parse_PutItemToCart,"putitemtocart"},
		{clif_parse_GetItemFromCart,"getitemfromcart"},
		{clif_parse_RemoveOption,"removeoption"},
		{clif_parse_ChangeCart,"changecart"},
		{clif_parse_StatusUp,"statusup"},
		{clif_parse_SkillUp,"skillup"},
		{clif_parse_UseSkillToId,"useskilltoid"},
		{clif_parse_UseSkillToPos,"useskilltopos"},
		{clif_parse_UseSkillToPosMoreInfo,"useskilltoposinfo"},
		{clif_parse_UseSkillMap,"useskillmap"},
		{clif_parse_RequestMemo,"requestmemo"},
		{clif_parse_ProduceMix,"producemix"},
		{clif_parse_NpcSelectMenu,"npcselectmenu"},
		{clif_parse_NpcNextClicked,"npcnextclicked"},
		{clif_parse_NpcAmountInput,"npcamountinput"},
		{clif_parse_NpcStringInput,"npcstringinput"},
		{clif_parse_NpcCloseClicked,"npccloseclicked"},
		{clif_parse_ItemIdentify,"itemidentify"},
		{clif_parse_SelectArrow,"selectarrow"},
		{clif_parse_AutoSpell,"autospell"},
		{clif_parse_UseCard,"usecard"},
		{clif_parse_InsertCard,"insertcard"},
		{clif_parse_RepairItem,"repairitem"},
		{clif_parse_WeaponRefine,"weaponrefine"},
		{clif_parse_SolveCharName,"solvecharname"},
		{clif_parse_ResetChar,"resetchar"},
		{clif_parse_LGMmessage,"lgmmessage"},
		{clif_parse_MoveToKafra,"movetokafra"},
		{clif_parse_MoveFromKafra,"movefromkafra"},
		{clif_parse_MoveToKafraFromCart,"movetokafrafromcart"},
		{clif_parse_MoveFromKafraToCart,"movefromkafratocart"},
		{clif_parse_CloseKafra,"closekafra"},
		{clif_parse_CreateParty,"createparty"},
		{clif_parse_CreateParty2,"createparty2"},
		{clif_parse_PartyInvite,"partyinvite"},
		{clif_parse_PartyInvite2,"partyinvite2"},
		{clif_parse_ReplyPartyInvite,"replypartyinvite"},
		{clif_parse_ReplyPartyInvite2,"replypartyinvite2"},
		{clif_parse_LeaveParty,"leaveparty"},
		{clif_parse_RemovePartyMember,"removepartymember"},
		{clif_parse_PartyChangeOption,"partychangeoption"},
		{clif_parse_PartyMessage,"partymessage"},
		{clif_parse_CloseVending,"closevending"},
		{clif_parse_VendingListReq,"vendinglistreq"},
		{clif_parse_PurchaseReq,"purchasereq"},
		{clif_parse_OpenVending,"openvending"},
		{clif_parse_CreateGuild,"createguild"},
		{clif_parse_GuildCheckMaster,"guildcheckmaster"},
		{clif_parse_GuildRequestInfo,"guildrequestinfo"},
		{clif_parse_GuildChangePositionInfo,"guildchangepositioninfo"},
		{clif_parse_GuildChangeMemberPosition,"guildchangememberposition"},
		{clif_parse_GuildRequestEmblem,"guildrequestemblem"},
		{clif_parse_GuildChangeEmblem,"guildchangeemblem"},
		{clif_parse_GuildChangeNotice,"guildchangenotice"},
		{clif_parse_GuildInvite,"guildinvite"},
		{clif_parse_GuildReplyInvite,"guildreplyinvite"},
		{clif_parse_GuildLeave,"guildleave"},
		{clif_parse_GuildExpulsion,"guildexplusion"},
		{clif_parse_GuildMessage,"guildmessage"},
		{clif_parse_GuildRequestAlliance,"guildrequestalliance"},
		{clif_parse_GuildReplyAlliance,"guildreplyalliance"},
		{clif_parse_GuildDelAlliance,"guilddelalliance"},
		{clif_parse_GuildOpposition,"guildopposition"},
		{clif_parse_GuildBreak,"guildbreak"},
		{clif_parse_PetMenu,"petmenu"},
		{clif_parse_CatchPet,"catchpet"},
		{clif_parse_SelectEgg,"selectegg"},
		{clif_parse_SendEmotion,"sendemotion"},
		{clif_parse_ChangePetName,"changepetname"},
		{clif_parse_GMKick,"gmkick"},
		{clif_parse_GMHide,"gmhide"},
		{clif_parse_GMReqNoChat,"gmreqnochat"},
		{clif_parse_GMReqNoChatCount,"gmreqnochatcount"},
		{clif_parse_NoviceDoriDori,"sndoridori"},
		{clif_parse_NoviceExplosionSpirits,"snexplosionspirits"},
		{clif_parse_PMIgnore,"wisexin"},
		{clif_parse_PMIgnoreList,"wisexlist"},
		{clif_parse_PMIgnoreAll,"wisall"},
		{clif_parse_FriendsListAdd,"friendslistadd"},
		{clif_parse_FriendsListRemove,"friendslistremove"},
		{clif_parse_FriendsListReply,"friendslistreply"},
		{clif_parse_GMKillAll,"killall"},
		{clif_parse_Recall,"summon"},
		{clif_parse_GM_Monster_Item,"itemmonster"},
		{clif_parse_Shift,"shift"},
		{clif_parse_Blacksmith,"blacksmith"},
		{clif_parse_Alchemist,"alchemist"},
		{clif_parse_Taekwon,"taekwon"},
		{clif_parse_RankingPk,"rankingpk"},
		{clif_parse_FeelSaveOk,"feelsaveok"},
		{clif_parse_AdoptRequest,"adopt"},
		{clif_parse_debug,"debug"},
		{clif_parse_ChangeHomunculusName,"changehomunculusname"},
		{clif_parse_HomMoveToMaster,"hommovetomaster"},
		{clif_parse_HomMoveTo,"hommoveto"},
		{clif_parse_HomAttack,"homattack"},
		{clif_parse_HomMenu,"hommenu"},
		{clif_parse_StoragePassword,"storagepassword"},
		{clif_parse_Hotkey,"hotkey"},
		{clif_parse_AutoRevive,"autorevive"},
		{NULL,NULL}
	};

	// initialize packet_db[SERVER] from hardcoded packet_len_table[] values
	memset(packet_db,0,sizeof(packet_db));
	for( i = 0; i < sizeof(packet_len_table)/sizeof(packet_len_table[0]); ++i )
		packet_len(i) = packet_len_table[i];

	sprintf(line, "%s/packet_db.txt", db_path);
	if( (fp=fopen(line,"r"))==NULL ){
		ShowFatalError("can't read %s\n", line);
		exit(1);
	}

	clif_config.packet_db_ver = MAX_PACKET_VER;
	packet_ver = MAX_PACKET_VER;	// read into packet_db's version by default
	while( fgets(line, sizeof(line), fp) )
	{
		ln++;
		if(line[0]=='/' && line[1]=='/')
			continue;
		if (sscanf(line,"%256[^:]: %256[^\r\n]",w1,w2) == 2)
		{
			if(strcmpi(w1,"packet_ver")==0) {
				int prev_ver = packet_ver;
				skip_ver = 0;
				packet_ver = atoi(w2);
				if ( packet_ver > MAX_PACKET_VER )
				{	//Check to avoid overflowing. [Skotlex]
					if( (warned&1) == 0 )
						ShowWarning("The packet_db table only has support up to version %d.\n", MAX_PACKET_VER);
					warned &= 1;
					skip_ver = 1;
				}
				else if( packet_ver < 0 )
				{
					if( (warned&2) == 0 )
						ShowWarning("Negative packet versions are not supported.\n");
					warned &= 2;
					skip_ver = 1;
				}
				else if( packet_ver == SERVER )
				{
					if( (warned&4) == 0 )
						ShowWarning("Packet version %d is reserved for server use only.\n", SERVER);
					warned &= 4;
					skip_ver = 1;
				}

				if( skip_ver )
				{
					ShowWarning("Skipping packet version %d.\n", packet_ver);
					packet_ver = prev_ver;
					continue;
				}
				// copy from previous version into new version and continue
				// - indicating all following packets should be read into the newer version
				memcpy(&packet_db[packet_ver], &packet_db[prev_ver], sizeof(packet_db[0]));
				continue;
			} else if(strcmpi(w1,"packet_db_ver")==0) {
				if(strcmpi(w2,"default")==0) //This is the preferred version.
					clif_config.packet_db_ver = MAX_PACKET_VER;
				else // to manually set the packet DB version
					clif_config.packet_db_ver = cap_value(atoi(w2), 0, MAX_PACKET_VER);
				
				continue;
			}
		}

		if( skip_ver != 0 )
			continue; // Skipping current packet version

		memset(str,0,sizeof(str));
		for(j=0,p=line;j<4 && p; ++j)
		{
			str[j]=p;
			p=strchr(p,',');
			if(p) *p++=0;
		}
		if(str[0]==NULL)
			continue;
		cmd=strtol(str[0],(char **)NULL,0);
		if(max_cmd < cmd)
			max_cmd = cmd;
		if(cmd <= 0 || cmd > MAX_PACKET_DB)
			continue;
		if(str[1]==NULL){
			ShowError("packet_db: packet len error\n");
			continue;
		}
		k = atoi(str[1]);
		packet_db[packet_ver][cmd].len = k;

		if(str[2]==NULL){
			packet_db[packet_ver][cmd].func = NULL;
			ln++;
			continue;
		}
		for(j=0;j<sizeof(clif_parse_func)/sizeof(clif_parse_func[0]);j++){
			if(clif_parse_func[j].name != NULL && strcmp(str[2],clif_parse_func[j].name)==0)
			{
				if (packet_db[packet_ver][cmd].func != clif_parse_func[j].func)
				{	//If we are updating a function, we need to zero up the previous one. [Skotlex]
					for(i=0;i<=MAX_PACKET_DB;i++){
						if (packet_db[packet_ver][i].func == clif_parse_func[j].func)
						{	
							memset(&packet_db[packet_ver][i], 0, sizeof(struct s_packet_db));
							break;
						}
					}
				}
				packet_db[packet_ver][cmd].func = clif_parse_func[j].func;
				break;
			}
		}
		// set the identifying cmd for the packet_db version
		if (strcmp(str[2],"wanttoconnection")==0)
			clif_config.connect_cmd[packet_ver] = cmd;
			
		if(str[3]==NULL){
			ShowError("packet_db: packet error\n");
			exit(1);
		}
		for(j=0,p2=str[3];p2;j++){
			str2[j]=p2;
			p2=strchr(p2,':');
			if(p2) *p2++=0;
			k = atoi(str2[j]);
			// if (packet_db[packet_ver][cmd].pos[j] != k && clif_config.prefer_packet_db)	// not used for now
			packet_db[packet_ver][cmd].pos[j] = k;
		}
	}
	fclose(fp);
	if(max_cmd > MAX_PACKET_DB)
	{
		ShowWarning("Found packets up to 0x%X, ignored 0x%X and above.\n", max_cmd, MAX_PACKET_DB);
		ShowWarning("Please increase MAX_PACKET_DB and recompile.\n");
	}
	if (!clif_config.connect_cmd[clif_config.packet_db_ver])
	{	//Locate the nearest version that we still support. [Skotlex]
		for(j = clif_config.packet_db_ver; j >= 0 && !clif_config.connect_cmd[j]; j--);
		
		clif_config.packet_db_ver = j?j:MAX_PACKET_VER;
	}
	ShowStatus("Done reading packet database from '"CL_WHITE"%s"CL_RESET"'. Using default packet version: "CL_WHITE"%d"CL_RESET".\n", "packet_db.txt", clif_config.packet_db_ver);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int do_init_clif(void)
{
	clif_config.packet_db_ver = -1; // the main packet version of the DB
	memset(clif_config.connect_cmd, 0, sizeof(clif_config.connect_cmd)); //The default connect command will be determined after reading the packet_db [Skotlex]

	memset(packet_db,0,sizeof(packet_db));
	//Using the packet_db file is the only way to set up packets now [Skotlex]
	packetdb_readdb();

	set_defaultparse(clif_parse);
	if (!make_listen_bind(bind_ip,map_port)) {
		ShowFatalError("can't bind game port\n");
		exit(1);
	}

	add_timer_func_list(clif_waitclose, "clif_waitclose");
	add_timer_func_list(clif_clearunit_delayed_sub, "clif_clearunit_delayed_sub");
	add_timer_func_list(clif_delayquit, "clif_delayquit");
	return 0;
}
