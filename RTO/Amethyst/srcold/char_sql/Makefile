COMMON_OBJ = ../common/obj/core.o ../common/obj/socket.o ../common/obj/timer.o \
	../common/obj/db.o ../common/obj/plugins.o ../common/obj/lock.o \
	../common/obj/malloc.o ../common/obj/showmsg.o ../common/obj/utils.o \
	../common/obj/strlib.o ../common/obj/grfio.o \
	../common/obj/mapindex.o ../common/obj/ers.o ../common/obj_sql/sql.o
COMMON_H = ../common/core.h ../common/socket.h ../common/timer.h ../common/mmo.h \
	../common/version.h ../common/db.h ../common/plugins.h ../common/lock.h \
	../common/malloc.h ../common/showmsg.h ../common/utils.h \
	../common/strlib.h ../common/grfio.h \
	../common/mapindex.h ../common/ers.h ../common/sql.h

CHAR_OBJ = char.o inter.o int_party.o int_guild.o int_storage.o int_pet.o int_homun.o

HAVE_MYSQL=yes
ifeq ($(HAVE_MYSQL),yes)
	CHAR_SERVER_SQL_DEPENDS=$(CHAR_OBJ) $(COMMON_OBJ)
else
	CHAR_SERVER_SQL_DEPENDS=needs_mysql
endif

#####################################################################
.PHONY : all char-server_sql clean help

all: char-server_sql

char-server_sql: $(CHAR_SERVER_SQL_DEPENDS)
	gcc  -L/usr/lib -o ../../char-server_sql $(CHAR_OBJ) $(COMMON_OBJ) -lm -lz  -ldl -L/usr/lib/mysql -lmysqlclient

clean:
	rm -f *.o ../../char-server_sql

help:
	@echo "possible targets are 'char-server_sql' 'all' 'clean' 'help'"
	@echo "'char-server_sql'  - char server (SQL version)"
	@echo "'all'              - builds all above targets"
	@echo "'clean'            - cleans builds and objects"
	@echo "'help'             - outputs this message"

#####################################################################

needs_mysql:
	@echo "MySQL not found or disabled by the configure script"
	@exit 1

%.o: %.c
	gcc -g -O2 -pipe -ffast-math -Wall -Wno-sign-compare -Wno-unused-parameter -Wno-pointer-sign -I/usr/include/mysql -DBIG_JOINS=1  -I../common -I/usr/include -c $(OUTPUT_OPTION) $<

# DO NOT DELETE

char.o: char.c char.h $(COMMON_H)
inter.o: inter.c inter.h int_party.h int_guild.h int_storage.h int_pet.h int_homun.h $(COMMON_H)
int_party.o: int_party.c int_party.h inter.h $(COMMON_H)
int_guild.o: int_guild.c int_guild.h inter.h $(COMMON_H)
int_storage.o: int_storage.c int_storage.h char.h $(COMMON_H)
int_pet.o: int_pet.c int_pet.h inter.h char.h $(COMMON_H)
int_homun.o: int_homun.c int_homun.h inter.h char.h $(COMMON_H)
