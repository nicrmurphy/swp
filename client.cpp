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
#include "ThreadPool.h"

#define PORT "9898"
#define MAX_DATA_SIZE 65000
#define MAX_FRAME_SIZE 65010 // to hold extra header data

#ifndef _DEBUG
#define _DEBUG true
#endif

using namespace std;

int sockfd;
char *host;
char *filepath;
int window_size;
int seq_size;
bool gbn;

bool *acked;
time_t gbn_timeout;
long data_pos = 0;  // holds how many consecutive bytes have been acked
long data_len;      // size of entire data buffer
int num_packets_sent = 0;
int num_packets_resent = 0;
mutex window_mutex;

mutex send_mutex;
mutex count_mutex;
int send_count = 0;
mutex data_mutex;
bool program_done = false;
mutex done_mutex;

ThreadPool pool(24);    // if the program exits with error "Resource temporarily unavailable", decrement this constant
long long lfs_ts = -1;
mutex lfs_ts_mutex;

struct packet_info {
    long seq_num;
    long data_loc;
    bool end;
    long leftover;
    long rw;
    long packet_data_size;
    bool resend;
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
int send_packet(addrinfo *servinfo, char *packet_data, bool end, bool resend, packet_info *info) {
    char frame[MAX_FRAME_SIZE];
    int bytes_sent;
    int frame_size = pack_data(frame, info->seq_num, packet_data, info->packet_data_size, end);
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

void send_thread(addrinfo *servinfo, char *data, long data_loc, bool end, long leftover, bool resend, packet_info *info) {
    // cout << "end: " << end << ", leftover: " << leftover << ", i: " << i << ", rw: " << rw << endl;
    if (end && leftover && data_loc >= info->rw - leftover) info->packet_data_size = leftover;
    char packet_data[info->packet_data_size];
    // cout << "data_len: " << data_len << " accessing data from " << i << " to " << i + packet_data_size << endl; 
    if (data_loc + info->packet_data_size > data_len) data_loc = data_len - info->packet_data_size;
    data_mutex.lock();
    memcpy(packet_data, data + data_loc, info->packet_data_size);
    data_mutex.unlock();
    send_packet(servinfo, packet_data, end && data_loc == info->rw - info->packet_data_size, resend, info);
    if (gbn) {
        lfs_ts_mutex.lock();
        lfs_ts = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
        // cout << "new lfs_ts: " << lfs_ts << endl;
        lfs_ts_mutex.unlock();
    }
}
void sr_thread(addrinfo *servinfo, char *data, const long data_pos, const long offset);
/**
 * Send all the packets in the window
 */
void send_window(addrinfo *servinfo, char *data, const bool resend) {
    long rw = data_pos + (window_size * MAX_DATA_SIZE);
    if (rw >= data_len) rw = data_len;
    for (long data_loc = data_pos; data_loc < rw; data_loc += MAX_DATA_SIZE) {
        pool.enqueue(sr_thread, servinfo, data, data_pos, data_loc - data_pos);
        this_thread::sleep_for(chrono::milliseconds(1));
    }
}

/**
 * 
 */
void sr_thread(addrinfo *servinfo, char *data, const long data_pos, const long offset) {
    // send packet

    // cout << "start thread " << this_thread::get_id() << " for offset " << offset << endl;
    long rw = data_pos + (window_size * MAX_DATA_SIZE);
    bool end = false;
    if (rw >= data_len) {
        end = true;
        rw = data_len;
    }
    long leftover = data_len % MAX_DATA_SIZE;
    long packet_data_size = MAX_DATA_SIZE;
    long data_loc = data_pos + offset;
    long seq_num = (data_loc / MAX_DATA_SIZE) % seq_size;
    // cout << "data pos: " << data_pos << "; offset: " << offset << "; seq num: " << seq_num << endl;
    if (data_loc > rw) return;

    bool done = false;
    bool resend = false;
    int timeout_ms = 10;
    window_mutex.lock();
    if (data_loc < ::data_pos) {
        window_mutex.unlock();
        return;
    }
    packet_info info;
    info.rw = rw;
    info.packet_data_size = packet_data_size;
    info.seq_num = seq_num;
    while (!done) {
        if (resend) {
            timeout_ms <<= 1;
            window_mutex.lock();
            if (data_loc < ::data_pos) break;
            if (!valid_seq_num(seq_num) || acked[(data_loc / MAX_DATA_SIZE) % window_size]) break;
            if (_DEBUG) cout << "Packet " << seq_num << " *****Timed Out *****" << endl;
        }
        send_thread(servinfo, data, data_loc, end, leftover, resend, &info);
        window_mutex.unlock();
        if (gbn) return;
        resend = true;
        this_thread::sleep_for(chrono::milliseconds(timeout_ms));
    }
    // cout << "closing thread for " << seq_num << endl;
    window_mutex.unlock();
}

void recv_ack(addrinfo *server, char *data, addrinfo *servinfo) {
    socklen_t addr_len = sizeof server;
    // sleep until receives next packet
    uint8_t ack;
    while (true) {
        if (::recvfrom(sockfd, &ack, 1, 0, (struct sockaddr *) server, &addr_len) == -1) {
            perror("recvfrom");
            exit(1);
        }
        // TODO: improve situational errors
        // if ((clock() & 2) == 0) continue;   // if system time is even number, drop ack
        // count ack and slide window
        window_mutex.lock();
        if (_DEBUG) printf("Ack %d received\n", ack);
        if (valid_seq_num(ack)) {
            // gbn always slides window to lar within window; sr only slides if lw is acked
            int index = data_pos;
            while (((index / MAX_DATA_SIZE) % seq_size) != ack) index += MAX_DATA_SIZE;
            // cout << "index: " << (index / MAX_DATA_SIZE) % window_size << endl;
            acked[(index / MAX_DATA_SIZE) % window_size] = true;
            int lw = (data_pos / MAX_DATA_SIZE) % window_size;
            // int rw = (lw + window_size) % window_size;
            int a = data_pos / MAX_DATA_SIZE;
            while (gbn && a <= ack) {
                // cout << "seq num: " << (a % seq_size) << ", index " << (a % seq_size % window_size) << " of " << window_size << endl;  // debug
                acked[a % seq_size % window_size] = true;     // ack all packets < lar
                a++;
            }
            while (acked[lw]) {
                data_pos += MAX_DATA_SIZE;
                if (data_pos >= data_len) {
                    window_mutex.unlock();
                    // cout << "recv_ack thread complete \n";
                    done_mutex.lock();
                    program_done = true;
                    done_mutex.unlock();
                    return;
                }
                acked[lw] = false;
                lw = (data_pos / MAX_DATA_SIZE) % window_size;
                // start next thread
                // cout << "data_pos: " << data_pos / MAX_DATA_SIZE << endl;
                // if (!gbn) {
                    pool.enqueue(sr_thread, servinfo, data, data_pos, (window_size - 1) * MAX_DATA_SIZE);
                // }
            }
        }
        if (_DEBUG) print_window();
        // if (_DEBUG) print_acked();  // debug
        // if (_DEBUG) print_indices();
        window_mutex.unlock();
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

    if (gbn) pool.enqueue(recv_ack, clientinfo, data, servinfo);
    bool resend = false;
    while (gbn) {
        window_mutex.lock();
        // lfs_ts_mutex.lock();
        auto now = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
        // cout << "check if need to resend window: (" << now << " - " << lfs_ts << " >= " << gbn_timeout << ")" << endl;
        if (now - lfs_ts >= gbn_timeout) {
            // resend window
            // lfs_ts_mutex.unlock();
            // window_mutex.lock();
            if (data_pos >= data_len) break;
            send_window(servinfo, data, resend);
            resend = true;
            // send_mutex.lock();
            window_mutex.unlock();
            // send_mutex.unlock();
            cout << "sleeping for " << gbn_timeout << " ms" << endl;
            this_thread::sleep_for(chrono::milliseconds(gbn_timeout));
        } else {
            window_mutex.unlock();
            // lfs_ts_mutex.unlock();
            cout << "sleeping for " << gbn_timeout - (now - lfs_ts) << " ms" << endl;
            this_thread::sleep_for(chrono::milliseconds(gbn_timeout - (now - lfs_ts)));
        }
    }
    if (!gbn) {
        for (int t = 0; t < min(window_size, numBlocks); t++) {
            pool.enqueue(sr_thread, servinfo, data, 0, t * MAX_DATA_SIZE);
            this_thread::sleep_for(chrono::milliseconds(1));
        }
        pool.enqueue(recv_ack, clientinfo, data, servinfo);
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

    //Throughput - # of packets * size of the packet * 8 / time
    //Throughput - # of bits sent / time

    cout << "Number of original packets sent: " << num_packets_sent << endl;
    cout << "Number of retransmitted packets: " << num_packets_resent << endl;
    auto end_time = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
    auto elapsed_ms = end_time - start_time;
    double elapsed_sec = elapsed_ms / 1000.0f;
    cout << "Total elapsed time: " << elapsed_sec << endl;
    cout << "Total throughput (Mbps): " << data_len / elapsed_sec / (1024*1024) << endl;
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
    window_size = 7;
    seq_size = 20;
    acked = new bool[window_size];
    memset(acked, 0, window_size);
    gbn = false;
    if (gbn) gbn_timeout = 10;    // in ms

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
    window_transfer_file(node, servinfo);
    print_stats(start_time);

    cout << endl;
    cout << "main thread complete\n";
    delete[] acked;
    close(sockfd);
    freeaddrinfo(clientinfo);
    freeaddrinfo(servinfo);
    return 0;
}