CC = g++
CFLAGS = -Wall -g -std=c++11 -pthread
NOPRINT = -D_DEBUG=0

all: server client
noprint: server_noprint client_noprint
	
server: balfanz_murphy_sell_server.cpp
	$(CC) $(CFLAGS) balfanz_murphy_sell_server.cpp -o server

client: balfanz_murphy_sell_client.cpp
	$(CC) $(CFLAGS) balfanz_murphy_sell_client.cpp -o client
	
server_noprint: balfanz_murphy_sell_server.cpp
	$(CC) $(CFLAGS) $(NOPRINT) balfanz_murphy_sell_server.cpp -o server

client_noprint: balfanz_murphy_sell_client.cpp
	$(CC) $(CFLAGS) $(NOPRINT) balfanz_murphy_sell_client.cpp -o client

clean:
	rm -rf server client *.dSYM
