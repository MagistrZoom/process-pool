CC=gcc
LD=gcc
CFLAGS=-Wall -Werror -pedantic-errors -c -m64 -o
LFLAGS=-m64 -lsocket -lnsl -o
SRCS=$(wildcard *.c)

build: ctags server client;

server: server.o lserver.o
	$(LD) $^ -lthread $(LFLAGS) $@

client: client.o
	$(LD) $< $(LFLAGS) $@
	
lserver.o: lserver.c
	$(LD) -D_XPG4_2 $< $(CFLAGS) $@

%.o: %.c
	$(CC) $< $(CFLAGS) $@

ctags: $(SRCS)
	$(CC) -M $(SRCS) | gsed -e 's@[\\ ]@\n@g' | \
		gsed -e "/\.o:/d; /^$$/d;" | ctags --fields=+S -L -

clean:                                                                         
	rm -f *.o client server tags t
