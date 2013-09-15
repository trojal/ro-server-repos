


#####################################################################
.PHONY : all adduser convert mapcache clean help

all: adduser convert mapcache

adduser:
	gcc -o ../../tools/adduser adduser.c

convert:
	gcc -o ../../tools/convert convert.c

mapcache: obj_mapcache
	gcc -c -o obj_mapcache/grfio.o grfio.c
	gcc -o ../../mapcache mapcache.c obj_mapcache/grfio.o -lz

clean:
	rm -rf *.o obj_mapcache ../../tools/adduser ../../tools/convert ../../mapcache

help:
	@echo "possible targets are 'adduser' 'convert' 'mapcache' 'all' 'clean' 'help'"
	@echo "'adduser'   - ???"
	@echo "'convert'   - ???"
	@echo "'mapcache'  - mapcache generator"
	@echo "'all'       - builds all above targets"
	@echo "'clean'     - cleans builds and objects"
	@echo "'help'      - outputs this message"

#####################################################################

obj_mapcache:
	-mkdir obj_mapcache
