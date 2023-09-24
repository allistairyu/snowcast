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

int numStations = 0;
const int BUFLEN = 256;
const int SONG_BUFLEN = 1024;
const int STREAM_RATE = 16384;

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
	char* name;
	int udpSocket;
} station_t;

typedef struct sig_handler {
    sigset_t set;
    pthread_t thread;
} sig_handler_t;

void pull_client(client_t *, client_t **);
void insert_client(client_t *, client_t **);
void client_constructor(struct client_data *);
void client_destructor(client_t *);
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

pthread_mutex_t serverMutex = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t sigint_received = 0;

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
		client_destructor(client); // TODO: any chance thread will free client before this gets called?
		fprintf(stderr, "pthread_create\n");
    }

    if ((err = pthread_detach(client->cd->thread))) {
		client_destructor(client);
		fprintf(stderr, "pthread_detach\n");
        // handle_error_en(err, "pthread_detach");
    }
}

void client_destructor(client_t *client) {
	close(client->cd->sock);
    free(client->cd);
    free(client);
}

void delete_all() {
	for (int i = 0; i < numStations; i++) {
		client_t *c = clientLists[i];
		while (c) {
			if (pthread_cancel(c->cd->thread)) {
				fprintf(stderr, "pthread_cancel");
			}
			c = c->next;
		}
	}
}

void thread_cleanup(void *arg) {
    client_t *c = arg;

    pthread_mutex_lock(&clientListMutexes[c->cd->station]);
    pull_client(c, &clientLists[c->cd->station]);
    pthread_mutex_unlock(&clientListMutexes[c->cd->station]);

    client_destructor(c);
}

void serialize_general_message(char *buf, struct GeneralMessage *msg) {
	buf[0] = msg->replyType;
	buf[1] = msg->size;
	for (int i = 0; i < msg->size; i++) {
		buf[i + 2] = msg->content[i];
	}
}

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
	return 0;
}

/*
 * start_routine for client
 * input is client_t malloc'ed in client_constructor
 * created and detached in client_constructor
 */
void *client_handler(void *c) {
	client_t *client = (client_t *)c;
	pthread_cleanup_push(thread_cleanup, client);

	struct sockaddr_in *addr_in = (struct sockaddr_in *)&(client->cd->addr);
	uint16_t port = htons(addr_in->sin_port);
	printf("session id %s:%d: new client connected; expecting HELLO\n", inet_ntoa(addr_in->sin_addr), port);

	char buf[3] = {0};
	int res;
	if ((res = recv(client->cd->sock, buf, 3, 0)) < 0) { //TODO: buf size 3?
		perror("recv");
	} else {
		if (res == 3) {
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
				return 0;
			}

			struct timeval tv = {
				.tv_usec = 0
			};
			if (setsockopt(client->cd->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
				fprintf(stderr, "setsockopt 1\n");
				pthread_exit((void *) 1);
			}
			if (setsockopt(client->cd->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
				fprintf(stderr, "setsockopt 1\n");
				pthread_exit((void *) 1);
			}


			while (1) {
				int count = 0;
				if ((count = recv(client->cd->sock, buf, 3, 0)) < 0) { // TODO: change from 30 do some error checking for malicious commands
					// fprintf(stderr, "here?\n");
					perror("recv");
				} else if (count == 0) {
					printf("client closed connection\n");
					// TODO: cleanup resources?
					break;
				} else {
					if (count < 3) {
						break;
					}
					// TODO: refactor
					// check if additional Hello message
					if (buf[0] == 0) {
						char invalid_msg[] = "Only one Hello message must be sent.";
						send_general_message(client->cd->sock, invalid_msg, 4);
						break;
					} else if (buf[0] != 1) {
						char invalid_msg[40];
						sprintf(invalid_msg, "Unknown command type: %d", buf[0]);
						send_general_message(client->cd->sock, invalid_msg, 4);
						break;
					}
					int newStation = (buf[1] << 8) + (buf[2] & 0xFF);

					if (newStation < 0 || newStation >= numStations) {
						// send invalid commmand
						char invalid_msg[40];
						sprintf(invalid_msg, "Station %d does not exist.", newStation);
						send_general_message(client->cd->sock, invalid_msg, 4);
						break; // kill this thread?
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
	}

	
	pthread_cleanup_pop(1);
	return 0;
}

void *station_handler(void *arg) {
	station_t *s = (station_t *)arg;

	// read and write from file
	while (1) {
		char buf[SONG_BUFLEN];
		int to_len = sizeof(struct sockaddr);
		int bytes_read;
		int announce = 0;

		while (s->file) {
			bytes_read = fread(buf, sizeof(char), SONG_BUFLEN, s->file);
			if (bytes_read < SONG_BUFLEN) {
				//TODO: announce song again
				announce = 1;

				if (fseek(s->file, 0, SEEK_SET) != 0) {
					fprintf(stderr, "failed to reset file pointer\n");
					break;
				}
			}

			// loop through client list for this station and send data
			client_t *c;
			for (c = clientLists[s->id]; c != NULL; c = c->next) {
				//TODO: conceptual: why is this socket diff from listener socket?
				struct sockaddr_in *a = (struct sockaddr_in *)&c->cd->addr;
				a->sin_port = htons(c->cd->udpPort);

				if (announce) {
					send_general_message(c->cd->sock, s->name, 3);
					announce = 0;
				}

				if (sendto(s->udpSocket, buf, SONG_BUFLEN, 0, &c->cd->addr, to_len) < 0) { 
					perror("sendto");
					exit(1);
				}
			}
			sleep(1); // change to 1/16
		}
	}


	// loop through client list for this station and write to each udpport
	return 0;
}

void sigint_handler(int n) {
	pthread_mutex_lock(&serverMutex);
	delete_all(); // TODO: anything else for SIGINT?
	pthread_mutex_unlock(&serverMutex);
	sigint_received = 1;
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
        pthread_mutex_lock(&serverMutex);
        delete_all(); // TODO: anything else for SIGINT?
        pthread_mutex_unlock(&serverMutex);
		pthread_exit((void *) 1);
    }

    return NULL;
}

sig_handler_t *sig_handler_constructor() {
    sig_handler_t *s = malloc(sizeof(sig_handler_t)); // TODO: free this at some point
    if (!s) {
        fprintf(stderr, "malloc\n");
        exit(1);
    }
    sigemptyset(&s->set);
    sigaddset(&s->set, SIGINT);
    int err;
    // create thread for sole purpose of waiting for and handling SIGINT
    if ((err = pthread_create(&s->thread, 0, monitor_signal, &s->set))) {
        // handle_error_en(err, "pthread_create");
		fprintf(stderr, "pthread_create");
    }

    return s;
}

void sig_handler_destructor(sig_handler_t *sighandler) {
    pthread_cancel(sighandler->thread);
    pthread_join(sighandler->thread, 0);
    free(sighandler);
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
	freeaddrinfo(servinfo);
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


//TODO: all error returns must clean up resources..
int main(int argc, char **argv) {

	if (argc < 3) {
		printf("usage: snowcast_server <tcpport> <file0> [file1] [file2] ...\n");
		return 0;
	}
	// sigset_t mask;
    // sigemptyset(&mask);
    // sigaddset(&mask, SIGINT);
    // pthread_sigmask(SIG_BLOCK, &mask, 0);
	// sig_handler_t *sighandler = sig_handler_constructor();
	struct sigaction sa = {sigint_handler, SA_RESTART};

	if (-1 == sigaction(SIGINT, &sa, NULL)) {
		perror("sigaction() failed");
		exit(EXIT_FAILURE);
	}
	while (!sigint_received) {

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
		// join thread at end of main
		if ((err = pthread_create(&repl_thread, NULL, repl_handler, 0))) {
			// handle_error_en(err, "pthread_create");
			//TODO: cleanup
			fprintf(stderr, "pthread_create\n");
			return 1;
		}

		// Create threads for each station
		pthread_t *station_threads = malloc(numStations * sizeof(pthread_t *));
		if (!station_threads) {
			perror("malloc");
			return 1;
		}

		stations = malloc(numStations * sizeof(station_t));
		if (!stations) {
			perror("malloc");
			return 1;
		}
		
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

			// set timeout sockopt TODO: before or after accept? also, does not
			// account for receiving 1 byte of Hello message resetting the timer i think
			

			csock = accept(tcpSocket, &client_addr, &client_len);
			if (csock == -1) {
				perror("accept");
				return 1;
			}
			struct timeval tv = {
				.tv_usec = 100000
			};
			if (setsockopt(csock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
				fprintf(stderr, "setsockopt 2\n");
				return 1;
			}
			struct client_data *cd = malloc(sizeof(struct client_data));
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
			// free(station_threads[i]);

		}
		// sig_handler_destructor(sighandler);
		free(station_threads);
		free(stations);
		pthread_cancel(repl_thread);
		pthread_join(repl_thread, 0);
		pthread_mutex_destroy(&serverMutex);
		close(tcpSocket);
	}
	return 0;
}