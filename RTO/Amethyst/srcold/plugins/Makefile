

PLUGINS = sample sig pid gui upnp console

COMMON_H = ../common/plugin.h ../common/cbasetypes.h

#####################################################################
.PHONY : all $(PLUGINS) clean help

all: $(PLUGINS)

sample: sample.so

sig: sig.so

pid: pid.so

gui: gui.so

upnp: upnp.so

console: console.so

clean:
	rm -rf *.o

help:
	@echo "possible targets are $(PLUGINS:%='%') 'all' 'clean' 'help'"
	@echo "'sample'   - sample plugin"
	@echo "'sig'      - signal handler plugin"
	@echo "'pid'      - process id plugin"
	@echo "'gui'      - gui plugin"
	@echo "'upnp'     - upnp plugin"
	@echo "'console'  - console plugin"
	@echo "'all'      - builds all above targets"
	@echo "'clean'    - cleans builds and objects"
	@echo "'help'     - outputs this message"

#####################################################################

%.so: %.c
	gcc -g -O2 -pipe -ffast-math -Wall -Wno-sign-compare -Wno-unused-parameter -Wno-pointer-sign  -I../common -I/usr/include -shared -o ../../plugins/$@ $<

sig.so: sig.c
	@$(MAKE) -C ../common txt
	gcc -g -O2 -pipe -ffast-math -Wall -Wno-sign-compare -Wno-unused-parameter -Wno-pointer-sign  -I../common -I/usr/include -shared -o ../../plugins/$@ $< \
	../common/obj/showmsg.o ../common/obj/utils.o ../common/obj/strlib.o \
	../common/obj/minimalloc.o

gui.so: ../../plugins/gui.conf
upnp.so: ../../plugins/upnp.conf

../../plugins/%.conf: %.txt
	cp -r $< $@

../../plugins/gui.conf: gui.txt
../../plugins/upnp.conf: upnp.txt

# DO NOT DELETE

sample.so: sample.c $(COMMON_H)
sig.so: sig.c $(COMMON_H)
pid.so: pid.c $(COMMON_H)
gui.so: gui.c $(COMMON_H)
#TODO where is upnp.c? [FlavioJS]
#upnp.so: upnp.c $(COMMON_H)
console.so: console.c $(COMMON_H)
