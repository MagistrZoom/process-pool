#include <stdio.h>

#include <sys/types.h>
#include <unistd.h>

#include <sys/ipc.h>
#include <sys/msg.h>

#include <sys/socket.h>

#include <errno.h>

#include "../clab5/zassert.h"


#define MAX_WAITING_CONNECTIONS 4

int main(int argc, char *argv[]){

	struct sockaddr_in saddr = {
		.sin_family = AF_INET,
		.sin_port 	= htons(35812),
		.sin_addr = INADDR_ANY
	};

	//message queue used to deliver to workers request handling
	int ipc_key = getuid()+8841;
	int ipc_id = msgget(ipc_key, IPC_EXCL | IPC_CREAT | 0600);
	zassert(ipc_id < 0);

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	zassert(sock < 0 );
	
	int bnd = bind(sock, (struct sockaddr*)&saddr, sizeof(saddr));
	zassert(bnd < 0);

	int ls = listen(sock, MAX_WAITING_CONNECTIONS);
	zassert(ls < 0);

	fd_set readset;

	struct sockaddr_in sock_in = { 0 };
	int slen = sizeof(sock_in);



	return 0;
}
