// Read from serial port in non-canonical mode
//
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 50

enum States{
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC1_OK,
    PAYLOAD,
    CHECK_BCC2,
    BCC2_OK,
    STOP_MACHINE
};

volatile int STOP = FALSE;

int destuffing(unsigned char *buf, unsigned char *destuf_buf, int n){
    int index = 1;
    int final_lenght = 1;

    for(int i = 0 ; i < n; i++){ //usar o n 
        
        if(buf[i] == 0x7D && buf[i+1] == 0x5E){
            destuf_buf[index] = 0x7E;
            i++;
        }
        else if (buf[i] == 0x7D && buf[i+1] == 0x5D){
            destuf_buf[index] = 0x7D;
            i++;
        }
        else destuf_buf[index] = buf[i];

        index++;
        final_lenght++;
    }

    return final_lenght;
}

unsigned char buildBCC2(const char *payload, int n){
    unsigned char bcc2 = payload[0];
    for(int i = 0 ; i < n - 1 ; i++) bcc2 = bcc2 ^ payload[i];
    return bcc2; 
}

int main(int argc, char *argv[])
{
    // Program usage: Uses either COM1 or COM2
    const char *serialPortName = argv[1];

    if (argc < 2)
    {
        printf("Incorrect program usage\n"
               "Usage: %s <SerialPort>\n"
               "Example: %s /dev/ttyS1\n",
               argv[0],
               argv[0]);
        exit(1);
    }

    // Open serial port device for reading and writing and not as controlling tty
    // because we don't want to get killed if linenoise sends CTRL-C.
    int fd = open(serialPortName, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        perror(serialPortName);
        exit(-1);
    }

    struct termios oldtio;
    struct termios newtio;

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    newtio.c_cc[VMIN] = 0;  // Blocking read until 5 chars received

    // VTIME e VMIN should be changed in order to protect with a
    // timeout the reception of the following character(s)

    // Now clean the line and activate the settings for the port
    // tcflush() discards data written to the object referred to
    // by fd but not transmitted, or data received but not read,
    // depending on the value of queue_selector:
    //   TCIFLUSH - flushes data received but not read.
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");


    // Loop for input
    unsigned char buf[BUF_SIZE];
    
    enum States currentState = START;

    while (STOP == FALSE)
    {
        int startOver = 0;
        int stop = 0;
        unsigned char stuffed_payload[1000];
        unsigned char destuffed_payload[500];
        unsigned char byte;
        int i = 0;
        int p = 0;
        int read_success = 0;
        int destuf_size;

        while(!startOver && !stop){
            
            if(read(fd, &byte, 1)>0 || read_success){
                printf("%d\n", currentState); 
                printf("rs = %d\n", read_success);
                printf("byte: %c \n" , (unsigned char) byte );
                
                printf("byte: %02x \n" , (unsigned char) byte );
                printf("i=%d\n", (int) i);
                switch (currentState)
                {
                case START:
                    if(byte==0x7E){
                        buf[i] = byte;
                        i++;
                        currentState = FLAG_RCV;
                    }
                    else{
                        startOver = 1;
                        i = 0;
                    }
                    break;

                case FLAG_RCV:
                    if(byte==0x03){
                        buf[i] = byte;
                        i++;
                        currentState = A_RCV;
                        break;
                    }
                    if(byte==0x7E){
                        startOver = 1;
                        i = 0;
                    }
                    else{
                        currentState = START;
                        startOver = 1;
                        i = 0;
                    }
                    break;

                case A_RCV:
                    if(byte==0x03){
                        buf[i] = byte;
                        i++;
                        currentState = C_RCV;
                        break;
                    }
                    if(byte==0x7E){
                        startOver = 1;
                        i = 0;
                        currentState = FLAG_RCV;
                    } 
                    else{
                        currentState = START;
                        startOver = 1;
                        i = 0;
                    }
                    break;

                case C_RCV:
                    
                    if(byte == buf[1] ^ buf[2]){
                        buf[i] = byte;
                        i++;
                        currentState = PAYLOAD;
                        break;
                    }
                    if(byte == 0x7E){
                        startOver = 1;
                        i = 0;
                        currentState = FLAG_RCV;
                    } 
                    else{
                        currentState = START;
                        startOver = 1;
                        i = 0;
                    }
                    break;

            
                    
                case PAYLOAD:
                    if(byte==0x7E){
                        buf[i] = byte;
                        read_success = 1;
                        for(int k = 0; k< p; k++){
                            printf("stpl= 0x%02x\n",stuffed_payload[k]);
                        }
                        currentState = CHECK_BCC2;
                        break;
                    }
                    else{
                        stuffed_payload[p] = byte;
                        buf[i] = byte;
                        p++;
                        i++;
                    }
                
                    break;
                
                case CHECK_BCC2:
                    destuf_size=destuffing(stuffed_payload, destuffed_payload, p); 
                    int bcc2 = buildBCC2(destuffed_payload, destuf_size);
                    if(bcc2 == destuffed_payload[destuf_size-1]){
                        currentState = BCC2_OK;
                        break;
                    }
                    else{
                        currentState = START;
                        startOver = 1;
                        i = 0;
                        read_success=0;
                    }
                
                    break;
                    
                case BCC2_OK:
                    if(byte==0x7E){
                        buf[i] = byte;
                        i++;
                        currentState = STOP_MACHINE;
                        break;
                    } 
                    else{
                        currentState = START;
                        startOver = 1;
                        i = 0;
                    }
                    break;
                
                case STOP_MACHINE:
                    for(int j = 0; j < i ; j++)
                        printf("buf%d = 0x%02x\n",j, (unsigned char)buf[j]);
                    read_success = 0;
                    stop = 1;
                    STOP = TRUE;
                    break;
                    }
                
            
            }
        }
        
    

    }

    
    
    
    unsigned char ua_buf[BUF_SIZE];
    ua_buf[0] = 0x7E;
    ua_buf[1] = 0x03;
    ua_buf[2] = 0x07;
    ua_buf[3] = ua_buf[1] ^ ua_buf[2];
    ua_buf[4] = 0x7E;
    
    
    int retbytes = write(fd, ua_buf, BUF_SIZE);
    
    sleep(1);

    // The while() cycle should be changed in order to respect the specifications
    // of the protocol indicated in the Lab guide

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}
