#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define SERVER_ADDR "ftp.up.pt"
#define SERVER_PORT 21
#define BUFFER_SIZE 1024

struct URL{
    char host[128];
    char path[128];
    char user[128];
    char password[128];
    char ip[128];
    char file[256];
    char host_name[128];
};


struct sockaddr_in server_address;
struct sockaddr_in data_server_address_in;
char buffer[BUFFER_SIZE];
char data_server_addr[INET_ADDRSTRLEN];

// ftp://[<user>:<password>@]<host>/<url-path>


int getIp(char* adress, struct URL *url){
    struct hostent *h;

    if ((h = gethostbyname(adress)) == NULL) {
        printf("Error getting host by name!\n");
        return -1;
    }

    strcpy(url->ip,inet_ntoa(*((struct in_addr *) h->h_addr)));
    strcpy(url->host_name,h->h_name);

    return 0;
}

int getFile(char* path, char* file){
  char strtokenpath[256];
  strcpy(strtokenpath, path);
  char* token = strtok(strtokenpath, "/");
  while( token != NULL ) {
    strcpy(file, token);
    token = strtok(NULL, "/");
  }
  return 0;
}

int checkAnonymous(char* untilhost){
    int isAnonymous = 1;

    for(int i = 0; i < strlen(untilhost); i++){
        if(untilhost[i] == ':'){
            isAnonymous = 0;
            break;
        }
    }
    return isAnonymous;
}

int parse(char *input, struct URL *url) {
    char *protocol = strtok(input,"/");//ftp     
    char *untilhost = strtok(NULL,"/"); //[<user>:<password>@]<host>
    char *untilhost_aux = untilhost;
    char *path = strtok(NULL,""); //<url-path>
    printf("path: /%s\n", path);

    if(path != NULL){
        strcpy(url->path, path);
    }

    int isAnonymous = checkAnonymous(untilhost);

    char *host;

    if(isAnonymous == 0){
        strtok(untilhost_aux,"@"); //<user>:<password>
        host = strtok(NULL,"/"); //<host>
        strcpy(url->host, host);
        printf("host: %s\n", host);
    }
    else{
        host = untilhost;
        strcpy(url->host, host);
        printf("host: %s\n", host);
    }

    

    if (strcmp(protocol, "ftp:") != 0) return -1;

    char *user;
    char *password;
    
    if (isAnonymous == 1) {
        user = "anonymous";
        password = "anonymous";
    } else {
        user = strtok(untilhost, ":");
        printf("user: %s\n", user);
        password = strtok(NULL, "@");
        printf("password: %s\n", password);
    }

    strcpy(url->user, user);
    strcpy(url->password, password);

    if (getIp(url->host, url) == -1) return -1;
    printf("ip: %s\n", url->ip);

    if (path != NULL) {
        if(getFile(url->path, url->file) != 0) return -1;
    }

    return 0;
}

int getResponses(int control_socket){
    size_t bytesRead;

    do {
        bytesRead = recv(control_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            printf("Server response: %s", buffer);
        }
    } while (bytesRead > 0 && strstr(buffer, "220 ") == NULL);

    return 0;
}

/**
 * @brief Opens a socket
*/
int openControlSocket(){
    int control_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (control_socket == -1) {
        perror("Error opening socket");
        exit(-1);
    }
    return control_socket;
}

/**
 * @brief Connects to the server
 * @param control_socket Socket to connect to the server
*/
int connectToServer(int control_socket, char* ip){
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(SERVER_PORT);
    server_address.sin_addr.s_addr = inet_addr(ip);

    printf("\nConnecting to server...\n");

    if (connect(control_socket, (struct sockaddr*)&server_address, sizeof(server_address)) == -1) {
        perror("Error connecting to server");
        exit(-1);
    }

    getResponses(control_socket);
    
    return 0;
}

/**
 * @brief Sends the username to the server
 * @param control_socket Socket to connect to the server
*/
int sendUser(int control_socket, char* user){
    char userCommand[5+strlen(user)+1]; sprintf(userCommand, "USER %s\r\n", user);
    printf("\nSending username...\n");
    send(control_socket, userCommand, strlen(userCommand), 0);
    int bytesRead = recv(control_socket, buffer, BUFFER_SIZE, 0);
    buffer[bytesRead] = '\0';
    printf("\nServer response: %s\n", buffer);
    return 0;
}

/**
 * @brief Sends the password to the server
 * @param control_socket Socket to connect to the server
*/
int sendPass(int control_socket, char* password){
    char passCommand[5+strlen(password)+1]; sprintf(passCommand, "PASS %s\r\n", password);
    printf("\nSending password...\n");
    send(control_socket, passCommand , strlen(passCommand), 0);
    int bytesRead = recv(control_socket, buffer, BUFFER_SIZE, 0);
    buffer[bytesRead] = '\0';
    printf("\nServer response: %s\n", buffer);
    return 0;
}

/**
 * @brief Sends the PASV command to the server
 * @param control_socket Socket to connect to the server
*/
int sendPASV(int control_socket){
    printf("\nSending PASV command...\n");
    send(control_socket, "PASV\r\n", strlen("PASV\r\n"), 0);
    int bytesRead = recv(control_socket, buffer, BUFFER_SIZE, 0);
    buffer[bytesRead] = '\0';
    printf("\nServer response: %s\n", buffer);
    return 0;
}

/**
 * @brief Gets the server reponse and extracts the server port
 * @return Returns the server port
*/
int getServerPort(){
    unsigned int ip[4], port[2];
    sscanf(buffer, "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u)\r\n",
           &ip[0], &ip[1], &ip[2], &ip[3], &port[0], &port[1]);

    sprintf(data_server_addr, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    int server_port = port[0] * 256 + port[1];

    printf("Data server address: %s\n", data_server_addr);
    printf("Data server port: %d\n", server_port);
    return server_port;
}

/**
 * @brief Connects to the data server
 * @param data_port Data server port
*/
int connectToDataServer(int data_socket, int data_port){
    memset(&data_server_address_in, 0, sizeof(data_server_address_in));
    data_server_address_in.sin_family = AF_INET;
    data_server_address_in.sin_port = htons(data_port);

    if (inet_pton(AF_INET, data_server_addr, &data_server_address_in.sin_addr) <= 0) {
        perror("Error converting server address");
        exit(-1);
    }

    if (connect(data_socket, (struct sockaddr*)&data_server_address_in, sizeof(data_server_address_in)) == -1) {
        perror("Error connecting to server");
        exit(-1);
    }
    
    printf("Connected to data server\n");

    return 0;
}

/**
 * @brief Sends the RETR command to the server
 * @param control_socket Socket to connect to the server
*/
int sendRETR(int control_socket, char* path){
    char retrPath[5+strlen(path)+1]; sprintf(retrPath, "RETR %s\r\n", path);
    printf("\nSending RETR command...\n");
    send(control_socket, retrPath, strlen(retrPath), 0);
    int bytesRead = recv(control_socket, buffer, BUFFER_SIZE, 0);
    buffer[bytesRead] = '\0';
    printf("\nServer response: %s\n", buffer);
    return 0;
}

/**
 * @brief Recieves the file from the server
 * @param data_socket Socket to connect to the server
*/
int recieveFile(int data_socket, char* filename){
    FILE *fd = fopen(filename, "wb");
    if (fd == NULL) {
        printf("Error opening or creating file '%s'\n", filename);
        exit(-1);
    }

    ssize_t bytesRead;
    while ((bytesRead = recv(data_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        fwrite(buffer, 1, bytesRead, fd);
    }

    printf("File '%s' downloaded successfully.\n", filename);

    fclose(fd);
    return 0;
}

int main(int argc, char *argv[]) {

    if (argc != 2) {
        printf("Usage: ./download ftp://[<user>:<password>@]<host>/<url-path>\n");
        exit(-1);
    } 

    struct URL url;
    parse(argv[1], &url);

    int control_socket = openControlSocket();

    connectToServer(control_socket, url.ip);

    sendUser(control_socket, url.user);

    sendPass(control_socket, url.password);

    sendPASV(control_socket);

    int data_port = getServerPort();

    int data_socket = openControlSocket();

    connectToDataServer(data_socket, data_port);

    sendRETR(control_socket, url.path);

    recieveFile(data_socket, url.file);

    return 0;    
}