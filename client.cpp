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

#define PORT "9898"

int main(int argc, char *argv[]) {
    // TODO: replace command line arguments with prompts
    if (argc != 3) {
        fprintf(stderr, "usage: %s hostname message\n", argv[0]);
        fprintf(stderr, "ex: %s localhost test\n", argv[0]);
        fprintf(stderr, "Sending \"exit\" to the server will close the server\n");
        exit(1);
    }
    char *host = argv[1];
    char *msg = argv[2];

    struct addrinfo hints, *servinfo;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    int status;
    if ((status = getaddrinfo(host, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        exit(1);
    }
    // servinfo now points to a linked list of 1 or more struct addrinfos

    // loop through all the results and make a socket
    struct addrinfo *node;
    int sockfd;
    for (node = servinfo; node != NULL; node = node->ai_next) {
        // attempt socket syscall
        if ((sockfd = socket(node->ai_family, node->ai_socktype, node->ai_protocol)) == -1) {
            perror("socket");
            continue;
        }
        break;
    }

    // if successful, node now points to the info of the successfully created socket
    if (node == NULL) {
        fprintf(stderr, "failed to create socket\n");
        exit(1);
    }

    int bytes_sent;
    if ((bytes_sent = sendto(sockfd, msg, strlen(msg), 0, node->ai_addr, node->ai_addrlen)) == -1) {
        perror("sendto");
        exit(1);
    }

    freeaddrinfo(servinfo); // free the linked list

    printf("sent %d bytes to %s\n", bytes_sent, host);
    close(sockfd);

    return 0;
}