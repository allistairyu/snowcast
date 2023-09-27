CC = gcc
CFLAGS = -g -Wall -pthread
#  -fsanitize=address -static-libasan -g

.PHONY: all

all: snowcast_control snowcast_server snowcast_listener

snowcast_control: snowcast_control.o
	$(CC) $(LDFLAGS) $(CFLAGS) $^ -o ../snowcast_control
	$(CC) $(LDFLAGS) $(CFLAGS) $^ -o snowcast_control

snowcast_server: snowcast_server.o
	$(CC) $(LDFLAGS) $(CFLAGS) $^ -o ../snowcast_server
	$(CC) $(LDFLAGS) $(CFLAGS) $^ -o snowcast_server

snowcast_listener: snowcast_listener.o
	$(CC) $(LDFLAGS) $(CFLAGS) $^ -o ../snowcast_listener
	$(CC) $(LDFLAGS) $(CFLAGS) $^ -o snowcast_listener

test: test.o
	$(CC) $(LDFLAGS) $(CFLAGS) $^ -o test

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -rf snowcast_control snowcast_server snowcast_listener