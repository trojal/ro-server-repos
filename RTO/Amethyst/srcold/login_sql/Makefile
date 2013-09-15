COMMON_OBJ = ../common/obj/core.o ../common/obj/socket.o ../common/obj/timer.o \
	../common/obj/db.o ../common/obj/plugins.o ../common/obj/lock.o \
	../common/obj/malloc.o ../common/obj/showmsg.o ../common/obj/utils.o \
	../common/obj/strlib.o ../common/obj/grfio.o ../common/obj/mapindex.o \
	../common/obj/ers.o ../common/obj/md5calc.o ../common/obj_sql/sql.o 
COMMON_H = ../common/core.h ../common/socket.h ../common/timer.h ../common/mmo.h \
	../common/version.h ../common/db.h ../common/plugins.h ../common/lock.h \
	../common/malloc.h ../common/showmsg.h ../common/utils.h ../common/strlib.h \
	../common/grfio.h ../common/mapindex.h \
	../common/ers.h ../common/md5calc.h  ../common/sql.h 

HAVE_MYSQL=yes
ifeq ($(HAVE_MYSQL),yes)
	LOGIN_SERVER_SQL_DEPENDS=login.o $(COMMON_OBJ)
else
	LOGIN_SERVER_SQL_DEPENDS=needs_mysql
endif

#####################################################################
.PHONY : all login-server_sql clean help

all: login-server_sql

login-server_sql: $(LOGIN_SERVER_SQL_DEPENDS)
	gcc  -L/usr/lib -o ../../login-server_sql login.o $(COMMON_OBJ) -lm -lz  -ldl -L/usr/lib/mysql -lmysqlclient

clean:
	rm -f *.o ../../login-server_sql

help:
	@echo "possible targets are 'login-server_sql' 'all' 'clean' 'help'"
	@echo "'login-server_sql'  - login server (SQL version)"
	@echo "'all'               - builds all above targets"
	@echo "'clean'             - cleans builds and objects"
	@echo "'help'              - outputs this message"

#####################################################################

needs_mysql:
	@echo "MySQL not found or disabled by the configure script"
	@exit 1

# DO NOT DELETE

login.o: login.c login.h $(COMMON_H)
	gcc -g -O2 -pipe -ffast-math -Wall -Wno-sign-compare -Wno-unused-parameter -Wno-pointer-sign -I/usr/include/mysql -DBIG_JOINS=1  -I../common -I/usr/include -c $(OUTPUT_OPTION) $<
