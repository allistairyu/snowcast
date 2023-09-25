#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <unistd.h>

in_port_t get_in_port(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
        return (((struct sockaddr_in*)sa)->sin_port);

    return (((struct sockaddr_in6*)sa)->sin6_port);
}

void receive_stream(int fd) {
	while (1) {
		char buf[1024];
		int msg_size;

		if ((msg_size = recvfrom(fd, buf, 1024, 0, 0, 0)) < 0) {
			perror("recvfrom");
			exit(1);
		}
		buf[msg_size - 1] = 0;
		fwrite(buf, 1, msg_size, stdout);
	}
}


int main(int argc, char **argv) {
	if (argc < 2) {
		printf("usage: snowcast_listener <udpport>\n");
		return 1;
	}

	int s;
	int sock;
	struct addrinfo hints;
	struct addrinfo *result;
	struct addrinfo *rp;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	char hostname[80];
	gethostname(hostname, 80);
	
	if ((s = getaddrinfo("localhost", argv[1], &hints, &result)) != 0) { // TODO: addr? gethostname + getaddrinfo? beej
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
	}

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		if ((sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) < 0) {
			continue;
		}
		if (bind(sock, rp->ai_addr, rp->ai_addrlen) >= 0) {
			break;
		}
		close(sock);
	}
	
	if (rp == NULL) {
		fprintf(stderr, "Could not communicate with localhost\n");
		freeaddrinfo(result);

		return 1;
	}

	receive_stream(sock);

	freeaddrinfo(result);
	return 0;
}
