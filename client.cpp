//Author: John Frye (jmf721)
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

#define BUF_SIZE 4

#define PACKET_LEN 100
#define CLIENT_TO_SERVER_EOT 3
#define SERVER_TO_CLIENT_EOT 2

using namespace std;

void updatePayload(char* original, char* newPayload, int length)
{
    if(length != 20) //may have reached end of file here, we expect 20 bytes at a time
    {
        original[length-1] = '\0';
    }
    for(int j = 0; j < length; j++)
    {
      original[j] = newPayload[j];
    }
}




int main(int argc, char *argv[]) {

//#####################################################
//SETUP SOCKET TO SEND DATA!
//#####################################################

    int snd_sock = socket(AF_INET, SOCK_DGRAM, 0); //Handshake socket
    if (snd_sock < 0) {
        perror("Error creating socket");
        exit(1);
    }

    struct hostent *server;           // pointer to a structure of type hostent
    server = gethostbyname(argv[1]);   // Gets host ip address // requires netdb.h 
	if(server == NULL){ // failed to obtain server's name
		cout << "Failed to obtain server.\n";
		exit(EXIT_FAILURE);
	}
    
    struct sockaddr_in server_address; //server structure
    socklen_t Server_Length = sizeof(server_address);
    memset(&server_address, 0, sizeof(server_address)); //reset memory
    server_address.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char*)&server_address.sin_addr.s_addr, server->h_length); //copy ip address resolved above to server data structure
    //server_address.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_address.sin_port = htons(atoi(argv[2])); //assign port number


//#####################################################
//SETUP SOCKET TO RECEIVE DATA!
//#####################################################


    struct sockaddr_in client_address; //client structure that represents the emulator
	socklen_t client_address_len = sizeof(client_address);

	int rcv_sock = socket(AF_INET, SOCK_DGRAM, 0); //ack socket
    if (rcv_sock < 0) {
        perror("Error creating socket");
        exit(1);
    }

    struct timeval timeout;
    timeout.tv_sec = 5; // Set timeout to 5 seconds
    timeout.tv_usec = 0;

    // Set receive timeout
    if (setsockopt(rcv_sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        perror("Error setting receive timeout");
        close(rcv_sock);
        return 1;
    }


 	struct sockaddr_in emulator_server_address; //server structure
    memset(&emulator_server_address, 0, sizeof(emulator_server_address));
    emulator_server_address.sin_family = AF_INET;
    emulator_server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    emulator_server_address.sin_port = htons(atoi(argv[3])); //assign port number to receive from emulator

    if (bind(rcv_sock, (struct sockaddr*) &emulator_server_address, sizeof(emulator_server_address)) < 0) { //binding socket with server port
        perror("Error binding socket");
        close(rcv_sock);
        exit(1);
    }

    ifstream input_file(argv[4]); //opening file
    if (!input_file.is_open()) {
        perror("Error opening input file");
        close(snd_sock);
        exit(1);
    }

    ofstream seqnumFile("clientseqnum.log"); //open output file
    if (!seqnumFile.is_open()) {
        perror("Error opening output file");
        close(rcv_sock);
        exit(1);
    }

    ofstream ackFile("clientack.log"); //open output file
    if (!ackFile.is_open()) {
        perror("Error opening arrival file");
        close(rcv_sock);
        exit(1);
    }






    char data[50];
    memset(data, 0, sizeof(data));
    ssize_t num_bytes;

    int sequenceNumber = 1;
    char serializedPacket[PACKET_LEN];  // for holding serialized packet
    char ackSerializedPacket[PACKET_LEN];  

    char textPayload[20] = {};
    int charCount = 0;


    // while (input_file.read(data, BUF_SIZE)) //Reads 20 character from file at a time, it will read less than 20 or whatever left for the last transmission
    while(input_file)
    { 
        while(charCount < 20 && !input_file.eof())
        {
            char currentCharacter = input_file.get(); //grab next char in file to be sent in payload

            if(currentCharacter < 0) //end of file..., fill up the rest of payload with null char
            {
              while(charCount < 20)
              {
                textPayload[charCount++] = '\0';
              }
              break;
            }

            textPayload[charCount++] = currentCharacter;
        }

        //fill in array with the next four bytes to be sent to the server
        updatePayload(data, textPayload, charCount);
        charCount = 0;


        // data[BUF_SIZE] = '\0'; //null termination
        memset(serializedPacket, 0, PACKET_LEN); // serialize the packet to be sent


        sequenceNumber = (sequenceNumber+1) % 2;

        packet dataPacket(1, sequenceNumber, strlen(data), data);

        dataPacket.serialize(serializedPacket);
        
        num_bytes = sendto(snd_sock, serializedPacket, PACKET_LEN, 0, (struct sockaddr*) &server_address, sizeof(server_address)); //sending data to server
        if (num_bytes < 0) {
            perror("Error sending data");
            break;
        }
        

        if(sequenceNumber)
        {
            seqnumFile.write("1\n", 2);
        }
        else
        {
            seqnumFile.write("0\n", 2);
        }

        memset(ackSerializedPacket, 0, PACKET_LEN);
        
        while((recvfrom(rcv_sock, ackSerializedPacket, PACKET_LEN, 0, (struct sockaddr *)&client_address, &client_address_len) <= 0))
        {
            //did not receive an ack... resend current packet and try again
            printf("Timeout! Resending!\n");
            num_bytes = sendto(snd_sock, serializedPacket, PACKET_LEN, 0, (struct sockaddr*) &server_address, sizeof(server_address)); //sending data to server

            if(sequenceNumber)
            {
                seqnumFile.write("1\n", 2);
            }
            else
            {
                seqnumFile.write("0\n", 2);
            }



        }

        packet ackPacket(0,0,0,0);
        ackPacket.deserialize(ackSerializedPacket);

        if(ackPacket.getSeqNum())
        {
            ackFile.write("1\n", 2);
        }
        else
        {
            ackFile.write("0\n", 2);
        }

        //make sure we get an ACK for the sequence number we sent.
        while(ackPacket.getSeqNum() != sequenceNumber)
        {
            cout << "WRONG ACK SEQ NUM!" << endl;
            //resend data.
            num_bytes = sendto(snd_sock, serializedPacket, PACKET_LEN, 0, (struct sockaddr*) &server_address, sizeof(server_address)); //sending data to server
            if (num_bytes < 0) {
                perror("Error sending data");
                break;
            }
            if(sequenceNumber)
            {
                seqnumFile.write("1\n", 2);
            }
            else
            {
                seqnumFile.write("0\n", 2);
            }

            while(recvfrom(rcv_sock, ackSerializedPacket, PACKET_LEN, 0, (struct sockaddr *)&client_address, &client_address_len) <= 0)
            {
                num_bytes = sendto(snd_sock, serializedPacket, PACKET_LEN, 0, (struct sockaddr*) &server_address, sizeof(server_address)); //sending data to server
                if(sequenceNumber)
                {
                    seqnumFile.write("1\n", 2);
                }
                else
                {
                    seqnumFile.write("0\n", 2);
                }
            } 



            ackPacket.deserialize(ackSerializedPacket);

            if(ackPacket.getSeqNum())
            {
                ackFile.write("1\n", 2);
            }
            else
            {
                ackFile.write("0\n", 2);
            }

        }
        

        memset(data, 0, sizeof(data));



    }

    // If we've reached the end of the file, send a final datagram with 0 bytes
    if (input_file.eof()) {

        sequenceNumber = (sequenceNumber+1) % 2;

            if(sequenceNumber)
            {
                seqnumFile.write("1\n", 2);
            }
            else
            {
                seqnumFile.write("0\n", 2);
            }


        packet dataPacket(CLIENT_TO_SERVER_EOT, sequenceNumber, 0, 0);

        dataPacket.serialize(serializedPacket);

        num_bytes = sendto(snd_sock, serializedPacket, PACKET_LEN, 0, (struct sockaddr*) &server_address, sizeof(server_address));
        if (num_bytes < 0) {
            perror("Error sending end-of-file indicator");
        }

        recvfrom(rcv_sock, serializedPacket, PACKET_LEN, 0, (struct sockaddr *)&client_address, &client_address_len); //receiving data from the server
	
        packet ackPacket(0,0,0,0);
        ackPacket.deserialize(serializedPacket);

        if(ackPacket.getType() == SERVER_TO_CLIENT_EOT)
        {
            cout << "EOT RECEIVED FROM SERVER!" << endl;
        }

        if(ackPacket.getSeqNum())
            {
                ackFile.write("1\n", 2);
            }
            else
            {
                ackFile.write("0\n", 2);
            }


    }

    close(snd_sock);
    close(rcv_sock);

    input_file.close();
    return 0;
}
