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

typedef struct Hello {
	uint8_t commandType;
	uint16_t udpPort;
} hello_msg;

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

int main(int argc, char** argv) {

	if (argc < 4) {
		fprintf(stderr, "Not enough arguments\n");
		return 1;
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
	hello_msg msg = {0, udpPort};

	char bytes[] = {msg.commandType, msg.udpPort >> 8, msg.udpPort & 0xFF};
	int bytes_sent;
	bytes_sent = send(sock, &bytes, 3, 0);
	if (!bytes_sent) {
		perror("send hello");
		return 1;
	}
	char buf[100];
	int res;
	if ((res = recv(sock, buf, 3, 0)) < 0) {
		perror("recv");
	} else {
		// printf("read in %d bytes\n", res);

		// for (int i = 0; i < res; i++) {
		// 	printf("%04x",buf[i]);
		// }
		// printf("\n");
		// printf("%hu, %hu\n", buf[1], buf[2]);

		uint32_t numStations = 0;
		numStations = (buf[1] << 8) + buf[2];
		printf("Welcome to Snowcast! The server has %d stations.\n", numStations);
	}

	return 0;
}