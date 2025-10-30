#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h>

#define PORT 8080
#define MAX_PENDING_CONNECTIONS 10
#define QUEUE_SIZE 256
#define CLIENT_THREAD_POOL_SIZE 8
#define WORKER_THREAD_POOL_SIZE 8
#define MAX_USERS 50
#define MAX_USERNAME_LEN 32
#define MAX_PASSWORD_LEN 32
#define MAX_FILENAME_LEN 200
#define BUFFER_SIZE 2048
#define SERVER_FILES_ROOT "server_files"

volatile sig_atomic_t shutdown_requested = 0;
int server_fd;

typedef struct { char username[MAX_USERNAME_LEN]; char password[MAX_PASSWORD_LEN]; pthread_mutex_t file_lock; } User;
User users[MAX_USERS];
int user_count = 0;
pthread_mutex_t user_list_lock;

typedef struct { int socket_fds[QUEUE_SIZE]; int head, tail, count; pthread_mutex_t lock; pthread_cond_t not_empty, not_full; } ClientQueue;
ClientQueue client_queue;

typedef enum { LIST, UPLOAD, DOWNLOAD, DELETE } CommandType;
typedef struct { CommandType command; int client_socket; int user_id; char arg1[BUFFER_SIZE]; char arg2[BUFFER_SIZE]; char response[BUFFER_SIZE]; int is_done; pthread_mutex_t lock; pthread_cond_t done_cond; } Task;
typedef struct { Task* tasks[QUEUE_SIZE]; int head, tail, count; pthread_mutex_t lock; pthread_cond_t not_empty, not_full; } TaskQueue;
TaskQueue task_queue;

void queue_init(ClientQueue *q) { q->head = q->tail = q->count = 0; pthread_mutex_init(&q->lock, NULL); pthread_cond_init(&q->not_empty, NULL); pthread_cond_init(&q->not_full, NULL); }
void queue_push(ClientQueue *q, int socket_fd) { pthread_mutex_lock(&q->lock); while (q->count == QUEUE_SIZE && !shutdown_requested) pthread_cond_wait(&q->not_full, &q->lock); if(shutdown_requested){ pthread_mutex_unlock(&q->lock); return; } q->socket_fds[q->tail] = socket_fd; q->tail = (q->tail + 1) % QUEUE_SIZE; q->count++; pthread_cond_signal(&q->not_empty); pthread_mutex_unlock(&q->lock); }
int queue_pop(ClientQueue *q) { pthread_mutex_lock(&q->lock); while (q->count == 0 && !shutdown_requested) pthread_cond_wait(&q->not_empty, &q->lock); if (shutdown_requested && q->count == 0) { pthread_mutex_unlock(&q->lock); return -1; } int socket_fd = q->socket_fds[q->head]; q->head = (q->head + 1) % QUEUE_SIZE; q->count--; pthread_cond_signal(&q->not_full); pthread_mutex_unlock(&q->lock); return socket_fd; }
void task_queue_init(TaskQueue *q) { q->head = q->tail = q->count = 0; pthread_mutex_init(&q->lock, NULL); pthread_cond_init(&q->not_empty, NULL); pthread_cond_init(&q->not_full, NULL); }
void task_queue_push(TaskQueue *q, Task *task) { pthread_mutex_lock(&q->lock); while (q->count == QUEUE_SIZE && !shutdown_requested) pthread_cond_wait(&q->not_full, &q->lock); if(shutdown_requested){ pthread_mutex_unlock(&q->lock); free(task); return; } q->tasks[q->tail] = task; q->tail = (q->tail + 1) % QUEUE_SIZE; q->count++; pthread_cond_signal(&q->not_empty); pthread_mutex_unlock(&q->lock); }
Task* task_queue_pop(TaskQueue *q) { pthread_mutex_lock(&q->lock); while (q->count == 0 && !shutdown_requested) pthread_cond_wait(&q->not_empty, &q->lock); if (shutdown_requested && q->count == 0) { pthread_mutex_unlock(&q->lock); return NULL; } Task *task = q->tasks[q->head]; q->head = (q->head + 1) % QUEUE_SIZE; q->count--; pthread_cond_signal(&q->not_full); pthread_mutex_unlock(&q->lock); return task; }

void handle_shutdown_signal(int sig) {
    shutdown_requested = 1;
    pthread_cond_broadcast(&client_queue.not_empty);
    pthread_cond_broadcast(&task_queue.not_empty);
    close(server_fd);
}

void* worker_thread_function(void *arg) {
    while (!shutdown_requested) {
        Task *task = task_queue_pop(&task_queue);
        if (task == NULL) continue;
        char user_dir_path[512]; char file_path[1024];
        snprintf(user_dir_path, sizeof(user_dir_path), "%s/%s", SERVER_FILES_ROOT, users[task->user_id].username);
        pthread_mutex_lock(&users[task->user_id].file_lock);
        if (strlen(task->arg1) > MAX_FILENAME_LEN) {
            snprintf(task->response, sizeof(task->response), "411 Filename too long.\n");
        } else if (strchr(task->arg1, '/') || strchr(task->arg1, '\\')) {
            snprintf(task->response, sizeof(task->response), "405 Invalid filename.\n");
        } else {
            char safe_filename[MAX_FILENAME_LEN + 1];
            strncpy(safe_filename, task->arg1, MAX_FILENAME_LEN);
            safe_filename[MAX_FILENAME_LEN] = '\0';
            snprintf(file_path, sizeof(file_path), "%s/%s", user_dir_path, safe_filename);
            switch (task->command) {
                case LIST: { DIR *d = opendir(user_dir_path); if (d) { strcpy(task->response, "210 OK\nFiles:\n"); struct dirent *dir; while ((dir = readdir(d)) != NULL) if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) { strncat(task->response, dir->d_name, sizeof(task->response) - strlen(task->response) - 1); strncat(task->response, "\n", sizeof(task->response) - strlen(task->response) - 1); } closedir(d); } else snprintf(task->response, sizeof(task->response), "404 Dir not found.\n"); break; }
                case UPLOAD: { FILE *fp = fopen(file_path, "w"); if (fp) { fprintf(fp, "%s", task->arg2); fclose(fp); snprintf(task->response, sizeof(task->response), "211 Upload OK.\n"); } else snprintf(task->response, sizeof(task->response), "406 Upload failed.\n"); break; }
                case DOWNLOAD: { FILE *fp = fopen(file_path, "r"); if (fp) { fseek(fp, 0, SEEK_END); long fsize = ftell(fp); fseek(fp, 0, SEEK_SET); char *content = malloc(fsize + 1); fread(content, 1, fsize, fp); fclose(fp); content[fsize] = 0; snprintf(task->response, sizeof(task->response), "212 OK\n%s\n", content); free(content); } else snprintf(task->response, sizeof(task->response), "407 File not found.\n"); break; }
                case DELETE: { if (remove(file_path) == 0) snprintf(task->response, sizeof(task->response), "213 Delete OK.\n"); else snprintf(task->response, sizeof(task->response), "408 Delete failed.\n"); break; }
            }
        }
        pthread_mutex_unlock(&users[task->user_id].file_lock);
        pthread_mutex_lock(&task->lock); task->is_done = 1; pthread_cond_signal(&task->done_cond); pthread_mutex_unlock(&task->lock);
    }
    return NULL;
}

void* client_thread_function(void *arg) {
    while (!shutdown_requested) {
        int client_socket = queue_pop(&client_queue);
        if (client_socket == -1) continue;
        char buffer[BUFFER_SIZE], response[BUFFER_SIZE];
        int bytes_read, logged_in_user_id = -1;
        send(client_socket, "200 Welcome!\n", 13, 0);
        while (!shutdown_requested && (bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[bytes_read] = '\0'; buffer[strcspn(buffer, "\r\n")] = 0;
            char command[32]={0}, arg1[BUFFER_SIZE]={0}, arg2[BUFFER_SIZE]={0};
            sscanf(buffer, "%s %s %[^\n]", command, arg1, arg2);
            if (strcasecmp(command, "SIGNUP") == 0) {
                if (strlen(arg1) >= MAX_USERNAME_LEN) {
                    snprintf(response, sizeof(response), "410 Username too long.\n");
                } else {
                    pthread_mutex_lock(&user_list_lock);
                    int user_exists = 0; for (int i = 0; i < user_count; i++) if (strcmp(users[i].username, arg1) == 0) user_exists = 1;
                    if (user_exists) snprintf(response, sizeof(response), "401 User exists.\n");
                    else if (user_count >= MAX_USERS) snprintf(response, sizeof(response), "402 Server full.\n");
                    else {
                        char safe_username[MAX_USERNAME_LEN];
                        strncpy(safe_username, arg1, MAX_USERNAME_LEN);
                        safe_username[MAX_USERNAME_LEN - 1] = '\0';
                        
                        strncpy(users[user_count].username, safe_username, MAX_USERNAME_LEN);
                        strncpy(users[user_count].password, arg2, MAX_PASSWORD_LEN);
                        users[user_count].password[MAX_PASSWORD_LEN-1] = '\0';
                        pthread_mutex_init(&users[user_count].file_lock, NULL);
                        user_count++;
                        char user_dir_path[512];
                        snprintf(user_dir_path, sizeof(user_dir_path), "%s/%s", SERVER_FILES_ROOT, safe_username);
                        mkdir(user_dir_path, 0777);
                        snprintf(response, sizeof(response), "201 Signup successful.\n");
                    }
                    pthread_mutex_unlock(&user_list_lock);
                }
                send(client_socket, response, strlen(response), 0);
            } else if (strcasecmp(command, "LOGIN") == 0) {
                 pthread_mutex_lock(&user_list_lock); int user_found = 0; for (int i = 0; i < user_count; i++) if (strcmp(users[i].username, arg1) == 0 && strcmp(users[i].password, arg2) == 0) { logged_in_user_id = i; user_found = 1; break; } pthread_mutex_unlock(&user_list_lock);
                 if (user_found) snprintf(response, sizeof(response), "202 Login successful.\n"); else snprintf(response, sizeof(response), "403 Invalid credentials.\n"); send(client_socket, response, strlen(response), 0);
            } else {
                if (logged_in_user_id == -1) {
                    send(client_socket, "500 Please login first.\n", 24, 0);
                } else {
                    Task *task = malloc(sizeof(Task));
                    if (task != NULL) {
                        pthread_mutex_init(&task->lock, NULL); pthread_cond_init(&task->done_cond, NULL);
                        task->is_done = 0; task->client_socket = client_socket; task->user_id = logged_in_user_id;
                        strncpy(task->arg1, arg1, BUFFER_SIZE); strncpy(task->arg2, arg2, BUFFER_SIZE);
                        int valid_cmd=1; if (strcasecmp(command, "LIST") == 0) task->command = LIST; else if (strcasecmp(command, "UPLOAD") == 0) task->command = UPLOAD; else if (strcasecmp(command, "DOWNLOAD") == 0) task->command = DOWNLOAD; else if (strcasecmp(command, "DELETE") == 0) task->command = DELETE; else valid_cmd=0;
                        if (valid_cmd) {
                            task_queue_push(&task_queue, task);
                            pthread_mutex_lock(&task->lock);
                            while (!task->is_done) pthread_cond_wait(&task->done_cond, &task->lock);
                            pthread_mutex_unlock(&task->lock);
                            send(client_socket, task->response, strlen(task->response), 0);
                        } else {
                            send(client_socket, "502 Unknown command.\n", 21, 0);
                            free(task);
                        }
                        pthread_mutex_destroy(&task->lock); pthread_cond_destroy(&task->done_cond);
                        if(valid_cmd) free(task);
                    }
                }
            }
        }
        close(client_socket);
    }
    return NULL;
}

int main() {
    struct sockaddr_in address; socklen_t addrlen = sizeof(address);
    mkdir(SERVER_FILES_ROOT, 0777);
    queue_init(&client_queue); task_queue_init(&task_queue); pthread_mutex_init(&user_list_lock, NULL);
    signal(SIGINT, handle_shutdown_signal);
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) { perror("socket failed"); exit(EXIT_FAILURE); }
    int opt = 1; if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) { perror("setsockopt"); exit(EXIT_FAILURE); }
    address.sin_family = AF_INET; address.sin_addr.s_addr = INADDR_ANY; address.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) { perror("bind failed"); exit(EXIT_FAILURE); }
    if (listen(server_fd, MAX_PENDING_CONNECTIONS) < 0) { perror("listen"); exit(EXIT_FAILURE); }
    printf("Server listening on port %d\n", PORT);
    pthread_t client_threads[CLIENT_THREAD_POOL_SIZE]; for (long i = 0; i < CLIENT_THREAD_POOL_SIZE; i++) pthread_create(&client_threads[i], NULL, client_thread_function, NULL);
    pthread_t worker_threads[WORKER_THREAD_POOL_SIZE]; for (long i = 0; i < WORKER_THREAD_POOL_SIZE; i++) pthread_create(&worker_threads[i], NULL, worker_thread_function, NULL);
    printf("Thread pools initialized. Waiting for connections...\n");
    while (!shutdown_requested) {
        int new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (new_socket < 0) { if(!shutdown_requested) perror("accept failed"); continue; }
        queue_push(&client_queue, new_socket);
    }
    printf("Joining threads...\n");
    for (int i = 0; i < CLIENT_THREAD_POOL_SIZE; i++) pthread_join(client_threads[i], NULL);
    for (int i = 0; i < WORKER_THREAD_POOL_SIZE; i++) pthread_join(worker_threads[i], NULL);
    printf("All threads have finished. Server shutdown complete.\n");
    return 0;
}
