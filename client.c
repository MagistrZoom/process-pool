#include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h> 

#include <string.h>

#include <errno.h>

#include <limits.h>

#include "../clab5/zassert.h"


int main(int argc, char* argv[]) {
	
	if (argc < 4) { 
		printf("Usage: host port dir [dir]\n");
		return 0;
	}
	int port;
	int r = sscanf(argv[2], "%d", &port);
	if(!r){
		printf("Port must be integer\n");
		return 2;
	}


	struct sockaddr_in saddr;
	char buf[32*PATH_MAX], dirbuf[32*PATH_MAX];
	int sock, i;
	saddr = (struct sockaddr_in){
		.sin_family = AF_INET,
		.sin_port = htons(port),
		.sin_addr.s_addr = inet_addr(argv[1])
	};

	sock = socket(AF_INET, SOCK_STREAM, 0);
	zassert(sock < 0);

	int cnt = connect(sock, (struct sockaddr *)&saddr, sizeof(saddr));
	zassert(cnt < 0);

	for (i = 3; i < argc; i++) {
		int wr = write(sock, argv[i], strlen(argv[i]));
		printf("Client sent: %s\n", argv[i]);
		zassert(wr < 0);
		int rd = read(sock, buf, 32*PATH_MAX);
		zassert(rd < 0);
		wr = write(STDOUT_FILENO, buf, rd);
		zassert(wr < 0);
	}
	
	int cls = close(sock); 
	zassert(cls < 0);

	return 0;
}
