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
#include <sys/time.h>
#include <errno.h>

int numStations = 0;
const int BUFLEN = 256;
const int SONG_BUFLEN = 1024;
const int STREAM_RATE = 16384;

struct ClientData {
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
	struct ClientData *cd;
	struct Client *prev;
	struct Client *next;
} client_t;

typedef struct Station {
	FILE *file;
	int id;
	char* name;
	pthread_t *thread;
} station_t;

void pull_client(client_t *, client_t **);
void insert_client(client_t *, client_t **);
void client_constructor(struct ClientData *);
void client_destructor(client_t *);
void delete_all();
void client_thread_cleanup(void *);
void serialize_general_message(char *, struct GeneralMessage *);
int send_general_message(int, char *, int);
void *client_handler(void *);
void *station_handler(void *);
void sigint_handler(int);
void print_stations(FILE *);
void *repl_handler(void *);
void change_station(client_t *, int);
void set_station(client_t *, int);
int parse(char[1024], char *[512]);
int set_up_socket(int, const char*);
void cleanup_server();


// array of clientList heads for each station
client_t **clientLists;
// array of mutexes for each clientLists head
pthread_mutex_t *clientListMutexes;
// array of stations
station_t *stations;

pthread_mutex_t serverMutex = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t sigint_received = 0;
int tcpSocket;
int udpSocket;

/*
 * Pulls client from doubly-linked client list
 * thread_list_head must be locked before this function is called
 */
void pull_client(client_t *c, client_t **head) {
	if (*head == NULL || c == NULL) {
		return;
	}
	if (*head == c) {
		*head = c->next;
	}
	if (c->next != NULL) {
		c->next->prev = c->prev;
	}
	if (c->prev != NULL) {
		c->prev->next = c->next;
	}
}

/*
 * Inserts client into doubly-linked client list
 * head must be locked before this function is called
 */
void insert_client(client_t *c, client_t **head) {
	c->next = *head;
	c->prev = NULL;
	if (*head != NULL) {
		(*head)->prev = c;
	}
	(*head) = c;
}

/*
 * creates client and thread with given client data
 * mallocs client
 */
void client_constructor(struct ClientData *cd) {
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
		if (client) {
			client_destructor(client);
		}
		fprintf(stderr, "pthread_create\n");
    }

    if ((err = pthread_detach(client->cd->thread))) {
		if (client) {
			client_destructor(client);
		}
		fprintf(stderr, "pthread_detach\n");
    }
}

/*
 * Frees/destroys Client resources
 */
void client_destructor(client_t *client) {
	close(client->cd->sock);
    free(client->cd);
    free(client);
}

/*
 * Cancels all client and station threads
 */
void delete_all() {
	for (int i = 0; i < numStations; i++) {
		client_t *c = clientLists[i];
		while (c) {
			if (pthread_cancel(c->cd->thread)) {
				fprintf(stderr, "pthread_cancel");
			}
			c = c->next;
		}
		if (pthread_cancel(*stations[i].thread)) {
			fprintf(stderr, "pthread_cancel");
		}
	}
}

/*
 * Pulls Client from client list and frees resources for Client
 */
void client_thread_cleanup(void *arg) {
    client_t *c = arg;
	int station = c->cd->station;
	if (station != -1) {
		pthread_mutex_lock(&clientListMutexes[station]);
		pull_client(c, &clientLists[station]);
		pthread_mutex_unlock(&clientListMutexes[station]);
	}

    client_destructor(c);
}

/*
 * Serializes a GeneralMessage into a buffer
 */
void serialize_general_message(char *buf, struct GeneralMessage *msg) {
	buf[0] = msg->replyType;
	buf[1] = msg->size;
	for (int i = 0; i < msg->size; i++) {
		buf[i + 2] = msg->content[i];
	}
}

/*
 * Sends a GeneralMessage to given TCP socket
 */
int send_general_message(int socket, char *stationName, int replyType) {
	int nameLen = strlen(stationName);
	struct GeneralMessage announcement = {replyType, (uint8_t)nameLen, stationName};
	char *msg = malloc(sizeof(char) * (nameLen + 2));
	if (!msg) {
		perror("malloc");
		return 1;
	}
	
	serialize_general_message(msg, &announcement);
	int bytes_sent;
	bytes_sent = send(socket, msg, nameLen + 2, 0);
	free(msg);
	if (!bytes_sent) {
		perror("send");
		return 1;
	}
	return bytes_sent;
}

/*
 * start_routine for client thread
 * input is client_t malloc'ed in client_constructor
 * created and detached in client_constructor
 */
void *client_handler(void *c) {
	client_t *client = (client_t *)c;
	pthread_cleanup_push(client_thread_cleanup, client);

	struct sockaddr_in *addr_in = (struct sockaddr_in *)&(client->cd->addr);
	uint16_t port = htons(addr_in->sin_port);
	printf("session id %s:%d: new client connected; expecting HELLO\n", inet_ntoa(addr_in->sin_addr), port);

	// int hello = 0;
	int res;
	char buf[3] = {0};

	if ((res = recv(client->cd->sock, buf, 3, 0)) < 0) {
		perror("recv");
	} else if (res == 0) {
		printf("client closed connection\n");
	} else if (buf[0] == 0) {
		if (res < 3) {
			if (recv(client->cd->sock, &buf[res], 3 - res, MSG_WAITALL) < 0) {
				perror("recv");
				client_thread_cleanup(client);
				return 0;
			}
		}
		uint16_t firstByte = buf[1] << 8;
		uint8_t secByte = buf[2] & 0xFF;
		uint16_t udpPort = firstByte + secByte;
		client->cd->udpPort = udpPort;

		printf("session id %s:%d: HELLO received; sending WELCOME; expecting SET_STATION\n", inet_ntoa(addr_in->sin_addr), port);

		int bytes_sent;
		struct Welcome msg = {2, htons(numStations)};

		bytes_sent = send(client->cd->sock, &msg, 3, 0);
		if (!bytes_sent) {
			perror("send");
			client_thread_cleanup(client);
			return 0;
		}

		struct timeval tv = {
			.tv_usec = 0
		};
		if (setsockopt(client->cd->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
			fprintf(stderr, "setsockopt\n");
			pthread_exit((void *) 1);
		}

		while (1) {
			int res;
			tv.tv_usec = 0;
			if (setsockopt(client->cd->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
				fprintf(stderr, "setsockopt 1\n");
				pthread_exit((void *) 1);
			}
			if ((res = recv(client->cd->sock, buf, 3, 0)) < 0) {
				perror("recv");
				break;
			} else if (res == 0) {
				printf("session id %s:%d: client closed connection\n", inet_ntoa(addr_in->sin_addr), port);
				break;
			} else {
				// check if additional Hello message
				if (buf[0] == 0) {
					char invalid_msg[] = "Only one Hello message can be sent from a client.";
					send_general_message(client->cd->sock, invalid_msg, 4);
					break;
				} else if (buf[0] != 1) {
					char invalid_msg[40];
					sprintf(invalid_msg, "Unknown command type: %d", buf[0]);
					send_general_message(client->cd->sock, invalid_msg, 4);
					break;
				}
				tv.tv_usec = 100000;
				if (setsockopt(client->cd->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
					fprintf(stderr, "setsockopt\n");
					break;
				}
				if (res < 3) {
					if ((res = recv(client->cd->sock, &buf[res], 3 - res, MSG_WAITALL)) < 0) {
						perror("recv");
						break;
					}
				}
				int newStation = (buf[1] << 8) + (buf[2] & 0xFF);

				if (newStation < 0 || newStation >= numStations) {
					// send invalid commmand
					char invalid_msg[40];
					sprintf(invalid_msg, "Station %d does not exist.", newStation);
					send_general_message(client->cd->sock, invalid_msg, 4);
					break;
				} else {
					printf("session id %s:%d: received SET_STATION to station %d\n", inet_ntoa(addr_in->sin_addr), client->cd->udpPort, newStation);
					send_general_message(client->cd->sock, stations[newStation].name, 3);
					if (client->cd->station == -1) {
						set_station(client, newStation);
					} else {
						change_station(client, newStation);
					}
				}					
			}
		}
	}
	pthread_cleanup_pop(1);
	return 0;
}

/*
 * start_routine for station thread
 * reads in FILE and streams via UDP to each client connected to station
 */
void *station_handler(void *arg) {
	station_t *s = (station_t *)arg;

	// read and write from file
	while (1) {
		char buf[SONG_BUFLEN];
		int to_len = sizeof(struct sockaddr);
		int bytes_read;
		int announce = 0;
		struct timespec tim, tim2;
		tim.tv_sec  = 0;
		tim.tv_nsec = 62500000L; 
		while (s->file) {
			bytes_read = fread(buf, sizeof(char), SONG_BUFLEN, s->file);
			if (bytes_read < SONG_BUFLEN) {
				announce = 1;
				if (fseek(s->file, 0, SEEK_SET) != 0) {
					fprintf(stderr, "failed to reset file pointer\n");
					break;
				}
			}

			// loop through client list for this station and send data
			client_t *c;
			for (c = clientLists[s->id]; c != NULL; c = c->next) {
				struct sockaddr_in *a = (struct sockaddr_in *)&c->cd->addr;
				a->sin_port = htons(c->cd->udpPort);

				if (announce) {
					send_general_message(c->cd->sock, s->name, 3);
				}

				if (sendto(udpSocket, buf, SONG_BUFLEN, 0, &c->cd->addr, to_len) < 0) { 
					perror("sendto");
					exit(1);
				}
			}
			announce = 0;
			
			if (nanosleep(&tim, &tim2) < 0) {
				printf("Nano sleep system call failed \n");
				exit(1);
			}
		}
	}
	return 0;
}

void sigint_handler(int n) {
	sigint_received = 1;
}

/*
 * Prints client list for each station
 */
void print_stations(FILE *f) {
	for (int i = 0; i < numStations; i++) {
		fprintf(f, "%d,%s", i, stations[i].name);
		client_t *c = clientLists[i];
		pthread_mutex_lock(&clientListMutexes[i]);
		while (c) {
			struct sockaddr_in *addr_in = (struct sockaddr_in *)&(c->cd->addr);
			fprintf(f, ",%s:%d", inet_ntoa(addr_in->sin_addr), c->cd->udpPort);
			c = c->next;
		}
		pthread_mutex_unlock(&clientListMutexes[i]);
		fprintf(f, "\n");
	}
}

/*
 * start_routine for repl thread
 */
void *repl_handler(void *arg) {
	while (1) {
		char buffer[BUFLEN];
		if (fgets(buffer, BUFLEN, stdin) != NULL) {
			char *tokens[BUFLEN];
            int num_tokens = parse(buffer, tokens);

			if (num_tokens == -1) {
				exit(0);
			} else if (num_tokens == 0) {
				exit(0);
			} else {
				if (strcmp(tokens[0], "p\n") == 0) {
					print_stations(stdout);
				} else if (strcmp(tokens[0], "p") == 0 && num_tokens == 2) {
					tokens[1][strcspn(tokens[1], "\n")] = 0;
					FILE *f = fopen(tokens[1], "w");
					print_stations(f);
					fclose(f);
				} else if (strcmp(tokens[0], "q\n") == 0) {
					sigint_received = 1;
					// cleanup_server();
					exit(0);
				}
			}
		}
	}
	return 0;
}

/*
 * Changes station for a particular Client
 * Client must already have a station
 */
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

/*
 * Sets station for a particular Client
 */
void set_station(client_t *client, int station) {
	pthread_mutex_lock(&clientListMutexes[station]);
	client_t **stationClient = &clientLists[station];
	insert_client(client, stationClient);
	client->cd->station = station;
	pthread_mutex_unlock(&clientListMutexes[station]);
}

/*
 * Parses buffer into tokens array and returns number of tokens read
 */
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

/*
 * Sets up socket for a given port.
 * type: 1=TCP, 0=UDP
 */
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
	freeaddrinfo(servinfo);

	if (r == NULL) {
		fprintf(stderr, "error connecting to the server\n");
		exit(1);
	}

	if (setsockopt(lsocket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) { // https://stackoverflow.com/questions/24194961/how-do-i-use-setsockoptso-reuseaddr
    	perror("setsockopt(SO_REUSEADDR) failed");
		exit(1);
	}
	return lsocket;
}

// void cleanup_server() {
// 	pthread_mutex_lock(&serverMutex);
// 	delete_all();
// 	pthread_mutex_unlock(&serverMutex);
// 	pthread_mutex_destroy(&serverMutex);
// 	close(tcpSocket);
// }


int main(int argc, char **argv) {

	if (argc < 3) {
		printf("usage: snowcast_server <tcpport> <file0> [file1] [file2] ...\n");
		return 0;
	}

	struct sigaction sa;
	sa.sa_handler = sigint_handler;
	sa.sa_flags = 0;
	sigemptyset( &sa.sa_mask );

	if (-1 == sigaction(SIGINT, &sa, NULL)) {
		perror("sigaction() failed");
		exit(EXIT_FAILURE);
	}

	numStations = argc - 2;
	const char* port = argv[1];

	tcpSocket = set_up_socket(1, port);
	udpSocket = set_up_socket(0, port);

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
	// join thread at end of main
    if ((err = pthread_create(&repl_thread, NULL, repl_handler, 0))) {
		fprintf(stderr, "pthread_create\n");
		return 1;
    }


	stations = calloc(numStations, sizeof(station_t));
	if (!stations) {
		perror("calloc");
		return 1;
	}
	
	for (int i = 0; i < numStations; i++) {
		int err;
		FILE *song = fopen(argv[i + 2], "r"); // may be NULL
		stations[i].file = song;
		stations[i].id = i;
		stations[i].name = argv[i + 2];
		pthread_t *thread = malloc(sizeof(pthread_t));
		stations[i].thread = thread;
		if ((err = pthread_create(stations[i].thread, NULL, station_handler, (void *) &stations[i]))) {
			// handle_error_en(err, "pthread_create");
			fprintf(stderr, "pthread_create\n");
		}

		if ((err = pthread_detach(*stations[i].thread))) {
			fprintf(stderr, "pthread_detach\n");
			// handle_error_en(err, "pthread_detach");
		}
	}
	// print_stations(stdout);
	// Accept connections from clients and create thread for each client
	while (!sigint_received) {
		int csock;
		struct sockaddr client_addr;
		socklen_t client_len = sizeof(client_addr);

		csock = accept(tcpSocket, &client_addr, &client_len);
		if (csock == -1) {
			if (sigint_received) {
				break;
			}
			perror("accept");
			
			return 1;
		}
		struct timeval tv = {
			.tv_usec = 100000
		};
		if (setsockopt(csock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
			fprintf(stderr, "setsockopt\n");
			return 1;
		}
		struct ClientData *cd = malloc(sizeof(struct ClientData));
		if (!cd) {
			perror("malloc");
			return 1;
		}
		memset(cd, 0, sizeof(struct ClientData));
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
	pthread_mutex_lock(&serverMutex);
	delete_all();
	pthread_mutex_unlock(&serverMutex);
	// free(stations);
	pthread_cancel(repl_thread);

	pthread_join(repl_thread, 0);

	pthread_mutex_destroy(&serverMutex);
	close(tcpSocket);

	return 0;
}