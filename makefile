CC=gcc
CMYSQL=-I/usr/include/mysql -fabi-version=2 -fno-omit-frame-pointer
LMYSQL=-L/usr/lib/x86_64-linux-gnu -lmysqlclient -lpthread -lm -lrt -ldl
CFLAGS=-c $(CMYSQL)
LDFLAGS=-lcurses -lmenu $(LMYSQL)

all: mus

mus: CLI-music.o
	$(CC) CLI-music.o -o mus $(LDFLAGS)

CLI-music.o: CLI-music.c
	$(CC) $(CFLAGS) CLI-music.c

clean:
	rm *.o
