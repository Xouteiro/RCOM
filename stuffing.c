// Write to serial port in non-canonical mode
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
#include <signal.h>

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 50


volatile int STOP = FALSE;
int alarmEnabled = FALSE;
int alarmCount = 0;


void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;

    printf("Alarm #%d\n", alarmCount);
}

void sendFrame(int fd, unsigned char *buf){
    int bytes = write(fd, buf, BUF_SIZE);  
    alarm(3);
}

unsigned char buildBCC2(const char *payload){
    unsigned char bcc2 = payload[0];
    for(int i = 1 ; i < strlen(payload) ; i++) bcc2 = bcc2 ^ payload[i];
    return bcc2; 
}

void stuffing(unsigned char *payload, unsigned char *new_payload){
    int index = 0;
    for(int i = 0; i < strlen(payload); i++){
        // printf("i is %d\n", i);
        // printf("index is %d\n", index);
        // printf("payload char is %c\n", payload[i]);;
        if(payload[i] == 0x7E){
            new_payload[index] = 0x7D;
            index++;
            new_payload[index] = 0x5E;
        }
        else if (payload[i] == 0x7D){
            new_payload[index] = 0x7D;
            index++;
            new_payload[index] = 0x5D;
        }
        else new_payload[index] = payload[i];

        index++;
    }
}

int main(int argc, char *argv[])
{
    (void)signal(SIGALRM, alarmHandler);
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

    // Open serial port device for reading and writing, and not as controlling tty
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


    unsigned char* payload = "~~~Hello World!~~~";
    unsigned char new_payload[2*strlen(payload)];
    // stuffing of the payload
    stuffing(payload, new_payload);

    // Create string to send
    unsigned char buf[BUF_SIZE];
    buf[0] = 0x7E;
    buf[1] = 0x03;
    buf[2] = 0x03;
    buf[3] = buf[1] ^ buf[2];
    long pos = 4;
    // fill buffer with stuffed payload
    for(int i = 0; i < strlen(new_payload); i++){
        buf[pos] = new_payload[i];
        pos++;
    }
    // build the bcc2 with the payload
    buf[pos] = buildBCC2(payload);
    buf[pos + 1] = 0x7E;
   
    printf("buf size is %d\n", (int)strlen(buf));
    for(int i = 0; i < 30; i++){
        printf("buf%d = %c\n", i, (unsigned char)buf[i]);
    }
    sendFrame(fd, buf);

    unsigned char ua_buf[BUF_SIZE];
    
    while(STOP == FALSE && alarmCount < 4){
        // Returns after 5 chars have been input
        int bytes = read(fd, ua_buf, BUF_SIZE);
        if (bytes && ua_buf[0] == 0x7E && ua_buf[1] == 0x03 
          && ua_buf[2] == 0x07 && ua_buf[3] == ua_buf[1] ^ ua_buf[2]
          && ua_buf[4] == 0x7E) {
            alarm(0);    

            for(int i = 0 ; i < bytes ; i++)
                printf("ua_buf%d = 0x%02x\n", i, (unsigned char)ua_buf[i]);
            
            STOP = TRUE;
        }
        else {
            if (alarmEnabled == FALSE)
              {
                 sendFrame(fd, buf);
                 alarmEnabled = TRUE;
              }
        }
    }

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}
