#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <pthread.h>
#include <unistd.h>

const int BUFLEN = 20;

int main(int argc, char **argv) {
	char buf[BUFLEN];
	FILE *f = fopen("../mp3/FX-Impact193.mp3", "r");
	if (f == NULL) {
		fprintf(stderr, "invalid file\n");
		return 1;
	}
	int bytes_read;
	while (1) {
		bytes_read = fread(buf, sizeof(char), BUFLEN, f);
		printf("read %d bytes", bytes_read);
		printf("%.*s\n", (int)bytes_read, buf);

		if (bytes_read < BUFLEN) {
			printf("resetting pointer\n");
			if (fseek(f, 0, SEEK_SET) != 0) {
				fprintf(stderr, "failed to reset file pointer\n");
				return 1;
			}
		}
		sleep(.1);
	}

	return 0;
}