#include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <errno.h>

#include "../clab5/zassert.h"

int main(void){
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	zassert(sock < 0);

	struct sockaddr_in saddr = {
		.sin_family = AF_INET,
		.sin_port 	= htons(35812),
		.sin_addr = INADDR_ANY
	};

	int con = connect(sock, (struct sockaddr*)&saddr, sizeof(saddr));
	zassert(con < 0);

	printf("Connected\n");
	return 0;
}
