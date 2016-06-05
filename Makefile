CC=gcc
LD=gcc
CFLAGS=-c -o
LFLAGS=-lsocket -o
SRCS=$(wildcard *.c)

build: ctags server client;

server: server.o
	$(LD) $< $(LFLAGS) $@
client: client.o
	$(LD) $< $(LFLAGS) $@
	
%.o: %.c
	$(CC) $< $(CFLAGS) $@

ctags: $(SRCS)
	$(CC) -M $(SRCS) | gsed -e 's@[\\ ]@\n@g' | \
		gsed -e "/\.o:/d; /^$$/d;" | ctags --fields=+S -L -

clean:                                                                         
	rm -f *.o client server
