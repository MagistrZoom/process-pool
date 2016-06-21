#include <stdio.h>

#include <stdlib.h>

#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h> 
#include <netdb.h>

#include <signal.h>

#include <string.h>

#include <errno.h>

#include <limits.h>

#define PORT 12345

#define zassert(eq) if(eq){ printf("Err on line: %d\n", __LINE__); exit(perr(errno)); }

#define WORKER_DIRECTORY_BUF 32*PATH_MAX

int perr(int err){                                
    char *err_ptr = strerror(err); 
    fprintf(stderr, "%s\n", err_ptr);     
    return err;                                   
}

/* opensource.apple.org */
void * memmem(const void *l, size_t l_len, const void *s, size_t s_len) {
	register char *cur, *last;
	const char *cl = (const char *)l;
	const char *cs = (const char *)s;

	/* we need something to compare */
	if (l_len == 0 || s_len == 0)
		return NULL;

	/* "s" must be smaller or equal to "l" */
	if (l_len < s_len)
		return NULL;

	/* special case where s_len == 1 */
	if (s_len == 1)
		return memchr(l, (int)*cs, l_len);

	/* the last position where its possible to find "s" in "l" */
	last = (char *)cl + l_len - s_len;

	for (cur = (char *)cl; cur <= last; cur++)
		if (cur[0] == cs[0] && memcmp(cur, cs, s_len) == 0)
			return cur;

	return NULL;
}

int sock = -1;

void free_handler(int sig){
	if(sock >= 0){
		close(sock);
	}
	exit(0);
}
int main(int argc, char* argv[]) {
	
	if (argc < 3) { 
		printf("Usage: host dir [dir]\n");
		return 0;
	}
	struct sigaction f_act = (struct sigaction){
		.sa_flags = 0,
		.sa_handler = &free_handler
	};
	int sig_ret = sigaction(SIGINT, &f_act, NULL);
	zassert(sig_ret < 0);

	int port = PORT;
/*	char *ptr;
	port = strtol(argv[2], &ptr, 10);
	if(argv[2] == ptr){
		puts("Invalid port");
		return 1;
	}
*/	

	struct hostent *host = gethostbyname(argv[1]);
	if(host == NULL || *host->h_addr_list == NULL){
		puts("Invalid hostname or unrecognized");
		return 1;
	}

	struct in_addr *target_host = (struct in_addr*)*host->h_addr_list;


	struct sockaddr_in saddr;
	char buf[WORKER_DIRECTORY_BUF];
	int i;
	saddr = (struct sockaddr_in){
		.sin_family = AF_INET,
		.sin_port = htons(port),
		.sin_addr.s_addr = target_host->s_addr
	};

	sock = socket(AF_INET, SOCK_STREAM, 0);
	zassert(sock < 0);

	int cnt = connect(sock, (struct sockaddr *)&saddr, sizeof(saddr));
	if(cnt < 0){
		perror("connect");
		free_handler(-1);
	}

	for (i = 2; i < argc; i++) {
		int wr = write(sock, argv[i], strlen(argv[i]));
		if(wr < 0){
			perror("write dir to server");
			free_handler(-1);
		}
		int rd;
		while((rd = read(sock, buf, WORKER_DIRECTORY_BUF)) > 0){
			wr = write(STDOUT_FILENO, buf, rd);
			if(wr < 0){
				perror("out dir");
				free_handler(-1);
			}
			if(memmem(buf, rd, "\0", 2)){
				break;
			}
		}
		if(rd < 0){
			perror("read dir from server");
			free_handler(-1);
		}
	}
	
	int cls = close(sock); 
	zassert(cls < 0);

	return 0;
}
