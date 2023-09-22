

int main(int argc, char **argv) {
	if (argc < 2) {
		printf("usage: snowcast_listener <udpport>\n");
		return 1;
	}

	int s;
	int sock;
	struct addrinfo hints;
	struct addrinfo *result
	struct addrinfo *rp;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	if ((s = getaddrinfo()))

}