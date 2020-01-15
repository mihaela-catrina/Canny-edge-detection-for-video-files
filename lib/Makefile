CC = gcc
CFLAGS = -g -Wall -Wno-deprecated-declarations
LDFLAGS = -lavformat -lavcodec -lavutil -lswscale -lm
INCLUDE_DIRS = -I/usr/include/ffmpeg

build: libde.so

de.o: de.c
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -fPIC -c $^ -o $@

libde.so: de.o
	$(CC) -shared $^ -o $@ $(LDFLAGS)

fep: libde_fep.so

de_fep.o: de.c
	$(CC) $(CFLAGS) -I$(shell pwd)/../ffmpeg -fPIC -c $^ -o $@

libde_fep.so: de_fep.o
	$(CC) -L$(shell pwd)/../libraries -Wl,-rpath=$(shell pwd)/../libraries $(LDFLAGS) -shared $^ -o  $@

clean:
	rm -rf de.o libde.so de_fep.o libde_fep.so
