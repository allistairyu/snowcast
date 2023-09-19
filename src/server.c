#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <pthread.h>

struct client_data {
	int sock;
	struct sockaddr addr;
	socklen_t addr_size;
	pthread_t thread;
};

struct Welcome {
	char replyType;
	unsigned short numStations;
};

void *client_handler(void *data) {
	struct client_data *cd = (struct client_data *)data;
	printf("Client %d connected!\n", cd->sock);
	char buf[10];	

	if (recv(cd->sock, buf, 3, 0) < 0) {
		perror("recv");
	} else {
		printf("hello received\n");
		int bytes_sent;
		char replyType = 2;
		unsigned short numStations = 2;
		bytes_sent = send(cd->sock, &replyType, 1, 0);
		if (!bytes_sent) {
			perror("send");
			return 0;
		}
		bytes_sent = send(cd->sock, &numStations, 2, 0);
		if (!bytes_sent) {
			perror("send");
			return 0;
		}

	}
	free(data);
	return 0;
}

int main(int argc, char **argv) {

	const char* port = "16800";

	int status;
	struct addrinfo hints;
	struct addrinfo *servinfo, *r;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if ((status = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
		return 2;
	}

	int lsocket; // TODO: necessary to iterate through linked list?
    for(r = servinfo; r != NULL; r = r->ai_next) {
        if ((lsocket = socket(r->ai_family, r->ai_socktype, r->ai_protocol)) < 0) {
			continue;
		}
		if (bind(lsocket, r->ai_addr, r->ai_addrlen) >= 0) {
			break;
		}
		close(lsocket);
    }

	if (r == NULL) {
		fprintf(stderr, "Could not find local interface\n");
		return 1;
	}

	freeaddrinfo(servinfo);

	if (setsockopt(lsocket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) { // https://stackoverflow.com/questions/24194961/how-do-i-use-setsockoptso-reuseaddr
    	perror("setsockopt(SO_REUSEADDR) failed");
	}

	if (listen(lsocket, 20) < 0) {
		perror("listen");
		return 1;
	}
	while (1) {
		int csock;
		struct sockaddr client_addr;
		socklen_t client_len = sizeof(client_addr);

		csock = accept(lsocket, &client_addr, &client_len);
		if (csock == -1) {
			perror("accept");
			return 1;
		}
		struct client_data *cd = (struct client_data *)malloc(sizeof(struct client_data));
		memset(cd, 0, sizeof(struct client_data));
		cd->sock = csock;
		memcpy(&cd->addr, &client_addr, client_len);
		cd->addr_size = client_len;

		pthread_create(&cd->thread, NULL, client_handler, (void*)cd);
	}

	return 0;
}