#include <stdio.h>
#include <stdlib.h>

#include <unistd.h> // For read()/write() and close()
#include <string.h> // For memset() and other string functions

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> // For struct hostent and gethostbyname()

int main(int argc, char *argv[]) {
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char buffer[256];
    portno = 5001; // Ensure this matches the server's listening port

    // Create socket and get file descriptor
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    server = gethostbyname("127.0.0.1");
    if (server == NULL) {
        fprintf(stderr, "ERROR, no such host\n");
        exit(0);
    }

    // Clear serv_addr structure
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    // Copy the server's IP address from the hostent structure to serv_addr
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(portno);

    // Connect to server with the server address set in serv_addr
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR while connecting");
        exit(1);
    }

    // Communicate with the server inside this loop
    while (1) {
        printf("What do you want to say? ");
        memset(buffer, 0, 256);
        fgets(buffer, 255, stdin); // Read a line from stdin including spaces
        buffer[strcspn(buffer, "\n")] = 0; // Remove the newline character

        // Send the message to the server
        n = write(sockfd, buffer, strlen(buffer));
        if (n < 0) {
            perror("ERROR while writing to socket");
            exit(1);
        }

        // Receive the response from the server
        memset(buffer, 0, 256);
        n = read(sockfd, buffer, 255);
        if (n < 0) {
            perror("ERROR while reading from socket");
            exit(1);
        }

        printf("Server replied: %s\n", buffer);

        // Exit the loop if the server sends the message "quit"
        if (strcmp(buffer, "quit") == 0) {
            break;
        }
    }

    close(sockfd); // Close the socket when done
    return 0;
}
