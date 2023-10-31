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
#include <sys/time.h>



unsigned char* parseControlPacket(unsigned char* packet, int size, int* nameSize) {
    unsigned char fileSizeNBytes = packet[6];
    unsigned char fileSizeAux[fileSizeNBytes];
    memcpy(fileSizeAux, packet + 7, fileSizeNBytes);
    unsigned char fileNameNBytes = packet[7 + fileSizeNBytes + 1];
    unsigned char* name = (unsigned char* )malloc(fileNameNBytes);
    memcpy(name, packet + 7 + fileSizeNBytes + 2, fileNameNBytes);
    *nameSize = fileNameNBytes;
    return name;
}

unsigned char* getControlPacket(const unsigned int c, const char* filename, long int length, unsigned int* size) {
    const int L1 = (int)ceil(log2f((float)length) / 8.0);
    const int L2 = strlen(filename);
    *size = 1 + 2 + L1 + 2 + L2;
    unsigned char* packet = (unsigned char* )malloc(*size);

    unsigned int pos = 0;
    packet[pos++] = c;
    packet[pos++] = 0;
    packet[pos++] = L1;

    for (unsigned char i = 0; i < L1; i++) {
        packet[2 + L1 - i] = length & 0xFF;
        length >>= 8;
    }
    pos += L1;
    packet[pos++] = 1;
    packet[pos++] = L2;
    memcpy(packet + pos, filename, L2);
    return packet;
}

unsigned char* getDataPacket(unsigned char sequence, unsigned char* data, int dataSize, int* packetSize) {
    *packetSize = 1 + 1 + 2 + dataSize;
    unsigned char* packet = (unsigned char* )malloc(*packetSize);

    packet[0] = 1;
    packet[1] = sequence;
    packet[2] = dataSize >> 8 & 0xFF;
    packet[3] = dataSize & 0xFF;
    memcpy(packet + 4, data, dataSize);

    return packet;
}

unsigned char* getData(FILE *fd, long int fileLength) {
    unsigned char* content = (unsigned char* )malloc(sizeof(unsigned char) * fileLength);
    fread(content, sizeof(unsigned char), fileLength, fd);
    return content;
}

void parseDataPacket(const unsigned char* packet, const unsigned int packetSize, unsigned char* buffer) {
    memcpy(buffer, packet + 4, packetSize - 4);
    buffer += packetSize;
}

    struct timeval start_total_time, end_total_time, start_packet_time, end_packet_time;

    double elapsed_total_time, elapsed_prop_time;

    double elapsed_packet_time[50];

void applicationLayer(const char* serialPort, const char* role, int baudRate,
                      int nTries, int timeout, const char* filename) {
    LinkLayer linkLayer;
    strcpy(linkLayer.serialPort, serialPort);
    linkLayer.role = strcmp(role, "tx") ? LlRx : LlTx;
    linkLayer.baudRate = baudRate;
    linkLayer.nRetransmissions = nTries;
    linkLayer.timeout = timeout;

    int fd;
    if ((fd = llopen(linkLayer)) < 0) {
        perror("Connection error\n");
        exit(-1);
    }
    printf("llopen done\n");

    switch (linkLayer.role) {
        case LlTx: {
            FILE *file = fopen(filename, "rb");
            if (file == NULL) {
                perror("File not found\n");
                exit(-1);
            }

            int prev = ftell(file);
            fseek(file, 0L, SEEK_END);
            long int fileSize = ftell(file) - prev;
            fseek(file, prev, SEEK_SET);

            printf("File size: %ld\n", fileSize);
            unsigned int cpSize;
            unsigned char* controlPacketStart = getControlPacket(2, filename, fileSize, &cpSize);
            
            if (llwrite(fd, controlPacketStart, cpSize) == -1) { // send start packet
                printf("Exit: error in start packet\n");
                exit(-1);
            }

            printf("Start packet sent\n");

            unsigned char sequence = 0;
            unsigned char* content = getData(file, fileSize);
            long int bytesLeft = fileSize;
            int k = 0;
            gettimeofday(&start_total_time, NULL); //start total time

            while (bytesLeft >= 0) {
                int dataSize = bytesLeft > (long int)MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : bytesLeft;
                unsigned char* data = (unsigned char* )malloc(dataSize);
                memcpy(data, content, dataSize);

                int packetSize;
                unsigned char* packet = getDataPacket(sequence, data, dataSize, &packetSize);
                gettimeofday(&start_packet_time, NULL);
                if (llwrite(fd, packet, packetSize) == -1) { // send data packet
                    printf("Exit: error in data packets\n");
                    exit(-1);
                }
                gettimeofday(&end_packet_time, NULL);

                elapsed_packet_time[k] = (end_packet_time.tv_sec - start_packet_time.tv_sec) + 
                   (end_packet_time.tv_usec - start_packet_time.tv_usec) / 1e6;
                k++;
                bytesLeft -= (long int)MAX_PAYLOAD_SIZE;
                content += dataSize;
                sequence = (sequence + 1) % 255;
            }
            gettimeofday(&end_total_time, NULL); // Record the ending time
            elapsed_total_time = (end_total_time.tv_sec - start_total_time.tv_sec) + 
                   (end_total_time.tv_usec - start_total_time.tv_usec) / 1e6;

            printf("File sent!\n");

            unsigned char* controlPacketEnd = getControlPacket(3, filename, fileSize, &cpSize);
            if (llwrite(fd, controlPacketEnd, cpSize) == -1) { // send disconnect packet
                printf("Exit: error in end packet\n");
                exit(-1);
            }

            if(llclose(fd, 0) == -1){
                printf("Exit: error in llclose\n");
                exit(-1);
            };
            double mean = 0;
            for(int i = 0; i < k; i++){
                mean += elapsed_packet_time[i];
                printf("Packet %d time: %fs\n", i, elapsed_packet_time[i]);
            }
            mean /= k;
            printf("Mean packet transmisson time: %fs\n", mean);
            printf("Total file transmission time = %fs\n", elapsed_total_time);
            printf("Disconnecting\n");
            break;
        }

        case LlRx: {
            unsigned char* packet = (unsigned char* )malloc(MAX_PAYLOAD_SIZE);
            int packetSize = -1;
            
            while ((packetSize = llread(fd, packet)) < 0); // wait for start packet
            printf("Start packet received\n");
            int nameSize = 0;
            parseControlPacket(packet, packetSize, &nameSize);

            FILE *newFile = fopen((char* )"penguin-received.gif", "wb+");
            if (newFile == NULL) {
                perror("File not found\n");
                exit(-1);
            }
            while (1) {
                packetSize = -1;
                while ((packetSize = llread(fd, packet)) < 0); // wait for data packet
                if (!packetSize) break;
                else if (packet[0] != 3) {
                    unsigned char* buffer = (unsigned char* )malloc(packetSize);
                    parseDataPacket(packet, packetSize, buffer);
                    fwrite(buffer, sizeof(unsigned char), packetSize - 5, newFile);
                    free(buffer);
                }
                else break;
            }
            printf("File received!\n");

            fclose(newFile);
            int disc = -1;
            while ((disc = llread(fd, packet)) < 0); // wait for disconnect packet
            printf("Disconnecting\n");
            break;
        }
        
        default:
            exit(-1);
            break;
        }
}
