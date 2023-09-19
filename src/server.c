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

int numStations;

struct client_data {
	int sock;
	struct sockaddr addr;
	socklen_t addr_size;
	pthread_t thread;
};

struct Welcome {
	uint8_t replyType;
	uint16_t numStations;
} __attribute__((packed));

void *client_handler(void *data) {
	struct client_data *cd = (struct client_data *)data;
	printf("Client connected!\n");
	char buf[30] = {0};
	int res;
	if ((res = recv(cd->sock, buf, 30, 0)) < 0) {
		perror("recv");
	} else {
		int bytes_sent;
		struct Welcome msg = {2, htons(numStations)};

		bytes_sent = send(cd->sock, &msg, 3, 0);
		if (!bytes_sent) {
			perror("send");
			return 0;
		}

	}
	free(data);
	return 0;
}

int main(int argc, char **argv) {

	if (argc < 3) {
		fprintf(stderr, "Not enough arguments\n");
		return 1;
	}

	const char* port = argv[1];
	// char *files[argc - 2];
	// for (int i = 2; i < argc; i++) {
	// 	files[i] = malloc(sizeof(argv[i]));
	// 	memcpy(files[i], argv[i], strlen(argv[i]));
	// }
	numStations = argc - 2;

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
	close(lsocket);

	return 0;
}