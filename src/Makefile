CC = gcc
CFLAGS = -g -Wall -pthread

.PHONY: all

all: client server

client: client.o
	$(CC) $(LDFLAGS) $(CFLAGS) $^ -o ../snowcast_control
	$(CC) $(LDFLAGS) $(CFLAGS) $^ -o snowcast_control

server: server.o
	$(CC) $(LDFLAGS) $(CFLAGS) $^ -o ../snowcast_server
	$(CC) $(LDFLAGS) $(CFLAGS) $^ -o snowcast_server

test: test.o
	$(CC) $(LDFLAGS) $(CFLAGS) $^ -o test

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -rf snowcast_control snowcast_server
