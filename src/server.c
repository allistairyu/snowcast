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
void *client_handler(void *);


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

typedef struct client {
	struct client_data *cd;
	// FILE *file;

	struct client *prev;
	struct client *next;
} client_t;

client_t *thread_list_head;

/*
 * Pulls client from circular doubly-linked thread list
 */
void pull_client(client_t *c) {
    if (c->prev == c) {
        thread_list_head = NULL;
    } else {
        c->prev->next = c->next;
        c->next->prev = c->prev;
    }
    if (c == thread_list_head) {
        thread_list_head = c->prev;
    }
}

/*
 * Inserts client into circular doubly-linked thread list
 */
void insert_client(client_t *c) {
    if (!thread_list_head) {
        c->next = c;
        c->prev = c;
    } else {
        client_t *last = thread_list_head->prev;
        c->next = thread_list_head;
        c->prev = last;
        last->next = c;
        thread_list_head->prev = c;
    }
    thread_list_head = c;
}

void client_constructor(struct client_data *cd) {
    int err;
    if ((err = pthread_create(&cd->thread, 0, client_handler,
                              cd))) {
        // handle_error_en(err, "pthread_create");
		fprintf(stderr, "pthread_create\n");
    }

    if ((err = pthread_detach(cd->thread))) {
		fprintf(stderr, "pthread_detach\n");
        // handle_error_en(err, "pthread_detach");
    }
}

void *client_handler(void *data) {
	// TODO: 
	struct client_data *cd = (struct client_data *)data;
	client_t *client = malloc(sizeof(client_t));
    if (!client) {
        fprintf(stderr, "malloc\n");
        exit(1);
    }
	client->cd = cd;
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
		printf("usage: snowcast_server <tcpport> <file0> [file1] [file2] ...\n");
		return 0;
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
		// call client_constructor here instead
	}
	close(lsocket);

	return 0;
}