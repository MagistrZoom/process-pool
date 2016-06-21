#include <stdio.h>

#include <stdlib.h>

#include <fcntl.h>

#include <sys/types.h>
#include <unistd.h>

#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <signal.h>


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <thread.h>
#include <synch.h>

#include <errno.h>

#include <string.h>

#include <limits.h>

#include <dirent.h>

#include "zassert.h"

#define MAX_WAITING_CONNECTIONS 32

//MAX_WORKERS MUST NOT BE GREATER THAN CFDS!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!1
//undefined behaviour

#define MIN_WORKERS 2
#define MAX_WORKERS 4
#define CFDS 64

#define WORKER_DIRECTORY_BUF 32*PATH_MAX


int send_file_descriptor(int socket, int fd_to_send);
int recv_file_descriptor(int socket);
int create_server();
int connect_server();

struct worker {
	char used:4;
	char is_free:4;
	char local_fd;
};

struct workers {
	mutex_t mutex_free_counter;
	int free_counter;
	mutex_t mutex_workerlist;
	struct worker list[CFDS];
};

struct workers *workers_protected;

int shared_id = -1;
int sock = -1, local_sock = -1;

void free_handler(int sig){
	if(shared_id >= 0)
		shmctl(shared_id, IPC_RMID, 0);
	if(sock >= 0)
		close(sock);
	if(local_sock >= 0)
		close(local_sock);
	exit(0);
}

void init_signal_handlers(){
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
}

int send_directory_content(int fd, char *dir){
	char buf[WORKER_DIRECTORY_BUF] = { 0 };

	struct dirent *dirent;
	DIR *dirp = opendir(dir);
	if(dirp == NULL){
		strcat(buf, "Invalid argument: ");
		strcat(buf, dir);
		strcat(buf, "\n");
		strcat(buf, "\0\0");

		int w = write(fd, buf, strlen(buf) + 2);
		if(w < 0){
			fputs("FAIL in worker", stderr);
			return 1;
		}

		//not an error, just warning. Dont need to finish worker
		return 0;
	}
	int sz = strlen(dir)+3+1;
	strcat(buf, dir);
	strcat(buf, ":\n");

	while((dirent = readdir(dirp)) != NULL){
		if((sz + strlen(dirent->d_name) + 1) > WORKER_DIRECTORY_BUF){
			//then flush buffer

		
			int wr = write(fd, buf, strlen(buf));
			if(wr < 0){
				fputs("Fail while writing", stderr);
				return 1;
			}

			*buf = 0;
		}
		int len = strlen(dirent->d_name);
		sz += len + 1; //plus \n

		strncat(buf, dirent->d_name, len);
		strcat(buf, "\n");
	}

	int wr = write(fd, buf, strlen(buf));
	if(wr < 0){
		fputs("Fail while writing", stderr);
		return 1;
	}


	wr = write(fd, "\0\0", 2);
	if(wr < 0){
		fputs("Not able to finish transmitting directory", stderr);
		return 1;
	}

	return 0;
}

int parse_command(int fd){
	char buf[PATH_MAX] = { 0 };

	int rd; 
	while((rd = read(fd, buf, PATH_MAX - 1)) > 0){
		buf[rd] = 0;
		//telnet
		char *cr = strchr(buf, '\r');
		if(cr != NULL){
			*cr = 0;
		}
		int ret = send_directory_content(fd, buf);
		if(ret)
			return ret;
	}
	zassert(rd < 0);

	return 0;
}

int first_unused_in_list(struct worker *list, int limit){
	int i;
	for(i = 0; i < limit; i++){
		if(!list[i].used){
			list[i].is_free = 1;
			return i;
		}
	}
	return -1;
}

int first_free_in_list(struct worker *list, int limit){
	int i;
	for(i = 0; i < limit; i++){
		if(list[i].is_free && list[i].used){
			return i;
		}
	}
	return -1;
}


void do_work(int id){
	mutex_lock(&workers_protected->mutex_workerlist);

	workers_protected->list[id].used = 1;
	workers_protected->list[id].is_free = 1;

	mutex_unlock(&workers_protected->mutex_workerlist);
	
	
	//first of all need to became a client of localserver to be able to 
	//handle connections
	int source = connect_server();

	
	mutex_lock(&workers_protected->mutex_free_counter);
	
	workers_protected->free_counter++;

	mutex_unlock(&workers_protected->mutex_free_counter);

	
	printf("Child %d started\n", id);
	
	while(1){
		printf("Worker #%d is waiting\n", id);
		int recv_fd = recv_file_descriptor(source);
		
		printf("Worker #%d received new client fd#%d\n", id, recv_fd);

	
		mutex_lock(&workers_protected->mutex_free_counter);

		workers_protected->free_counter--;

		mutex_unlock(&workers_protected->mutex_free_counter);


		int ret = parse_command(recv_fd);	
		//error occured during interacting with remote client
		if(ret){
			fprintf(stderr, "%d: error\n", __LINE__);
			break;		
		}
		

		int cls = close(recv_fd);
		zassert(cls < 0);

		mutex_lock(&workers_protected->mutex_workerlist);

		workers_protected->list[id].is_free = 1;
		
		mutex_lock(&workers_protected->mutex_free_counter);
		
		//if free_counter + 1 will be greater than MAX_WORKERS, worker must die

		if(workers_protected->free_counter + 1 > MAX_WORKERS){
			//i know that there are duplicate below. There are case when 
			//process is going to die, but it's used==1 and dispatcher can
			//send new client to almost death worker
			workers_protected->list[id].used = 0;
		
			mutex_unlock(&workers_protected->mutex_free_counter);

			mutex_unlock(&workers_protected->mutex_workerlist);

			
			break;
		}

		workers_protected->free_counter++;

		mutex_unlock(&workers_protected->mutex_free_counter);
		

		mutex_unlock(&workers_protected->mutex_workerlist);
	
	}



	mutex_lock(&workers_protected->mutex_workerlist);

	workers_protected->list[id].used = 0;

	mutex_unlock(&workers_protected->mutex_workerlist);


	printf("Worker #%d is going to die. Now %d free processes in the pool\n", 
			id, workers_protected->free_counter);
	exit(0);
}

void initialize_workers() {

	//make base of workers pool
	int i = 0;
	while(i++ < MIN_WORKERS){
		if(fork() == 0){ //worker code
			mutex_lock(&workers_protected->mutex_workerlist);
			
			int index = first_unused_in_list(workers_protected->list, CFDS);
			printf("Instaniate: generated worker PID #%d\n", index);
			
			mutex_unlock(&workers_protected->mutex_workerlist);


			do_work(index);
		}
	}
}

int start_tcp(struct in_addr *addr, int port) {
	struct sockaddr_in saddr = {
		.sin_family = AF_INET,
		.sin_port 	= htons(port),
		.sin_addr.s_addr = addr->s_addr
	};

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	zassert(sock < 0);
	int sopt = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));
    zassert(sopt < 0);

	int bnd = bind(sock, (struct sockaddr*)&saddr, sizeof(saddr));
	zassert(bnd < 0);

	int ls = listen(sock, MAX_WAITING_CONNECTIONS);
	zassert(ls < 0);

	return sock;
}

int main(int argc, char *argv[]) {
	//TODO:
	//select in worker to kill by timeout
	
	if(argc < 3){
		puts("usage: ./server host port\n");
		return 0;
	}

	struct hostent *host = gethostbyname(argv[1]);
	if(host == NULL || *host->h_addr_list == NULL){
		puts("Invalid hostname or unrecognized");
		return 1;
	}

	struct in_addr *target_host = (struct in_addr*)*host->h_addr_list;

	//initialize signal handlers
	init_signal_handlers();

	int shared_key = getuid()+8842;
	shared_id = shmget(shared_key, sizeof(struct workers), IPC_CREAT | IPC_EXCL | 0600);
	zassert(shared_id < 0);

	//init a interprocess mutex and  worker counter protected by them
	workers_protected = shmat(shared_id, NULL, 0);
	zassert(workers_protected == (void*)-1);

	memset(workers_protected, 0, sizeof(struct workers));

	int mut_init = mutex_init(&workers_protected->mutex_free_counter, USYNC_PROCESS, NULL);
	zassert(mut_init < 0);
	mut_init = mutex_init(&workers_protected->mutex_workerlist, USYNC_PROCESS, NULL);
	zassert(mut_init < 0);


	//now just init TCP listener
	int port;
	sscanf(argv[2], "%d", &port);
	sock = start_tcp(target_host, port);
	
	local_sock = create_server();

	//MIN_WORKERS pool
	initialize_workers();

	//accept local socket MIN_WORKER times
	int i;
	for(i = 0; i < MIN_WORKERS; i++){
		struct sockaddr_in local_in = { 0 };
		socklen_t slen = sizeof(struct sockaddr_in);
		workers_protected->list[i].local_fd = accept(local_sock, (struct sockaddr*)&local_in, &slen);
	}

	while(1){
		struct sockaddr_in sock_in = { 0 };
		socklen_t slen = sizeof(struct sockaddr_in);

		int client_fd = accept(sock, (struct sockaddr*)&sock_in, &slen);
		zassert(client_fd < 0);
		printf("### Main loop just got a connection to fd #%d\n", client_fd);
		

		mutex_lock(&workers_protected->mutex_workerlist);

		int current_free = first_free_in_list(workers_protected->list, CFDS);
		
		mutex_unlock(&workers_protected->mutex_workerlist);


		//that is easy. If there is no free processes, just create new one
		int flg = 0;
		if(current_free == -1){
			//All pool's slots used. Waiting for the end of one of the processes
			//or freed processes
			int index; 

			mutex_lock(&workers_protected->mutex_workerlist);
				
			index = first_unused_in_list(workers_protected->list, CFDS);
			//all CFDS threads is on now
			//so just skip instaniate of new process (because pool is full)
			//and wait for the freed worker
			if(index < 0)
				flg = 1;
		
			mutex_unlock(&workers_protected->mutex_workerlist);
			
			//need to create new process
			if(flg) {
				flg = 0;
			} else if(fork() == 0){
				do_work(index);
			} else{
				struct sockaddr_in local_in = { 0 };
				socklen_t slen = sizeof(struct sockaddr_in);
				int lfd = accept(local_sock, (struct sockaddr*)&local_in, &slen);


				mutex_lock(&workers_protected->mutex_workerlist);

				workers_protected->list[index].local_fd = lfd;

				mutex_unlock(&workers_protected->mutex_workerlist);

				printf("Accepted local connect from process #%d\n", index);
			}
		}
			
		int process_id;
		while(1){
			mutex_lock(&workers_protected->mutex_workerlist);

			process_id = first_free_in_list(workers_protected->list, CFDS);
			if(process_id >= 0)
				break;
			
			mutex_unlock(&workers_protected->mutex_workerlist);
		
			//to be honest, it's a hack, i think. At this time i have no idea
			//about some scheme of selecting free worker without sleep
			//async mb
			usleep(60000);
		}
		
		//make it unfree before dispatch
		workers_protected->list[process_id].is_free = 0;

		mutex_unlock(&workers_protected->mutex_workerlist);
		
		printf("--- Client #%d passed to handle by process #%d\n", client_fd, process_id);
		int sendfd_res = send_file_descriptor(
			workers_protected->list[process_id].local_fd, client_fd);
		zassert(sendfd_res < 0);

		int cls = close(client_fd);
		zassert(cls < 0);
	}

	free_handler(0);

	return 0;
}
