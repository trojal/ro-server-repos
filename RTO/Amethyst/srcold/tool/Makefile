#####################################################################
.PHONY : all adduser convert mapcache clean help

all: adduser convert mapcache

adduser:
	gcc -o ../../tools/adduser adduser.c

convert:
	gcc -o ../../tools/convert convert.c

mapcache:
	gcc -c -o grfio.o grfio.c
	gcc -o ../../mapcache mapcache.c grfio.o -lz

clean:
	rm -rf *.o ../../tools/adduser ../../tools/convert ../../mapcache

help:
	@echo "possible targets are 'adduser' 'convert' 'mapcache' 'all' 'clean' 'help'"
	@echo "'adduser'   - ???"
	@echo "'convert'   - ???"
	@echo "'mapcache'  - mapcache generator"
	@echo "'all'       - builds all above targets"
	@echo "'clean'     - cleans builds and objects"
	@echo "'help'      - outputs this message"

#####################################################################
