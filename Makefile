csv2osm.bin: csv2osm.o
	gcc csv2osm.o -o csv2osm -lxml2

csv2osm.o: csv2osm.c
	#gcc -g3 -DDEBUG -c -I/usr/include/libxml2 csv2osm.c -o csv2osm.o
	gcc -O3 -c -I/usr/include/libxml2 csv2osm.c -o csv2osm.o

clean:
	rm csv2osm csv2osm.o
