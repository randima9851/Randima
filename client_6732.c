#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h> // Included for select()

#define PORT 12732
#define BUFFER_SIZE 1024

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};

    // 1. Create a TCP socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    // 2. Configure the server address structure
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT); // Use personalized port

    // Convert localhost IP address from text to binary format
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address or Address not supported \n");
        return -1;
    }

    // 3. Connect to the server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed! Make sure the server is running.\n");
        return -1;
    }

    printf("NetMessenger Client connected to 127.0.0.1:%d!\n", PORT);
    printf("Type your messages below:\n");

    // Variables for select() multiplexing
    fd_set read_fds;
    int max_sd = sock;

    // 4. Main Event Loop
    while (1) {
        // Clear the file descriptor set
        FD_ZERO(&read_fds);
        
        // Watch standard input (fd 0) for user typing
        FD_SET(STDIN_FILENO, &read_fds);
        
        // Watch the network socket for incoming messages from the server
        FD_SET(sock, &read_fds);

        // Block and wait for activity on either standard input OR the network socket
        if (select(max_sd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("select error");
            break;
        }

        // Case A: The user typed something in the terminal
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            memset(buffer, 0, BUFFER_SIZE);
            if (fgets(buffer, BUFFER_SIZE, stdin) != NULL) {
                // Send the typed message to the server
                send(sock, buffer, strlen(buffer), 0);
            }
        }

        // Case B: The server sent a message over the network
        if (FD_ISSET(sock, &read_fds)) {
            memset(buffer, 0, BUFFER_SIZE);
            int valread = read(sock, buffer, BUFFER_SIZE - 1);
            
            if (valread == 0) {
                // Read returning 0 means the server closed the connection
                printf("\nServer disconnected.\n");
                break;
            } else if (valread > 0) {
                // Print the message received from the server
                printf("Server: %s", buffer);
            }
        }
    }

    // Clean up and close socket
    close(sock);
    return 0;
}
