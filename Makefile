CC = g++
CFLAGS = -Wall -g -std=c++11 -pthread

all: server client
	
server: server.cpp
	$(CC) $(CFLAGS) server.cpp -o server

client: client.cpp
	$(CC) $(CFLAGS) client.cpp -o client

clean:
	rm -rf server client *.dSYM
