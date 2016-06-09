#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <sys/types.h>

#include <unistd.h>
#include <fcntl.h>


#include <stdio.h>

#define SOCKET_PATH "/tmp/izoomko"

#define MAX_PENDING 2

int send_file_descriptor(int socket, int fd_to_send) {
	struct msghdr message;
	struct iovec iov[1];
	struct cmsghdr *control_message = NULL;
	char ctrl_buf[CMSG_SPACE(sizeof(int))];
	
	memset(&message, 0, sizeof(struct msghdr));
	memset(ctrl_buf, 0, CMSG_SPACE(sizeof(int)));
	
	char data[1] = { ' ' };
	iov[0].iov_base = data;
	iov[0].iov_len = sizeof(data);
	
	message.msg_name = NULL;
	message.msg_namelen = 0;
	message.msg_iov = iov;
	message.msg_iovlen = 1;
	message.msg_controllen =  CMSG_SPACE(sizeof(int));
	message.msg_control = ctrl_buf;
	
	control_message = CMSG_FIRSTHDR(&message);
	control_message->cmsg_level = SOL_SOCKET;
	control_message->cmsg_type = SCM_RIGHTS;
	control_message->cmsg_len = CMSG_LEN(sizeof(int));
	
	*((int *) CMSG_DATA(control_message)) = fd_to_send;
	
	return sendmsg(socket, &message, 0);
}

int create_server() {
	struct sockaddr_un addr;
	int fd;
	
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		puts("Failed to create server socket");
		return fd;
	}
	
	memset(&addr, 0, sizeof(addr));
	
	addr.sun_family = AF_UNIX;
	unlink(SOCKET_PATH);
	strcpy(addr.sun_path, SOCKET_PATH);
	
	if (bind(fd, (struct sockaddr *) &(addr), sizeof(addr)) < 0) {
		puts("Failed to bind server socket");
		return -1;
	}
	
	if (listen(fd, MAX_PENDING) < 0) {
		puts("Failed to listen on server socket");
		return -1;
	}
	
	return fd;
}

int connect_server() {
	struct sockaddr_un addr;
	int fd;
	
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		puts("Failed to create client socket");
		return fd;
	}
	
	memset(&addr, 0, sizeof(addr));
	
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, SOCKET_PATH);
	
	if (connect(fd, (struct sockaddr *) &(addr), sizeof(addr)) < 0) {
		puts("Failed to connect to server");
		return -1;
	}
	
	return fd;
}

int recv_file_descriptor(int socket) {
	int sent_fd;
	struct msghdr message;
	struct iovec iov[1];
	struct cmsghdr *control_message = NULL;
	char ctrl_buf[CMSG_SPACE(sizeof(int))];
	int res;
	
	memset(&message, 0, sizeof(struct msghdr));
	memset(ctrl_buf, 0, CMSG_SPACE(sizeof(int)));
	
	message.msg_name = NULL;
	message.msg_namelen = 0;
	message.msg_control = ctrl_buf;
	message.msg_controllen = CMSG_SPACE(sizeof(int));
	message.msg_iov = iov;
	message.msg_iovlen = 1;
	
	if((res = recvmsg(socket, &message, 0)) <= 0)
		return res;
	
	for(control_message = CMSG_FIRSTHDR(&message);
	    control_message != NULL;
	    control_message = CMSG_NXTHDR(&message, control_message)) {
		if((control_message->cmsg_level == SOL_SOCKET) &&
	    	(control_message->cmsg_type == SCM_RIGHTS)) {
	  		return *((int *) CMSG_DATA(control_message));
		}
	}
	
	return -1;
}
