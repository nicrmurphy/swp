#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <iostream>
#include <fstream>

#define HOST NULL   // NULL = localhost
#define PORT "9898"
#define MAX_BUF 1024 * 9  // 9 kb

using namespace std;

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *addr) {
	if (addr->sa_family == AF_UNSPEC) {
		return &(((struct sockaddr_in*)addr)->sin_addr);
	}
	return &(((struct sockaddr_in6*)addr)->sin6_addr);
}

int main(int argc, char *argv[]) {
    // prepare socket syscall
    struct addrinfo hints, *servinfo;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;    // don't care IPv4 or IPv6
    hints.ai_socktype = SOCK_DGRAM; // datagram socket
    hints.ai_flags = AI_PASSIVE;    // fill in IP

    int status;
    if ((status = getaddrinfo(HOST, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        exit(1);
    }
    // servinfo now points to a linked list of 1 or more struct addrinfos

    // loop through all the results and bind to the first successful
    struct addrinfo *node;
    int sockfd;
    for (node = servinfo; node != NULL; node = node->ai_next) {
        // attempt socket syscall
        if ((sockfd = socket(node->ai_family, node->ai_socktype, node->ai_protocol)) == -1) {
            perror("socket");
            continue;
        }

        // if successful, bind socket
        if (::bind(sockfd, node->ai_addr, node->ai_addrlen) == -1) {
            close(sockfd);
            perror("bind");
            continue;
        }

        break;
    }

    // if successful, node now points to the info of the successfully bound socket
    if (node == NULL) {
        fprintf(stderr, "failed to bind socket\n");
        exit(1);
    }

    freeaddrinfo(servinfo); // free the linked list

    printf("listening on port %s...\n", PORT);

    sockaddr_storage client;
    socklen_t addr_len = sizeof client;
    char buf[MAX_BUF];
    int bytes_recv;
    ofstream dst ("dst");
    while (1) {
        // sleep until receives next packet
        if ((bytes_recv = recvfrom(sockfd, buf, MAX_BUF, 0, (struct sockaddr *) &client, &addr_len)) == -1) {
            perror("recvfrom");
            exit(1);
        }

        char client_addr[INET6_ADDRSTRLEN];
        inet_ntop(client.ss_family, get_in_addr((struct sockaddr *) &client), client_addr, sizeof client_addr);
        cout << "received " << bytes_recv << " bytes from " << client_addr << '\n';

        // write packet contents to file
        if (dst.is_open()) {
            dst.write(buf, bytes_recv);
            dst.seekp(0, ios::end);
        }
    }
    dst.close();
    close(sockfd);
    return 0;
}