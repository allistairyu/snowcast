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
#include <signal.h>

int numStations;
const int BUFLEN = 256;

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

struct GeneralMessage {
	uint8_t replyType;
	uint8_t size;
	char *content;
} __attribute__((packed));

typedef struct Client {
	struct client_data *cd;
	// FILE *file;

	struct Client *prev;
	struct Client *next;
} client_t;

typedef struct Station {
	FILE *file;
	int id;
	const char* name;
	int udpSocket;
} station_t;

void pull_client(client_t *, client_t **);
void insert_client(client_t *, client_t **);
void client_constructor(struct client_data *);
void *client_handler(void *);
void print_stations();
void change_station(client_t *, int);
void set_station(client_t *, int);
void *station_handler(void *);
int parse(char[1024], char *[512]);
int set_up_socket(int, const char*);


// array of clientList heads for each station
client_t **clientLists;
// array of mutexes for each clientLists head
pthread_mutex_t *clientListMutexes;
// array of stations
station_t *stations;

/*
 * Pulls client from doubly-linked client list
 * thread_list_head must be locked before this function is called
 */
void pull_client(client_t *c, client_t **head) {
    if (*head == c) {
		*head = (*head)->next;
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
 * Inserts client into doubly-linked client list
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
	client->next = NULL;
	client->prev = NULL;

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

void *monitor_signal(void *arg) {
    // TODO: Wait for a SIGINT to be sent to the server process and cancel
    // all client threads when one arrives.

    sigset_t *s = arg;
    int sig;
    while (1) {
        // wait for SIGINT
        sigwait(s, &sig);

        printf("SIGINT received, cancelling all clients\n");
        // pthread_mutex_lock(&sc.server_mutex);
        // delete_all();
        // pthread_mutex_unlock(&sc.server_mutex);
    }

    return NULL;
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
		printf("udpPort is %d\n", udpPort);
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
			// TODO: cleanup resources?
			break;
		} else {
			int newStation = (buf[1] << 8) + (buf[2] & 0xFF);

			if (client->cd->station == -1) {
				set_station(client, newStation);
			} else {
				change_station(client, newStation);
			}
		}
	}

	free(client);
	return 0;
}

void *station_handler(void *arg) {
	station_t *s = (station_t *)arg;
	printf("station thread %d, socketfd %d\n", s->id, s->id);

	// read and write from file
	while (1) {
		char buf[1024];
		int to_len = sizeof(struct sockaddr);
		size_t newLen = fread(buf, sizeof(char), 1024, s->file);
		

		// loop through client list for this station and send data
		client_t *c;
		for (c = clientLists[s->id]; c != NULL; c = c->next) {
			//TODO: conceptual: why is this socket diff from listener socket?
			// struct sockaddr_in *a = (struct sockaddr_in *)&c->cd->addr;
			// a->sin_port = htons(c->cd->udpPort);
			// printf("sending to socket %d\n", s->udpSocket);
			if (sendto(s->udpSocket, "thank you", 9, 0, &c->cd->addr, to_len) < 0) { 
				perror("sendto");
				exit(1);
			}
		}
		sleep(1);
	}


	// loop through client list for this station and write to each udpport
	return 0;
}

void print_stations(FILE *f) {
	for (int i = 0; i < numStations; i++) {
		fprintf(f, "%d,%s", i, stations[i].name);
		client_t *c = clientLists[i];
		while (c) {
			struct sockaddr_in *addr_in = (struct sockaddr_in *)&(c->cd->addr);
			fprintf(f, ",%s:%d", inet_ntoa(addr_in->sin_addr), c->cd->udpPort);
			c = c->next;
		}
		fprintf(f, "\n");
	}
}

void *repl_handler(void *arg) {
	while (1) {
		char buffer[BUFLEN];
		if (fgets(buffer, BUFLEN, stdin) != NULL) {
			char *tokens[BUFLEN];
            int num_tokens = parse(buffer, tokens);

			if (num_tokens == -1) {
				//idk
			} else if (num_tokens == 0) {
				//idk
			} else {
				if (strcmp(tokens[0], "p\n") == 0) {
					print_stations(stdout);
				} else if (strcmp(tokens[0], "p") == 0 && num_tokens == 2) {
					tokens[1][strcspn(tokens[1], "\n")] = 0;
					FILE *f = fopen(tokens[1], "w");// TODO: 
					print_stations(f);
					fclose(f);
				} else if (strcmp(tokens[0], "q\n") == 0) {
					// TODO: more clean up
					pthread_exit(0);
				}
			}
		}
	}
}

void change_station(client_t *client, int newStation) {
	int curStation = client->cd->station;
	pthread_mutex_lock(&clientListMutexes[curStation]);
	pthread_mutex_lock(&clientListMutexes[newStation]);

	client_t **curStationClient = &clientLists[curStation];
	client_t **newStationClient = &clientLists[newStation];
	pull_client(client, curStationClient);
	insert_client(client, newStationClient);
	client->cd->station = newStation;

	pthread_mutex_unlock(&clientListMutexes[curStation]);
	pthread_mutex_unlock(&clientListMutexes[newStation]);
}

void set_station(client_t *client, int station) {
	pthread_mutex_lock(&clientListMutexes[station]);
	client_t **stationClient = &clientLists[station];
	insert_client(client, stationClient);
	client->cd->station = station;
	pthread_mutex_unlock(&clientListMutexes[station]);
}

// return -1 if no tokens read
// else return number of tokens
int parse(char buffer[1024], char *tokens[512]) {
    char delimiters[] = " \t";
    char *next_token = strtok(buffer, delimiters);

    if (*next_token == '\n') {
        return -1;
    }

    int i = 0;
    // iterate through buffer token by token to fill tokens array
    while (next_token) {
		tokens[i] = next_token;
		i++;
        next_token = strtok(NULL, delimiters);
    }
    if (!i) {
        return -1;
    }
    return i;
}

// set up socket
// type: 1=tcp, 0=udp
int set_up_socket(int type, const char *port) {

	int status;
	struct addrinfo hints;
	struct addrinfo *servinfo, *r;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = type ? SOCK_STREAM : SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	if ((status = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
		exit(2);
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
	// freeaddrinfo(servinfo);
	// TODO: where to put this?

	if (r == NULL) {
		fprintf(stderr, "error connecting to the server\n");
		//TODO: cleanup

		exit(1);
	}

	if (setsockopt(lsocket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) { // https://stackoverflow.com/questions/24194961/how-do-i-use-setsockoptso-reuseaddr
    	perror("setsockopt(SO_REUSEADDR) failed");
		exit(1);
	}
	return lsocket;
}

int main(int argc, char **argv) {

	if (argc < 3) {
		printf("usage: snowcast_server <tcpport> <file0> [file1] [file2] ...\n");
		return 0;
	}

	numStations = argc - 2;
	const char* port = argv[1];

	int tcpSocket = set_up_socket(1, port);
	int udpSocket = set_up_socket(0, port);

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

	if (listen(tcpSocket, 20) < 0) {
		perror("listen");
		return 1;
	}

	int err;
	pthread_t repl_thread;
    if ((err = pthread_create(&repl_thread, NULL, repl_handler, 0))) {
        // handle_error_en(err, "pthread_create");
		//TODO: cleanup
		fprintf(stderr, "pthread_create\n");
		return 1;
    }
	if ((err = pthread_detach(repl_thread))) {
		fprintf(stderr, "pthread_detach\n");
		return 1;
        // handle_error_en(err, "pthread_detach");
    }

	// Create threads for each station
	pthread_t *station_threads = malloc(numStations * sizeof(pthread_t));
	if (!station_threads) {
		perror("malloc");
		return 1;
	}

	stations = malloc(numStations * sizeof(station_t));
	
	for (int i = 0; i < numStations; i++) {
		int err;
		FILE *song = fopen(argv[i + 2], "r"); // may be NULL
		stations[i].file = song;
		stations[i].id = i;
		stations[i].name = argv[i + 2];
		stations[i].udpSocket = udpSocket;
		if ((err = pthread_create(&station_threads[i], NULL, station_handler, (void *) &stations[i]))) {
			// handle_error_en(err, "pthread_create");
			fprintf(stderr, "pthread_create\n");
		}

		if ((err = pthread_detach(station_threads[i]))) {
			fprintf(stderr, "pthread_detach\n");
			// handle_error_en(err, "pthread_detach");
		}
	}
	

	// Accept connections from clients and create thread for each client
	while (1) {
		int csock;
		struct sockaddr client_addr;
		socklen_t client_len = sizeof(client_addr);

		csock = accept(tcpSocket, &client_addr, &client_len);
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

		
		struct sockaddr_in *sin = (struct sockaddr_in *)&client_addr;
		uint16_t p;
		p = htons (sin->sin_port);
		printf ("client port is %d\n", p);


		cd->addrSize = client_len;
		cd->station = -1; // no station selected by default

		// create new client and detached thread
		client_constructor(cd);
	}
	// https://stackoverflow.com/questions/449617/how-should-i-close-a-socket-in-a-signal-handler


	for (int i = 0; i < numStations; i++) {
		pthread_mutex_destroy(&clientListMutexes[i]);
	}
	free(station_threads);
	close(tcpSocket);

	return 0;
}