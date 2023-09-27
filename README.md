## Structure

### Server
The server begins by setting up a TCP socket on a port that the user specifies.
The server also sets up a UDP socket with which it will stream data
later. Then, the program creates a separate REPL thread that handles commands "q" that exits
the program and "p" that prints out each station name and a list of clients that
are connected to it (optionally to a file instead of stdout). Throughout the
program, I rely on 3 structs:
- `struct Client {
	struct ClientData *cd;
	struct Client *prev;
	struct Client *next;
};`
	We want to maintain lists of clients for each station. To efficiently pull
	and insert from these lists (needed when clients set or change stations), I
	implemented these client lists as linked lists where a Client is a node.
	Each Client stores its data in its cd field.
	
- `struct ClientData {
	int sock;
	struct sockaddr addr;
	socklen_t addrSize;
	pthread_t thread;
	int station;
	int udpPort;
};`
	Information about each client that connects to the server is stored these
	structs. This information is used whenever we need to communicate with the
	client or need to find out which station a client is connected to, etc.
- `struct Station {
	FILE *file;
	int id;
	char* name;
	pthread_t *thread;
};`
	The Station struct packs together relevant information for each station.

The server creates an array, clientLists, of size numStations that stores the
heads of each client list (initially all NULL with 0 clients). Once clients
connect to the server and select a station, they are added to the correct client
list. 

For each file that the user passes in, the program creates a Station struct.
Since each station must broadcast their respective files concurrently, the
server creates a new thread for each station, handled by `station_handler`. The
station handler continually reads in from the file 1024 bytes at a time, loops
through the client list for the particular station, and `sendto`s the client's
UDP port. Between every iteration, a delay of 1/16 of a second is added with
`nanosleep` to ensure an average streaming rate of 16KiB/s. If the end of the
song file is reached, the file pointer is reset to the beginning of the file and
a TCP announcement is sent to the control client.

In the main thread, the server continually attempts to
accept client connections. Once a client connects, the server creates a new
Client with `client_constructor`, which also creates a new thread using
`client_handler` to manage future communication with this client. This client
handler begins by performing a handshake with the control client (waits for
Hello, responds with Welcome). On receiving any valid set station commands, the
handler adds or updates the Client to the corresponding clientLists list. For
any invalid commands it receives (additional Hellos, invalid commandType, or
set station with invalid staiton number), the client connection is terminated. 

### Control
After setting up a TCP socket on the host and port passed in by the user, the
control program performs a handshake with the server (sends Hello, receives
and prints Welcome). The control program then has two tasks: listen for messages
from the server and read input from a REPL (and send commands if input is
valid).

Since both tasks must happen simultaneously, I created a thread to handle the
REPL. If the user enters "q" by itself, the program exits. If the user enters a
number by itself, the control sends a SetStation message with this number (may
or may not be a valid station number). Otherwise, the REPL ignores the input. 

At the same time, the main thread handles listening for announcements from the
server. The program returns if it receives: an InvalidCommand from the server,
another Welcome message is received, a message with an unkonwn messageType, an
Announce if the station has not been set yet (used a mutex to sync with REPL),
or if any receive times out as per the timeout conditions. Otherwise, it prints
any announce messages it receives to stdout.

### Listener
The listener program is straightforward. It first sets up a UDP socket on the
port that the user passes in. Then, in `receive_stream`, it continually calls
`recvfrom` on the socket to fill a buffer of size 1024 and writes the buffer to
stdout. Note that the listener does not control the data rate; it simply reads
as fast as it can from the socket.

## Bugs
I'm aware of one test that occasionally fails: TestStreamRateMultipleStations. 

As I did this project in C, I fully expect there to be occasional memory leaks
or segfaults. I tried my best to eliminate any errors I encountered with Address
Sanitizer. With more time I would be a lot more thorough with implementing
destructors and clean up functions for allocated memory and threads. Moreover, I
attempted to handle signals (mainly SIGINT), but without proper clean up
procedures and a only hazy recollection of how to properly handle them, I did
not complete this functionality. 