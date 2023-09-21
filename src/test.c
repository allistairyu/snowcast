#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <pthread.h>

int BUFLEN = 512;
pthread_mutex_t *mutexes;

int main(int argc, char **argv) {
	int i = 5;
	mutexes = malloc(i * sizeof(pthread_mutex_t));
	for (int j = 0; j < i; j++) {
		pthread_mutex_init(&mutexes[j], NULL);
	}

	return 0;
}