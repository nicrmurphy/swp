main: server client
	
server: server.o 
	g++ -std=c++11 server.o -o server

client:client.o 
	g++ client.o -std=c++11 -o client
	
server.o: server.cpp
	g++ -c -std=c++11 server.cpp -o server.o
	
client.o: client.cpp
	g++ -c -std=c++11 client.cpp -o client.o

clean:
	rm server client *.o
