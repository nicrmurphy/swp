CC = g++
CFLAGS = -Wall -g -std=c++11

all: clean server client
	
server:
	$(CC) $(CFLAGS) server.cpp -o server

client:
	$(CC) $(CFLAGS) client.cpp -o client

clean:
	rm -rf server client *.dSYM
