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
#define MAX_DATA_SIZE 1024 * 9  // 9 kb
#define MAX_FRAME_SIZE 1024 * 9 + 10 // to hold extra header data

using namespace std;

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

int pack_data(char* frame, int seq_num, char* buff, int buff_size, bool end){
    frame[0] = end ? 0x0 : 0x1;
    uint32_t net_seq_num = htonl(seq_num);
    uint32_t net_data_size = htonl(buff_size);
    memcpy(frame + 1, &net_seq_num, 4);
    memcpy(frame + 5, &net_data_size, 4);
    memcpy(frame + 9, buff, buff_size);
    frame[buff_size + 9] = checksum(frame, buff_size + (int) 9);

    return buff_size + (int)10;
}

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
    char data[MAX_DATA_SIZE];
    long data_len;
    ifstream src(filepath, ios::in | ios::binary | ios::ate);
    if (src.is_open()) {
        data_len = src.tellg();     // fill data_len with size of file in bytes
        //data = new char[data_len];  // allocate memory for data memory block
        src.seekg(0, ios::beg);     // change stream pointer location to beginning of file
       // src.read(data, data_len);   // read in file contents to data
       // src.close();                // close iostream
    } else {
        fprintf(stderr, "failed to read file %s\n", filepath);
        exit(1);
    }

    // determine the number of packets required for the transfer
    int numBlocks = data_len / (MAX_DATA_SIZE);
    // determine size of last packet
    int leftover = data_len % (MAX_DATA_SIZE);
    if(leftover){
        numBlocks++;
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
    char frame[MAX_FRAME_SIZE];
    int frame_size;
    int end = false;
    // read each section of data from the file, package, and send them 
    for (int i = 0; i < numBlocks; i++)
    {
        
        int data_size = MAX_DATA_SIZE;
        if(i == numBlocks - 1){
            data_size = leftover;
            end = true;
        } 
        // read the data from the file. This should probably later be done in larger chunks
        src.read(data,data_size);
        frame_size = pack_data(frame,0,data, data_size, end);
        if ((bytes_sent = sendto(sockfd, frame, frame_size, 0, node->ai_addr, node->ai_addrlen)) == -1) {
            perror("sendto");
            exit(1);
        }
        total += bytes_sent;
        cout << "sent " << bytes_sent << " (" << total << "/" << data_len << ") bytes to " << host << "\n";
    }
    cout << "sent " << total << " bytes to " << host << "\n";
    src.close();

    cout << endl;
    close(sockfd);
    return 0;
}