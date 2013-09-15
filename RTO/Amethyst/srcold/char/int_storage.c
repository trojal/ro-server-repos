// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/mmo.h"
#include "../common/malloc.h"
#include "../common/socket.h"
#include "../common/db.h"
#include "../common/lock.h"
#include "../common/showmsg.h"
#include "../common/utils.h"
#include "char.h"
#include "inter.h"
#include "int_storage.h"
#include "int_pet.h"
#include "int_guild.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// �t�@�C�����̃f�t�H���g
// inter_config_read()�ōĐݒ肳���
char storage_txt[1024]="save/storage.txt";
char guild_storage_txt[1024]="save/g_storage.txt";

#ifndef TXT_SQL_CONVERT
static struct dbt *storage_db;
static struct dbt *guild_storage_db;

// �q�Ƀf�[�^�𕶎���ɕϊ�
int storage_tostr(char *str,struct storage *p)
{
	int i,j,f=0;
	char *str_p = str;
	str_p += sprintf(str_p,"%d,%d\t",p->account_id,p->storage_amount);

	for(i=0;i<MAX_STORAGE;i++)
		if( (p->storage_[i].nameid) && (p->storage_[i].amount) ){
			str_p += sprintf(str_p,"%d,%d,%d,%d,%d,%d,%d",
				p->storage_[i].id,p->storage_[i].nameid,p->storage_[i].amount,p->storage_[i].equip,
				p->storage_[i].identify,p->storage_[i].refine,p->storage_[i].attribute);
			for(j=0; j<MAX_SLOTS; j++)
				str_p += sprintf(str_p,",%d",p->storage_[i].card[j]);
			str_p += sprintf(str_p," ");
			f++;
		}

	*(str_p++)='\t';

	*str_p='\0';
	if(!f)
		str[0]=0;
	return 0;
}
#endif //TXT_SQL_CONVERT
// �������q�Ƀf�[�^�ɕϊ�
int storage_fromstr(char *str,struct storage *p)
{
	int tmp_int[256];
	char tmp_str[256];
	int set,next,len,i,j;

	set=sscanf(str,"%d,%d%n",&tmp_int[0],&tmp_int[1],&next);
	p->storage_amount=tmp_int[1];

	if(set!=2)
		return 1;
	if(str[next]=='\n' || str[next]=='\r')
		return 0;
	next++;
	for(i=0;str[next] && str[next]!='\t' && i < MAX_STORAGE;i++){
		if(sscanf(str + next, "%d,%d,%d,%d,%d,%d,%d%[0-9,-]%n",
		      &tmp_int[0], &tmp_int[1], &tmp_int[2], &tmp_int[3],
		      &tmp_int[4], &tmp_int[5], &tmp_int[6], tmp_str, &len) == 8) {
			p->storage_[i].id = tmp_int[0];
			p->storage_[i].nameid = tmp_int[1];
			p->storage_[i].amount = tmp_int[2];
			p->storage_[i].equip = tmp_int[3];
			p->storage_[i].identify = tmp_int[4];
			p->storage_[i].refine = tmp_int[5];
			p->storage_[i].attribute = tmp_int[6];
			
			for(j = 0; j < MAX_SLOTS && tmp_str && sscanf(tmp_str, ",%d%[0-9,-]",&tmp_int[0], tmp_str) > 0; j++)
				p->storage_[i].card[j] = tmp_int[0];
			
			next += len;
			if (str[next] == ' ')
				next++;
		}
		else return 1;
	}
	if (i >= MAX_STORAGE && str[next] && str[next]!='\t')
		ShowWarning("storage_fromstr: Found a storage line with more items than MAX_STORAGE (%d), remaining items have been discarded!\n", MAX_STORAGE);
	return 0;
}
#ifndef TXT_SQL_CONVERT
int guild_storage_tostr(char *str,struct guild_storage *p)
{
	int i,j,f=0;
	char *str_p = str;
	str_p+=sprintf(str,"%d,%d\t",p->guild_id,p->storage_amount);

	for(i=0;i<MAX_GUILD_STORAGE;i++)
		if( (p->storage_[i].nameid) && (p->storage_[i].amount) ){
			str_p += sprintf(str_p,"%d,%d,%d,%d,%d,%d,%d",
				p->storage_[i].id,p->storage_[i].nameid,p->storage_[i].amount,p->storage_[i].equip,
				p->storage_[i].identify,p->storage_[i].refine,p->storage_[i].attribute);
			for(j=0; j<MAX_SLOTS; j++)
				str_p += sprintf(str_p,",%d",p->storage_[i].card[j]);
			str_p += sprintf(str_p," ");
			f++;
		}

	*(str_p++)='\t';

	*str_p='\0';
	if(!f)
		str[0]=0;
	return 0;
}
#endif //TXT_SQL_CONVERT
int guild_storage_fromstr(char *str,struct guild_storage *p)
{
	int tmp_int[256];
	char tmp_str[256];
	int set,next,len,i,j;

	set=sscanf(str,"%d,%d%n",&tmp_int[0],&tmp_int[1],&next);
	p->storage_amount=tmp_int[1];

	if(set!=2)
		return 1;
	if(str[next]=='\n' || str[next]=='\r')
		return 0;
	next++;
	for(i=0;str[next] && str[next]!='\t' && i < MAX_GUILD_STORAGE;i++){
		if(sscanf(str + next, "%d,%d,%d,%d,%d,%d,%d%[0-9,-]%n",
			&tmp_int[0], &tmp_int[1], &tmp_int[2], &tmp_int[3],
			&tmp_int[4], &tmp_int[5], &tmp_int[6], tmp_str, &len) == 8)
		{
			p->storage_[i].id = tmp_int[0];
			p->storage_[i].nameid = tmp_int[1];
			p->storage_[i].amount = tmp_int[2];
			p->storage_[i].equip = tmp_int[3];
			p->storage_[i].identify = tmp_int[4];
			p->storage_[i].refine = tmp_int[5];
			p->storage_[i].attribute = tmp_int[6];
			for(j = 0; j < MAX_SLOTS && tmp_str && sscanf(tmp_str, ",%d%[0-9,-]",&tmp_int[0], tmp_str) > 0; j++)
				p->storage_[i].card[j] = tmp_int[0];
			next += len;
			if (str[next] == ' ')
				next++;
		}
		else return 1;
	}
	if (i >= MAX_GUILD_STORAGE && str[next] && str[next]!='\t')
		ShowWarning("guild_storage_fromstr: Found a storage line with more items than MAX_GUILD_STORAGE (%d), remaining items have been discarded!\n", MAX_GUILD_STORAGE);
	return 0;
}
#ifndef TXT_SQL_CONVERT
static void* create_storage(DBKey key, va_list args) {
	struct storage *s;
	s = (struct storage *) aCalloc(sizeof(struct storage), 1);
	s->account_id=key.i;
	return s;
}

// �A�J�E���g����q�Ƀf�[�^�C���f�b�N�X�𓾂�i�V�K�q�ɒǉ��\�j
struct storage *account2storage(int account_id)
{
	struct storage *s;
	s= idb_ensure(storage_db, account_id, create_storage);
	return s;
}

static void* create_guildstorage(DBKey key, va_list args) {
	struct guild_storage *gs = NULL;
	gs = (struct guild_storage *) aCalloc(sizeof(struct guild_storage), 1);
	gs->guild_id=key.i;
	return gs;
}

struct guild_storage *guild2storage(int guild_id)
{
	struct guild_storage *gs = NULL;
	if(inter_guild_search(guild_id) != NULL)
		gs= idb_ensure(guild_storage_db, guild_id, create_guildstorage);
	return gs;
}

//---------------------------------------------------------
// �q�Ƀf�[�^��ǂݍ���
int inter_storage_init()
{
	char line[65536];
	int c=0,tmp_int;
	struct storage *s;
	struct guild_storage *gs;
	FILE *fp;

	storage_db = db_alloc(__FILE__,__LINE__,DB_INT,DB_OPT_RELEASE_DATA,sizeof(int));

	fp=fopen(storage_txt,"r");
	if(fp==NULL){
		ShowError("can't read : %s\n",storage_txt);
		return 1;
	}
	while(fgets(line, sizeof(line), fp))
	{
		sscanf(line,"%d",&tmp_int);
		s = (struct storage*)aCalloc(sizeof(struct storage), 1);
		if(s==NULL){
			ShowFatalError("int_storage: out of memory!\n");
			exit(0);
		}
//		memset(s,0,sizeof(struct storage)); aCalloc does this...
		s->account_id=tmp_int;
		if(s->account_id > 0 && storage_fromstr(line,s) == 0) {
			idb_put(storage_db,s->account_id,s);
		}
		else{
			ShowError("int_storage: broken data [%s] line %d\n",storage_txt,c);
			aFree(s);
		}
		c++;
	}
	fclose(fp);

	c = 0;
	guild_storage_db = db_alloc(__FILE__,__LINE__,DB_INT,DB_OPT_RELEASE_DATA,sizeof(int));

	fp=fopen(guild_storage_txt,"r");
	if(fp==NULL){
		ShowError("can't read : %s\n",guild_storage_txt);
		return 1;
	}
	while(fgets(line, sizeof(line), fp))
	{
		sscanf(line,"%d",&tmp_int);
		gs = (struct guild_storage*)aCalloc(sizeof(struct guild_storage), 1);
		if(gs==NULL){
			ShowFatalError("int_storage: out of memory!\n");
			exit(0);
		}
//		memset(gs,0,sizeof(struct guild_storage)); aCalloc...
		gs->guild_id=tmp_int;
		if(gs->guild_id > 0 && guild_storage_fromstr(line,gs) == 0) {
			idb_put(guild_storage_db,gs->guild_id,gs);
		}
		else{
			ShowError("int_storage: broken data [%s] line %d\n",guild_storage_txt,c);
			aFree(gs);
		}
		c++;
	}
	fclose(fp);

	return 0;
}

void inter_storage_final() {
	storage_db->destroy(storage_db, NULL);
	guild_storage_db->destroy(guild_storage_db, NULL);
	return;
}

int inter_storage_save_sub(DBKey key,void *data,va_list ap)
{
	char line[65536];
	FILE *fp;
	storage_tostr(line,(struct storage *)data);
	fp=va_arg(ap,FILE *);
	if(*line)
		fprintf(fp,"%s\n",line);
	return 0;
}
//---------------------------------------------------------
// �q�Ƀf�[�^����������
int inter_storage_save()
{
	FILE *fp;
	int lock;
	if( (fp=lock_fopen(storage_txt,&lock))==NULL ){
		ShowError("int_storage: can't write [%s] !!! data is lost !!!\n",storage_txt);
		return 1;
	}
	storage_db->foreach(storage_db,inter_storage_save_sub,fp);
	lock_fclose(fp,storage_txt,&lock);
//	printf("int_storage: %s saved.\n",storage_txt);
	return 0;
}

int inter_guild_storage_save_sub(DBKey key,void *data,va_list ap)
{
	char line[65536];
	FILE *fp;
	if(inter_guild_search(((struct guild_storage *)data)->guild_id) != NULL) {
		guild_storage_tostr(line,(struct guild_storage *)data);
		fp=va_arg(ap,FILE *);
		if(*line)
			fprintf(fp,"%s\n",line);
	}
	return 0;
}
//---------------------------------------------------------
// �q�Ƀf�[�^����������
int inter_guild_storage_save()
{
	FILE *fp;
	int  lock;
	if( (fp=lock_fopen(guild_storage_txt,&lock))==NULL ){
		ShowError("int_storage: can't write [%s] !!! data is lost !!!\n",guild_storage_txt);
		return 1;
	}
	guild_storage_db->foreach(guild_storage_db,inter_guild_storage_save_sub,fp);
	lock_fclose(fp,guild_storage_txt,&lock);
//	printf("int_storage: %s saved.\n",guild_storage_txt);
	return 0;
}

// �q�Ƀf�[�^�폜
int inter_storage_delete(int account_id)
{
	struct storage *s = idb_get(storage_db,account_id);
	if(s) {
		int i;
		for(i=0;i<s->storage_amount;i++){
			if(s->storage_[i].card[0] == (short)0xff00)
				inter_pet_delete( MakeDWord(s->storage_[i].card[1],s->storage_[i].card[2]) );
		}
		idb_remove(storage_db,account_id);
	}
	return 0;
}

// �M���h�q�Ƀf�[�^�폜
int inter_guild_storage_delete(int guild_id)
{
	struct guild_storage *gs = idb_get(guild_storage_db,guild_id);
	if(gs) {
		int i;
		for(i=0;i<gs->storage_amount;i++){
			if(gs->storage_[i].card[0] == (short)0xff00)
				inter_pet_delete( MakeDWord(gs->storage_[i].card[1],gs->storage_[i].card[2]) );
		}
		idb_remove(guild_storage_db,guild_id);
	}
	return 0;
}

//---------------------------------------------------------
// map server�ւ̒ʐM

// �q�Ƀf�[�^�̑��M
int mapif_load_storage(int fd,int account_id)
{
	struct storage *s=account2storage(account_id);
        WFIFOHEAD(fd, sizeof(struct storage)+8);
	WFIFOW(fd,0)=0x3810;
	WFIFOW(fd,2)=sizeof(struct storage)+8;
	WFIFOL(fd,4)=account_id;
	memcpy(WFIFOP(fd,8),s,sizeof(struct storage));
	WFIFOSET(fd,WFIFOW(fd,2));
	return 0;
}
// �q�Ƀf�[�^�ۑ��������M
int mapif_save_storage_ack(int fd,int account_id)
{
        WFIFOHEAD(fd, 7);
	WFIFOW(fd,0)=0x3811;
	WFIFOL(fd,2)=account_id;
	WFIFOB(fd,6)=0;
	WFIFOSET(fd,7);
	return 0;
}

int mapif_load_guild_storage(int fd,int account_id,int guild_id)
{
	struct guild_storage *gs=guild2storage(guild_id);
        WFIFOHEAD(fd, sizeof(struct guild_storage)+12);
	WFIFOW(fd,0)=0x3818;
	if(gs) {
		WFIFOW(fd,2)=sizeof(struct guild_storage)+12;
		WFIFOL(fd,4)=account_id;
		WFIFOL(fd,8)=guild_id;
		memcpy(WFIFOP(fd,12),gs,sizeof(struct guild_storage));
	}
	else {
		WFIFOW(fd,2)=12;
		WFIFOL(fd,4)=account_id;
		WFIFOL(fd,8)=0;
	}
	WFIFOSET(fd,WFIFOW(fd,2));

	return 0;
}
int mapif_save_guild_storage_ack(int fd,int account_id,int guild_id,int fail)
{
        WFIFOHEAD(fd, 11);
	WFIFOW(fd,0)=0x3819;
	WFIFOL(fd,2)=account_id;
	WFIFOL(fd,6)=guild_id;
	WFIFOB(fd,10)=fail;
	WFIFOSET(fd,11);
	return 0;
}

//---------------------------------------------------------
// map server����̒ʐM

// �q�Ƀf�[�^�v����M
int mapif_parse_LoadStorage(int fd)
{
	RFIFOHEAD(fd);
	mapif_load_storage(fd,RFIFOL(fd,2));
	return 0;
}
// �q�Ƀf�[�^��M���ۑ�
int mapif_parse_SaveStorage(int fd)
{
	struct storage *s;
	int account_id, len;
	RFIFOHEAD(fd);
	account_id=RFIFOL(fd,4);
	len=RFIFOW(fd,2);
	if(sizeof(struct storage)!=len-8){
		ShowError("inter storage: data size error %d %d\n",sizeof(struct storage),len-8);
	}
	else {
		s=account2storage(account_id);
		memcpy(s,RFIFOP(fd,8),sizeof(struct storage));
		mapif_save_storage_ack(fd,account_id);
	}
	return 0;
}

int mapif_parse_LoadGuildStorage(int fd)
{
	RFIFOHEAD(fd);
	mapif_load_guild_storage(fd,RFIFOL(fd,2),RFIFOL(fd,6));
	return 0;
}
int mapif_parse_SaveGuildStorage(int fd)
{
	struct guild_storage *gs;
	int guild_id, len;
	RFIFOHEAD(fd);
	guild_id=RFIFOL(fd,8);
	len=RFIFOW(fd,2);
	if(sizeof(struct guild_storage)!=len-12){
		ShowError("inter storage: data size error %d %d\n",sizeof(struct guild_storage),len-12);
	}
	else {
		gs=guild2storage(guild_id);
		if(gs) {
			memcpy(gs,RFIFOP(fd,12),sizeof(struct guild_storage));
			mapif_save_guild_storage_ack(fd,RFIFOL(fd,4),guild_id,0);
		}
		else
			mapif_save_guild_storage_ack(fd,RFIFOL(fd,4),guild_id,1);
	}
	return 0;
}

// map server ����̒ʐM
// �E�P�p�P�b�g�̂݉�͂��邱��
// �E�p�P�b�g���f�[�^��inter.c�ɃZ�b�g���Ă�������
// �E�p�P�b�g���`�F�b�N��ARFIFOSKIP�͌Ăяo�����ōs����̂ōs���Ă͂Ȃ�Ȃ�
// �E�G���[�Ȃ�0(false)�A�����łȂ��Ȃ�1(true)���������Ȃ���΂Ȃ�Ȃ�
int inter_storage_parse_frommap(int fd)
{
	RFIFOHEAD(fd);
	switch(RFIFOW(fd,0)){
	case 0x3010: mapif_parse_LoadStorage(fd); break;
	case 0x3011: mapif_parse_SaveStorage(fd); break;
	case 0x3018: mapif_parse_LoadGuildStorage(fd); break;
	case 0x3019: mapif_parse_SaveGuildStorage(fd); break;
	default:
		return 0;
	}
	return 1;
}
#endif //TXT_SQL_CONVERT
