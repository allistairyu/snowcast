#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <signal.h>

typedef struct sig_handler {
    sigset_t set;
    pthread_t thread;
} sig_handler_t;

void *monitor_signal(void *arg) {
    sigset_t *s = arg;
    int sig;
    while (1) {
        // wait for SIGINT
		printf("waiting for signal...\n");
        sigwait(s, &sig);

        printf("SIGINT received, cancelling all clients\n");
		break;
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

int main(int argc, char **argv) {
	sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    pthread_sigmask(SIG_BLOCK, &mask, 0);
	sig_handler_t *sighandler = sig_handler_constructor();

	while (1) {}
	free(sighandler);

	return 0;
}