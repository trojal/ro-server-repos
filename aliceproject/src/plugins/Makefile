
COMMON_OBJ = ../common/obj_all/showmsg.o ../common/obj_all/utils.o ../common/obj_all/strlib.o \
	../common/obj_all/minimalloc.o
COMMON_H = ../common/plugin.h ../common/cbasetypes.h \
	../common/showmsg.h ../common/utils.h ../common/strlib.h \
	../common/malloc.h

PLUGINS = sample sig pid gui upnp



#####################################################################
.PHONY : all $(PLUGINS) clean help

all: $(PLUGINS)

sample: sample.dll

sig: sig.dll

pid: pid.dll

gui: gui.dll

upnp: upnp.dll

clean:
	rm -rf *.o

help:
	@echo "possible targets are $(PLUGINS:%='%') 'all' 'clean' 'help'"
	@echo "'sample'   - sample plugin"
	@echo "'sig'      - signal handler plugin"
	@echo "'pid'      - process id plugin"
	@echo "'gui'      - gui plugin"
	@echo "'upnp'     - upnp plugin"
	@echo "'all'      - builds all above targets"
	@echo "'clean'    - cleans builds and objects"
	@echo "'help'     - outputs this message"

#####################################################################

%.dll: %.c
	gcc -g -O2 -pipe -ffast-math -Wall -Wno-sign-compare -Wno-unused-parameter -Wno-switch -DHAVE_SETRLIMIT  -I../common -I/usr/include -DFD_SETSIZE=4096 -DCYGWIN  -L/usr/lib -shared -o ../../plugins/$@ $<

gui.dll: ../../plugins/gui.conf
upnp.dll: ../../plugins/upnp.conf

sig.dll: sig.c $(COMMON_OBJ)
	gcc -g -O2 -pipe -ffast-math -Wall -Wno-sign-compare -Wno-unused-parameter -Wno-switch -DHAVE_SETRLIMIT  -I../common -I/usr/include -DFD_SETSIZE=4096 -DCYGWIN  -L/usr/lib -shared -o ../../plugins/$@ $< $(COMMON_OBJ)

../../plugins/%.conf: %.txt
	cp -r $< $@

# missing common object files
../common/obj_all/%.o: ../common/%.c $(COMMON_H)
	@$(MAKE) -C ../common txt

../common/obj_all/mini%.o: ../common/%.c $(COMMON_H)
	@$(MAKE) -C ../common txt
