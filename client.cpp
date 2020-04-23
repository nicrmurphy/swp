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
#include <chrono>
#include <ctime>
#include <thread>

#define PORT "9898"
#define MAX_DATA_SIZE 1024 * 8
#define MAX_FRAME_SIZE 1024 * 8 + 10 // to hold extra header data

using namespace std;

int lps;    // last packet sent
mutex lps_mutex;

int lar;    // last ack received
mutex lar_mutex;

int sockfd;

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

/**
 * Returns number of bytes sent
 */
int send_packet(addrinfo *servinfo, char *frame, const int seq_num, char *data, size_t data_size, bool end) {
    int bytes_sent;
    int frame_size = pack_data(frame, seq_num, data, data_size, end);
    if ((bytes_sent = sendto(sockfd, frame, frame_size, 0, servinfo->ai_addr, servinfo->ai_addrlen)) == -1) {
        perror("sendto");
        return 1;
    }

    lps_mutex.lock();
    lps = seq_num;
    lps_mutex.unlock();

    return bytes_sent;
}

void recv_ack(addrinfo *server, const int num_acks) {
    int timeout = 0;

    socklen_t addr_len = sizeof server;
    // sleep until receives next packet
    uint8_t ack;
    bool done = false;
    while (!done) {
        if (recvfrom(sockfd, &ack, 1, 0, (struct sockaddr *) server, &addr_len) == -1) {
            perror("recvfrom");
            exit(1);
        } else {
            printf("received ack %d\n", ack);
            if (ack == num_acks - 1) done = true;
            lar_mutex.lock();
            lar = ack;
            lar_mutex.unlock();
        }
    }
    cout << "recv_ack thread complete \n";
}

// int gbn() {
//     int total_packets = 15;
//     size_t window_size = 10;
//     char *window = new char[window_size];
//     // time_t timers[] = new time_t[total_packets];
//     int lw = 0;
//     int rw = window_size;
//     int lar;    // last ack received
//     bool done = false;
    
//     while (!done) {
//         // send packets in window
//         for (int i = 0; i < window_size; i++) {
//             send_packet(i);
//             // timers[i] = chrono::system_clock::now();
//         }

//         // if something is able to be received (poll? , select?)
//             // received ack(n)
//             // lar = recv_ack(0);
//             // check if n is smallest un-acked packet
//            if (lar >= lw) {
//                 lw = lar + 1;
//                 rw = lw + window_size;
//             }

//         // set done = true when received all acks (packet termination flag)
//         if (lar == total_packets - 1) done = true;
//     }
//     delete[] window; 
//     // delete[] timers; 

//     return 0;
// }

// int sr(){
//     int total_packets = 15;
//     size_t window_size = 10;
//     char *window = new char[window_size];
//     // time_t timers[] = new time_t[total_packets];
//     bool acked[window_size];
//     int lw = 0;
//     int rw = window_size;
//     int lar;    // last ack received
//     bool done = false;
    
//     while (!done) {
//         // send packets in window
//         for (int i = 0; i < window_size; i++) {
//             if(acked[i] == false){
//                 send_packet(i);
//             }
//             // timers[i] = chrono::system_clock::now();
//         }

//         // if something is able to be received (poll? , select?)
//             // received ack(n)
//             // lar = recv_ack(0);
//             acked[lar] = true;
//             // check if n is smallest un-acked packet
//            for (size_t i = 0; i < window_size; i++)
//            {
//                //stop shifting at the first unacked window
//                if(acked[i] == false){
//                    break;
//                }
//                //shift window right
//                lw++;
//                 rw = lw + window_size;
//            }
//         // set done = true when received all acks (packet termination flag)
//         if (lar == total_packets - 1) done = true;
//     }
//     delete[] window; 
//     // delete[] timers; 

//     return 0;
    
//     // send packet Sn

//     // start timeout timer for Sn

//     //if timeout,
//         //Send packet again,
//         //restart timeout timer

//     // if get ack sb 
//         // mark packet as received 
//         // if the seqence number is the smallest unacked packet
//             //shift sb to the next unacked packet

//     return 0;
// }

int main(int argc, char *argv[]) {
    // string protocol;
    // int packetSize;
    // int timeoutInterval;
    // int sizeOfWindow;
    // int rangeOfSequence;
    
    //  promptUserInput(&protocol, &packetSize, &timeoutInterval, &sizeOfWindow, &rangeOfSequence);

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
    struct addrinfo hints, *servinfo, *clientinfo;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    // fill servinfo with addrinfo
    int status;
    if ((status = getaddrinfo(host, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        exit(1);
    }
    // servinfo now points to a linked list of 1 or more struct addrinfos

    // fill clientinfo with addrinfo
    hints.ai_flags = AI_PASSIVE;
    if ((status = getaddrinfo(NULL, "7819", &hints, &clientinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        exit(1);
    }
    // clientinfo now points to a linked list of 1 or more struct addrinfos

    // loop through all the results and make a socket
    struct addrinfo *node;
    for (node = clientinfo; node != NULL; node = node->ai_next) {
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
    freeaddrinfo(clientinfo); // free the linked list

    // if successful, node now points to the info of the successfully created socket
    if (node == NULL) {
        fprintf(stderr, "failed to create socket\n");
        exit(1);
    }
    thread recv_thread(recv_ack, node, numBlocks);

    // break up file data into packets and send packets
    int bytes_sent = 0, total = 0;
    char frame[MAX_FRAME_SIZE];
    int end = false;
    int num_packets_sent = 0;
    // read each section of data from the file, package, and send them 
    for (int i = 0; i < numBlocks; i++)
    {
        int seq_num = i % 256;      // TODO: implement sequence numbers
        int data_size = MAX_DATA_SIZE;
        if(i == numBlocks - 1){
            data_size = leftover;
            end = true;
        } 
        // read the data from the file. This should probably later be done in larger chunks
        src.read(data,data_size);
        bytes_sent = send_packet(servinfo, frame, seq_num, data, data_size, end);
        total += bytes_sent;
        num_packets_sent++;
        cout << "sent packet " << seq_num << "; " << bytes_sent << " (total: " << total << ") bytes to " << host << "\n";
    }
    cout << "sent " << total << " bytes in " << num_packets_sent << " packets to " << host << "\n";
    src.close();

    cout << endl;
    cout << "main thread complete\n";
    recv_thread.detach();
    // close(sockfd);
    return 0;
}