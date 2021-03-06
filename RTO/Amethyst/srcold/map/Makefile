
HAVE_MYSQL=yes
ifeq ($(HAVE_MYSQL),yes)
	ALL_DEPENDS=txt sql
	SQL_DEPENDS=obj_sql map-server_sql
else
	ALL_TARGET=txt
	SQL_DEPENDS=needs_mysql
endif

HAVE_PCRE=
ifeq ($(HAVE_PCRE),yes)
	PCRE_CFLAGS=-DPCRE_SUPPORT 
else
	PCRE_CFLAGS=
endif


#####################################################################
.PHONY : all txt sql clean help

all: $(ALL_DEPENDS)

txt: obj_txt map-server

sql: $(SQL_DEPENDS)

clean:
	rm -rf *.o ../../map-server ../../map-server_sql obj_txt obj_sql

help:
ifeq ($(HAVE_MYSQL),yes)
	@echo "possible targets are 'sql' 'txt' 'all' 'clean' 'help'"
	@echo "'sql'   - map server (SQL version)"
else
	@echo "possible targets are 'txt' 'all' 'clean' 'help'"
endif
	@echo "'txt'   - map server (TXT version)"
	@echo "'all'   - builds all above targets"
	@echo "'clean' - cleans builds and objects"
	@echo "'help'  - outputs this message"

#####################################################################

needs_mysql:
	@echo "MySQL not found or disabled by the configure script"
	@exit 1

obj_txt:
	-mkdir obj_txt

obj_sql:
	-mkdir obj_sql

COMMON_OBJ = ../common/obj/core.o ../common/obj/socket.o ../common/obj/timer.o \
	../common/obj/db.o ../common/obj/plugins.o ../common/obj/lock.o \
	../common/obj/nullpo.o ../common/obj/malloc.o ../common/obj/showmsg.o \
	../common/obj/utils.o ../common/obj/strlib.o ../common/obj/grfio.o \
	../common/obj/mapindex.o ../common/obj/ers.o

COMMON_H = ../common/core.h ../common/socket.h ../common/timer.h \
	../common/db.h ../common/plugins.h ../common/lock.h \
	../common/nullpo.h ../common/malloc.h ../common/showmsg.h \
	../common/utils.h ../common/strlib.h ../common/grfio.h \
	../common/mapindex.h ../common/ers.h

COMMON_SQL_OBJ = $(COMMON_OBJ) ../common/obj_sql/sql.o

COMMON_SQL_H = $(COMMON_H) ../common/sql.h

MAP_OBJ = obj/map.o obj/chrif.o obj/clif.o obj/pc.o obj/status.o obj/npc.o \
	obj/npc_chat.o obj/chat.o obj/path.o obj/itemdb.o obj/mob.o obj/script.o \
	obj/storage.o obj/skill.o obj/atcommand.o obj/charcommand.o obj/battle.o \
	obj/intif.o obj/trade.o obj/party.o obj/vending.o obj/guild.o obj/pet.o \
	obj/log.o obj/mail.o obj/date.o obj/irc.o obj/unit.o obj/mercenary.o

map-server: $(MAP_OBJ:obj/%=obj_txt/%) $(COMMON_OBJ)
	gcc  -L/usr/lib -o ../../map-server $(MAP_OBJ:obj/%=obj_txt/%) $(COMMON_OBJ) -lm -lz  -ldl 

map-server_sql: $(MAP_OBJ:obj/%=obj_sql/%) $(COMMON_SQL_OBJ)
	gcc  -L/usr/lib -o ../../map-server_sql $(MAP_OBJ:obj/%=obj_sql/%) $(COMMON_SQL_OBJ) -lm -lz  -ldl  -L/usr/lib/mysql -lmysqlclient

obj_txt/%.o: %.c
	gcc -g -O2 -pipe -ffast-math -Wall -Wno-sign-compare -Wno-unused-parameter -Wno-pointer-sign $(PCRE_CFLAGS) -DTXT_ONLY  -I../common -I/usr/include -c $(OUTPUT_OPTION) $<

obj_sql/%.o: %.c
	gcc -g -O2 -pipe -ffast-math -Wall -Wno-sign-compare -Wno-unused-parameter -Wno-pointer-sign $(PCRE_CFLAGS) -I/usr/include/mysql -DBIG_JOINS=1  -I../common -I/usr/include -c $(OUTPUT_OPTION) $<

# DO NOT DELETE

obj_txt/map.o: map.c map.h chrif.h clif.h npc.h pc.h mob.h chat.h skill.h itemdb.h storage.h party.h pet.h atcommand.h irc.h $(COMMON_H)
obj_txt/chrif.o: chrif.c map.h battle.h chrif.h clif.h intif.h pc.h npc.h $(COMMON_H)
obj_txt/clif.o: clif.c map.h chrif.h clif.h mob.h intif.h pc.h npc.h itemdb.h chat.h script.h storage.h party.h guild.h atcommand.h pet.h charcommand.h irc.h $(COMMON_H)
obj_txt/pc.o: pc.c map.h clif.h intif.h pc.h npc.h mob.h itemdb.h battle.h skill.h script.h party.h guild.h pet.h trade.h storage.h chat.h vending.h $(COMMON_H)
obj_txt/status.o: status.c pc.h map.h clif.h status.h mob.h itemdb.h battle.h skill.h script.h pet.h guild.h $(COMMON_H)
obj_txt/npc.o: npc.c map.h npc.h clif.h pc.h script.h mob.h itemdb.h battle.h $(COMMON_H)
obj_txt/npc_chat.o: npc_chat.c map.h npc.h clif.h pc.h script.h mob.h itemdb.h battle.h $(COMMON_H)
obj_txt/chat.o: chat.c map.h clif.h pc.h chat.h $(COMMON_H)
obj_txt/path.o: path.c map.h battle.h $(COMMON_H)
obj_txt/itemdb.o: itemdb.c map.h battle.h itemdb.h $(COMMON_H)
obj_txt/mob.o: mob.c map.h clif.h intif.h pc.h mob.h skill.h battle.h npc.h itemdb.h date.h irc.h $(COMMON_H)
obj_txt/script.o: script.c itemdb.h map.h pc.h mob.h clif.h intif.h npc.h script.h storage.h skill.h pet.h battle.h log.h irc.h $(COMMON_H)
obj_txt/storage.o: storage.c itemdb.h pc.h clif.h intif.h storage.h guild.h $(COMMON_H)
obj_txt/skill.o: skill.c skill.h map.h clif.h pc.h mob.h battle.h itemdb.h script.h date.h $(COMMON_H)
obj_txt/atcommand.o: atcommand.c atcommand.h itemdb.h pc.h map.h skill.h clif.h mob.h intif.h battle.h storage.h guild.h pet.h log.h $(COMMON_H)
obj_txt/battle.o: battle.c battle.h skill.h map.h mob.h pc.h pet.h guild.h $(COMMON_H)
obj_txt/intif.o: intif.c intif.h chrif.h clif.h party.h guild.h storage.h map.h battle.h pet.h $(COMMON_H)
obj_txt/trade.o: trade.c trade.h clif.h itemdb.h map.h pc.h npc.h $(COMMON_H)
obj_txt/party.o: party.c party.h clif.h intif.h pc.h map.h battle.h $(COMMON_H)
obj_txt/vending.o: vending.c vending.h clif.h itemdb.h map.h pc.h irc.h $(COMMON_H)
obj_txt/guild.o: guild.c guild.h storage.h battle.h clif.h intif.h pc.h npc.h map.h $(COMMON_H)
obj_txt/pet.o: pet.c pet.h map.h clif.h chrif.h intif.h pc.h itemdb.h battle.h mob.h npc.h script.h $(COMMON_H)
obj_txt/log.o: log.c log.h map.h $(COMMON_H)
obj_txt/charcommand.o: charcommand.c charcommand.h itemdb.h pc.h map.h skill.h clif.h mob.h intif.h battle.h storage.h guild.h pet.h log.h $(COMMON_H)
obj_txt/date.o: date.c date.h $(COMMON_H)
obj_txt/irc.o: irc.c irc.h map.h pc.h $(COMMON_H)
obj_txt/unit.o: unit.c unit.h $(COMMON_H)
obj_txt/mercenary.o: mercenary.c mercenary.h $(COMMON_H)

obj_sql/map.o: map.c map.h chrif.h clif.h npc.h pc.h mob.h chat.h skill.h itemdb.h storage.h party.h pet.h atcommand.h log.h irc.h $(COMMON_SQL_H)
obj_sql/chrif.o: chrif.c map.h battle.h chrif.h clif.h intif.h pc.h npc.h $(COMMON_SQL_H)
obj_sql/clif.o: clif.c map.h chrif.h clif.h mob.h intif.h pc.h npc.h itemdb.h chat.h script.h storage.h party.h guild.h atcommand.h pet.h charcommand.h $(COMMON_SQL_H)
obj_sql/pc.o: pc.c map.h clif.h intif.h pc.h npc.h mob.h itemdb.h battle.h skill.h script.h party.h guild.h pet.h trade.h storage.h chat.h vending.h log.h $(COMMON_SQL_H)
obj_sql/status.o: status.c pc.h map.h clif.h status.h mob.h itemdb.h battle.h skill.h script.h pet.h guild.h $(COMMON_SQL_H)
obj_sql/npc.o: npc.c map.h npc.h clif.h pc.h script.h mob.h itemdb.h battle.h $(COMMON_SQL_H)
obj_sql/npc_chat.o: npc_chat.c map.h npc.h clif.h pc.h script.h mob.h itemdb.h battle.h $(COMMON_SQL_H)
obj_sql/chat.o: chat.c map.h clif.h pc.h chat.h $(COMMON_SQL_H)
obj_sql/path.o: path.c map.h battle.h $(COMMON_SQL_H)
obj_sql/itemdb.o: itemdb.c map.h battle.h itemdb.h $(COMMON_SQL_H)
obj_sql/mob.o: mob.c map.h clif.h intif.h pc.h mob.h skill.h battle.h npc.h itemdb.h log.h date.h irc.h $(COMMON_SQL_H)
obj_sql/script.o: script.c itemdb.h map.h pc.h mob.h clif.h intif.h npc.h script.h storage.h skill.h pet.h battle.h log.h irc.h $(COMMON_SQL_H)
obj_sql/storage.o: storage.c itemdb.h pc.h clif.h intif.h storage.h guild.h $(COMMON_SQL_H)
obj_sql/skill.o: skill.c skill.h map.h clif.h pc.h mob.h battle.h itemdb.h script.h log.h date.h $(COMMON_SQL_H)
obj_sql/atcommand.o: atcommand.c atcommand.h itemdb.h pc.h map.h skill.h clif.h mob.h intif.h battle.h storage.h guild.h pet.h log.h $(COMMON_SQL_H)
obj_sql/battle.o: battle.c battle.h skill.h map.h mob.h pc.h pet.h guild.h $(COMMON_SQL_H)
obj_sql/intif.o: intif.c intif.h chrif.h clif.h party.h guild.h storage.h map.h battle.h pet.h $(COMMON_SQL_H)
obj_sql/trade.o: trade.c trade.h clif.h itemdb.h map.h pc.h npc.h log.h $(COMMON_SQL_H)
obj_sql/party.o: party.c party.h clif.h intif.h pc.h map.h battle.h $(COMMON_SQL_H)
obj_sql/vending.o: vending.c vending.h clif.h itemdb.h map.h pc.h log.h irc.h $(COMMON_SQL_H)
obj_sql/guild.o: guild.c guild.h storage.h battle.h clif.h intif.h pc.h npc.h map.h irc.h $(COMMON_SQL_H)
obj_sql/pet.o: pet.c pet.h map.h clif.h chrif.h intif.h pc.h itemdb.h battle.h mob.h npc.h script.h $(COMMON_SQL_H)
obj_sql/mail.o: mail.c mail.h $(COMMON_SQL_H)
obj_sql/log.o: log.c log.h map.h $(COMMON_SQL_H)
obj_sql/charcommand.o: charcommand.c charcommand.h itemdb.h pc.h map.h skill.h clif.h mob.h intif.h battle.h storage.h guild.h pet.h log.h $(COMMON_SQL_H)
obj_sql/date.o: date.c date.h $(COMMON_SQL_H)
obj_sql/irc.o: irc.c irc.h map.h pc.h $(COMMON_SQL_H)
obj_sql/unit.o: unit.c unit.h $(COMMON_SQL_H)
obj_sql/mercenary.o: mercenary.c mercenary.h $(COMMON_SQL_H)
