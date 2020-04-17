// Write CPP code here
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/types.h>  /* for Socket data types */
#include <sys/socket.h> /* for socket(), connect(), send(), and recv() */
#include <netinet/in.h> /* for IP Socket data types */
#include <arpa/inet.h>  /* for sockaddr_in and inet_addr() */
#include <stdlib.h>     /* for atoi() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() */
#include <vector>
#include <iostream>
#define MAX 80
#define PORT 9105
#define SA struct sockaddr

using namespace std;

void func(int sockfd)
{
    string test = "Test String";
    //Create char array buffer
    char *testBuff = new char[4096];
    int testLength = test.length();
    strcpy(testBuff, test.c_str());
    // send length
    send(sockfd, &testLength, sizeof(testLength), 0);
    // send message
    send(sockfd, testBuff, testLength, 0);

    cout << "Sent: " << test << endl;
}

void generateErrors(){}
void promptErrors(){}

int main()
{
    string protocol;
    int packetSize;
    int timeoutInterval;
    int sizeOfWindow;
    int rangeOfSequence;
    //START USER INPUT
    
    cout << "Type of protocol (GBN or SR): ";
    cin >> protocol;
    cout << "Packet Size (kB): ";
    cin >> packetSize;
    cout << "Timeout interval (0 for ping calculated): ";
    cin >> timeoutInterval;
    cout << "Size of sliding window: ";
    cin >> sizeOfWindow;
    cout << "Range of sequence numbers: ";
    cin >> rangeOfSequence;

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

    int sockfd, connfd;
    struct sockaddr_in servaddr, cli;

    // socket create and verification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        printf("socket creation failed...\n");
        exit(0);
    }
    else
        printf("Socket successfully created..\n");
    bzero(&servaddr, sizeof(servaddr));

    cout << "Enter a number to specify the server want to connect to: " << endl;
    cout << "0 - thing0" << endl;
    cout << "1 - thing1" << endl;
    cout << "2 - thing2" << endl;
    cout << "3 - thing3" << endl;
    cout << "4 - localhost" << endl;

    string choice;
    string ip;
    cin >> choice;
    if (choice == "0")
    {
        ip = "10.35.195.46";
    }
    else if (choice == "1")
    {
        ip = "10.35.195.47";
    }
    else if (choice == "2")
    {
        ip = "10.35.195.48";
    }
    else if (choice == "3")
    {
        ip = "10.35.195.49";
    }
    else if (choice == "4")
    {
        ip = "127.0.0.1";
    }
    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(ip.c_str());
    servaddr.sin_port = htons(PORT);

    // connect the client socket to server socket
    if (connect(sockfd, (SA *)&servaddr, sizeof(servaddr)) != 0)
    {
        printf("connection with the server failed...\n");
        exit(0);
    }
    else
        printf("connected to the server..\n");

    // function for chat
    func(sockfd);

    // close the socket
    close(sockfd);
}
