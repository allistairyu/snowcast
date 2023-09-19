#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

typedef struct Hello {
	char commandType;
	unsigned short udpPort;
} hello_msg;

char *serialize(int ints[3]) {
	return (char *) ints;
}

int main(int argc, char** argv) {
	int s;
	int sock;
	struct addrinfo hints;
	struct addrinfo *result;
	struct addrinfo *rp;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if ((s = getaddrinfo("localhost", "16800", &hints, &result)) != 0) {
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

	// struct Hello msg = {1, 1};
	// char *bytes = {msg.commandType, msg.udpPort};
	char commandType = 0;
	unsigned short udpPort = 256;
	int bytes_sent;
	bytes_sent = send(sock, &commandType, 1, 0);
	if (!bytes_sent) {
		perror("send");
		return 1;
	}
	bytes_sent = send(sock, &udpPort, 2, 0);
	if (!bytes_sent) {
		perror("send");
		return 1;
	}
	char buf[10];
	if (recv(sock, buf, 3, 0) < 0) {
		perror("recv");
	} else {
		printf("welcome received\n");
	}

	return 0;
}