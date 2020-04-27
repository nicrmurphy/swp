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
#define MAX_DATA_SIZE 64000
#define MAX_FRAME_SIZE 64000 + 10 // to hold extra header data
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

/**
 * Returns true if the index is within the current window.
 */
bool inWindow(int lw, int rw, int index){

    if(lw == rw){
        return (lw == index);
    }else{

    return (index >= lw && index <= rw) || (index >= lw && rw <= lw) || (index <= rw && rw <= lw);
    }
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
 * Transfer a file using sliding window.
 */
int window_recv_file(char *data, size_t *data_filled) {

    int window_size = 5;
    bool gbn = true;
    //Recv window will always be 1 with GBN
    if(gbn){
        window_size = 1;
    }
    //Seq_size must be the same as in client
    int seq_size = 10;
    //Used to record the size of each packet. 0 if the window is ready to be filled
    int recv_size[seq_size];
    char window[seq_size][MAX_DATA_SIZE];
    int lw = 0;
    int rw = window_size - 1;
    int last_seq_num = -1;

    sockaddr_storage client;
    socklen_t addr_len = sizeof client;
    char frame[MAX_FRAME_SIZE];
    char data_buff[MAX_DATA_SIZE];
    memset(data_buff, '\0', MAX_DATA_SIZE);
    
    for (int i = 0; i < seq_size; i++)
    {
        recv_size[i] = 0;
    }
    
    int bytes_recv;
    int seq_num;
    int frame_error;
    int databuff_size;
    bool end;
    int end_packet_size = 0;
    bool foundEnd = false;
    bool file_end = false;
    int total_bytes_recv = 0;
    int num_packets_recv = 0;
    while (!file_end) {

        // sleep until receives next packet
        if ((bytes_recv = recvfrom(sockfd, frame, MAX_FRAME_SIZE, 0, (struct sockaddr *) &client, &addr_len)) == -1) {
            perror("recvfrom");
            continue;
        }

        //unpack the sent frame
        frame_error = unpack_data(frame, &seq_num, data_buff, &databuff_size, &end);
        if(frame_error){
            cout << "There has been a frame error!";
        }
        
        //only copy data into the window if it has not been received yet and it's crc passes
        if(!frame_error && !recv_size[seq_num]&& inWindow(lw,rw,seq_num)){
            memcpy(window[seq_num], data_buff, databuff_size);
            recv_size[seq_num] = databuff_size;
            total_bytes_recv += bytes_recv;
            num_packets_recv++;
            //record the last sequence num
            if(end){
                last_seq_num = seq_num;
                foundEnd = true;
                end_packet_size = databuff_size;
            }
            cout << "received packet " << (int) seq_num << "; data: "<<databuff_size<<"; " << bytes_recv << " bytes (total: " << total_bytes_recv << ")\n";     // debug
        }else{
            //cout << "Received exta packet " << seq_num << ", thowing." << endl;
        }

        // shift the window if needed
        while (recv_size[lw]) {
                send_ack(sockfd, client, addr_len, lw);

                lw = (lw + 1) % seq_size;
                rw = (rw + 1) % seq_size;
                //cout << "Window: [" << lw << " to " << rw << "]" << endl;
                //when the rw hits a maximum, write out all window data from 0 -> lw to the buffer
                if(rw == seq_size - 1){
                    for (int i = 0; i < lw; i++){
                        if(!end && recv_size[i] ){
                            //cout << "Writing " << "tp2 " << i << endl; //debug
                            memcpy(data + *data_filled, window[i], recv_size[i]);
                            *data_filled += MAX_DATA_SIZE;
                            // zero out the received array
                            recv_size[i] = 0;
                        }
                    }
                }
                //If lw is 0, write out all data from rw + 1 to the end into the buffer
                if(lw == 0){
                    for (int i = rw + 1; i < seq_size; i++){
                        if(!end && recv_size[i]){
                            //cout << "Writing " << "tp2 " << i << endl; //debug
                            memcpy(data + *data_filled, window[i], recv_size[i]);
                            *data_filled += MAX_DATA_SIZE;
                            recv_size[i] = 0;
                        }
                    }
                }
    }

        if (*data_filled + databuff_size > MB_512) {
            // TODO: write to file on new thread instead of killing the program
            fprintf(stderr, "Buffer not big enough");
            exit(1);
        }
        
        // Because end can change if packets arrive out of order, keep track of found End
        // if(end){
        //     foundEnd = true;
        //     end_packet_size = databuff_size;
        // }
       
        //write out ending data to the buffer
        if(foundEnd && !inWindow(lw,rw,last_seq_num) && last_seq_num > 0){
            // write out the data in the window to the buffer
            for (int i = 0; i < last_seq_num; i++)
            {   
                if(recv_size[i]){
                    //cout << "Writing " << "tp3 " << i << endl; //debug
                    memcpy(data + *data_filled, window[i], recv_size[i]);
                    *data_filled += MAX_DATA_SIZE;
                }
            } 
            // fill in the last packet into data
            if(recv_size[seq_num]){
                //cout << "Writing " << "tp4 " << last_seq_num << endl; //debug
                 memcpy(data + *data_filled, window[last_seq_num], end_packet_size);
                *data_filled += end_packet_size;
            }

            cout << "Received File in " << num_packets_recv << " packets. Closing" << endl;
            file_end = true;
            
        }
    }
    return total_bytes_recv;
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
            cout << "There has been a frame error!";
        }
        total_bytes_recv += bytes_recv;
        num_packets_recv++;
        cout << "received packet " << (int) seq_num << "; data: "<<databuff_size<<"; " << bytes_recv << " bytes (total: " << total_bytes_recv << ")\n";     // debug

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

    //int total_bytes_recv = recv_file(data, &data_filled);
    int total_bytes_recv = window_recv_file(data, &data_filled);


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