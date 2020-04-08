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
#define MAX_DATA_SIZE 1023 * 9 // 9 kb
#define MAX_FRAME_SIZE 1024 * 9 + 10 // to hold extra header data


using namespace std;

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *addr) {
	if (addr->sa_family == AF_UNSPEC) {
		return &(((struct sockaddr_in*)addr)->sin_addr);
	}
	return &(((struct sockaddr_in6*)addr)->sin6_addr);
}

char checksum(char *frame, int count) {
    u_long sum = 0;
    while (count--) {
        sum += *frame++;
        if (sum & 0xFFFF0000) {
            sum &= 0xFFFF;
            sum++; 
        }
    }
    return (sum & 0xFFFF);
}

bool unpack_data(char* frame, int* seq_num, char* buff, int* buff_size, bool* end){
    *end = frame[0] == 0x0 ? true : false;

    uint32_t net_seq_num;
    memcpy(&net_seq_num, frame + 1, 4);
    *seq_num = ntohl(net_seq_num);

    uint32_t net_data_size;
    memcpy(&net_data_size, frame + 5, 4);
    *buff_size = ntohl(net_data_size);

    memcpy(buff, frame + 9, *buff_size);

    return frame[*buff_size + 9] != checksum(frame, *buff_size + (int) 9);
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
    char frame[MAX_FRAME_SIZE];
    char data_buff[MAX_DATA_SIZE];
    int bytes_recv;
    int seq_num;
    int frame_error;
    int databuff_size;
    bool end;
    ofstream dst ("dst");
    bool file_end = false;
    while (!file_end) {
        // sleep until receives next packet
        if ((bytes_recv = recvfrom(sockfd, frame, MAX_FRAME_SIZE, 0, (struct sockaddr *) &client, &addr_len)) == -1) {
            perror("recvfrom");
            exit(1);
        }
        frame_error = unpack_data(frame, &seq_num, data_buff, &databuff_size, &end);
        if(frame_error){
            
        }
        char client_addr[INET6_ADDRSTRLEN];
        inet_ntop(client.ss_family, get_in_addr((struct sockaddr *) &client), client_addr, sizeof client_addr);
        cout << "received " << bytes_recv << " bytes from " << client_addr << '\n';

        // write packet contents to file
        if (dst.is_open()) {
            dst.write(data_buff, databuff_size);
            dst.seekp(0, ios::end);
        }

        if(end){
            cout << "Received File. Closing" << endl;
            file_end = true;
        }
    }
    dst.close();
    close(sockfd);
    return 0;
}