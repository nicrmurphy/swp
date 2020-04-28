CC = g++
CFLAGS = -Wall -g -std=c++11 -pthread
NOPRINT = -D_DEBUG=0

all: server client
noprint: server_noprint client_noprint
	
server: server.cpp
	$(CC) $(CFLAGS) server.cpp -o server

client: client.cpp
	$(CC) $(CFLAGS) client.cpp -o client
	
server_noprint: server.cpp
	$(CC) $(CFLAGS) $(NOPRINT) server.cpp -o server

client_noprint: client.cpp
	$(CC) $(CFLAGS) $(NOPRINT) client.cpp -o client

clean:
	rm -rf server client *.dSYM
