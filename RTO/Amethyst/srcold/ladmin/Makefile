COMMON_OBJ = ../common/obj/minicore.o ../common/obj/minisocket.o ../common/obj/timer.o \
	../common/obj/malloc.o ../common/obj/showmsg.o ../common/obj/strlib.o \
	../common/obj/utils.o ../common/obj/md5calc.o
COMMON_H = ../common/core.h ../common/socket.h ../common/timer.h ../common/mmo.h \
	../common/version.h ../common/malloc.h ../common/showmsg.h ../common/strlib.h \
	../common/utils.h ../common/md5calc.h

#####################################################################
.PHONY : all ladmin clean help

all: ladmin

ladmin: ladmin.o $(COMMON_OBJ)
	gcc  -L/usr/lib -o ../../ladmin ladmin.o $(COMMON_OBJ) -lm -lz  -ldl

clean:
	rm -f *.o ../../ladmin

help:
	@echo "possible targets are 'ladmin' 'all' 'clean' 'help'"
	@echo "'ladmin'  - remote administration tool"
	@echo "'all'     - builds all above targets"
	@echo "'clean'   - cleans builds and objects"
	@echo "'help'    - outputs this message"

#####################################################################

# DO NOT DELETE

ladmin.o: ladmin.c ladmin.h $(COMMON_H)
	gcc -g -O2 -pipe -ffast-math -Wall -Wno-sign-compare -Wno-unused-parameter -Wno-pointer-sign  -L/usr/lib  -I../common -I/usr/include -c $(OUTPUT_OPTION) $<
