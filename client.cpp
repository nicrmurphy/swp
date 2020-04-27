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
#include <time.h>
#include <mutex>

#define PORT "9898"
#define MAX_DATA_SIZE 64000
#define MAX_FRAME_SIZE 64000 + 10 // to hold extra header data

using namespace std;

int sockfd;
char *host;
char *filepath;
int window_size;
int seq_size;
bool gbn;

bool *acked;
time_t gbn_timeout;
time_t *sr_timeouts;
long data_pos = 0;  // holds how many consecutive bytes have been acked
long data_len;      // size of entire data buffer
int num_packets_sent = 0;
int num_packets_resent = 0;
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

void print_window() {
    cout << "Current window = [";
    for (long i = data_pos; i < data_pos + (window_size * MAX_DATA_SIZE); i += MAX_DATA_SIZE) {
        long seq_num = (i / MAX_DATA_SIZE) % seq_size;
        cout << (i != data_pos ? ", " : "") << seq_num;
    }
    cout << "]" << endl;
}

/**
 * Prints ack mask array to console. Used in debugging.
 */
void print_acked() {
    cout << "         Acked = [";
    for (long i = data_pos; i < data_pos + (window_size * MAX_DATA_SIZE); i += MAX_DATA_SIZE) {
        long seq_num = (i / MAX_DATA_SIZE) % seq_size;
        cout << (i != data_pos ? ", " : "") << (seq_num > 9 ? " " : "") << acked[seq_num % window_size];
    }
    cout << "]" << endl;
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
 * Returns number of bytes sent
 */
int send_packet(addrinfo *servinfo, const int seq_num, char *data, size_t data_size, bool end) {
    char frame[MAX_FRAME_SIZE];
    int bytes_sent;
    int frame_size = pack_data(frame, seq_num, data, data_size, end);
    if ((bytes_sent = sendto(sockfd, frame, frame_size, 0, servinfo->ai_addr, servinfo->ai_addrlen)) == -1) {
        perror("sendto");
        return 1;
    }

    return bytes_sent;
}

void recv_ack(addrinfo *server, const int num_acks) {
    socklen_t addr_len = sizeof server;
    // sleep until receives next packet
    uint8_t ack;
    bool done = false;
    while (!done) {
        if (recvfrom(sockfd, &ack, 1, 0, (struct sockaddr *) server, &addr_len) == -1) {
            perror("recvfrom");
            exit(1);
        }
        // TODO: improve situational errors
        // if ((clock() & 2) == 0) continue;   // if system time is even number, drop ack
        printf("Ack %d received\n", ack);

        // count ack and slide window
        window_mutex.lock();
        // gbn always slides window to lar within window; sr only slides if lw is acked
        acked[ack % window_size] = true;
        int lw = (data_pos / MAX_DATA_SIZE) % window_size;
        // int rw = (lw + window_size) % window_size;
        int a = data_pos / MAX_DATA_SIZE;
        while (gbn && a <= ack) {
            // cout << (a % seq_size) << endl;  // debug
            acked[a % seq_size] = true;     // ack all packets < lar
            a++;
        }
        while (acked[lw]) {
            data_pos += MAX_DATA_SIZE;
            if (data_pos >= data_len) {
                window_mutex.unlock();
                cout << "recv_ack thread complete \n";
                return;
            }
            acked[lw] = false;
            lw = (data_pos / MAX_DATA_SIZE) % window_size;
        }
        print_window();
        // print_acked();  // debug
        window_mutex.unlock();
    }
    cout << "recv_ack thread complete \n";
}

/**
 * Send all the packets in the window
 */
void send_window(addrinfo *servinfo, char *data) {
    long rw = data_pos + (window_size * MAX_DATA_SIZE);
    bool end = false;
    if (rw >= data_len) {
        end = true;
        rw = data_len;
    }
    long leftover = data_len % MAX_DATA_SIZE;
    long packet_data_size = MAX_DATA_SIZE;
    // TODO: gbn sends all always; sr only sends non-acked packets
    for (long i = data_pos; i < rw; i += MAX_DATA_SIZE) {
        long seq_num = (i / MAX_DATA_SIZE) % seq_size;
        // cout << "end: " << end << ", leftover: " << leftover << ", i: " << i << ", rw: " << rw << endl;
        if (end && leftover && i >= rw - leftover) packet_data_size = leftover;
        char packet_data[packet_data_size];
        // cout << "data_len: " << data_len << " accessing data from " << i << " to " << i + packet_data_size << endl; 
        if (i + packet_data_size > data_len) i = data_len - packet_data_size;
        memcpy(packet_data, data + i, packet_data_size);
        /*int bytes_sent = */send_packet(servinfo, seq_num, packet_data, packet_data_size, end && i == rw - packet_data_size);
        cout << "Packet " << seq_num << " sent" << endl;//"; " << bytes_sent << " bytes to " << host << "\n";
    }
}

/**
 * read in entire file contents to data array
 */
void read_file(char **data, long *data_len) {
    ifstream src(filepath, ios::in | ios::binary | ios::ate);
    if (src.is_open()) {
        *data_len = src.tellg();     // fill data_len with size of file in bytes
        *data = new char[*data_len];  // allocate memory for data memory block
        src.seekg(0, ios::beg);     // change stream pointer location to beginning of file
        src.read(*data, *data_len);   // read in file contents to data
        src.close();                // close iostream
    } else {
        fprintf(stderr, "failed to read file %s\n", filepath);
        exit(1);
    }
}

/**
 * Transfer a file using sliding window.
 */
void window_transfer_file(addrinfo *clientinfo, addrinfo *servinfo){
    // read in file 
    char *data;
    read_file(&data, &data_len);

    // determine the number of packets required for the transfer
    int numBlocks = data_len / (MAX_DATA_SIZE);
    // determine size of last packet
    int leftover = data_len % (MAX_DATA_SIZE);
    if (leftover) numBlocks++;
    num_packets_sent = numBlocks;

    thread recv_thread(recv_ack, clientinfo, numBlocks);
    bool done = false;
    while (!done) {
        window_mutex.lock();
        if (data_pos >= data_len) break;
        send_window(servinfo, data);
        window_mutex.unlock();
        /*if (gbn) */this_thread::sleep_for(chrono::milliseconds(gbn_timeout));
    }
    delete[] data;
    recv_thread.detach();
}

void print_stats(clock_t start_time) {
    cout << "Number of original packets sent: " << num_packets_sent << endl;
    cout << "Number of retransmitted packets: " << num_packets_resent << endl;
    double elapsed_seconds = (clock() - start_time) / (double) CLOCKS_PER_SEC * 10;    // TODO: might be wrong
    cout << "Total elapsed time: " << (double) elapsed_seconds << endl;
    cout << "Total throughput (Mbps): " << data_len * 8 / elapsed_seconds / 1048576 << endl;
    cout << "Effective throughput: " << endl;
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
    window_size = 8;
    seq_size = 32;
    acked = new bool[window_size];
    sr_timeouts = new time_t[window_size];
    gbn = true;
    /*if (gbn) */gbn_timeout = 10;    // in ms

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
    freeaddrinfo(clientinfo);

    // if successful, node now points to the info of the successfully created socket
    if (node == NULL) {
        fprintf(stderr, "failed to create socket\n");
        exit(1);
    }

    // start timer
    clock_t start_time = clock();
    window_transfer_file(node, servinfo);
    print_stats(start_time);

    cout << endl;
    cout << "main thread complete\n";
    delete[] acked;
    delete[] sr_timeouts;
    close(sockfd);
    freeaddrinfo(servinfo);
    return 0;
}