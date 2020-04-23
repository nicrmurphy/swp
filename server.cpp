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
#include <thread>
#include <stdint.h>

#define HOST NULL   // NULL = localhost
#define PORT "9898"
#define MAX_DATA_SIZE 1024 * 8
#define MAX_FRAME_SIZE 1024 * 8 + 10 // to hold extra header data
#define MB_512 536870912

using namespace std;

int sockfd;

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *addr) {
	if (addr->sa_family == AF_UNSPEC) {
		return &(((struct sockaddr_in*)addr)->sin_addr);
	}
	return &(((struct sockaddr_in6*)addr)->sin6_addr);
}

// CURRENT MAX SEQUENCE NUMBER: 256
int send_ack(const int sockfd, sockaddr_storage client, socklen_t addr_len, const uint8_t seq_num) {
    if (sendto(sockfd, &seq_num, 1, 0, (struct sockaddr *) &client, addr_len) == -1) {
        perror("sendto");
        exit(1);
    }
    cout << "ack " << (int) seq_num << " sent\n";
    return 0;
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

void generateErrors(){}
void promptErrors(){}

void promptUserInput(string* protocol, int* packetSize, int* timeoutInterval, int* sizeOfWindow, int* rangeOfSequence){
    //START USER INPUT
    
    cout << "Type of protocol (GBN or SR): ";
    cin >> *protocol;
    cout << "Packet Size (kB): ";
    cin >> *packetSize;
    cout << "Timeout interval (0 for ping calculated): ";
    cin >> *timeoutInterval;
    cout << "Size of sliding window: ";
    cin >> *sizeOfWindow;
    cout << "Range of sequence numbers: ";
    cin >> *rangeOfSequence;

    string userInput;
    cout << "Situational Errors" << endl;
    cout << "------------------" << endl;
    cout << "1. None" << endl;
    cout << "2. Randomly Generated" << endl;
    cout << "3. User-Specified" << endl;
    cout << "> ";
    cin >> userInput;
    if(userInput.compare("2") == 0){
        generateErrors();
    } else if(userInput.compare("3") == 0){
        promptErrors();
    }

    //END USER INPUT
}

// LAR -> last ack received
// LFR -> last frame received
// LFS -> last frame sent
// VAR -> expected frame
// LW -> left wall
// RW -> right wall
// SWS -> sender window size
// RWS -> receiver window wise

void gbn(){
    //Receive Packets
    //If packet is expected packet
        //Accept packet
        //send ack for Sequence number
        //Seq num ++
        //If seq num > Smax
            //seq num = 0 or seq min
    //If packet is not expected packet AND seq num > 0
        //send ack for previously received packet
    //Else
        //If lastAck = null
            //Dont send anything, waiting for the first packet

}

void sr(){
    //Receive packet n
    // if n is in [sb, sb + windowsize -1]
        // send ack(n)
        //if n is not the smallest unreceived 
            // buffer n
    //else
        //deliver n and following in order buffered until next unreceived packet
}

/**
 * Sets global variable sockfd to valid socket file descriptor
 */
void create_socket() {
    // prepare socket syscall
    struct addrinfo hints, *servinfo;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;    // IPv4
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
}

/**
 * Returns total bytes received
 */
int recv_file(char *data, size_t *data_filled) {
    sockaddr_storage client;
    socklen_t addr_len = sizeof client;
    char frame[MAX_FRAME_SIZE];
    char data_buff[MAX_DATA_SIZE];
    int bytes_recv;
    int seq_num;
    int frame_error;
    int databuff_size;
    bool end;
    bool file_end = false;
    int total_bytes_recv = 0;
    int num_packets_recv = 0;
    while (!file_end) {
        // sleep until receives next packet
        if ((bytes_recv = recvfrom(sockfd, frame, MAX_FRAME_SIZE, 0, (struct sockaddr *) &client, &addr_len)) == -1) {
            perror("recvfrom");
            continue;
        }
        frame_error = unpack_data(frame, &seq_num, data_buff, &databuff_size, &end);
        if(frame_error){
            
        }
        total_bytes_recv += bytes_recv;
        num_packets_recv++;
        cout << "received packet " << (int) seq_num << "; " << bytes_recv << " bytes (total: " << total_bytes_recv << ")\n";     // debug

        // send ack
        send_ack(sockfd, client, addr_len, seq_num);

        if (*data_filled + databuff_size > MB_512) {
            // TODO: write to file on new thread instead of killing the program
            fprintf(stderr, "Buffer not big enough");
            exit(1);
        }

        memcpy(data + *data_filled, data_buff, databuff_size);
        *data_filled += databuff_size;

        if(end){
            cout << "Received File in " << num_packets_recv << " packets. Closing" << endl;
            file_end = true;
        }
    }
    return total_bytes_recv;
}

int main(int argc, char *argv[]) {
    // string protocol;
    // int packetSize;
    // int timeoutInterval;
    // int sizeOfWindow;
    // int rangeOfSequence;
    
    //promptUserInput(&protocol, &packetSize, &timeoutInterval, &sizeOfWindow, &rangeOfSequence);
    create_socket();

    char *data = new char[MB_512];   // allocate 512 mb
    size_t data_filled = 0;
    int total_bytes_recv = recv_file(data, &data_filled);

    // write packet contents to file
    ofstream dst ("dst");
    if (dst.is_open()) {
        cout << "data_filled: " << data_filled << endl;
        dst.write(data, data_filled);
        // dst.seekp(0, ios::end);
    }
    delete[] data;

    // char client_addr[INET6_ADDRSTRLEN];
    // inet_ntop(client.ss_family, get_in_addr((struct sockaddr *) &client), client_addr, sizeof client_addr);
    cout << "received " << total_bytes_recv << " bytes\n";

    dst.close();
    close(sockfd);
    return 0;
}