#include <stdio.h>
#include <queue.h>

FILE* queue_read(queue_t *queue) {
    if (queue->tail == queue->head) {
        return NULL;
    }

    queue->len--;
    FILE* handle = queue->data[queue->tail];
    queue->data[queue->tail] = NULL;
    queue->tail = (queue->tail + 1) % queue->size;
    return handle;
}

int queue_write(queue_t *queue, FILE* handle) {
    if (((queue->head + 1) % queue->size) == queue->tail) {
        return -1;
    }

    queue->len++;
    queue->data[queue->head] = handle;
    queue->head = (queue->head + 1) % queue->size;
    return 0;
}