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
#include <assert.h>

int numStations;

struct client_data {
	int sock;
	struct sockaddr addr;
	socklen_t addrSize;
	pthread_t thread;
	int station;
	int udpPort;
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

void *client_handler(void *);
void pull_client(client_t *, client_t **);
void insert_client(client_t *, client_t **);
void client_constructor(struct client_data *);
void *client_handler(void *);
void print_stations();
void change_station(client_t *, int);
void set_station(client_t *, int);


// array of client_list heads for each station
client_t **clientLists;
// array of mutexes for each client_list head
// TODO: this seems really stupid is there a better way to do this
pthread_mutex_t *clientListMutexes;

pthread_mutex_t bigMutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Pulls client from circular doubly-linked thread list
 * thread_list_head must be locked before this function is called
 */
void pull_client(client_t *c, client_t **head) {
    if (*head == c) {
		*head = c->next;
		if (*head) {
			(*head)->prev = NULL;
		}
		c->next = NULL;
	} else {
		c->next->prev = c->prev;
		c->prev->next = c->next;
	}
}

/*
 * Inserts client into doubly-linked thread list
 * head must be locked before this function is called
 */
void insert_client(client_t *c, client_t **head) {
	if (*head) {
		(*head)->prev = c;
		c->next = *head;
		(*head) = c;
	} else {
		*head = c;
	}
}

/*
 * creates client and thread with given client data
 * mallocs client
 */
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
		free(client); // TODO: any chance thread will free client before this gets called?
		fprintf(stderr, "pthread_create\n");
    }

    if ((err = pthread_detach(client->cd->thread))) {
		free(client);
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
	if ((res = recv(client->cd->sock, buf, 3, 0)) < 0) { //TODO: change from 30
		perror("recv");
	} else {
		int udpPort = (buf[1] << 8) + (buf[2] & 0xFF);
		client->cd->udpPort = udpPort;
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
			int newStation = (buf[1] << 8) + (buf[2] & 0xFF);

			printf("new station is %d\n", newStation);
			if (client->cd->station == -1) {
				printf("set station\n");
				set_station(client, newStation);
			} else {
				printf("change station\n");
				change_station(client, newStation);
			}
			print_stations();
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
	for (int i = 0; i < numStations; i++) {
		printf("%d,%s", i, "station name");
		client_t *c = clientLists[i];
		while (c) {
			printf(",client_addr??:%d", c->cd->udpPort);
			c = c->next;
		}
		printf("\n");
	}
}

void change_station(client_t *client, int newStation) {
	int curStation = client->cd->station;
	// pthread_mutex_lock(&clientListMutexes[curStation]);
	// pthread_mutex_lock(&clientListMutexes[newStation]);

	pthread_mutex_lock(&bigMutex);
	client_t **curStationClient = &clientLists[curStation];
	client_t **newStationClient = &clientLists[newStation];
	pull_client(client, curStationClient);
	insert_client(client, newStationClient);
	client->cd->station = newStation;

	// pthread_mutex_unlock(&clientListMutexes[curStation]);
	// pthread_mutex_unlock(&clientListMutexes[newStation]);


	pthread_mutex_unlock(&bigMutex);

}

void set_station(client_t *client, int station) {
	pthread_mutex_lock(&clientListMutexes[station]);
	client_t **stationClient = &clientLists[station];
	insert_client(client, stationClient);
	client->cd->station = station;
	pthread_mutex_unlock(&clientListMutexes[station]);
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
	clientLists = calloc(numStations, sizeof(client_t));
	clientListMutexes = malloc(numStations * sizeof(pthread_mutex_t));
	if (!clientLists || !clientListMutexes) {
		perror("malloc");
		return 1;
	}
	for (int i = 0; i < numStations; i++) {
		pthread_mutex_init(&clientListMutexes[i], NULL);
	}
	// memset(clientLists, 0, numStations);


	if (listen(lsocket, 20) < 0) {
		perror("listen");
		return 1;
	}

	// Accept connections from clients and create thread for each client
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
		cd->station = -1; // no station selected by default

		// create new client and detached thread
		client_constructor(cd);
	}
	// https://stackoverflow.com/questions/449617/how-should-i-close-a-socket-in-a-signal-handler


	for (int i = 0; i < numStations; i++) {
		pthread_mutex_destroy(&clientListMutexes[i]);
	}
	pthread_mutex_destroy(&bigMutex);
	close(lsocket);

	return 0;
}