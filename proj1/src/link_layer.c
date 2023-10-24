// Link layer protocol implementation

#include "link_layer.h"

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1
#define FRAME_CONTROL(Ns) (Ns << 6)
#define RR(Nr) ((Nr << 7) | 0x05)
#define REJECT(Nr) ((Nr << 7) | 0x01)
#define BUF_SIZE 5
#define FLAG 0x7E
#define A_TR 0x03
#define A_REC 0x01
#define C_SET 0x03
#define C_UA 0x07
#define C_DISC 0x0B
#define ESC 0x7D

enum States {
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    CP_RCV,
    BCC_OK,
    BCC1_OK,
    PAYLOAD,
    CHECK_BCC2,
    BCC2_OK,
    STOP_MACHINE,
    STOP_MACHINE_DISC
};

volatile int STOP = FALSE;
int alarmEnabled = FALSE;
int alarmCount = 0;
int tramaTr = 0;
int tramaRc = 1;

void alarmHandler(int signal) {
    alarmEnabled = FALSE;
    alarmCount++;
    printf("Alarm #%d\n", alarmCount);
}

void sendFrame(int fd, unsigned char *buf, int n) {
    write(fd, buf, n);
    alarm(3);
}

int sendSup(int fd, unsigned char A, unsigned char C) {
    unsigned char UA[5] = {FLAG, A, C, A ^ C, FLAG};
    return write(fd, UA, 5);
}

unsigned char buildBCC2(const unsigned char* payload, int n) {
    unsigned char bcc2 = payload[0];
    for (int i = 1; i < n; i++) bcc2 = bcc2 ^ payload[i];
    return bcc2;
}

int stuffing(unsigned char* buf, unsigned char* new_buf, int n) {
    int index = 1;
    new_buf[0] = buf[0];
    for (int i = 1; i < n; i++) {
        if (buf[i] == FLAG) {
            new_buf[index] = ESC;
            index++;
            new_buf[index] = 0x5E;
        }
        else if (buf[i] == ESC) {
            new_buf[index] = ESC;
            index++;
            new_buf[index] = 0x5D;
        }
        else new_buf[index] = buf[i];

        index++;
    }
    new_buf[index] = buf[n];
    return index;
}

int destuffing(unsigned char* buf, unsigned char* destuf_buf, int n) {
    int index = 0;
    int final_lenght = 1;

    for (int i = 0; i < n; i++) { // usar o n
        if (buf[i] == ESC && buf[i + 1] == 0x5E) {
            destuf_buf[index] = FLAG;
            i++;
        }
        else if (buf[i] == ESC && buf[i + 1] == 0x5D) {
            destuf_buf[index] = ESC;
            i++;
        }
        else destuf_buf[index] = buf[i];

        index++;
        final_lenght++;
    }
    final_lenght--;
    return final_lenght;
}

int connect(const char* serialPort) {
    int fd;
    if((fd = open(serialPort, O_RDWR | O_NOCTTY)) < 0) {
        perror(serialPort);
        return -1;
    }

    struct termios oldtio;
    struct termios newtio;

    if (tcgetattr(fd, &oldtio) == -1) {
        perror("tcgetattr");
        exit(-1);
    }

    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0;
    newtio.c_cc[VMIN] = 0;

    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &newtio) == -1) {
        perror("tcsetattr");
        return -1;
    }

    return fd;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters) {
    int fd;
    if((fd = connect(connectionParameters.serialPort)) < 0) {
        perror("Connection error\n");
        return -1;
    }

    switch (connectionParameters.role) {
        case LlTx: {
            (void)signal(SIGALRM, alarmHandler);
            unsigned char set_buf[5];
            set_buf[0] = FLAG;
            set_buf[1] = A_TR;
            set_buf[2] = C_SET;
            set_buf[3] = set_buf[1] ^ set_buf[2];
            set_buf[4] = FLAG;

            sendFrame(fd, set_buf, 5); // send connection set

            unsigned char received_ua_buf[5];

            while (STOP == FALSE && alarmCount < 4) {
                // Returns after 5 chars have been inputted
                int bytes = read(fd, received_ua_buf, 5); // receive connection ua

                if (bytes && received_ua_buf[0] == FLAG && received_ua_buf[1] == A_TR
                && received_ua_buf[2] == C_UA && received_ua_buf[3] == (received_ua_buf[1] ^ received_ua_buf[2])
                && received_ua_buf[4] == FLAG) {
                    alarm(0);
                    STOP = TRUE;
                }
                else {
                    if (alarmEnabled == FALSE) {
                        sendFrame(fd, set_buf, 5);
                        alarmEnabled = TRUE;
                    }
                }
            }
            return fd;
        }

        case LlRx: {
            unsigned char received_buf[5];

            enum States currentState = START;

            while (STOP == FALSE) {
                // Returns after 5 chars have been input
                int bytes = read(fd, received_buf, 5); // receive connection set

                int startOver = 0;
                int stop = 0;

                for (int i = 0; i < bytes && !startOver && !stop; i++) {
                    switch (currentState) {
                        case START:
                            if (received_buf[i] == FLAG) currentState = FLAG_RCV;
                            else startOver = 1;
                            break;

                        case FLAG_RCV:
                            if (received_buf[i] == A_TR) currentState = A_RCV;
                            else if (received_buf[i] == FLAG) startOver = 1;
                            else {
                                currentState = START;
                                startOver = 1;
                            }
                            break;

                        case A_RCV:
                            if (received_buf[i] == A_TR) currentState = C_RCV;
                            else if (received_buf[i] == FLAG) {
                                startOver = 1;
                                currentState = FLAG_RCV;
                            }
                            else {
                                currentState = START;
                                startOver = 1;
                            }
                            break;

                        case C_RCV:
                            if (received_buf[i] == (received_buf[1] ^ received_buf[2])) currentState = BCC_OK;
                            else if (received_buf[i] == FLAG) {
                                startOver = 1;
                                currentState = FLAG_RCV;
                            }
                            else {
                                currentState = START;
                                startOver = 1;
                            }
                            break;

                        case BCC_OK:
                            if (received_buf[i] == FLAG) currentState = STOP_MACHINE;
                            else {
                                currentState = START;
                                startOver = 1;
                            }
                            break;

                        case STOP_MACHINE:
                            stop = 1;
                            STOP = TRUE;
                            break;

                        default:
                            break;
                    }
                }
            }

            sendSup(fd, A_TR, C_UA); // send connection ua
            break;
        }
    }

    return fd;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(int fd, const unsigned char* payload, int payloadSize) {
    unsigned char new_buf[2 * payloadSize];
    unsigned char buf[payloadSize + 5];
    buf[0] = FLAG;
    buf[1] = A_TR;
    buf[2] = FRAME_CONTROL(tramaTr);
    buf[3] = buf[1] ^ buf[2];
    long pos = 4;

    // fill buffer with stuffed payload
    for (int i = 0; i < payloadSize; i++) {
        buf[pos] = payload[i];
        pos++;
    }
    // build the bcc2 with the payload
    buf[pos] = buildBCC2(payload, payloadSize);
    pos++;
    buf[pos] = FLAG;

    int new_buf_size = stuffing(buf, new_buf, pos);
    sendFrame(fd, new_buf, new_buf_size + 1);

    unsigned char ua_buf[5];
    STOP = FALSE;
    while (STOP == FALSE && alarmCount < 5) { //mudar 5 para define
        int accepted = 0;
        int bytes = read(fd, ua_buf, 5);

        if (bytes && ua_buf[0] == 0x7E && (ua_buf[1] == 0x03 || ua_buf[1] == 0x01) && (ua_buf[2] == RR(0) || ua_buf[2] == RR(1)) && ua_buf[3] == ua_buf[1] ^ ua_buf[2] && ua_buf[4] == FLAG ) {
            alarm(0);
            tramaTr = (tramaTr + 1) % 2;
            STOP = TRUE;
        }

        else if(bytes && ua_buf[0] == 0x7E && (ua_buf[1] == 0x03 || ua_buf[1] == 0x01) && (ua_buf[2] == REJECT(0) || ua_buf[2] == REJECT(1)) && ua_buf[3] == ua_buf[1] ^ ua_buf[2] && ua_buf[4] == FLAG ){
            alarm(0);
            sendFrame(fd, new_buf, new_buf_size + 1);            
        }

        else {
            if (alarmEnabled == FALSE) {
                sendFrame(fd, new_buf, new_buf_size + 1);
                alarmEnabled = TRUE;
            }
        }
    }

    if(alarmCount >= 5) { //mudar aqui tambem
        perror("Alarm count exceeded\n");
        return -1;
    }

    return new_buf_size + 1;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(int fd, unsigned char* packet) {
    unsigned char buf[1050];
    unsigned char destuffed_payload[1000];
    STOP = FALSE;
    enum States currentState = START;
    int i = 0;
    int destuf_size;

    while (STOP == FALSE) {
        int startOver = 0;
        int stop = 0;
        unsigned char stuffed_payload[2100];
        unsigned char byte;
        int p = 0;
        int read_success = 0;
        int its_disc = 0;
        int c_b;

        while (!startOver && !stop) {
            if (read(fd, &byte, 1) || read_success) {
                switch (currentState) {
                    case START:
                        if (byte == FLAG) {
                            buf[i] = byte;
                            i++;
                            currentState = FLAG_RCV;
                        }
                        else {
                            startOver = 1;
                            i = 0;
                        }
                        break;

                    case FLAG_RCV:
                        if (byte == A_TR) {
                            buf[i] = byte;
                            i++;
                            currentState = A_RCV;
                        }
                        else if (byte == FLAG) {
                            startOver = 1;
                            i = 0;
                        }
                        else {
                            currentState = START;
                            startOver = 1;
                            i = 0;
                        }
                        break;

                    case A_RCV:
                        if (byte == 0x00 || byte == 0x40) { // control package
                            buf[i] = byte;
                            c_b = byte;
                            i++;
                            currentState = CP_RCV;
                        }
                        else if (byte == C_SET) { // regular set
                            c_b = byte;
                            currentState = C_RCV;
                        }
                        else if (byte == C_DISC) { // disconnect
                            c_b = byte;
                            currentState = C_RCV;
                            its_disc = 1;
                        }
                        else if (byte == FLAG) {
                            startOver = 1;
                            i = 0;
                            currentState = FLAG_RCV;
                        }
                        else {
                            currentState = START;
                            startOver = 1;
                            i = 0;
                        }
                        break;

                    case C_RCV:
                        if (byte == (buf[1] ^ c_b)) {
                            buf[i] = byte;
                            i++;
                            currentState = BCC1_OK;
                            read_success = 1;
                        }
                        else if (byte == FLAG) {
                            startOver = 1;
                            i = 0;
                            currentState = FLAG_RCV;
                        }
                        else {
                            currentState = START;
                            startOver = 1;
                            i = 0;
                        }
                        break;

                    case BCC1_OK:
                        if (byte == FLAG) {
                            if (its_disc) currentState = STOP_MACHINE_DISC;
                            else currentState = STOP_MACHINE;
                        }
                        else {
                            currentState = START;
                            startOver = 1;
                        }
                        break;

                    case CP_RCV:
                        if (byte == (buf[1] ^ c_b)) {
                            buf[i] = byte;
                            i++;
                            currentState = PAYLOAD;
                        }
                        else if (byte == FLAG) {
                            startOver = 1;
                            i = 0;
                            currentState = FLAG_RCV;
                        }
                        else {
                            currentState = START;
                            startOver = 1;
                            i = 0;
                        }
                        break;

                    case PAYLOAD:
                        if (byte == FLAG) {
                            buf[i] = byte;
                            read_success = 1;
                            currentState = CHECK_BCC2;
                        }
                        else {
                            stuffed_payload[p] = byte;
                            buf[i] = byte;
                            p++;
                            i++;
                        }
                        break;

                    case CHECK_BCC2:
                        destuf_size = destuffing(stuffed_payload, destuffed_payload, p);
                        int bcc2 = buildBCC2(destuffed_payload, destuf_size - 1);

                        if (bcc2 == destuffed_payload[destuf_size-1]) currentState = BCC2_OK;
                        else {
                            i = 0;
                            read_success = 0;
                            stop = 1;
                            STOP = TRUE;
                            printf("Packet reject. Retransmiting");
                            sendSup(fd, A_REC, REJECT(tramaRc)); //send reject
                        }
                        break;

                    case BCC2_OK:
                        if (byte == FLAG) {
                            buf[i] = byte;
                            i++;
                            currentState = STOP_MACHINE;
                        }
                        else {
                            currentState = START;
                            startOver = 1;
                            i = 0;
                        }
                        break;

                    case STOP_MACHINE:
                        sendSup(fd, A_TR, RR(tramaRc)); // send rr
                        tramaRc = (tramaRc + 1)%2;
                        read_success = 0;
                        stop = 1;
                        STOP = TRUE;
                        break;

                    case STOP_MACHINE_DISC:
                        sendSup(fd, A_REC, c_b); // send disc
                        read_success = 0;
                        stop = 1;
                        STOP = TRUE;
                        break;

                    default:
                        break;
                }
            }
        }
    }

    for (int j = 0; j < destuf_size; j++) packet[j] = destuffed_payload[j];

    return destuf_size;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int fd, int showStatistics) {
    enum States currentState = START;
    (void)signal(SIGALRM, alarmHandler);
    
    // mandar disc
    unsigned char disc_buf[5];
    disc_buf[0] = FLAG;
    disc_buf[1] = A_TR;
    disc_buf[2] = C_DISC;
    disc_buf[3] = disc_buf[1] ^ disc_buf[2];
    disc_buf[4] = FLAG;

    sendFrame(fd, disc_buf, 5); // senf connection set

    // receber disc
    unsigned char byte;
    int stop = 0;

    while (stop == 0) {
        if (read(fd, &byte, 1)) {
            switch (currentState) {
                case START:
                    if (byte == FLAG) currentState = FLAG_RCV;
                    break;

                case FLAG_RCV:
                    if (byte == A_REC) currentState = A_RCV;
                    else if (byte != FLAG) currentState = START;
                    break;

                case A_RCV:
                    if (byte == C_DISC) currentState = C_RCV;
                    else if (byte == FLAG) currentState = FLAG_RCV;
                    else currentState = START;
                    break;

                case C_RCV:
                    if (byte == (A_REC ^ C_DISC)) currentState = BCC1_OK;
                    else if (byte == FLAG) currentState = FLAG_RCV;
                    else currentState = START;
                    break;

                case BCC1_OK:
                    if (byte == FLAG) stop = 1;
                    else currentState = START;
                    break;

                default:
                    break;
            }
        }
    }

    // mandar ua_disc
    if (stop != 1) return -1;
    sendSup(fd, A_TR, C_UA);
    alarm(0);
    return close(fd);
}
