#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

void *client_thread(void *arg) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char *request = "GET_TOP_PROCESSES";
    char buffer[1024] = {0};
    int port = 8005;

    int sleep_time = rand() % 4 + 2;
    sleep(sleep_time);

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return NULL;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Invalid address / Address not supported");
        close(sock);
        return NULL;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        close(sock);
        return NULL;
    }

    send(sock, request, strlen(request), 0);
    printf("Request sent from client\n");
    read(sock, buffer, 1024);
    printf("Response from server:\n%s\n", buffer);

    close(sock);
    return NULL;
}

int main(int argc, char const *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Error- enter the clients correctly");
        exit(EXIT_FAILURE);
    }

    int n = atoi(argv[1]);
    if (n <= 0) {
        fprintf(stderr, "Number of clients must be greater than 0 \n");
        exit(EXIT_FAILURE);
    }

    pthread_t *threads = malloc(n * sizeof(pthread_t));
    if (threads == NULL) {
        perror("Failed to allocate memory for threads");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < n; ++i) {
        if (pthread_create(&threads[i], NULL, client_thread, NULL) != 0) {
            perror("Failed to create thread");
            free(threads);
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < n; ++i) {
        pthread_join(threads[i], NULL);
    }

    free(threads);
    return 0;
}
