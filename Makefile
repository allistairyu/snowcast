CC = gcc
CFLAGS = -g -pthread

.PHONY: all

all:
	+$(MAKE) -C src

clean:
	rm -rf snowcast_control snowcast_server