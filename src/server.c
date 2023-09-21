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
	socklen_t addrSize;
	pthread_t thread;
	int curStation;
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

// array of client_list heads for each station
client_t **clientLists;
// array of mutexes for each client_list head
// TODO: this seems really stupid is there a better way to do this
pthread_mutex_t *clientListMutexes;

/*
 * Pulls client from circular doubly-linked thread list
 */
void pull_client(client_t *c, client_t *thread_list_head) {
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
void insert_client(client_t *c, client_t *thread_list_head) {
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
	client_t *client = malloc(sizeof(client_t));
	if (!client) {
        fprintf(stderr, "malloc\n");
        exit(1);
    }
	client->cd = cd;

    int err;
    if ((err = pthread_create(&client->cd->thread, NULL, client_handler, (void*)client))) {
        // handle_error_en(err, "pthread_create");
		fprintf(stderr, "pthread_create\n");
    }

    if ((err = pthread_detach(client->cd->thread))) {
		fprintf(stderr, "pthread_detach\n");
        // handle_error_en(err, "pthread_detach");
    }
}

/*
 * start_routine for client
 * input is client_t malloc'ed in client_constructor
 * created and detached in client_constructor
 */
void *client_handler(void *c) {
	client_t *client = (client_t *)c;
    
	printf("Client connected!\n");
	char buf[30] = {0};
	int res;
	if ((res = recv(client->cd->sock, buf, 30, 0)) < 0) { //TODO: change from 30
		perror("recv");
	} else {
		int bytes_sent;
		struct Welcome msg = {2, htons(numStations)};

		bytes_sent = send(client->cd->sock, &msg, 3, 0);
		if (!bytes_sent) {
			perror("send");
			return 0;
		}
	}

	while (1) {
		if ((res = recv(client->cd->sock, buf, 30, 0)) < 0) { // TODO: change from 30
			perror("recv");
		} else if (res == 0) {
			printf("client closed connection\n");
			break;
		} else {
			int newStation = (buf[1] << 8) + buf[2];
			printf("new station is %d\n", newStation);
		}
	}


	free(client);
	return 0;
}

void handle_station(void *) {
	// int udp_socket;
	// struct addrinfo udp_hints;
	// struct addrinfo *result;

	// memset(&udp_hints, 0, sizeof(udp_hints));
	// udp_hints.ai_family = AF_INET;
	// udp_hints.ai_socktype = SOCK_DGRAM;
	// udp_hints.ai_flags = AI_PASSIVE;

	// int err;
	// if ((err = getaddrinfo(NULL, )))
}

void print_stations() {

}

void change_station(client_t *client, int newStation) {
	int curStation = client->cd->curStation;
	// lock mutex for client's cur station
	client_t *curStationClient = clientLists[curStation];
	// lock mutex for new station?
	client_t *newStationClient = clientLists[newStation];
	pull_client(client, curStationClient);
	insert_client(client, newStationClient);

	// unlock mutex for new station?

	// unlock mutex for client's 
}

void create_station() {

}

int main(int argc, char **argv) {

	if (argc < 3) {
		printf("usage: snowcast_server <tcpport> <file0> [file1] [file2] ...\n");
		return 0;
	}

	const char* port = argv[1];
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

	int lsocket;
    for (r = servinfo; r != NULL; r = r->ai_next) {
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

	// allocate memory for client lists and their respective mutexes
	clientLists = malloc(numStations * sizeof(client_t));
	if (!clientLists) {
		perror("malloc");
		return 1;
	}
	for (int i = 0; i < numStations; i++) {
		
	}


	if (listen(lsocket, 20) < 0) {
		perror("listen");
		return 1;
	}

	// Accept messages from clients and create thread for each client
	// TODO: does this need to be in new thread to allow for server repl?
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
		if (!cd) {
			perror("malloc");
			return 1;
		}
		memset(cd, 0, sizeof(struct client_data));
		cd->sock = csock;
		memcpy(&cd->addr, &client_addr, client_len);
		cd->addrSize = client_len;
		cd->curStation = -1; // no station selected by default

		// create new client and detached thread
		client_constructor(cd);
	}
	// https://stackoverflow.com/questions/449617/how-should-i-close-a-socket-in-a-signal-handler
	close(lsocket);

	return 0;
}