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
    BCC_OK,
    STOP_MACHINE
};

volatile int STOP = FALSE;

int destuffing(unsigned char *buf, unsigned char *destuf_buf){
    int index = 1;
    int final_lenght = 1;
    int i = 1;
    destuf_buf[0] = 0x7E;
    //printf("strlen(buf) is %d\n",(unsigned int) strlen(buf));
    while( buf[i] != 0x7E ){ //corrigir o 30, strlen(buf) esta a dar 3 
        
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
        i++;
    }
    destuf_buf[index] = 0x7E;
    return final_lenght;
}

unsigned char buildBCC2(const char *payload){
    unsigned char bcc2 = payload[4];
    printf("len payload = %d\n", (int)strlen(payload)) ;
    for(int i = 5 ; i < strlen(payload) ; i++) bcc2 = bcc2 ^ payload[i];
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
    newtio.c_cc[VTIME] = 1; // Inter-character timer unused
    newtio.c_cc[VMIN] = 1;  // Blocking read until 5 chars received

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

    while (STOP == FALSE)
    {
        // Returns after 5 chars have been input
        //sleep(2); //talvez trocar para ler byte a byte
    
        int bytes = read(fd, buf, BUF_SIZE);
        for(int i = 0; i < bytes; i++){
            printf("buf before destuf%d: %c \n", i, (unsigned char)buf[i]);
        }
            
        

        unsigned char destuf_buf[bytes];
        int destuf_size = destuffing(buf, destuf_buf);
        unsigned char bcc2 = buildBCC2(destuf_buf);
        printf("bcc2 = %02x\n", bcc2);
        printf("bcc2 is %c\n", bcc2 );
        

        for(int i = 0; i< destuf_size; i++){
            printf("destuf_buf%d = %c\n", i, (unsigned char)destuf_buf[i]);
        }

        
        STOP = TRUE;
        
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
