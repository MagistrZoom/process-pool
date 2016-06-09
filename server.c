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

#define MSG_ALERT_TYPE 1
#define MSG_KILL_TYPE 2

#define MAX_WAITING_CONNECTIONS 8
#define MIN_WORKERS 1
#define MAX_WORKERS 2

struct msg_buf{
	int mtype;
	int a;
};
struct workers {
	mutex_t mutex_global_id;
	int global_id;
	mutex_t mutex_free_counter;
	int free_counter;
};

struct workers *workers_protected;

int ipc_id;
int shared_id;
int sock;

void free_handler(int sig){
	//TODO:
	//cleanup
	msgctl(ipc_id, IPC_RMID, 0);
	shmctl(shared_id, IPC_RMID, 0);
	close(sock);
	exit(0);
}

void worker(int id, int fd){
	mutex_unlock(&workers_protected->mutex_global_id);

	int previous_accept = fd>0?1:0;

//	printf("Free counter mutex in #%d before %d lock free counter\n", id, __LINE__);
	mutex_lock(&workers_protected->mutex_free_counter);
	
	workers_protected->free_counter++;

//	printf("Free counter mutex in #%d before %d unlock free counter\n", id, __LINE__);
	mutex_unlock(&workers_protected->mutex_free_counter);
	
	printf("Child started\n");
	
	struct msg_buf rmsg = { 0 };
	
	struct sockaddr_in sock_in = { 0 };
	socklen_t slen = sizeof(sock_in);

	while(1){
		if(previous_accept == 0){
			//it is old process, so he will receive connections by his own
			//accept
			printf("In-pool child #%d listening for a connection\n", id);
			fd = accept(sock, (struct sockaddr*)&sock_in, &slen); 
		} else {
			printf("New child #%d listening got a connection\n", id);
			previous_accept = 0; //or new created by pool-controller
		}

		struct msg_buf msg = {
			.mtype = MSG_ALERT_TYPE
		};
		int ipc_snd = msgsnd(ipc_id, &msg, sizeof(struct msg_buf), 0);
		zassert(ipc_snd < 0);
		
		//lock mutex&&decrement free_counter
		//printf("Free counter mutex in #%d before %d lock free counter\n", id, __LINE__);
		mutex_lock(&workers_protected->mutex_free_counter);

		workers_protected->free_counter--;

		//printf("Free counter mutex in #%d before %d unlock free counter\n", id, __LINE__);
		mutex_unlock(&workers_protected->mutex_free_counter);

		sleep(1);

		char buf[256] = { 0 };
		sprintf(buf, "Time %d", time(NULL));
		puts(buf);
		ssize_t written = send(fd, buf, strlen(buf), 0);
		zassert(written < 0);

		int ipc_rcv = msgrcv(ipc_id, &rmsg, sizeof(rmsg), MSG_KILL_TYPE, IPC_NOWAIT);
		if(ipc_rcv != 0 && rmsg.mtype == MSG_KILL_TYPE){
			//printf("Free counter mutex in #%d before %d lock free counter\n", id, __LINE__);
			mutex_lock(&workers_protected->mutex_free_counter);

			workers_protected->free_counter--;

			//printf("Free counter mutex in #%d before %d unlock free counter\n", id, __LINE__);
			mutex_unlock(&workers_protected->mutex_free_counter);

			break;
		}
		
		//printf("Free counter mutex in #%d before %d lock free counter\n", id, __LINE__);
		mutex_lock(&workers_protected->mutex_free_counter);

		workers_protected->free_counter++;

		//printf("Free counter mutex in #%d before %d unlock free counter\n", id, __LINE__);
		mutex_unlock(&workers_protected->mutex_free_counter);
	}

	printf("Process #%d received kill message. Now %d free processes in the pool\n", 
			id, workers_protected->free_counter);
	exit(0);
}

void initialize_workers(){

	//make base of workers pool
	
	mutex_lock(&workers_protected->mutex_global_id);
	workers_protected->global_id = 0;

	while(workers_protected->global_id < MIN_WORKERS){
		mutex_unlock(&workers_protected->mutex_global_id);

		mutex_lock(&workers_protected->mutex_global_id);
		if(fork() == 0){ //worker code
			worker(workers_protected->global_id, -1);
		}
		mutex_lock(&workers_protected->mutex_global_id);
		workers_protected->global_id++;
	}
	
	mutex_unlock(&workers_protected->mutex_global_id);
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

	//message queue used to deliver to workers request handling
	int ipc_key = getuid()+8841;
	ipc_id = msgget(ipc_key, IPC_EXCL | IPC_CREAT | 0600);
	zassert(ipc_id < 0);
	
	int shared_key = getuid()+8842;
	shared_id = shmget(shared_key, sizeof(struct workers), IPC_CREAT | IPC_EXCL | 0600);
	msgassert(shared_id < 0, ipc_id);

	//init a interprocess mutex and  worker counter protected by them
	workers_protected = shmat(shared_id, NULL, 0);

	zassert(workers_protected < NULL);
	int mut_init = mutex_init(&workers_protected->mutex_free_counter, USYNC_PROCESS, NULL);
	zassert(mut_init < 0);
	mut_init = mutex_init(&workers_protected->mutex_global_id, USYNC_PROCESS, NULL);
	zassert(mut_init < 0);
	workers_protected->free_counter = 0;
	workers_protected->global_id = 0;

	struct sockaddr_in saddr = {
		.sin_family = AF_INET,
		.sin_port 	= htons(35812),
		.sin_addr = INADDR_ANY
	};

	//now just init TCP listener
	sock = socket(AF_INET, SOCK_STREAM, 0);
	msgassert(sock < 0, ipc_id);
	int sopt = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));
    msgassert(sopt < 0, ipc_id);
	
	int bnd = bind(sock, (struct sockaddr*)&saddr, sizeof(saddr));
	msgassert(bnd < 0, ipc_id);

	int ls = listen(sock, MAX_WAITING_CONNECTIONS);
	msgassert(ls < 0, ipc_id);

	//MIN_WORKERS pool
	initialize_workers();

	while(1){
		struct msg_buf msg = { 0 };
		int ipc_rcv = msgrcv(ipc_id, &msg, sizeof(msg), MSG_ALERT_TYPE, 0);

		//printf("Free counter mutex in #%d before %d lock free counter\n", 0, __LINE__);
		mutex_lock(&workers_protected->mutex_free_counter);

		int current_free = workers_protected->free_counter;

		//printf("Free counter mutex in #%d before %d unlock free counter\n", 0, __LINE__);
		mutex_unlock(&workers_protected->mutex_free_counter);

		printf("There are %d free processes in pool\n", current_free); 

		if(current_free == 0){		
			struct sockaddr_in sock_in = { 0 };
			socklen_t slen = sizeof(struct sockaddr_in);

			int client_fd = accept(sock, (struct sockaddr*)&sock_in, &slen);
			msgassert(client_fd < 0, ipc_id);
			printf("Main loop just got a connection\n");
		
		
			mutex_lock(&workers_protected->mutex_global_id);
			workers_protected->global_id++;
			if(fork() == 0) {
				worker(workers_protected->global_id, client_fd);
			}
		} 
		else {
			printf("Need we kill excess processes? %d instead of %d\n", current_free, MAX_WORKERS);
			mutex_lock(&workers_protected->mutex_free_counter);

			current_free = workers_protected->free_counter;
			workers_protected->free_counter = current_free>MAX_WORKERS?MAX_WORKERS:current_free;

			mutex_lock(&workers_protected->mutex_free_counter);
			
			msg.mtype = MSG_KILL_TYPE;
			while(current_free > MAX_WORKERS){
				int ipc_snd = msgsnd(ipc_id, &msg, sizeof(struct msg_buf), 0);
				msgassert(ipc_snd < 0, ipc_id);

				current_free--;
			}
		}
	}

	int ipc_ctl = msgctl(ipc_id, IPC_RMID, 0);
	zassert(ipc_ctl < 0);

	return 0;
}
