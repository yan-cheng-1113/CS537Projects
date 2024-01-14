#include "safequeue.h"
#define RESPONSE_BUFSIZE 10000
extern int queue_size;

Job* create_queue() {
    Job* job = malloc(sizeof(Job*));
    job->next = NULL;
    return job;
}

int add_work(Job* job_head, char* buffer, int max_size, int client_fd, int bytes_read) {
    if (queue_size == max_size) {
        printf("Queue full\n");
        return 0;
    }
    Job* start = job_head;
    Job* new_job = malloc(sizeof(Job*));
    new_job->priority = buffer[5] - '0';
    new_job->next = NULL;
    new_job->buffer = (char *)malloc(RESPONSE_BUFSIZE * sizeof(char));
    strcpy(new_job->buffer, buffer);
    // printf("buffer: %s\n", buffer);
    new_job->client_fd = client_fd;
    new_job->bytes_read = bytes_read;
    // printf("job head priority: %d\n", start->priority);
    if (start == NULL) {
        printf("start is null\n");
    }
    if (start->next == NULL) {
        // printf("ok add work, priority: %d\n", new_job->priority);
        start->next = new_job;
        // printf("new job buffer: %s\n", new_job->buffer);
    } else {
        while (start->next != NULL) {
            if (start->next->priority < new_job->priority) {
                new_job->next = start->next;
                start->next = new_job;
                return 1;
            } else {
                start = start->next;
            }
        }
        start->next = new_job;
    }
    return 1;
}

Job* get_work(Job* job_head) {
    
    return job_head->next;
}

Job* get_work_nonblocking(Job* job_head) {
    Job* ret = NULL;
    ret = job_head->next;
    job_head->next = ret->next;
    queue_size--;
    return ret;
}

