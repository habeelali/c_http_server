#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <semaphore.h>
#include <signal.h>
#include "request_handler.h"
#include "logger.h"

#define PORT 8080
#define MAX_THREADS 256
#define MAX_QUEUE 256

// Job queue
typedef struct
{
    int client_socket;
} job_t;

typedef struct
{
    job_t jobs[MAX_QUEUE];
    int front, rear, count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} job_queue_t;

job_queue_t job_queue;
pthread_t threads[MAX_THREADS];
sem_t file_semaphore;

// Function prototypes
void initialize_server(int *server_fd, struct sockaddr_in *address);
void *worker_thread(void *arg);

void job_queue_init(job_queue_t *queue)
{
    queue->front = queue->rear = queue->count = 0;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
}

void job_queue_push(job_queue_t *queue, int client_socket)
{
    pthread_mutex_lock(&queue->mutex);
    while (queue->count == MAX_QUEUE)
    {
        log_message(LOG_WARNING, "Job queue full, waiting...");
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }
    queue->jobs[queue->rear].client_socket = client_socket;
    queue->rear = (queue->rear + 1) % MAX_QUEUE;
    queue->count++;
    log_message(LOG_INFO, "Added to queue. Current queue size: %d", queue->count);
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

int job_queue_pop(job_queue_t *queue)
{
    pthread_mutex_lock(&queue->mutex);
    while (queue->count == 0)
    {
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }
    int client_socket = queue->jobs[queue->front].client_socket;
    queue->front = (queue->front + 1) % MAX_QUEUE;
    queue->count--;
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
    return client_socket;
}

int main()
{
    signal(SIGPIPE, SIG_IGN);
    int server_fd;
    struct sockaddr_in address;
    job_queue_init(&job_queue);
    sem_init(&file_semaphore, 0, 3); // Limit concurrent file access to 3 threads

    initialize_server(&server_fd, &address);

    // Create worker threads
    for (int i = 0; i < MAX_THREADS; i++)
    {
        pthread_create(&threads[i], NULL, worker_thread, (void *)(size_t)i);
    }

    printf("Server is running on port %d...\n", PORT);

    while (1)
    {
        int client_socket = accept(server_fd, NULL, NULL);
        if (client_socket < 0)
        {
            perror("Accept failed");
            continue;
        }
        job_queue_push(&job_queue, client_socket);
    }

    close(server_fd);
    return 0;
}

void initialize_server(int *server_fd, struct sockaddr_in *address)
{
    *server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (*server_fd == 0)
    {
        log_message(LOG_ERROR, "Socket creation failed");
        exit(EXIT_FAILURE);
    }
    log_message(LOG_INFO, "Socket created successfully");

    address->sin_family = AF_INET;
    address->sin_addr.s_addr = INADDR_ANY;
    address->sin_port = htons(PORT);

    while (1)
    {
        if (bind(*server_fd, (struct sockaddr *)address, sizeof(*address)) < 0)
        {
            // log_message(LOG_ERROR, "Bind failed");
            // exit(EXIT_FAILURE);
        }
        else
            break;
    }
    log_message(LOG_INFO, "Bind successful, server is running on port %d", PORT);

    if (listen(*server_fd, MAX_QUEUE) < 0)
    {
        log_message(LOG_ERROR, "Listen failed");
        exit(EXIT_FAILURE);
    }
    log_message(LOG_INFO, "Server listening...");
}

void *worker_thread(void *arg)
{
    int thread_id = (int)(size_t)arg; // Assign unique ID to each thread
    while (1)
    {
        log_message(LOG_INFO, "Worker thread %d waiting for a job...", thread_id);
        int client_socket = job_queue_pop(&job_queue);
        log_message(LOG_INFO, "Worker thread %d handling connection on socket %d", thread_id, client_socket);
        handle_request(client_socket);
        close(client_socket);
        log_message(LOG_INFO, "Worker thread %d finished job on socket %d", thread_id, client_socket);
    }
    return NULL;
}
