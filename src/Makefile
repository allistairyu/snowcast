CC = gcc
CFLAGS = -g -Wall

.PHONY: all

all: client server

client: client.o
	$(CC) $(LDFLAGS) $(CFLAGS) $^ -o $@

server: server.o
	$(CC) $(LDFLAGS) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -rf client server
