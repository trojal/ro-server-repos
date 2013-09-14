


#####################################################################
.PHONY : all adduser mapcache clean help

all: adduser mapcache

adduser:
	gcc -o ../../tools/adduser.exe adduser.c

mapcache: obj_mapcache
	gcc -c -o obj_mapcache/grfio.o grfio.c
	gcc -o ../../mapcache.exe mapcache.c obj_mapcache/grfio.o -lz

clean:
	rm -rf *.o obj_mapcache ../../tools/adduser.exe ../../mapcache.exe

help:
	@echo "possible targets are 'adduser' 'mapcache' 'all' 'clean' 'help'"
	@echo "'adduser'   - ???"
	@echo "'mapcache'  - mapcache generator"
	@echo "'all'       - builds all above targets"
	@echo "'clean'     - cleans builds and objects"
	@echo "'help'      - outputs this message"

#####################################################################

obj_mapcache:
	-mkdir obj_mapcache
