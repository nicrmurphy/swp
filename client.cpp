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
#include <sstream>
#include <chrono>
#include <ctime>
#include <thread>
#include <time.h>
#include <mutex>
#include "ThreadPool.h"

#define PORT "9898"

#ifndef _DEBUG
#define _DEBUG true
#endif

using namespace std;

long MAX_DATA_SIZE;
long MAX_FRAME_SIZE;
int sockfd;
char *filepath;
int window_size;
int seq_size;
bool gbn;

bool *acked;
time_t gbn_timeout;
long data_pos = 0;          // holds how many consecutive bytes have been acked
long data_len;              // size of entire data buffer
int num_packets_sent = 0;
int num_packets_resent = 0;
mutex window_mutex;

mutex data_mutex;
bool program_done = false;
mutex done_mutex;

ThreadPool pool(24);    // if the program exits with error "Resource temporarily unavailable", decrement this constant
long long lfs_ts = -1;

struct packet_info {
    long seq_num;
    long data_loc;
    bool last_window;
    long leftover;
    long rw;
    long packet_data_size;
    bool final_packet;
};

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
    data_mutex.lock();
    memcpy(frame + 9, buff, buff_size);
    data_mutex.unlock();
    frame[buff_size + 9] = checksum(frame, buff_size + (int) 9);

    return buff_size + (int)10;
}

/**
 * Returns true if the given sequence number is within the current window
 */
bool valid_seq_num(const int seq_num) {
    for (long i = data_pos; i < data_pos + (window_size * MAX_DATA_SIZE); i += MAX_DATA_SIZE) {
        if ((i / MAX_DATA_SIZE) % seq_size == seq_num) return true;
    }
    return false;
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
        cout << (i != data_pos ? ", " : "") << (seq_num > 9 ? " " : "") << acked[(i / MAX_DATA_SIZE) % window_size];
    }
    cout << "]" << endl;
}

/**
 * 
 */
void print_indices() {
    cout << "       Indices = [";
    for (long i = data_pos; i < data_pos + (window_size * MAX_DATA_SIZE); i += MAX_DATA_SIZE) {
        long seq_num = (i / MAX_DATA_SIZE) % seq_size;
        cout << (i != data_pos ? ", " : "") << (seq_num > 9 ? " " : "") << (i / MAX_DATA_SIZE) % window_size;
    }
    cout << "]" << endl;
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
    cout << "Input sequence numbers to drop ack in space separated list (2 4 5 6 7). Only one drop ack per sequence number" << endl;
    cout << "> ";
    getline(cin, input);

    stringstream ssin(input);
    string inputNumber;
    int i = 0;
    while(ssin >> inputNumber && i < sequenceRange){
        errors[stoi(inputNumber)] = true;
    }

    return errors;
}

void promptUserInput(string* protocol, long* packetSize, int* timeoutInterval, int* sizeOfWindow, int* rangeOfSequence, bool** errorArray){
    //START USER INPUT
    
    string input;

    cout << "Type of protocol (GBN or SR) (SR default): ";
    getline(cin, input);
    if(!input.empty()){
        stringstream stream(input);
        stream >> *protocol;
    }
    cout << "Packet Size (B) (65010 B default): ";
    getline(cin, input);
    if(!input.empty()){
        istringstream stream(input);
        stream >> *packetSize;
    }
    cout << "Timeout interval (ms) (10 default): ";
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
    cout << "Range of sequence numbers (20 default): ";
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
    getline(cin, userInput);
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
}


/**
 * Returns number of bytes sent
 */
int send_packet(addrinfo *servinfo, char *packet_data, packet_info *info, const bool resend) {
    char frame[MAX_FRAME_SIZE];
    int bytes_sent;
    int frame_size = pack_data(frame, info->seq_num, packet_data, info->packet_data_size, info->final_packet);
    if ((bytes_sent = sendto(sockfd, frame, frame_size, 0, servinfo->ai_addr, servinfo->ai_addrlen)) == -1) {
        perror("sendto");
        return 1;
    }
    if (_DEBUG) {
        if (resend) {
            cout << "Packet " << info->seq_num << " Re-transmitted." << endl;
            num_packets_resent++;
        }
        else cout << "Packet " << info->seq_num << " sent" << endl;
    }

    return bytes_sent;
}

void get_packet_info(packet_info *info, const long data_pos, const long offset) {
    info->rw = min(data_pos + (window_size * MAX_DATA_SIZE), data_len);      // right wall of sending window
    info->last_window = info->rw >= data_len;                                // if the current window hits eof 
    info->data_loc = data_pos + offset;
    const long leftover = data_len % MAX_DATA_SIZE;
    info->packet_data_size = (info->last_window && leftover && info->data_loc >= info->rw - leftover) ? leftover : MAX_DATA_SIZE;
    info->seq_num = (info->data_loc / MAX_DATA_SIZE) % seq_size;
    info->final_packet = info->last_window && info->data_loc == info->rw - info->packet_data_size;
}

/**
 * A thread safe function used to send packets to the receiver.
 */
void packet_sender(addrinfo *servinfo, char *data, const long data_pos, const long offset, bool resend) {
    packet_info info;
    memset(&info, 0, sizeof info);
    get_packet_info(&info, data_pos, offset);
    if (info.data_loc > info.rw) return;    // requested packet is past outside the window

    // copy data to packet_data
    char packet_data[info.packet_data_size];
    data_mutex.lock();
    memcpy(packet_data, data + info.data_loc, info.packet_data_size);
    data_mutex.unlock();

    // bool resend = false;        // flag if packet is sent more than once
    int timeout_ms = 10;        // time in ms before packet is resent
    int send_count = 0;
    while (true) {
        window_mutex.lock();
        // exit if requested packet is outside the window
        if (info.data_loc < ::data_pos ||
            !valid_seq_num(info.seq_num) || 
            acked[(info.data_loc / MAX_DATA_SIZE) % window_size]
        ) break;
        if (_DEBUG && !gbn && resend) cout << "Packet " << info.seq_num << " *****Timed Out *****" << endl;
        if (send_count >= 10) {
            if (_DEBUG) cout << "Packet " << info.seq_num << " sent " << send_count << " times - closing." << endl;
            done_mutex.lock();
            program_done = true;
            done_mutex.unlock();
            break;
        }
        send_packet(servinfo, packet_data, &info, resend);
        if (gbn) {
            lfs_ts = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
            // cout << "new lfs_ts: " << lfs_ts << endl;
            if (gbn) break;        // gbn never resends from this function
        }
        window_mutex.unlock();
        resend = true;
        this_thread::sleep_for(chrono::milliseconds(timeout_ms));
        timeout_ms <<= 1;
        send_count++;
    }
    // cout << "closing thread for " << seq_num << endl;
    window_mutex.unlock();
}

void recv_ack(addrinfo *server, char *data, addrinfo *servinfo, bool *errorArray) {
    socklen_t addr_len = sizeof server;
    uint8_t ack;
    while (true) {
        // sleep until receives next packet
        if (::recvfrom(sockfd, &ack, 1, 0, (struct sockaddr *) server, &addr_len) == -1) {
            perror("recvfrom");
            exit(1);
        }
        if (errorArray != NULL && errorArray[ack]) {
            if (_DEBUG) printf("Ack %d dropped\n", ack);
            errorArray[ack] = false;
        } else {
            window_mutex.lock();
            if (_DEBUG) printf("Ack %d received\n", ack);
            if (valid_seq_num(ack)) {
                // count ack and slide window
                // gbn always slides window to lar within window; sr only slides if lw is acked
                int index = data_pos;
                while (((index / MAX_DATA_SIZE) % seq_size) != ack) index += MAX_DATA_SIZE; // cycle until index is found
                acked[(index / MAX_DATA_SIZE) % window_size] = true;
                index = data_pos / MAX_DATA_SIZE;
                while (gbn && index <= ack) {
                    acked[index % seq_size % window_size] = true;     // ack all packets < lar
                    index++;
                }
                int lw = (data_pos / MAX_DATA_SIZE) % window_size;
                while (acked[lw]) {
                    data_pos += MAX_DATA_SIZE;
                    if (data_pos >= data_len) {
                        window_mutex.unlock();
                        done_mutex.lock();
                        program_done = true;
                        done_mutex.unlock();
                        return;
                    }
                    acked[lw] = false;
                    lw = (data_pos / MAX_DATA_SIZE) % window_size;
                    // start next thread
                    pool.enqueue(packet_sender, servinfo, data, data_pos, (window_size - 1) * MAX_DATA_SIZE, false);
                }
            }
            if (_DEBUG) print_window();
            // if (_DEBUG) print_acked();  // debug
            // if (_DEBUG) print_indices();
            window_mutex.unlock();
        }
    }
}

/**
 * Send all the packets in the window
 */
void send_window(addrinfo *servinfo, char *data, const bool resend) {
    long rw = min(data_pos + (window_size * MAX_DATA_SIZE), data_len);
    for (long data_loc = data_pos; data_loc < rw; data_loc += MAX_DATA_SIZE) {
        pool.enqueue(packet_sender, servinfo, data, data_pos, data_loc - data_pos, resend);
        this_thread::sleep_for(chrono::microseconds(100));
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
void window_transfer_file(addrinfo *clientinfo, addrinfo *servinfo, bool *errorArray) {
    // read in file 
    char *data;
    read_file(&data, &data_len);

    // determine the number of packets required for the transfer
    int numBlocks = data_len / (MAX_DATA_SIZE);
    // determine size of last packet
    int leftover = data_len % (MAX_DATA_SIZE);
    if (leftover) numBlocks++;
    num_packets_sent = numBlocks;

    if (gbn) pool.enqueue(recv_ack, clientinfo, data, servinfo, errorArray);
    bool resend = false;
    int send_count = 0;
    long prev_data_pos = 0;
    int timeout_ms = gbn_timeout;
    while (gbn) {
        window_mutex.lock();
        auto now = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
        // cout << "check if need to resend window: (" << now - lfs_ts << " >= " << gbn_timeout << ")" << endl;
        if (now - lfs_ts >= timeout_ms) {
            // resend window
            if (_DEBUG && resend) cout << "Window *****Timed Out *****" << endl;
            if (data_pos >= data_len) break;
            send_window(servinfo, data, resend);
            resend = true;
            if (prev_data_pos == data_pos) {
                send_count++;
                timeout_ms <<= 1;
            }
            else {
                send_count = 1;
                timeout_ms = gbn_timeout;
            }
            prev_data_pos = data_pos;
            if (send_count >= 10) {
                if (_DEBUG) cout << "Window sent " << send_count << " times - closing." << endl;
                break;
            }
            window_mutex.unlock();
            // cout << "sleeping for " << gbn_timeout << " ms" << endl;
            this_thread::sleep_for(chrono::milliseconds(timeout_ms));
        } else {
            window_mutex.unlock();
            // cout << "sleeping for " << gbn_timeout - (now - lfs_ts) << " ms" << endl;
            this_thread::sleep_for(chrono::milliseconds(gbn_timeout - (now - lfs_ts)));
        }
    }
    if (!gbn) {
        for (int t = 0; t < min(window_size, numBlocks); t++) {
            pool.enqueue(packet_sender, servinfo, data, 0, t * MAX_DATA_SIZE, false);
            this_thread::sleep_for(chrono::milliseconds(1));
        }
        pool.enqueue(recv_ack, clientinfo, data, servinfo, errorArray);
        while (true) {
            this_thread::sleep_for(chrono::milliseconds(10));
            done_mutex.lock();  // wait for recv_ack thread to complete
            if (program_done) break;
            done_mutex.unlock();
        }
    }
    delete[] data;
}

void print_stats(clock_t start_time) {
    cout << "Number of original packets sent: " << num_packets_sent << endl;
    cout << "Number of retransmitted packets: " << num_packets_resent << endl;
    auto end_time = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
    auto elapsed_ms = end_time - start_time;
    double elapsed_sec = elapsed_ms / 1000.0f;
    cout << "Total elapsed time: " << elapsed_sec << endl;
    cout << "Total throughput (Mbps): " << (num_packets_sent + num_packets_resent) * MAX_FRAME_SIZE / elapsed_sec / (1024*1024) << endl;
    cout << "Effective throughput: " << (num_packets_sent) * MAX_FRAME_SIZE / elapsed_sec / (1024*1024) << endl;
}

int main(int argc, char *argv[]) {
    string protocol;
    MAX_FRAME_SIZE = 65010;
    int timeoutInterval = 10;
    int sizeOfWindow = 7;
    int rangeOfSequence = 20;
    bool* errorArray;
    
    if (argc != 3) {
        fprintf(stderr, "usage: %s hostname filepath\n", argv[0]);
        fprintf(stderr, "ex: %s thing2 src\n", argv[0]);
        exit(1);
    }
    char *host = argv[1];
    filepath = argv[2];
    window_size = 5;
    seq_size = 20;
    acked = new bool[window_size];
    memset(acked, 0, window_size);
    promptUserInput(&protocol, &MAX_FRAME_SIZE, &timeoutInterval, &sizeOfWindow, &rangeOfSequence, &errorArray);
    MAX_DATA_SIZE = MAX_FRAME_SIZE - 10;
    gbn = strcmp(protocol.c_str(), "GBN") == 0;
    if (gbn) gbn_timeout = timeoutInterval;    // in ms
    window_size = sizeOfWindow;
    seq_size = rangeOfSequence;
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

    // if successful, node now points to the info of the successfully created socket
    if (node == NULL) {
        fprintf(stderr, "failed to create socket\n");
        exit(1);
    }

    // start timer
    auto start_time = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
    window_transfer_file(node, servinfo, errorArray);
    print_stats(start_time);

    delete[] acked;
    close(sockfd);
    freeaddrinfo(clientinfo);
    freeaddrinfo(servinfo);
    return 0;
}
