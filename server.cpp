
//Author: John Frye
//Fall 2023



#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h> 

#include "packet.h"

#define BUF_SIZE 20
#define PACKET_LEN 100

#define CLIENT_TO_SERVER_EOT 3
#define SERVER_TO_CLIENT_EOT 2

using namespace std;

int main(int argc, char *argv[]) {


//################################################
//SETUP SOCKET TO RECEIVE DATA!
//################################################
	struct sockaddr_in client_address; //client structure
	socklen_t client_address_len = sizeof(client_address);

	int rcv_sock = socket(AF_INET, SOCK_DGRAM, 0); //Handshake socket
    if (rcv_sock < 0) {
        perror("Error creating socket");
        exit(1);
    }


 	struct sockaddr_in server_address; //server structure
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(atoi(argv[2])); //assign port number to receive??

    if (bind(rcv_sock, (struct sockaddr*) &server_address, sizeof(server_address)) < 0) { //binding socket with server port
        perror("Error binding socket");
        close(rcv_sock);
        exit(1);
    }

//#####################################################
//SETUP SOCKET TO SEND DATA!
//#####################################################

    int snd_sock = socket(AF_INET, SOCK_DGRAM, 0); //Handshake socket
    if (snd_sock < 0) {
        perror("Error creating socket");
        exit(1);
    }

    struct hostent *emulator_server;           // pointer to a structure of type hostent
    emulator_server = gethostbyname(argv[1]);   // Gets host ip address // requires netdb.h 
	if(emulator_server == NULL){ // failed to obtain server's name
		cout << "Failed to obtain server.\n";
		exit(EXIT_FAILURE);
	}
    
    struct sockaddr_in emulator_server_address; //server structure
    socklen_t Emulator_Server_Length = sizeof(emulator_server_address);
    memset(&emulator_server_address, 0, sizeof(emulator_server_address)); //reset memory
    emulator_server_address.sin_family = AF_INET;
    bcopy((char *)emulator_server->h_addr, (char*)&emulator_server_address.sin_addr.s_addr, emulator_server->h_length); //copy ip address resolved above to server data structure
    //server_address.sin_addr.s_addr = inet_addr("127.0.0.1");
    emulator_server_address.sin_port = htons(atoi(argv[3])); //assign port number to send data to emulator


//################################################
//FILE TRANSFER
//################################################

    ofstream output_file(argv[4]); //open output file
    if (!output_file.is_open()) {
        perror("Error opening output file");
        close(rcv_sock);
        exit(1);
    }

    ofstream arrivalFile("arrival.log"); //open output file
    if (!arrivalFile.is_open()) {
        perror("Error opening arrival file");
        close(rcv_sock);
        exit(1);
    }

    char buf[BUF_SIZE];
    ssize_t num_bytes;
	// char ack[4];

    char payload[512];
    memset(payload, 0, 512);
    char serialized[512];
    memset(serialized, 0, 512); 

    packet rcvdPacket(0,0,0,payload);

    int expectedSequenceNumber = 1;
    bool incrementSequenceNumber = true;

    int doTwice = 2;


    while ((num_bytes = recvfrom(rcv_sock, serialized, 512, 0, (struct sockaddr*) &client_address, &client_address_len)) > 0) { //receive data from client 4 chars at a time
        buf[num_bytes] = '\0';

        char serializedPacket[PACKET_LEN];  // for holding serialized packet  
        memset(serializedPacket, 0, PACKET_LEN); // serialize the packet to be sent

        rcvdPacket.deserialize(serialized);
        // rcvdPacket.printContents();

        if(rcvdPacket.getSeqNum())
        {
            arrivalFile.write("1\n", 2);
        }
        else
        {
            arrivalFile.write("0\n", 2);
        }

        if(incrementSequenceNumber) expectedSequenceNumber = (expectedSequenceNumber + 1) % 2;

        if(rcvdPacket.getSeqNum() != expectedSequenceNumber)
        {
            cout << "Received duplicate packet... no ACK being sent." << endl;
            incrementSequenceNumber = false;


            packet ackPacket(0, (rcvdPacket.getSeqNum()+1)%2, 0, 0); //send ack for last correctly received packet
            ackPacket.serialize(serializedPacket);

		    sendto(snd_sock, serializedPacket, PACKET_LEN, 0, (struct sockaddr *)&emulator_server_address, Emulator_Server_Length);

            continue;
        }
        else
        {
            incrementSequenceNumber = true;
        }


        output_file.write(rcvdPacket.getData(), rcvdPacket.getLength()); //writing the received data to file

        packet ackPacket(0, rcvdPacket.getSeqNum(), 0, 0);

        if(rcvdPacket.getType() == CLIENT_TO_SERVER_EOT)
        {
            cout << "EOT RECEIVED FROM CLIENT! SENDING SERVER EOT TO CLIENT!" << endl;
            packet EOTPacket(SERVER_TO_CLIENT_EOT, rcvdPacket.getSeqNum(), 0, 0);
            EOTPacket.serialize(serializedPacket);
            sendto(snd_sock, serializedPacket, PACKET_LEN, 0, (struct sockaddr *)&emulator_server_address, Emulator_Server_Length);
            break;
        }

        ackPacket.serialize(serializedPacket);

		sendto(snd_sock, serializedPacket, PACKET_LEN, 0, (struct sockaddr *)&emulator_server_address, Emulator_Server_Length);


    }

    if (num_bytes < 0) {
        perror("Error receiving data");
    }

    close(rcv_sock);
    close(snd_sock);
    output_file.close();
    return 0;
}
