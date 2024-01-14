#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "proxyserver.h"
#include "safequeue.h"

/*
 * Constants
 */
#define RESPONSE_BUFSIZE 10000
pthread_mutex_t lock;
struct thread_data thread_data_array[30];

Job* job_queue_head;
int queue_size = 0;
/*
 * Global configuration variables.
 * Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
int num_listener;   
int *listener_ports;
int num_workers;
char *fileserver_ipaddr;
int fileserver_port;
int max_queue_size;
pthread_cond_t cond1 = PTHREAD_COND_INITIALIZER;
pthread_mutex_t cond_lock = PTHREAD_MUTEX_INITIALIZER;

void send_error_response(int client_fd, status_code_t err_code, char *err_msg) {
    http_start_response(client_fd, err_code);
    http_send_header(client_fd, "Content-Type", "text/html");
    http_end_headers(client_fd);
    char *buf = malloc(strlen(err_msg) + 2);
    sprintf(buf, "%s\n", err_msg);
    http_send_string(client_fd, buf);
    return;
}

int extractDelayTime(char *request) {
    const char *delayHeader = "Delay: ";
    const char *newline = "\r\n";
    // const char *delimiter = " ";

    // Find the position of "Delay: "
    char *delayPosition = strstr(request, delayHeader);

    if (delayPosition == NULL) {
        // printf("Delay header not found.\n");
        return -1; // Return -1 if "Delay" header is not found
    }

    // Find the end of the line containing "Delay: "
    char *lineEnd = strstr(delayPosition, newline);

    if (lineEnd == NULL) {
        // printf("Invalid request format.\n");
        return -1; // Return -1 if the format is invalid
    }

    // Calculate the length of the delay value
    size_t delayLength = lineEnd - (delayPosition + strlen(delayHeader));

    // Allocate memory for the delay value
    char *delayValue = (char *)malloc(delayLength + 1);

    if (delayValue == NULL) {
        // printf("Memory allocation failed.\n");
        return -1; // Return -1 if memory allocation fails
    }

    // Copy the delay value to the allocated memory
    strncpy(delayValue, delayPosition + strlen(delayHeader), delayLength);
    delayValue[delayLength] = '\0'; // Null-terminate the string

    // Convert the delay value to an integer
    int delayTime = atoi(delayValue);

    // Free the allocated memory
    free(delayValue);

    return delayTime;
}

/*
 * forward the client request to the fileserver and
 * forward the fileserver response to the client
 */
void serve_request(int client_fd, char* buffer, int bytes_read) {

    // create a fileserver socket
    int fileserver_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fileserver_fd == -1) {
        fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
        exit(errno);
    }
    // create the full fileserver address
    struct sockaddr_in fileserver_address;
    fileserver_address.sin_addr.s_addr = inet_addr(fileserver_ipaddr);
    fileserver_address.sin_family = AF_INET;
    fileserver_address.sin_port = htons(fileserver_port);

    // connect to the fileserver
    int connection_status = connect(fileserver_fd, (struct sockaddr *)&fileserver_address,
                                    sizeof(fileserver_address));
    if (connection_status < 0) {
        // failed to connect to the fileserver
        printf("Failed to connect to the file server\n");
        printf("fileserver_ipaddr: %s, fileserver_port: %d\n", fileserver_ipaddr, fileserver_port);
        printf("error: %s\n", strerror(errno));
        send_error_response(client_fd, BAD_GATEWAY, "Bad Gateway");
        return;
    }

    // successfully connected to the file server
    
    // printf("ok, buffer: %s\n", buffer);
    int delay = extractDelayTime(buffer);
    if (delay > 0) {
        sleep(delay);
    }
    int ret = http_send_data(fileserver_fd, buffer, bytes_read);
    if (ret < 0) {
        printf("Failed to send request to the file server\n");
        send_error_response(client_fd, BAD_GATEWAY, "Bad Gateway");

    } else {
        // forward the fileserver response to the client
        while (1) {
            int bytes_read = recv(fileserver_fd, buffer, RESPONSE_BUFSIZE - 1, 0);
            if (bytes_read <= 0) // fileserver_fd has been closed, break
                break;
            ret = http_send_data(client_fd, buffer, bytes_read);
            if (ret < 0) { // write failed, client_fd has been closed
                break;
            }
        }
    }

    // close the connection to the fileserver
    shutdown(fileserver_fd, SHUT_WR);
    close(fileserver_fd);

    // Free resources and exit
    // free(buffer);
    // printf("ok1\n");
}

void  handle_get_job(int fd, char *buffer){
    //http_start_response(client_fd, OK);
    char *path = NULL;
    int is_first = 1;
    size_t size;

    char *token = strtok(buffer, "\r\n");
    while (token != NULL) {
        size = strlen(token);
        if (is_first) {
            is_first = 0;
            // get path
            char *s1 = strstr(token, " ");
            char *s2 = strstr(s1 + 1, " ");
            size = s2 - s1 - 1;
            path = strndup(s1 + 1, size);
        }    
        token = strtok(NULL, "\r\n");
    }
    send_error_response(fd, OK, path);
    shutdown(fd, SHUT_RDWR);
    close(fd); 
}

/*
 * opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(void *threadarg) {

    struct thread_data *my_data;
    my_data = (struct thread_data *) threadarg;
    int taskid = my_data->thread_id;
    int* server_fd = &my_data->server_fd;
    // create a socket to listen
    *server_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (*server_fd == -1) {
        perror("Failed to create a new socket");
        exit(errno);
    }
    // manipulate options for the socket
    int socket_option = 1;
    if (setsockopt(*server_fd, SOL_SOCKET, SO_REUSEADDR, &socket_option,
                   sizeof(socket_option)) == -1) {
        perror("Failed to set socket options");
        exit(errno);
    }


    int proxy_port = listener_ports[taskid];
    // create the full address of this proxyserver
    struct sockaddr_in proxy_address;
    memset(&proxy_address, 0, sizeof(proxy_address));
    proxy_address.sin_family = AF_INET;
    proxy_address.sin_addr.s_addr = INADDR_ANY;
    proxy_address.sin_port = htons(proxy_port); // listening port

    // bind the socket to the address and port number specified in
    if (bind(*server_fd, (struct sockaddr *)&proxy_address,
             sizeof(proxy_address)) == -1) {
        perror("Failed to bind on socket");
        exit(errno);
    }

    // starts waiting for the client to request a connection
    if (listen(*server_fd, 1024) == -1) {
        perror("Failed to listen on socket");
        exit(errno);
    }

    printf("Listening on port %d...\n", proxy_port);

    struct sockaddr_in client_address;
    size_t client_address_length = sizeof(client_address);
    int client_fd;
    while (1) {
        client_fd = accept(*server_fd,
                           (struct sockaddr *)&client_address,
                           (socklen_t *)&client_address_length);
        if (client_fd < 0) {
            perror("Error accepting socket");
            continue;
        }

        printf("Accepted connection from %s on port %d\n",
               inet_ntoa(client_address.sin_addr),
               client_address.sin_port);
        char *buffer = (char *)malloc(RESPONSE_BUFSIZE * sizeof(char));

        // forward the client request to the fileserver
        int bytes_read = read(client_fd, buffer, RESPONSE_BUFSIZE);

        //GetJob
        if(strstr(buffer, GETJOBCMD) != NULL){
            if(queue_size == 0){
                pthread_mutex_unlock(&lock);
                send_error_response(client_fd, QUEUE_EMPTY, "Queue EMPTY");
                shutdown(client_fd, SHUT_RDWR);
                close(client_fd);
            }else{
                pthread_mutex_lock(&lock);
                Job *temp = get_work_nonblocking(job_queue_head);
                pthread_mutex_unlock(&lock);
                if(temp != NULL){
                    handle_get_job(client_fd, temp->buffer);
                    //shutdown(*server_fd, SHUT_RDWR);
                    //close(*server_fd);
                }
            }
            //continue;     
        }
        
        // printf("buffer: %s, bytes_read: %d\n", buffer, bytes_read);
        // if(queue_size >= max_queue_size){
        //     send_error_response(client_fd, QUEUE_FULL, "Queue FULL");
        // }
        
        else {
            pthread_mutex_lock(&lock);
            if (!add_work(job_queue_head, buffer, max_queue_size, client_fd, bytes_read)) {
                pthread_mutex_unlock(&lock);
                send_error_response(client_fd, QUEUE_FULL, "Queue FULL");
                shutdown(client_fd, SHUT_RDWR);
                close(client_fd);
            } else {
                queue_size += 1;
                // printf("added buffer: %s", job_queue_head->next->buffer);
                pthread_mutex_unlock(&lock);
                // printf()
                pthread_cond_signal(&cond1);
            }
            // free(buffer);
        }
    }

    shutdown(*server_fd, SHUT_RDWR);
    close(*server_fd);
}

void worker() {
    while (1) {
        printf("ok\n");
        pthread_mutex_lock(&lock);
        if (queue_size == 0) {
            pthread_cond_wait(&cond1, &lock);
        }
        // if (worker_job->next != NULL) {
        //     printf("bytes read: %d\n", worker_job->next->bytes_read);

        // }
        // printf("worker client fd: %d", worker_job->next->client_fd);
        // printf("worker buffer: %s", worker_job->next->buffer);
        Job* worker_job = get_work(job_queue_head);
        if(worker_job != NULL){
            printf("Dequeue success\n");
        }
        // int delay = extractDelayTime(worker_job->buffer);
        // if (delay > 0) {
        //     sleep(delay);
        // }
        job_queue_head->next = worker_job->next;
        worker_job->next = NULL;
        queue_size -= 1;
        pthread_mutex_unlock(&lock);
        // printf("queue_size: %d, buffer: %s\n", queue_size, worker_job->buffer);
        serve_request(worker_job->client_fd, worker_job->buffer, worker_job->bytes_read);
        
        // close the connection to the client
        shutdown(worker_job->client_fd, SHUT_WR);
        close(worker_job->client_fd);
        // free(worker_job->next);
        // printf("worker buffer: %s\n",worker_job->buffer);
        // free(worker_job->buffer);
        // free(worker_job);
    }
}
/*
 * Default settings for in the global configuration variables
 */
void default_settings() {
    num_listener = 1;
    listener_ports = (int *)malloc(num_listener * sizeof(int));
    listener_ports[0] = 8000;

    num_workers = 1;

    fileserver_ipaddr = "127.0.0.1";
    fileserver_port = 3333;

    max_queue_size = 100;
}

void print_settings() {
    printf("\t---- Setting ----\n");
    printf("\t%d listeners [", num_listener);
    for (int i = 0; i < num_listener; i++)
        printf(" %d", listener_ports[i]);
    printf(" ]\n");
    printf("\t%d workers\n", num_listener);
    printf("\tfileserver ipaddr %s port %d\n", fileserver_ipaddr, fileserver_port);
    printf("\tmax queue size  %d\n", max_queue_size);
    printf("\t  ----\t----\t\n");
}

void signal_callback_handler(int signum) {
    printf("Caught signal %d: %s\n", signum, strsignal(signum));
    for (int i = 0; i < num_listener; i++) {
        if (close(thread_data_array[i].server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
    }
    free(listener_ports);
    exit(0);
}

char *USAGE =
    "Usage: ./proxyserver [-l 1 8000] [-n 1] [-i 127.0.0.1 -p 3333] [-q 100]\n";

void exit_with_usage() {
    fprintf(stderr, "%s", USAGE);
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
    signal(SIGINT, signal_callback_handler);

    /* Default settings */
    default_settings();

    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp("-l", argv[i]) == 0) {
            num_listener = atoi(argv[++i]);
            printf("num_listener: %d\n", num_listener);
            free(listener_ports);
            listener_ports = (int *)malloc(num_listener * sizeof(int));
            for (int j = 0; j < num_listener; j++) {
                listener_ports[j] = atoi(argv[++i]);
                
            }
        } else if (strcmp("-w", argv[i]) == 0) {
            num_workers = atoi(argv[++i]);
        } else if (strcmp("-q", argv[i]) == 0) {
            max_queue_size = atoi(argv[++i]);
        } else if (strcmp("-i", argv[i]) == 0) {
            fileserver_ipaddr = argv[++i];
        } else if (strcmp("-p", argv[i]) == 0) {
            fileserver_port = atoi(argv[++i]);
        } else {
            // printf("argv[i]: %d, %s\n", i, argv[i]);
            fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
            exit_with_usage();
        }
    }
    print_settings();
    job_queue_head = create_queue();
    pthread_mutex_init(&lock, NULL);
    // struct thread_data thread_data_array[num_listener];
    pthread_t listener_pool[num_listener];
    pthread_t worker_pool[num_workers];
    for (int i = 0; i < num_listener; i++) {
        thread_data_array[i].thread_id = i;
        thread_data_array[i].server_fd = 0;
        pthread_create(&listener_pool[i], NULL, (void*)serve_forever, &thread_data_array[i]);
    }
    
    for (int i = 0; i < num_workers; i++) {
            pthread_create(&worker_pool[i], NULL, (void*)worker, NULL);
    }
    for (int i = 0; i < num_listener; i++) {
        pthread_join(listener_pool[i], NULL);
    }
    for(int i = 0; i < num_workers; i++){
        pthread_join(worker_pool[i], NULL);
    }
    
    // serve_forever(&server_fd);

    return EXIT_SUCCESS;
}
