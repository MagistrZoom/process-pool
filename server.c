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

#include <string.h>

#include <limits.h>

#include <dirent.h>

#include "../clab5/zassert.h"

#define MAX_WAITING_CONNECTIONS 16
#define MIN_WORKERS 1
#define MAX_WORKERS 2

#define CFDS 256


int send_file_descriptor(int socket, int fd_to_send);
int recv_file_descriptor(int socket);
int create_server();
int connect_server();

struct msg_buf {
	int mtype;
	int a;
};

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

int shared_id;
int sock, local_sock;

void free_handler(int sig){
	shmctl(shared_id, IPC_RMID, 0);
	close(sock);
	close(local_sock);
	exit(0);
}

void send_directory_content(int fd, char *dir){
	char buf[32*PATH_MAX] = { 0 };


	struct dirent *dirent;
	DIR *dirp = opendir(dir);
	if(dirp == NULL){
		strcat(buf, "Invalid argument: ");
		strcat(buf, dir);
		strcat(buf, "\n");

		int w = write(fd, buf, strlen(buf));

		return;
	}
	int sz = strlen(dir)+3+1;
	strcat(buf, dir);
	strcat(buf, ":\n");

	while((dirent = readdir(dirp)) != NULL){
		if((sz + strlen(dirent->d_name) + 1) > 32*PATH_MAX){
			printf("Bad directory\n");
			break;
		} else {
		//	if(!strcmp(dirent->d_name, ".."))
		//		continue;
		//	if(!strcmp(dirent->d_name, "."))
		//		continue;
			
			strcat(buf, dirent->d_name);
			strcat(buf, "\n");
		}
	}

	strcat(buf, "\n");

	int wr = write(fd, buf, strlen(buf));

}

void parse_command(int fd){
	char buf[PATH_MAX] = { 0 };

	int rd; 
	while((rd = read(fd, buf, PATH_MAX - 1)) > 0){
		//telnet
		char *cr = strchr(buf, '\r');
		if(cr != NULL){
			*cr = 0;
		}
		send_directory_content(fd, buf);
	}
	zassert(rd < 0);
}

void do_work(int id){
	mutex_lock(&workers_protected->mutex_workerlist);

	workers_protected->list[id].used = 1;
	workers_protected->list[id].is_free = 1;

	mutex_unlock(&workers_protected->mutex_workerlist);
	
	
	//first of all need to became a client of localserver
	int source = connect_server();

	
	mutex_lock(&workers_protected->mutex_free_counter);
	
	workers_protected->free_counter++;

	mutex_unlock(&workers_protected->mutex_free_counter);

	
	printf("Child started\n");
	
	while(1){
		printf("PID #%d is waiting\n", id);
		int recv_fd = recv_file_descriptor(source);
		
		printf("PID #%d received new client fd#%d\n", id, recv_fd);

	
		mutex_lock(&workers_protected->mutex_free_counter);

		workers_protected->free_counter--;

		mutex_unlock(&workers_protected->mutex_free_counter);


		parse_command(recv_fd);	


		int cls = close(recv_fd);
		zassert(cls < 0);


		mutex_lock(&workers_protected->mutex_workerlist);

		workers_protected->list[id].is_free = 1;
		
		mutex_unlock(&workers_protected->mutex_workerlist);
	
		
		mutex_lock(&workers_protected->mutex_free_counter);
		
		//if free_counter + 1 will be greater than MAX_WORKERS, it must die
		if(workers_protected->free_counter + 1 > MAX_WORKERS){
			break;
		}

		workers_protected->free_counter++;

		mutex_unlock(&workers_protected->mutex_free_counter);
		
	}


	mutex_lock(&workers_protected->mutex_workerlist);

	workers_protected->list[id].used = 0;

	mutex_unlock(&workers_protected->mutex_workerlist);



	mutex_unlock(&workers_protected->mutex_free_counter);
		

	printf("Process #%d received kill message. Now %d free processes in the pool\n", 
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
			printf("Instaniate: generated process PID #%d\n", index);
			
			mutex_unlock(&workers_protected->mutex_workerlist);


			do_work(index);
		}
	}
}

int start_tcp(char *addr, int port) {
	struct sockaddr_in saddr = {
		.sin_family = AF_INET,
		.sin_port 	= htons(port),
		.sin_addr.s_addr = inet_addr(addr)
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
			printf("WORKER %d: free %d, used %d, connect %d\n", i, list[i].is_free, list[i].used, list[i].local_fd);
			return i;
		}
	}
	return -1;
}

int main(int argc, char *argv[]) {
	//TODO:
	//rework with usage gethostbyname
	//
	//TODO:
	//Optionally, i can replace shared memory with pipes
	//and poll. But i dont want to do that
	//
	//TODO:
	//Server must able to read large directories
	
	if(argc < 3){
		puts("usage: ./server host port\n");
		return 0;
	}

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

	int shared_key = getuid()+8842;
	shared_id = shmget(shared_key, sizeof(struct workers), IPC_CREAT | IPC_EXCL | 0600);
	zassert(shared_id < 0);

	//init a interprocess mutex and  worker counter protected by them
	workers_protected = shmat(shared_id, NULL, 0);

	zassert(workers_protected < NULL);

	memset(workers_protected, 0, sizeof(struct workers));

	int mut_init = mutex_init(&workers_protected->mutex_free_counter, USYNC_PROCESS, NULL);
	zassert(mut_init < 0);
	mut_init = mutex_init(&workers_protected->mutex_workerlist, USYNC_PROCESS, NULL);
	zassert(mut_init < 0);


	//now just init TCP listener
	int port;
	sscanf(argv[2], "%d", &port);
	sock = start_tcp(argv[1], port);
	
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
		if(current_free == -1)
			first_unused_in_list(workers_protected->list, CFDS);
		
		mutex_unlock(&workers_protected->mutex_workerlist);


		//that is easy. If there is no free processes, just create new one
		if(current_free == -1){
			mutex_lock(&workers_protected->mutex_workerlist);
			int index; 
			while((index = first_unused_in_list(workers_protected->list, CFDS)) == -1){
				sleep(1);
			}
			mutex_unlock(&workers_protected->mutex_workerlist);

			if(fork() == 0){
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

			
		//then push new client to be handled by one of pool's processes
		int process_id;
		mutex_lock(&workers_protected->mutex_workerlist);
		while((process_id = first_free_in_list(workers_protected->list, CFDS)) < 0){
			mutex_unlock(&workers_protected->mutex_workerlist);
			sleep(1);
		}
		
		//make it unfree
		workers_protected->list[process_id].is_free = 0;

		mutex_unlock(&workers_protected->mutex_workerlist);
		
		printf("--- Client #%d pushed to handle by process #%d\n", client_fd, process_id);
		//now process_id is not free
		int sendfd_res = send_file_descriptor(
			workers_protected->list[process_id].local_fd, client_fd);
		zassert(sendfd_res < 0);

		int cls = close(client_fd);
		zassert(cls < 0);
	}

	free_handler(0);

	return 0;
}
