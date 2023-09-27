#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <pthread.h>
#include <signal.h>

const int BUFLEN = 256;
pthread_mutex_t welcomeMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t welcomeCond = PTHREAD_COND_INITIALIZER;
int wait = 1;
int stationFlag = 0;
pthread_mutex_t stationMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t stationCond = PTHREAD_COND_INITIALIZER;


struct Message {
	uint8_t commandType;
	uint16_t content;
} __attribute__((packed));

int str_to_uint16(const char *str, uint16_t *res) {
    char *end;
    errno = 0;
    long val = strtol(str, &end, 10);
    if (errno || end == str || *end != '\0' || val < 0 || val >= 0x10000) {
        return 0;
    }
    *res = (uint16_t)val;
    return 1;
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

int isnumber(char *s) {
	for (int i = 0; i < strlen(s); i++) {
		if (!isdigit(s[i])) {
			return 0;
		}
		s++;
	}
	return 1;
}

void *repl_handler(void *arg) {
	int sock = *(int *)arg;
	while (1) {
		pthread_mutex_lock(&welcomeMutex);
		while (wait) {
			pthread_cond_wait(&welcomeCond, &welcomeMutex);
		}
		pthread_mutex_unlock(&welcomeMutex);

		printf("> ");

		char buffer[BUFLEN];
        if (fgets(buffer, BUFLEN, stdin) != NULL) {
            // parse input
            char *tokens[BUFLEN];
            int num_tokens = parse(buffer, tokens);

			// TODO: refactor
			if (num_tokens == -1) {
				fprintf(stderr, "eof?\n");
				return 0;
			} else if (num_tokens == 0 || num_tokens > 1) {
				printf("Invalid input: number or 'q' expected\n");
			} else {
				if (strcmp(tokens[0], "q\n") == 0) {
					break;
				}
				char *end;
				tokens[0][strcspn(tokens[0], "\n")] = 0;
				if (!isnumber(tokens[0])) {
					printf("Invalid input: number or 'q' expected\n");
				} else {
					const long i = strtol(tokens[0], &end, 10);
					// send request to switch to station i
					struct Message stationChange = {1, htons((uint16_t) i)};
					int bytes_sent = send(sock, &stationChange, 3, 0);
					if (!bytes_sent) {
						perror("send station change");
						return 0;
					}
					pthread_mutex_lock(&stationMutex);
					stationFlag = 1;
					pthread_mutex_unlock(&stationMutex);
					printf("Waiting for an announce...\n");
					pthread_mutex_lock(&welcomeMutex);
					wait = 1;
					pthread_mutex_unlock(&welcomeMutex);
				}
			}
        }
	}
	close(sock);
	exit(0);
	return 0;
}

int main(int argc, char** argv) {
	if (argc < 4) {
		printf("usage: snowcast_control <servername> <serverport> <udpport>\n");
		return 0;
	}

	int s;
	int sock;
	struct addrinfo hints;
	struct addrinfo *result;
	struct addrinfo *rp;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if ((s = getaddrinfo(argv[1], argv[2], &hints, &result)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		return 1;
	}

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		if ((sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) < 0) {
			continue;
		}
		if (connect(sock, rp->ai_addr, rp->ai_addrlen) >= 0) {
			break;
		}
		close(sock);
	}
	if (rp == NULL) {
		freeaddrinfo(result);
		close(sock);
		fprintf(stderr, "Could not connect to localhost\n");
		return 1;
	}
	freeaddrinfo(result);

	pthread_t repl_thread;
	if (pthread_create(&repl_thread, NULL, repl_handler, (void *)&sock)) {
		fprintf(stderr, "pthread_create\n");//TODO: when to use fprintf vs perror
		return 1;
	}
	if (pthread_detach(repl_thread)) {
		fprintf(stderr, "pthread_detach\n");
		return 1;
	}

	uint16_t udpPort;
	str_to_uint16(argv[3], &udpPort);
	struct Message msg = {0, htons(udpPort)};

	int bytes_sent;
	bytes_sent = send(sock, &msg, 3, 0);
	if (!bytes_sent) {
		perror("send hello");
		return 0;
	}

	int res;
	struct timeval tv = {
		.tv_usec = 100000
	};
	if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
		fprintf(stderr, "setsockopt\n");
		return 0;
	}

	char buf[3];
	if ((res = recv(sock, buf, 3, 0)) < 0) {
		perror("recv");
		return 0;
	} else {
		int messageType = buf[0];
		if (messageType == 2) {
			while (res < 3) {
				if ((res += recv(sock, &buf[res], 3 - res, 0)) < 0) {
					perror("recv");
					return 0;
				}
			}
			uint16_t numStations = buf[1] << 8;
			numStations += buf[2] & 0xFF;
			printf("Welcome to Snowcast! The server has %d stations.\n", numStations);
			fflush(stdout);
			wait = 0;
			pthread_cond_signal(&welcomeCond);
		} else {
			close(sock);
			return 0;
		}
	}

	char new_buf[80];
	while (1) {
		tv.tv_usec = 0;
		if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
			fprintf(stderr, "setsockopt\n");
			return 0;
		}
		if ((res = recv(sock, new_buf, 2, 0)) < 0) {
			perror("recv");
			return 0;
		} else if (res == 0) {
			printf("server closed connection\n");
			return 0;	
		} else {
			int messageType = new_buf[0];
			if (messageType < 2 || messageType > 4) {
				fprintf(stderr, "Received invalid message type.\n");
				break;
			} else if (messageType == 4) {
				fprintf(stderr, "Received invalid command response.\n");
				break;
			} else if (messageType == 2) {
				fprintf(stderr, "Received more than one Welcome message\n");
				break;
			} else if (messageType == 3) {
				tv.tv_usec = 100000;
				if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
					fprintf(stderr, "setsockopt\n");
					return 0;
				}
				if (res == 1) {
					if ((res = recv(sock, &new_buf[1], 1, MSG_WAITALL)) < 0) {
						perror("recv");
						return 0;
					}
				}
				int msgSize = new_buf[1];
				if ((res = recv(sock, new_buf, msgSize, MSG_WAITALL)) < 0) {
					perror("recv");
					return 0;
				}
				pthread_mutex_lock(&stationMutex);
				if (!stationFlag) {
					break;
				}
				pthread_mutex_unlock(&stationMutex);
				pthread_mutex_lock(&welcomeMutex);
				new_buf[msgSize] = 0;
				printf("New song announced: %.*s\n", msgSize, new_buf);
				if (!wait) {
					printf("> ");
				}
				fflush(stdout);
				wait = 0;
				pthread_mutex_unlock(&welcomeMutex);
				pthread_cond_signal(&welcomeCond);

			}
		}
	}
	
	// TODO: figure out how to close socket in case of SIGINT, EOF, etc.
	// https://stackoverflow.com/questions/449617/how-should-i-close-a-socket-in-a-signal-handler
	close(sock);
	return 0;
}