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
#include <sstream>
#include <fstream>
#include <thread>
#include <stdint.h>

#define HOST NULL   // NULL = localhost
#define PORT "9898"

#ifndef _DEBUG
#define _DEBUG true
#endif

using namespace std;

uint32_t MAX_DATA_SIZE;
uint32_t MAX_FRAME_SIZE;
int window_size;
bool gbn;
int seq_size; 
int *recv_size; //Used to record the size of each packet in window. 0 if the window is ready to be filled
int lw = 0;
int rw;
int last_seq_num = -1;

int num_packets_recv;
int num_retransmitted_packets;

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
    if (_DEBUG) cout << "ack " << (int) seq_num << " sent\n";
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

bool* generateErrors(int sequenceRange){
    bool* errors = (bool*)malloc(sizeof(bool) * sequenceRange); //array of bool for each sequence number
    int chance = 10; //Out of 100 (%)
    srand(time(NULL));
    for(int i = 0; i < sequenceRange; i++){
        if((rand() % 100 + 1) <= chance){ //If chance has been met
            errors[i] = true; //Drop error at sequence number i
        }
    }
    
    return errors; //Return filled array of errors
}
bool* promptErrors(int sequenceRange){
    bool* errors = (bool*)malloc(sizeof(bool) * sequenceRange);
    string input;
    cout << "Input sequence numbers to drop packet in space separated list (2 4 5 6 7). Only one drop packet per sequence number" << endl;
    cout << "> ";
    getline(cin, input);
    getline(cin, input);

    stringstream ssin(input);
    string inputNumber;
    int i = 0;
    while(ssin >> inputNumber && i < sequenceRange){
        errors[stoi(inputNumber)] = true;
    }

    return errors;
}

void promptUserInput(string* protocol, int* packetSize, int* timeoutInterval, int* sizeOfWindow, int* rangeOfSequence, bool** errorArray){
    //START USER INPUT
    
    string input;

    cout << "Type of protocol (GBN or SR): ";
    getline(cin, input);
    if(!input.empty()){
        stringstream stream(input);
        stream >> *protocol;
    }
    cout << "Packet Size (kB) (32kB default): ";
    getline(cin, input);
    if(!input.empty()){
        istringstream stream(input);
        stream >> *packetSize;
    }
    cout << "Timeout interval (0 for ping calculated): ";
    getline(cin, input);
    if(!input.empty()){
        istringstream stream(input);
        stream >> *timeoutInterval;
    }
    cout << "Size of sliding window (5 default): ";
    getline(cin, input);
    if(!input.empty()){
        istringstream stream(input);
        stream >> *sizeOfWindow;
    }
    cout << "Range of sequence numbers (64 default): ";
    getline(cin, input);
    if(!input.empty()){
        istringstream stream(input);
        stream >> *rangeOfSequence;
    }

    string userInput;
    cout << "Situational Errors" << endl;
    cout << "------------------" << endl;
    cout << "1. None" << endl;
    cout << "2. Randomly Generated" << endl;
    cout << "3. User-Specified" << endl;
    cout << "> ";
    cin >> userInput;
    if(userInput.compare("1") == 0){
        *errorArray = (bool*)malloc(sizeof(bool) * (*rangeOfSequence));
        for(int i = 0; i < *rangeOfSequence; i++){
            (*errorArray)[i] = false;
        }
    } else if(userInput.compare("2") == 0){
        *errorArray = generateErrors(*rangeOfSequence);
    } else if(userInput.compare("3") == 0){
        *errorArray = promptErrors(*rangeOfSequence);
    }
    //END USER INPUT
}

/**
 * Returns the last received ack for GBN
 */
int last_ack(){
    if(lw == 0){
        return seq_size - 1;
    }else{
        return lw - 1;
    }
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
 * Prints current window
 */
void print_window() {
    cout << "Current window = [";
    for (long i = lw % seq_size; i != rw + 1; i++) {
        if(i == seq_size){
            i = 0;
        }
        cout << i << (i != rw ? ", " : "");
        //cout << i << ", ";
    }
    cout << "]" << endl;
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
 * Write the contents in data buffer to output file
 */
void write_file(ofstream &dst, char *data, size_t data_size) {
    // write packet contents to file
    if (dst.is_open()) {
        dst.write(data, data_size);
        //cout << "wrote " << data_size << " bytes to file." << endl;
        dst.seekp(0, ios::end);
    }
}

void check_buffer(ofstream &dst, char *data, size_t *data_filled, int databuff_size) {
    if (*data_filled + databuff_size > MAX_DATA_SIZE * 8) {
        write_file(dst, data, *data_filled);
        *data_filled = 0;
    }
}

void print_stats() {
    cout << "Last packet seq# received:" << last_seq_num << endl; 
    cout << "Number of original packets received: " << num_packets_recv << endl;
    cout << "Number of retransmitted packets: " << num_retransmitted_packets << endl;
}

/**
 * Transfer a file using sliding window.
 */
int window_recv_file(char *data, size_t *data_filled, bool* errorArray) {
    ofstream dst("dst");
    char window[seq_size][MAX_DATA_SIZE];

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
    bool foundEnd = false;
    bool file_end = false;
    int total_bytes_recv = 0;
    num_packets_recv = 0;
    num_retransmitted_packets = 0;
    while (!file_end) {
        // sleep until receives next packet
        if ((bytes_recv = recvfrom(sockfd, frame, MAX_FRAME_SIZE, 0, (struct sockaddr *) &client, &addr_len)) == -1) {
            perror("recvfrom");
            continue;
        }

        if(errorArray != NULL && errorArray[lw]){ //If should drop packet at lw
            recv_size[lw] = 0; //Drop packet
            errorArray[lw] = false;
        } else{
            //unpack the sent frame
            frame_error = unpack_data(frame, &seq_num, data_buff, &databuff_size, &end);
            if (_DEBUG) {
                cout << "Packet " << seq_num << " received" << endl;
                if(frame_error){
                    cout << "Checksum Failed" << endl;        
                }else{
                    cout << "Checksum OK" << endl;
                    if (!gbn){
                        send_ack(sockfd, client, addr_len, seq_num);
                    }else{
                        if(inWindow(lw,rw,seq_num)){
                            send_ack(sockfd, client, addr_len, seq_num);
                        }else{
                            send_ack(sockfd, client, addr_len, last_ack());
                        }
                    }
                }
            }
            if(!frame_error && !recv_size[seq_num]&& inWindow(lw,rw,seq_num)){
                memcpy(window[seq_num], data_buff, databuff_size);
                recv_size[seq_num] = databuff_size;
                total_bytes_recv += bytes_recv;
                num_packets_recv++;
                //record the last sequence num
                if(end){
                    last_seq_num = seq_num;
                    foundEnd = true;
                }
            // cout << "received packet " << (int) seq_num << "; data: "<<databuff_size<<"; " << bytes_recv << " bytes (total: " << total_bytes_recv << ")\n";     // debug
            }else{
                num_retransmitted_packets++;
            }
        }
        
        // shift the window if needed
        while (recv_size[lw]) {
            lw = (lw + 1) % seq_size;
            rw = (rw + 1) % seq_size;
            if (_DEBUG) print_window();
            //write data to the buffer when rw is max or lw is min
            if(!end && (rw == seq_size - 1 || lw == 0)){
                for (int i = 0; i < seq_size; i++){
                    if(!inWindow(lw,rw,i) && recv_size[i] ){
                        check_buffer(dst, data, data_filled, databuff_size);
                        memcpy(data + *data_filled, window[i], recv_size[i]);
                        *data_filled += recv_size[i];
                        // zero out the received array to signal it's empty
                        recv_size[i] = 0;
                    }
                }
            }
                   
        }   
        //write out ending data to the buffer
        if(foundEnd && !inWindow(lw,rw,last_seq_num) && last_seq_num >= 0){
            //write out the data in the window to the buffer 
            for (int i = 0; i <= last_seq_num; i++)
            {   
                if(recv_size[i]){
                    check_buffer(dst, data, data_filled, databuff_size);
                    memcpy(data + *data_filled, window[i], recv_size[i]);
                    *data_filled += recv_size[i];
                }
            } 
            write_file(dst, data, *data_filled);    // write remainder of buffer to file
            cout << "Received File in " << num_packets_recv << " packets. Closing" << endl;
            file_end = true;
            dst.close();
        }
    }
    
    return total_bytes_recv;
}


int main(int argc, char *argv[]) {
    string protocol;
    int packetSize = 32000;
    int timeoutInterval;
    int sizeOfWindow = 5;
    int rangeOfSequence = 64;
    bool* errorArray;
    promptUserInput(&protocol, &packetSize, &timeoutInterval, &sizeOfWindow, &rangeOfSequence, &errorArray);

    MAX_DATA_SIZE = 65000;
    MAX_FRAME_SIZE = MAX_DATA_SIZE + 10;
    window_size = 7;
    gbn = false;
    //Recv window will always be 1 with GBN
    if(gbn){
        window_size = 1;
    }
    //Seq_size must be the same as in client
    seq_size = 64;
    //Used to record the size of each packet. 0 if the window is ready to be filled
    recv_size = new int[seq_size];
    rw = window_size - 1;
    //promptUserInput(&protocol, &packetSize, &timeoutInterval, &sizeOfWindow, &rangeOfSequence);
    create_socket();

    char *data = new char[MAX_DATA_SIZE*8];
    size_t data_filled = 0;

    //int total_bytes_recv = recv_file(data, &data_filled);
    int total_bytes_recv = window_recv_file(data, &data_filled, errorArray);

    delete[] data;
    delete[] recv_size;

    // char client_addr[INET6_ADDRSTRLEN];
    // inet_ntop(client.ss_family, get_in_addr((struct sockaddr *) &client), client_addr, sizeof client_addr);
    cout << "received " << total_bytes_recv << " bytes\n";

    print_stats();
    close(sockfd);
    return 0;
}