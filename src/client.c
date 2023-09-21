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

const int BUFLEN = 256;

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
		fprintf(stderr, "Could not connect to localhost\n");
		return 1;
	}
	freeaddrinfo(result);

	uint16_t udpPort;
	str_to_uint16(argv[3], &udpPort);
	struct Message msg = {0, htons(udpPort)};

	int bytes_sent;
	bytes_sent = send(sock, &msg, 3, 0);
	if (!bytes_sent) {
		perror("send hello");
		return 1;
	}
	char buf[100];
	int res;
	uint32_t numStations = 0;
	if ((res = recv(sock, buf, 3, 0)) < 0) {
		perror("recv");
	} else {
		numStations = (buf[1] << 8) + (buf[2] & 0xFF);
		printf("Welcome to Snowcast! The server has %d stations.\n", numStations);
	}
	while (1) {
		printf("> ");

		// get input
        char buffer[BUFLEN];
        if (fgets(buffer, BUFLEN, stdin) != NULL) {
            // parse input
            char *tokens[BUFLEN];
            int num_tokens = parse(buffer, tokens);

			// TODO: refactor
			if (num_tokens == -1) {
				fprintf(stderr, "eof?\n");
				return 1;
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
					if (i < 0 || i >= numStations) {
						//TODO: bunch of error stuff
						break;
					} else {
						// switch to station i
						struct Message stationChange = {1, htons((uint16_t) i)};
						bytes_sent = send(sock, &stationChange, 3, 0);
						if (!bytes_sent) {
							perror("send station change");
							return 1;
						}
					}
				}

			}
        } else {
            // TODO: exit gracefully...
        }

	}
	// TODO: figure out how to close socket in case of SIGINT, EOF, etc.
	// https://stackoverflow.com/questions/449617/how-should-i-close-a-socket-in-a-signal-handler
	close(sock);

	return 0;
}