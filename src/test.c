#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <pthread.h>

int BUFLEN = 512;
pthread_mutex_t *mutexes;

typedef struct Node {
	int val;
	struct Node *next;
	struct Node *prev;
} node_t;

node_t **lists;
int numStations;

void insert(node_t *c, node_t **head) {
    if (!*head) {
        c->next = c;
        c->prev = c;
    } else {
        node_t *last = (*head)->prev;
        c->next = *head;
        c->prev = last;
        last->next = c;
        (*head)->prev = c;
    }
    *head = c;
}

void pull(node_t *c, node_t **head) {
    if (c->prev == c) {
        *head = NULL;
    } else {
        c->prev->next = c->next;
        c->next->prev = c->prev;
    }
    if (c == *head) {
        *head = c->prev;
    }
}

void print_stations() {
	printf("printing\n");
	for (int i = 0; i < numStations; i++) {
		printf("station %d: ", i);
		node_t *n = lists[i];
		node_t *start = n;
		while (n) {
			printf("%d,", n->val);
			n = n->next;
			if (n == start) {
				break;
			}
		}
		printf("\n");
	}
}

int main(int argc, char **argv) {
	/*
	numStations = 5;
	lists = calloc(numStations, sizeof(node_t));
	if (!lists) {
		perror("malloc");
		return 1;
	}

	// lists: [0, 0, 0, 0, 0]

	node_t *n1 = malloc(sizeof(node_t));
	n1->val = 1;
	n1->next = NULL;
	n1->prev = NULL;
	insert(n1, &lists[1]);
	node_t *n2 = malloc(sizeof(node_t));
	n2->val = 2;
	insert(n2, &lists[1]);
	print_stations();


	pull(n1, &lists[1]);
	insert(n1, &lists[2]);
	print_stations();

	*/

	const char *name = argv[1];
	printf("%s\n", name);

	return 0;
}