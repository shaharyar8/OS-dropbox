#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define SERVER_IP "127.0.0.1"
#define PORT 8080
#define BUFFER_SIZE 2048
void send_and_receive(int sock, const char* command) 
{
    char buffer[BUFFER_SIZE] = {0};

    printf("\n[C] > %s", command);
    send(sock, command, strlen(command), 0);
    int bytes_read = read(sock, buffer, BUFFER_SIZE - 1);
    if (bytes_read > 0) 
    {
        buffer[bytes_read] = '\0';
        printf("[S] < %s", buffer);
    }
}

int main() 
{
    int sock = 0;
    struct sockaddr_in serv_addr;
    char initial_buffer[BUFFER_SIZE] = {0};

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
    {
    
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) 
    {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("\nConnection Failed \n");
        return -1;
    }
    int bytes_read = read(sock, initial_buffer, BUFFER_SIZE - 1);
    if (bytes_read > 0) {
        initial_buffer[bytes_read] = '\0';
        printf("[S] < %s", initial_buffer);
    }

    send_and_receive(sock, "SIGNUP testuser testpass\n");
    send_and_receive(sock, "LOGIN testuser testpass\n");
    send_and_receive(sock, "UPLOAD file1.txt This is the content of the first file.\n");
    send_and_receive(sock, "UPLOAD file2.txt Some other content here.\n");
    send_and_receive(sock, "LIST\n");
    send_and_receive(sock, "DOWNLOAD file1.txt\n");
    send_and_receive(sock, "DELETE file2.txt\n");
    send_and_receive(sock, "LIST\n");
    send_and_receive(sock, "DOWNLOAD file2.txt\n");
    close(sock);
    return 0;
}
