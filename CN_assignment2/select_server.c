#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <errno.h>

#define PORT 8080
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define MAX_BUF 256

typedef struct {
    unsigned long user_time;
    unsigned long system_time;
    int id;
    char exec[256];
} ProcData;

int is_valid_process(struct dirent *dir_entry) {
    if (dir_entry->d_type != DT_DIR) {
        return 0;
    }
    int process_id = atoi(dir_entry->d_name);
    return process_id > 0;
}

int get_process_data(const char *pid_str, ProcData *pd) {
    char stat_path[MAX_BUF];
    snprintf(stat_path, sizeof(stat_path), "/proc/%s/stat", pid_str);

    FILE *stat_file = fopen(stat_path, "r");
    if (!stat_file) {
        return 0;
    }

    pd->id = atoi(pid_str);
    fscanf(stat_file, "%*d (%255[^)]) %*c %*s %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu",
           pd->exec, &pd->user_time, &pd->system_time);

    fclose(stat_file);
    return 1;
}

void format_top_processes(char *output, ProcData *top1, ProcData *top2) {
    if (top1 && top2) {
        snprintf(output, BUFFER_SIZE,
                 "1. PID: %d, Name: %s, User Time: %lu, Kernel Time: %lu\n"
                 "2. PID: %d, Name: %s, User Time: %lu, Kernel Time: %lu\n",
                 top1->id, top1->exec, top1->user_time, top1->system_time,
                 top2->id, top2->exec, top2->user_time, top2->system_time);
    } else if (top1) {
        snprintf(output, BUFFER_SIZE,
                 "1. PID: %d, Name: %s, User Time: %lu, Kernel Time: %lu\n",
                 top1->id, top1->exec, top1->user_time, top1->system_time);
    } else {
        strcpy(output, "No processes found\n");
    }
}

void fetch_top_processes(char *output) {
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) {
        strcpy(output, "Failed to open /proc\n");
        return;
    }

    ProcData proc_list[MAX_BUF];
    int proc_count = 0;

    struct dirent *dir_entry;
    while ((dir_entry = readdir(proc_dir)) != NULL && proc_count < MAX_BUF) {
        if (is_valid_process(dir_entry)) {
            ProcData pd;
            if (get_process_data(dir_entry->d_name, &pd)) {
                proc_list[proc_count++] = pd;
            }
        }
    }

    closedir(proc_dir);

    if (proc_count == 0) {
        strcpy(output, "No processes found\n");
        return;
    }

    // Find the top two processes by CPU usage using two loops
    ProcData *top1 = NULL, *top2 = NULL;

    // First loop: find the most CPU-consuming process
    for (int i = 0; i < proc_count; i++) {
        unsigned long total_time = proc_list[i].user_time + proc_list[i].system_time;
        if (top1 == NULL || total_time > (top1->user_time + top1->system_time)) {
            top1 = &proc_list[i];
        }
    }

    // Second loop: find the second most CPU-consuming process
    for (int i = 0; i < proc_count; i++) {
        unsigned long total_time = proc_list[i].user_time + proc_list[i].system_time;
        if (&proc_list[i] != top1 &&
            (top2 == NULL || total_time > (top2->user_time + top2->system_time))) {
            top2 = &proc_list[i];
        }
    }

    format_top_processes(output, top1, top2);
}

int main() {
    int server_fd, new_socket, client_sockets[MAX_CLIENTS], max_sd, sd;
    struct sockaddr_in address;
    fd_set readfds;
    char buffer[BUFFER_SIZE];

    // Initialize client sockets
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_sockets[i] = 0;
    }

    // Create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    // Bind the socket to the address and port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Listening on port %d\n", PORT);

    int addrlen = sizeof(address);

    while (1) {
        // Clear the socket set
        FD_ZERO(&readfds);

        // Add server socket to set
        FD_SET(server_fd, &readfds);
        max_sd = server_fd;

        // Add client sockets to set
        for (int i = 0; i < MAX_CLIENTS; i++) {
            sd = client_sockets[i];
            if (sd > 0) {
                FD_SET(sd, &readfds);
            }
            if (sd > max_sd) {
                max_sd = sd;
            }
        }

        // Wait for activity on any of the sockets
        int activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if ((activity < 0) && (errno != EINTR)) {
            perror("Select error");
        }

        // If something happened on the server socket, it's an incoming connection
        if (FD_ISSET(server_fd, &readfds)) {
            if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
                perror("Accept failed");
                exit(EXIT_FAILURE);
            }

            printf("New connection, socket fd is %d\n", new_socket);

            // Add new socket to the client_sockets array
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i] == 0) {
                    client_sockets[i] = new_socket;
                    printf("Adding to list of sockets as %d\n", i);
                    break;
                }
            }
        }

        // Check all client sockets
        for (int i = 0; i < MAX_CLIENTS; i++) {
            sd = client_sockets[i];
            if (FD_ISSET(sd, &readfds)) {
                // Check if it was for closing, and also read the incoming message
                int valread = read(sd, buffer, BUFFER_SIZE);
                if (valread == 0) {
                    // Client disconnected
                    getpeername(sd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
                    printf("Host disconnected, IP %s, port %d\n",
                           inet_ntoa(address.sin_addr), ntohs(address.sin_port));

                    close(sd);
                    client_sockets[i] = 0;
                } else {
                    // Echo back the message to the client
                    buffer[valread] = '\0';
                    printf("Received: %s\n", buffer);
                    char response[BUFFER_SIZE];
                    fetch_top_processes(response);
                    send(sd, response, strlen(response), 0);
                }
            }
        }
    }

    return 0;
}
