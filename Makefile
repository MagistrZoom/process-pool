CC=gcc
LD=gcc
CFLAGS=-c -o
LFLAGS=-lsocket -o

build: server client;

server: server.o
	$(LD) $< $(LFLAGS) $@
client: client.o
	$(LD) $< $(LFLAGS) $@
	
%.o: %.c
	$(CC) $< $(CFLAGS) $@

clean:                                                                         
	rm -f *.o client server
