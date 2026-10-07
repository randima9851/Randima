#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <time.h>

#define PORT 12732
#define NID_TAG " NID:7467\n"
#define MAX_CLIENTS 10
#define BUFFER_SIZE 2048
#define MAX_ROOMS 10
#define MAX_ROOM_MEMBERS 10
#define LOG_FILE "netmsg_IT23746732.log" // Personalized log file

typedef struct {
    int fd;
    char username[50];
    int registered;
    char recv_buffer[BUFFER_SIZE * 2];
    int recv_len;
    
    int receiving_file;
    int expected_file_size;
    int received_file_size;
    char file_target[50];
    char file_name[255];
    FILE* file_ptr;
} Client;

typedef struct {
    char name[50];
    int active;
    int members[MAX_ROOM_MEMBERS];
} Room;

Client clients[MAX_CLIENTS];
Room rooms[MAX_ROOMS];

// Logging helper function
void server_log(const char* message) {
    FILE* log_fp = fopen(LOG_FILE, "a");
    if (log_fp != NULL) {
        time_t now = time(NULL);
        char* time_str = ctime(&now);
        time_str[strlen(time_str) - 1] = '\0'; // Strip newline from ctime
        
        fprintf(log_fp, "[%s] %s\n", time_str, message);
        printf("[%s] %s\n", time_str, message); // Print to terminal too
        fclose(log_fp);
    }
}

void init_clients() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = 0;
        clients[i].registered = 0;
        clients[i].recv_len = 0;
        clients[i].receiving_file = 0;
        clients[i].file_ptr = NULL;
        memset(clients[i].username, 0, sizeof(clients[i].username));
        memset(clients[i].recv_buffer, 0, sizeof(clients[i].recv_buffer));
    }
}

void init_rooms() {
    for (int i = 0; i < MAX_ROOMS; i++) {
        rooms[i].active = 0;
        memset(rooms[i].name, 0, sizeof(rooms[i].name));
        for (int j = 0; j < MAX_ROOM_MEMBERS; j++) {
            rooms[i].members[j] = -1;
        }
    }
}

int check_target_exists(char* target) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd > 0 && clients[i].registered && strcmp(clients[i].username, target) == 0) return 1;
    }
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].active && strcmp(rooms[i].name, target) == 0) return 2;
    }
    return 0;
}

int process_command(int client_index, char* command) {
    int sd = clients[client_index].fd;
    char response[BUFFER_SIZE];
    memset(response, 0, BUFFER_SIZE);

    if (command[strlen(command) - 1] == '\r') {
        command[strlen(command) - 1] = '\0';
    }
    
    // Log the incoming command
    char log_msg[512];
    if (clients[client_index].registered) {
        snprintf(log_msg, sizeof(log_msg), "[%s] Command: %s", clients[client_index].username, command);
    } else {
        snprintf(log_msg, sizeof(log_msg), "[fd %d] Command: %s", sd, command);
    }
    server_log(log_msg);

    if (strncmp(command, "REGISTER ", 9) == 0) {
        char* requested_name = command + 9;
        int taken = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd > 0 && clients[i].registered && strcmp(clients[i].username, requested_name) == 0) {
                taken = 1; break;
            }
        }
        if (taken) snprintf(response, sizeof(response), "ERR 001 USERNAME_TAKEN%s", NID_TAG);
        else {
            strncpy(clients[client_index].username, requested_name, 49);
            clients[client_index].registered = 1;
            snprintf(response, sizeof(response), "OK REGISTERED %s%s", requested_name, NID_TAG);
        }
        send(sd, response, strlen(response), 0);
    } 
    else if (!clients[client_index].registered) {
        snprintf(response, sizeof(response), "ERR 000 NOT_REGISTERED%s", NID_TAG);
        send(sd, response, strlen(response), 0);
    }
    else if (strcmp(command, "LIST") == 0) {
        strcpy(response, "OK USERS ");
        int first = 1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd > 0 && clients[i].registered) {
                if (!first) strcat(response, ",");
                strcat(response, clients[i].username);
                first = 0;
            }
        }
        strcat(response, NID_TAG);
        send(sd, response, strlen(response), 0);
    }
    else if (strncmp(command, "BCAST ", 6) == 0) {
        char* msg = command + 6;
        char fwd_msg[BUFFER_SIZE];
        snprintf(fwd_msg, sizeof(fwd_msg), "MSG BCAST %s %s\n", clients[client_index].username, msg);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (i != client_index && clients[i].fd > 0 && clients[i].registered) {
                send(clients[i].fd, fwd_msg, strlen(fwd_msg), 0);
            }
        }
        snprintf(response, sizeof(response), "OK SENT%s", NID_TAG);
        send(sd, response, strlen(response), 0);
    }
    else if (strncmp(command, "PMSG ", 5) == 0) {
        char* space_ptr = strchr(command + 5, ' ');
        if (space_ptr != NULL) {
            *space_ptr = '\0'; 
            char* target_user = command + 5;
            char* msg = space_ptr + 1;
            int found = 0;
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].fd > 0 && clients[i].registered && strcmp(clients[i].username, target_user) == 0) {
                    found = 1;
                    char fwd_msg[BUFFER_SIZE];
                    snprintf(fwd_msg, sizeof(fwd_msg), "MSG PRIV %s %s\n", clients[client_index].username, msg);
                    send(clients[i].fd, fwd_msg, strlen(fwd_msg), 0);
                    break;
                }
            }
            if (found) snprintf(response, sizeof(response), "OK SENT%s", NID_TAG);
            else snprintf(response, sizeof(response), "ERR 002 USER_NOT_FOUND%s", NID_TAG);
        } else {
            snprintf(response, sizeof(response), "ERR 005 MALFORMED_COMMAND%s", NID_TAG);
        }
        send(sd, response, strlen(response), 0);
    }
    else if (strncmp(command, "JOIN ", 5) == 0) {
        char* room_name = command + 5;
        int room_idx = -1;
        for (int i = 0; i < MAX_ROOMS; i++) if (rooms[i].active && strcmp(rooms[i].name, room_name) == 0) { room_idx = i; break; }
        if (room_idx == -1) for (int i = 0; i < MAX_ROOMS; i++) if (!rooms[i].active) { room_idx = i; rooms[i].active = 1; strncpy(rooms[i].name, room_name, 49); break; }
        if (room_idx != -1) {
            int joined = 0;
            for(int j=0; j<MAX_ROOM_MEMBERS; j++) if (rooms[room_idx].members[j] == client_index) { joined = 1; break; }
            if (!joined) for (int j = 0; j < MAX_ROOM_MEMBERS; j++) if (rooms[room_idx].members[j] == -1) { rooms[room_idx].members[j] = client_index; joined = 1; break; }
            if (joined) snprintf(response, sizeof(response), "OK JOINED %s%s", room_name, NID_TAG);
            else snprintf(response, sizeof(response), "ERR 006 ROOM_FULL%s", NID_TAG);
        } else snprintf(response, sizeof(response), "ERR 007 MAX_ROOMS_REACHED%s", NID_TAG);
        send(sd, response, strlen(response), 0);
    }
    else if (strncmp(command, "LEAVE ", 6) == 0) {
        char* room_name = command + 6;
        int found_room = -1, found_user = 0;
        for (int i = 0; i < MAX_ROOMS; i++) {
            if (rooms[i].active && strcmp(rooms[i].name, room_name) == 0) {
                found_room = i;
                for (int j = 0; j < MAX_ROOM_MEMBERS; j++) if (rooms[i].members[j] == client_index) { rooms[i].members[j] = -1; found_user = 1; break; }
                int is_empty = 1;
                for (int j = 0; j < MAX_ROOM_MEMBERS; j++) if (rooms[i].members[j] != -1) { is_empty = 0; break; }
                if (is_empty) rooms[i].active = 0;
                break;
            }
        }
        if (found_room != -1 && found_user) snprintf(response, sizeof(response), "OK LEFT %s%s", room_name, NID_TAG);
        else snprintf(response, sizeof(response), "ERR 003 ROOM_NOT_FOUND%s", NID_TAG);
        send(sd, response, strlen(response), 0);
    }
    else if (strcmp(command, "ROOMS") == 0) {
        strcpy(response, "OK ROOMS ");
        int first = 1;
        for (int i = 0; i < MAX_ROOMS; i++) {
            if (rooms[i].active) {
                if (!first) strcat(response, ",");
                strcat(response, rooms[i].name);
                first = 0;
            }
        }
        strcat(response, NID_TAG);
        send(sd, response, strlen(response), 0);
    }
    else if (strncmp(command, "RMSG ", 5) == 0) {
        char* space_ptr = strchr(command + 5, ' ');
        if (space_ptr != NULL) {
            *space_ptr = '\0';
            char* room_name = command + 5;
            char* msg = space_ptr + 1;
            int room_idx = -1;
            for (int i = 0; i < MAX_ROOMS; i++) if (rooms[i].active && strcmp(rooms[i].name, room_name) == 0) { room_idx = i; break; }
            if (room_idx != -1) {
                int is_member = 0;
                for (int j = 0; j < MAX_ROOM_MEMBERS; j++) if (rooms[room_idx].members[j] == client_index) { is_member = 1; break; }
                if (is_member) {
                    char fwd_msg[BUFFER_SIZE];
                    snprintf(fwd_msg, sizeof(fwd_msg), "MSG ROOM %s %s %s\n", room_name, clients[client_index].username, msg);
                    for (int j = 0; j < MAX_ROOM_MEMBERS; j++) {
                        int mem_idx = rooms[room_idx].members[j];
                        if (mem_idx != -1 && mem_idx != client_index && clients[mem_idx].fd > 0) send(clients[mem_idx].fd, fwd_msg, strlen(fwd_msg), 0);
                    }
                    snprintf(response, sizeof(response), "OK SENT%s", NID_TAG);
                } else snprintf(response, sizeof(response), "ERR 003 ROOM_NOT_FOUND%s", NID_TAG);
            } else snprintf(response, sizeof(response), "ERR 003 ROOM_NOT_FOUND%s", NID_TAG);
        } else snprintf(response, sizeof(response), "ERR 005 MALFORMED_COMMAND%s", NID_TAG);
        send(sd, response, strlen(response), 0);
    }
    else if (strncmp(command, "SENDFILE ", 9) == 0) {
        char target[50], filename[255];
        int filesize = 0;
        
        if (sscanf(command + 9, "%s %s %d", target, filename, &filesize) == 3) {
            int exists = check_target_exists(target);
            if (exists) {
                char dir_path[256];
                snprintf(dir_path, sizeof(dir_path), "./storage/IT23746732/%s", clients[client_index].username);
                char mkdir_cmd[300];
                snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", dir_path);
                system(mkdir_cmd);
                
                char filepath[512];
                snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, filename);
                clients[client_index].file_ptr = fopen(filepath, "wb");
                
                if (clients[client_index].file_ptr != NULL) {
                    clients[client_index].receiving_file = 1;
                    clients[client_index].expected_file_size = filesize;
                    clients[client_index].received_file_size = 0;
                    strncpy(clients[client_index].file_target, target, 49);
                    strncpy(clients[client_index].file_name, filename, 254);
                    return 1;
                } else {
                    snprintf(response, sizeof(response), "ERR 008 FILE_OPEN_ERROR%s", NID_TAG);
                }
            } else {
                snprintf(response, sizeof(response), "ERR 002 USER_NOT_FOUND%s", NID_TAG);
            }
        } else {
            snprintf(response, sizeof(response), "ERR 005 MALFORMED_COMMAND%s", NID_TAG);
        }
        send(sd, response, strlen(response), 0);
    }
    else {
        snprintf(response, sizeof(response), "ERR 005 UNKNOWN_COMMAND%s", NID_TAG);
        send(sd, response, strlen(response), 0);
    }
    return 0; 
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size;
    fd_set read_fds, master_fds;
    int max_sd;
    
    init_clients();
    init_rooms();

    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 5) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    char log_startup[256];
    snprintf(log_startup, sizeof(log_startup), "Server started on port %d", PORT);
    server_log(log_startup);

    FD_ZERO(&master_fds);
    FD_SET(server_socket, &master_fds);
    max_sd = server_socket;

    while (1) {
        read_fds = master_fds;
        if (select(max_sd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("Select error");
            break;
        }

        if (FD_ISSET(server_socket, &read_fds)) {
            addr_size = sizeof(client_addr);
            if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_size)) < 0) {
                perror("Accept failed");
                continue;
            }

            char log_conn[256];
            snprintf(log_conn, sizeof(log_conn), "New connection: fd %d IP %s", client_socket, inet_ntoa(client_addr.sin_addr));
            server_log(log_conn);

            FD_SET(client_socket, &master_fds);
            if (client_socket > max_sd) max_sd = client_socket;

            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].fd == 0) {
                    clients[i].fd = client_socket;
                    clients[i].registered = 0;
                    clients[i].recv_len = 0;
                    clients[i].receiving_file = 0;
                    break;
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = clients[i].fd;

            if (sd > 0 && FD_ISSET(sd, &read_fds)) {
                char buffer[BUFFER_SIZE];
                memset(buffer, 0, BUFFER_SIZE);
                int valread = read(sd, buffer, BUFFER_SIZE - 1);
                
                if (valread == 0) {
                    char log_disconn[256];
                    if (clients[i].registered) snprintf(log_disconn, sizeof(log_disconn), "Client disconnected: %s (fd %d)", clients[i].username, sd);
                    else snprintf(log_disconn, sizeof(log_disconn), "Client disconnected: unregistered (fd %d)", sd);
                    server_log(log_disconn);

                    if (clients[i].file_ptr) fclose(clients[i].file_ptr);
                    close(sd);
                    FD_CLR(sd, &master_fds);
                    clients[i].fd = 0;
                    clients[i].registered = 0;
                    
                    for (int r = 0; r < MAX_ROOMS; r++) {
                        if (rooms[r].active) {
                            for (int m = 0; m < MAX_ROOM_MEMBERS; m++) {
                                if (rooms[r].members[m] == i) rooms[r].members[m] = -1;
                            }
                            int is_empty = 1;
                            for (int m = 0; m < MAX_ROOM_MEMBERS; m++) {
                                if (rooms[r].members[m] != -1) { is_empty = 0; break; }
                            }
                            if (is_empty) rooms[r].active = 0;
                        }
                    }
                } else if (valread > 0) {
                    if (clients[i].receiving_file) {
                        int to_write = valread;
                        if (clients[i].received_file_size + to_write > clients[i].expected_file_size) {
                            to_write = clients[i].expected_file_size - clients[i].received_file_size;
                        }
                        
                        fwrite(buffer, 1, to_write, clients[i].file_ptr);
                        clients[i].received_file_size += to_write;
                        
                        if (clients[i].received_file_size >= clients[i].expected_file_size) {
                            fclose(clients[i].file_ptr);
                            clients[i].file_ptr = NULL;
                            clients[i].receiving_file = 0; 
                            
                            char log_file[512];
                            snprintf(log_file, sizeof(log_file), "[%s] Completed file transfer: %s (%d bytes)", clients[i].username, clients[i].file_name, clients[i].received_file_size);
                            server_log(log_file);
                            
                            char response[256];
                            snprintf(response, sizeof(response), "OK FILE_RECEIVED %s%s", clients[i].file_name, NID_TAG);
                            send(sd, response, strlen(response), 0);
                        }
                    } 
                    else {
                        if (clients[i].recv_len + valread < (BUFFER_SIZE * 2) - 1) {
                            memcpy(clients[i].recv_buffer + clients[i].recv_len, buffer, valread);
                            clients[i].recv_len += valread;
                            clients[i].recv_buffer[clients[i].recv_len] = '\0';
                            
                            char *newline_ptr;
                            while (!clients[i].receiving_file && (newline_ptr = strchr(clients[i].recv_buffer, '\n')) != NULL) {
                                *newline_ptr = '\0';
                                
                                char command[BUFFER_SIZE];
                                strcpy(command, clients[i].recv_buffer);
                                
                                int transitioned = process_command(i, command);
                                int remaining_len = clients[i].recv_len - (newline_ptr - clients[i].recv_buffer + 1);
                                
                                if (transitioned) {
                                    if (remaining_len > 0) {
                                        int to_write = remaining_len;
                                        if (to_write > clients[i].expected_file_size) to_write = clients[i].expected_file_size;
                                        fwrite(newline_ptr + 1, 1, to_write, clients[i].file_ptr);
                                        clients[i].received_file_size += to_write;
                                        
                                        if (clients[i].received_file_size >= clients[i].expected_file_size) {
                                            fclose(clients[i].file_ptr);
                                            clients[i].file_ptr = NULL;
                                            clients[i].receiving_file = 0;
                                            
                                            char log_file[512];
                                            snprintf(log_file, sizeof(log_file), "[%s] Completed file transfer: %s (%d bytes)", clients[i].username, clients[i].file_name, clients[i].received_file_size);
                                            server_log(log_file);
                                            
                                            char response[256];
                                            snprintf(response, sizeof(response), "OK FILE_RECEIVED %s%s", clients[i].file_name, NID_TAG);
                                            send(sd, response, strlen(response), 0);
                                        }
                                    }
                                    clients[i].recv_len = 0;
                                    break; 
                                } else {
                                    memmove(clients[i].recv_buffer, newline_ptr + 1, remaining_len);
                                    clients[i].recv_len = remaining_len;
                                    clients[i].recv_buffer[clients[i].recv_len] = '\0';
                                }
                            }
                        } else {
                            clients[i].recv_len = 0; 
                        }
                    }
                }
            }
        }
    }
    return 0;
}
