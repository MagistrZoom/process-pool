#include <stdio.h>

#include <sys/types.h>
#include <unistd.h>

#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>

#include <sys/socket.h>

#include <thread.h>
#include <synch.h>

#include <errno.h>

#include <signal.h>

#include "../clab5/zassert.h"

#define MSG_TYPE 1
#define MAX_WAITING_CONNECTIONS 8
#define MIN_WORKERS 4
#define MAX_WORKERS 8

struct msg_buf{
	int mtype;
	int a;
};
struct workers {
	mutex_t mutex_free_counter;
	int free_counter;
};

struct workers *workers_protected;

int ipc_id;
int shared_id;

void free_handler(int sig){
	//TODO:
	//cleanup
	exit(0);
}

void worker(int id){
	//TODO: 
	//if msgrcv returns error because queue is empty and total amount of 
	//childs > MAX_WORKERS, some of them should exit successively by
	//locking mutex and checking protected by them variable
	
	printf("Child started\n");
	
	struct msg_buf rmsg = { 0 };
	
	int ipc_rcv = msgrcv(ipc_id, &rmsg, sizeof(rmsg), MSG_TYPE, 0);
	printf("Time %d\n", rmsg.a);
	printf("Child #%d died\n", id);
	exit(0);
}

void initialize_workers(){
	int i = 0;

	//make base of workers pool
	while(i < MIN_WORKERS){
		if(fork() == 0){ //worker code
			worker(i);
		}
		i++;
	}
}


int main(int argc, char *argv[]){

	struct sigaction f_act = (struct sigaction){
		.sa_flags = SA_RESTART | SA_NOCLDWAIT
	};
	int sig_ret = sigaction(SIGCHLD, &f_act, NULL);
	zassert(sig_ret < 0);

	f_act = (struct sigaction){
		.sa_flags = 0,
		.sa_handler = &free_handler
	};
	sig_ret = sigaction(SIGINT, &f_act, NULL);
	zassert(sig_ret < 0);


	struct sockaddr_in saddr = {
		.sin_family = AF_INET,
		.sin_port 	= htons(35812),
		.sin_addr = INADDR_ANY
	};

	//message queue used to deliver to workers request handling
	int ipc_key = getuid()+8841;
	ipc_id = msgget(ipc_key, IPC_EXCL | IPC_CREAT | 0600);
	zassert(ipc_id < 0);
	
	//TODO:
	//Make a shared memory with amount of workers protected by mutex 
	int shared_key = getuid()+8842;
	int shared_id = shmget(shared_key, sizeof(struct workers), IPC_CREAT | IPC_EXCL | 0600);
	msgassert(shared_id < 0, ipc_id);

	//init a interprocess mutex and  worker counter protected by them
	workers_protected = shmat(shared_id, NULL, 0);

	zassert(workers_protected < NULL);
	int mut_init = mutex_init(&workers_protected->mutex_free_counter, USYNC_PROCESS, NULL);
	workers_protected->free_counter = 0;
	zassert(mut_init < 0);

	//now just init TCP listener
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	msgassert(sock < 0, ipc_id);
	
	int bnd = bind(sock, (struct sockaddr*)&saddr, sizeof(saddr));
	msgassert(bnd < 0, ipc_id);

	int ls = listen(sock, MAX_WAITING_CONNECTIONS);
	msgassert(ls < 0, ipc_id);

	struct sockaddr_in sock_in = { 0 };
	socklen_t slen = sizeof(sock_in);


	initialize_workers();

	while(1){
		int client_fd = accept(sock, (struct sockaddr*)&sock_in, &slen);
		msgassert(client_fd < 0, ipc_id);

		struct msg_buf msg = { 
			.mtype = MSG_TYPE,
			.a = time(NULL)
		};

		int ipc_snd = msgsnd(ipc_id, &msg, sizeof(struct msg_buf), 0);
		msgassert(ipc_snd < 0, ipc_id);
		
		//TODO:
		//there are will be code which sends (protected)free_counter-MAX_WORKERS
		//messages of type 2 (each worker which takes that message must die)
		//
		//
		//
	}

	int ipc_ctl = msgctl(ipc_id, IPC_RMID, 0);
	zassert(ipc_ctl < 0);

	return 0;
}
