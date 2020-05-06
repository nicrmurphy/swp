CC = g++
CFLAGS = -Wall -g -std=c++11 -pthread
NOPRINT = -D_DEBUG=0

all: server client
noprint: server_noprint client_noprint
	
server: balfanpt1456_murphynr0562_sellej8974_server.cpp
	$(CC) $(CFLAGS) balfanpt1456_murphynr0562_sellej8974_server.cpp -o server

client: balfanpt1456_murphynr0562_sellej8974_client.cpp
	$(CC) $(CFLAGS) balfanpt1456_murphynr0562_sellej8974_client.cpp -o client
	
server_noprint: balfanpt1456_murphynr0562_sellej8974_server.cpp
	$(CC) $(CFLAGS) $(NOPRINT) balfanpt1456_murphynr0562_sellej8974_server.cpp -o server

client_noprint: balfanpt1456_murphynr0562_sellej8974_client.cpp
	$(CC) $(CFLAGS) $(NOPRINT) balfanpt1456_murphynr0562_sellej8974_client.cpp -o client

clean:
	rm -rf server client *.dSYM
