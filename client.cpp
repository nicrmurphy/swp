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
#include <mutex>

#define PORT "9898"
#define MAX_DATA_SIZE 1024
#define MAX_FRAME_SIZE 1024 + 10 // to hold extra header data

using namespace std;

int lps;    // last packet sent
mutex lps_mutex;

int lar;    // last ack received
mutex lar_mutex;

int sockfd;
char *host;
char *filepath;

int window_size = 5;
int seq_size = 10;
bool acked[10];
char window[10][MAX_FRAME_SIZE];
int lw = 0;
int rw = window_size - 1;
int total_bytes_sent = 0;
mutex window_mutex;

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

// Returns true if index is in the current window
bool inWindow(int index){
    return (index >= lw && index <= rw) || (index >= lw && rw <= lw) || (index <= rw && rw <= lw);

}
int send_packet_no_pack(addrinfo *servinfo, char *frame, const int seq_num, bool end) {
    int bytes;
    //int frame_size = pack_data(frame, seq_num, data, data_size, end);
    if ((bytes = sendto(sockfd, frame, MAX_FRAME_SIZE, 0, servinfo->ai_addr, servinfo->ai_addrlen)) == -1) {
        perror("sendto");
        return 1;
    }

    lps_mutex.lock();
    lps = seq_num;
    lps_mutex.unlock();

    return bytes;
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
            // lar_mutex.lock();
            // lar = ack;
            // lar_mutex.unlock();

            window_mutex.lock();
            acked[ack] = true;

            // shift window
            while (acked[lw] == true) {
                lw = (lw + 1) % seq_size;
                rw = (rw + 1) % seq_size;
                total_bytes_sent += MAX_FRAME_SIZE;
                cout << "Window: [" << lw << " to " << rw << "]" << endl;

            }
            window_mutex.unlock();
        }
    }
    cout << "recv_ack thread complete \n";
}

/**
 * Send all the packets in the window
 */
void send_window(addrinfo *servinfo, char *frame, char *data, size_t data_size, bool end) {
    // loop until it hits the right wall
    for (size_t seq_num = lw % seq_size; seq_num != rw + 1; seq_num++)
    {
        int bytes_sent = send_packet(servinfo, frame, seq_num, data, data_size, end);
        cout << "sent packet " << seq_num << "; " << bytes_sent << " bytes to " << host << "\n";

    }
}

void gbn(addrinfo *clientinfo, addrinfo *servinfo) {
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
    thread recv_thread(recv_ack, clientinfo, numBlocks);

    // break up file data into packets and send packets
    char frame[MAX_FRAME_SIZE];
    int end = false;
    bool done = false;
    int seq_num = 0;
    int data_size = MAX_DATA_SIZE;
    int num_packets_sent = 0;
    // read each section of data from the file, package, and send them 
    while(!done) {
        if (num_packets_sent == numBlocks - 1) {
            if (leftover) {
                data_size = leftover;
            }
            end = true;
        }
        // read the data from the file. This should probably later be done in larger chunks
        src.read(data,data_size);
        send_window(servinfo, frame, data, data_size, end);

        // if(end){
        //     for 
        //         // check all ack

        //     if()
        // }
        // if (end && ) done = true;

        // end when the last packet has been sent and all acks in the acked array are true
        
    }
    // cout << "sent " << total << " bytes in " << num_packets_sent << " packets to " << host << "\n";
    src.close();
    recv_thread.detach();

    // int total_packets = 15;
    // [x,x,0,0,0]

    // size_t window_size = 10;
    // char *window = new char[window_size];
    // // time_t timers[] = new time_t[total_packets];
    // int lw = 0;
    // int rw = window_size;
    // int lar;    // last ack received
    // bool done = false;
    
    // while (!done) {
    //     // send packets in window
    //     for (int i = 0; i < window_size; i++) {
    //         send_packet(i);
    //         // timers[i] = chrono::system_clock::now();
    //     }

    //     // if something is able to be received (poll? , select?)
    //         // received ack(n)
    //         // lar = recv_ack(0);
    //         // check if n is smallest un-acked packet
    //        if (lar >= lw) {
    //             lw = lar + 1;
    //             rw = lw + window_size;
    //         }

    //     // set done = true when received all acks (packet termination flag)
    //     if (lar == total_packets - 1) done = true;
    // }
    // delete[] window; 
    // // delete[] timers; 

    // return 0;
}

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

void transfer_file(addrinfo *clientinfo, addrinfo *servinfo){
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
    thread recv_thread(recv_ack, clientinfo, numBlocks);

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
        if (i == numBlocks - 1) {
            if (leftover) {
                data_size = leftover;
            }
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
    recv_thread.detach();
}

/*
*   Transfer a file using sliding window.
    Currently only works if the total number of pakcets <= window size.
    TODO: add reading in data and clearing out old ones so it can slide back to zero.
*/
void window_transfer_file(addrinfo *clientinfo, addrinfo *servinfo){
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
    // create a buffer to hold all data
    char buffer[MAX_DATA_SIZE * numBlocks];
    memset(buffer, 0, MAX_DATA_SIZE *numBlocks );

    src.read(buffer, data_len);
    cout << "Sending file in " << numBlocks << " of size " << data_len << endl;

    //initialize the window and load in data
    // if the number of blocks exceeds the number of sequence numbers, fully load the array from 0 - seq_max
    if(numBlocks > seq_size){
       for (size_t i = 0; i < seq_size; i++)
       {
           // pack daata into the window from the buffer
           pack_data(window[i], i, buffer  + i * MAX_DATA_SIZE, MAX_DATA_SIZE, false);
           acked[i] = false;
        }
    }else{
        // numBlocks < max_seq_val
        // means there will be a window that's data size < MAX_DATA_SIZE
        if(numBlocks > 1){
            for (size_t i = 0; i < numBlocks - 1; i++)
            {

                pack_data(window[i], i, buffer  + i * MAX_DATA_SIZE, MAX_DATA_SIZE, false);
                acked[i] = false;
            } 
        }
        // calculate the data size of the last leftover packet
        int data_size;
        if(leftover){
            data_size = leftover;
        }else{
            data_size = MAX_DATA_SIZE;
        }
        //pack the data into it's slot in the window
        pack_data(window[numBlocks - 1], numBlocks - 1, buffer + (numBlocks - 1 ) * MAX_DATA_SIZE, data_size, true);
        acked[numBlocks - 1] = false;
    }
   
    thread recv_thread(recv_ack, clientinfo, numBlocks);

    bool done = false;
    bool end_of_file = false;
    int end_seq_num;
    int bytes_sent = 0;
    while(!done){
        // not sure if I should move the lock, it seems like the recv thread can never get into anything if this loop hoards the variables
        window_mutex.lock();
        //cycle through from [lw to rw]
        for (size_t seq_num = lw % seq_size; seq_num != rw + 1; seq_num++)
        {            
            //go through and send each packet in the window that has not been acked already
            if(total_bytes_sent < data_len && !acked[seq_num]){
                // if it is the last packet, make end of file
                //TODO: make a better way of keeping track if it's the last packet or not. total_bytes_sent currently just += with MAX_DATA_SIZE every time something is acked.
                if(total_bytes_sent + MAX_DATA_SIZE >= data_len && !end_of_file){
                    // keep track of the last sequence number to make sure we know when to end
                    end_seq_num = seq_num;
                    end_of_file = true;
                    
                }
                bytes_sent = send_packet_no_pack(servinfo, window[seq_num], seq_num, end_of_file);
                //cout << "sent packet " << seq_num << "; " << bytes_sent << " bytes to " << host << "\n";

            }
        }
        // if we have sent the last packet, and the ending sequence number is not in the window,
        // we know all packets have been sent and acked
        if(end_of_file){
            if(!inWindow(end_seq_num)){
                done = true;
            }
        } 
        window_mutex.unlock();
        
    }


    src.close();
    recv_thread.detach();
}

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
    host = argv[1];
    filepath = argv[2];

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

    //transfer_file(node, servinfo);
    window_transfer_file(node, servinfo);

    cout << endl;
    cout << "main thread complete\n";
    // close(sockfd);
    return 0;
}