
COMMON_OBJ = ../common/obj_all/minicore.o ../common/obj_all/minisocket.o ../common/obj_all/timer.o \
	../common/obj_all/malloc.o ../common/obj_all/showmsg.o ../common/obj_all/strlib.o \
	../common/obj_all/utils.o ../common/obj_all/md5calc.o
COMMON_H = ../common/core.h ../common/socket.h ../common/timer.h ../common/mmo.h \
	../common/version.h ../common/malloc.h ../common/showmsg.h ../common/strlib.h \
	../common/utils.h ../common/md5calc.h ../common/cbasetypes.h

LADMIN_OBJ = obj_all/ladmin.o
LADMIN_H = ladmin.h



#####################################################################
.PHONY : all ladmin clean help

all: ladmin

ladmin: obj_all $(LADMIN_OBJ) $(COMMON_OBJ)
	gcc  -L/usr/lib -o ../../ladmin $(LADMIN_OBJ) $(COMMON_OBJ) -lrt -lm -lz  -ldl

clean:
	rm -rf *.o obj_all ../../ladmin

help:
	@echo "possible targets are 'ladmin' 'all' 'clean' 'help'"
	@echo "'ladmin'  - remote administration tool"
	@echo "'all'     - builds all above targets"
	@echo "'clean'   - cleans builds and objects"
	@echo "'help'    - outputs this message"

#####################################################################

obj_all:
	-mkdir obj_all

obj_all/%.o: %.c $(LADMIN_H) $(COMMON_H)
	gcc -g -O2 -pipe -ffast-math -Wall -Wno-sign-compare -Wno-unused-parameter -Wno-switch $(CUSTOM_CFLAGS)  -L/usr/lib  -I../common -I/usr/include -c $(OUTPUT_OPTION) $<

# missing common object files
../common/obj_all/%.o:
	@$(MAKE) -C ../common txt

../common/obj_all/mini%.o:
	@$(MAKE) -C ../common txt
