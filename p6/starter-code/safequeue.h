#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct job {  
    char* buffer;
    // Lower values indicate higher priority  
    int priority;
    struct job* next;
    int client_fd;
    int bytes_read;
} Job;

Job* create_queue();
int add_work(Job*, char*, int, int, int);
Job* get_work(Job*);
Job* get_work_nonblocking(Job*);