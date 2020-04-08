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

#define PORT "9898"
#define MAX_PKT_SIZE 256 * 36  // 36 bytes

using namespace std;

int main(int argc, char *argv[]) {
    // TODO: replace command line arguments with prompts
    if (argc != 3) {
        fprintf(stderr, "usage: %s hostname filepath\n", argv[0]);
        fprintf(stderr, "ex: %s thing2 src\n", argv[0]);
        exit(1);
    }
    char *host = argv[1];
    char *filepath = argv[2];

    // read in file 
    char *data;
    streampos data_len;
    ifstream src(filepath, ios::in | ios::binary | ios::ate);
    if (src.is_open()) {
        data_len = src.tellg();     // fill data_len with size of file in bytes
        data = new char[data_len];  // allocate memory for data memory block
        src.seekg(0, ios::beg);     // change stream pointer location to beginning of file
        src.read(data, data_len);   // read in file contents to data
        src.close();                // close iostream
    } else {
        fprintf(stderr, "failed to read file %s\n", filepath);
        exit(1);
    }

    // prepare socket
    struct addrinfo hints, *servinfo;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    // fill servinfo with addrinfo
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
    freeaddrinfo(servinfo); // free the linked list

    // if successful, node now points to the info of the successfully created socket
    if (node == NULL) {
        fprintf(stderr, "failed to create socket\n");
        exit(1);
    }

    // break up file data into packets and send packets
    int bytes_sent = 0, total = 0;
    while (total < data_len) {
        int pkt_size = (int) data_len - total;
        pkt_size = pkt_size > MAX_PKT_SIZE ? MAX_PKT_SIZE : pkt_size;
        if ((bytes_sent = sendto(sockfd, data + total, pkt_size, 0, node->ai_addr, node->ai_addrlen)) == -1) {
            perror("sendto");
            exit(1);
        }
        total += bytes_sent;
        cout << "sent " << bytes_sent << " (" << total << "/" << data_len << ") bytes to " << host << "\n";
    }

    cout << endl;
    close(sockfd);
    return 0;
}