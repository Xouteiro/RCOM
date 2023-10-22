// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>


unsigned char*  parseControlPacket(unsigned char* packet, int size, int *nameSize) {

    unsigned char fileSizeNBytes = packet[6];
    unsigned char fileSizeAux[fileSizeNBytes];
    memcpy(fileSizeAux, packet+7, fileSizeNBytes);
    unsigned char fileNameNBytes = packet[7+fileSizeNBytes+1];
    unsigned char *name = (unsigned char*)malloc(fileNameNBytes);
    memcpy(name, packet+7+fileSizeNBytes+2, fileNameNBytes);
    *nameSize = fileNameNBytes;
    return name;
}

unsigned char * getControlPacket(const unsigned int c, const char* filename, long int length, unsigned int* size){

    const int L1 = (int) ceil(log2f((float)length)/8.0);
    const int L2 = strlen(filename);
    *size = 1+2+L1+2+L2;
    unsigned char *packet = (unsigned char*)malloc(*size);
    
    unsigned int pos = 0;
    packet[pos++]=c;
    packet[pos++]=0;
    packet[pos++]=L1;

    for (unsigned char i = 0 ; i < L1 ; i++) {
        packet[2+L1-i] = length & 0xFF;
        length >>= 8;
    }
    pos+=L1;
    packet[pos++]=1;
    packet[pos++]=L2;
    memcpy(packet+pos, filename, L2);
    for(int i = 0; i < *size; i++) printf("packet= %02x && %c\n", packet[i], packet[i]);
    return packet;
}

unsigned char * getDataPacket(unsigned char sequence, unsigned char *data, int dataSize, int *packetSize){

    *packetSize = 1 + 1 + 2 + dataSize;
    unsigned char* packet = (unsigned char*)malloc(*packetSize);

    packet[0] = 1;   
    packet[1] = sequence;
    packet[2] = dataSize >> 8 & 0xFF;
    packet[3] = dataSize & 0xFF;
    memcpy(packet+4, data, dataSize);

    return packet;
}

unsigned char * getData(FILE* fd, long int fileLength) {
    unsigned char* content = (unsigned char*)malloc(sizeof(unsigned char) * fileLength);
    int check = fread(content, sizeof(unsigned char), fileLength, fd);
    return content;
}

void parseDataPacket(const unsigned char* packet, const unsigned int packetSize, unsigned char* buffer) {
    //for(int i = 3; i < packetSize; i++) printf("packet%d= %02x && %c\n",i, packet[i], packet[i]);
    memcpy(buffer,packet+4,packetSize-4);
    buffer += packetSize;
}

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer linkLayer;
    strcpy(linkLayer.serialPort,serialPort);
    linkLayer.role = strcmp(role, "tx") ? LlRx : LlTx;
    linkLayer.baudRate = baudRate;
    linkLayer.nRetransmissions = nTries;
    linkLayer.timeout = timeout;

    int fd = llopen(linkLayer); //por o llopen a retornar negativo se der erro  
    printf("llopen done\n");
    if (fd < 0) {
        perror("Connection error\n");
        exit(-1);
    }

    switch (linkLayer.role) {

        case LlTx: {
            
            FILE* file = fopen(filename, "rb");
            printf("file: %p\n", file);
            printf("filename: %s\n", filename);
            if (file == NULL) {
                perror("File not found\n");
                exit(-1);
            }

            int prev = ftell(file);
            fseek(file,0L,SEEK_END);
            long int fileSize = ftell(file)-prev;
            fseek(file,prev,SEEK_SET);
            
            printf("File size: %ld\n", fileSize);
            unsigned int cpSize;
            unsigned char *controlPacketStart = getControlPacket(2, filename, fileSize, &cpSize);
            if(llwrite(fd, controlPacketStart, cpSize) == -1){ 
                printf("Exit: error in start packet\n");
                exit(-1);
            }

            unsigned char sequence = 0;
            unsigned char* content = getData(file, fileSize);
            long int bytesLeft = fileSize;


            while (bytesLeft >= 0) { 
                printf("bytesLeft: %ld\n", bytesLeft);
                int dataSize = bytesLeft > (long int) MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE: bytesLeft;
                unsigned char* data = (unsigned char*) malloc(dataSize);
                memcpy(data, content, dataSize);
                printf("Data size: %d\n", dataSize);
                
                int packetSize;\
                unsigned char* packet = getDataPacket(sequence, data, dataSize, &packetSize);
                printf("Packet size: %d\n", packetSize);
                if(llwrite(fd, packet, packetSize) == -1) {
                    printf("Exit: error in data packets\n");
                    exit(-1);
                }
                
                bytesLeft -= (long int) MAX_PAYLOAD_SIZE; 
                content += dataSize; 
                sequence = (sequence + 1) % 255;   
            }

            

            unsigned char *controlPacketEnd = getControlPacket(3, filename, fileSize, &cpSize);
            if(llwrite(fd, controlPacketEnd, cpSize) == -1) { 
                printf("Exit: error in end packet\n");
                exit(-1);
            }
            llclose(fd);
            break;
        }

        case LlRx: {

            unsigned char *packet = (unsigned char *)malloc(MAX_PAYLOAD_SIZE);
            int packetSize = -1;
            while ((packetSize = llread(fd, packet)) < 0);
            int nameSize  = 0;
            unsigned char* name = parseControlPacket(packet, packetSize, &nameSize);  
            
            FILE* newFile = fopen((char *) "penguin-received.gif", "wb+");
            if(newFile == NULL) {
                perror("File not found\n");
                exit(-1);
            }   
            while (1) {   
                packetSize = -1;
                while ((packetSize=llread(fd, packet)) < 0);
                printf("Packet size up: %d\n", packetSize);
                if(packetSize == 0) break;
                else if(packet[0] != 3){
                    unsigned char *buffer = (unsigned char*)malloc(packetSize);
                    parseDataPacket(packet, packetSize, buffer);
                    int check = fwrite(buffer, sizeof(unsigned char), packetSize-5, newFile);
                    printf("check: %d\n", check);

                    free(buffer);
                    
                }
                else break;
            }

            fclose(newFile);
            printf("File closed\n");
            break;

        default:
            exit(-1);
            break;
    }}
}