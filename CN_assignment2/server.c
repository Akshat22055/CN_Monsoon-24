#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <pthread.h>

#define MAX_BUF 2048

typedef struct
{
    unsigned long user_time;
    unsigned long system_time;
    int id;
    char exec[256];
} ProcData;

int is_valid_process(struct dirent *dir_entry)
{
    if (dir_entry->d_type != DT_DIR)
    {
        return 0;
    }
    int process_id = atoi(dir_entry->d_name);
    return process_id > 0;
}

int get_process_data(const char *pid_str, ProcData *pd)
{
    char stat_path[MAX_BUF];
    snprintf(stat_path, sizeof(stat_path), "/proc/%s/stat", pid_str);

    FILE *stat_file = fopen(stat_path, "r");
    if (!stat_file)
    {
        return 0;
    }

    pd->id = atoi(pid_str);
    fscanf(stat_file, "%*d (%255[^)]) %*c %*s %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu",
           pd->exec, &pd->user_time, &pd->system_time);

    fclose(stat_file);
    return 1;
}

void format_top_processes(char *output, ProcData *top1, ProcData *top2)
{
    if (top1 && top2)
    {
        snprintf(output, MAX_BUF,
                 "1. PID: %d, Name: %s, User Time: %lu, Kernel Time: %lu\n"
                 "2. PID: %d, Name: %s, User Time: %lu, Kernel Time: %lu\n",
                 top1->id, top1->exec, top1->user_time, top1->system_time,
                 top2->id, top2->exec, top2->user_time, top2->system_time);
    }
    else if (top1)
    {
        snprintf(output, MAX_BUF,
                 "1. PID: %d, Name: %s, User Time: %lu, Kernel Time: %lu\n",
                 top1->id, top1->exec, top1->user_time, top1->system_time);
    }
    else
    {
        strcpy(output, "No processes found");
    }
}

void fetch_top_processes(char *output)
{
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir)
    {
        strcpy(output, "Failed to open /proc");
        return;
    }

    ProcData proc_list[MAX_BUF];
    int proc_count = 0;

    struct dirent *dir_entry;
    while ((dir_entry = readdir(proc_dir)) != NULL && proc_count < MAX_BUF)
    {
        if (is_valid_process(dir_entry))
        {
            ProcData pd;
            if (get_process_data(dir_entry->d_name, &pd))
            {
                proc_list[proc_count++] = pd;
            }
        }
    }

    closedir(proc_dir);

    if (proc_count == 0)
    {
        strcpy(output, "No processes found");
        return;
    }

    // Find the top two processes by CPU usage using two loops
    ProcData *top1 = NULL, *top2 = NULL;

    // First loop: find the most CPU-consuming process
    for (int i = 0; i < proc_count; i++)
    {
        unsigned long total_time = proc_list[i].user_time + proc_list[i].system_time;
        if (top1 == NULL || total_time > (top1->user_time + top1->system_time))
        {
            top1 = &proc_list[i];
        }
    }

    // Second loop: find the second most CPU-consuming process
    for (int i = 0; i < proc_count; i++)
    {
        unsigned long total_time = proc_list[i].user_time + proc_list[i].system_time;
        if (&proc_list[i] != top1 &&
            (top2 == NULL || total_time > (top2->user_time + top2->system_time)))
        {
            top2 = &proc_list[i];
        }
    }

    format_top_processes(output, top1, top2);
}



void *client_handler(void *sock_ptr)
{
    int client_sock = *((int *)sock_ptr);
    free(sock_ptr);

    char request[1024] = {0};
    char response[MAX_BUF] = {0};

    read(client_sock, request, 1024);
    if (strcmp(request, "GET_TOP_PROCESSES") == 0)
    {
        fetch_top_processes(response);
        send(client_sock, response, strlen(response), 0);
    }
    else
    {
        const char *error_msg = "Invalid request";
        send(client_sock, error_msg, strlen(error_msg), 0);
    }

    close(client_sock);
    return NULL;
}

int main()
{
    int server_sock;
    struct sockaddr_in server_addr;
    int addr_len = sizeof(server_addr);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8005);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind operation failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_sock, 3) < 0)
    {
        perror("Listen operation failed");
        exit(EXIT_FAILURE);
    }

    while (1)
    {
        int *client_sock = malloc(sizeof(int));
        *client_sock = accept(server_sock, (struct sockaddr *)&server_addr, (socklen_t *)&addr_len);
        if (*client_sock < 0)
        {
            perror("Accept operation failed");
            free(client_sock);
            continue;
        }

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, client_handler, (void *)client_sock) != 0)
        {
            perror("Thread creation failed");
            free(client_sock);
        }
        else
        {
            pthread_detach(thread_id);
        }
    }

    close(server_sock);
    return 0;
}
