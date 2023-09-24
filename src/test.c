#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <pthread.h>
#include <unistd.h>

const int BUFLEN = 20;

int main(int argc, char **argv) {
	char *msg;
	// msg = "asdf\n";
	sprintf(msg, "asdf\n");

	printf("%s", msg);

	return 0;
}