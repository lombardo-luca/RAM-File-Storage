#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>

#include <fileQueue.h>

fileT* createFile(FILE *file, int O_LOCK) {
    fileT *f;

    // alloco la memoria
    if ((f = (fileT*) calloc(1, sizeof(fileT))) == NULL) {
        perror("Calloc createFile");
        return (fileT*) NULL;
    }

    if (O_LOCK < 0) {
        perror("Flag O_LOCK invalido");
        return (fileT*) NULL;
    }

    f->file = file;
    f->O_LOCK = O_LOCK;

    return f;
}

queueT* createQueue(size_t maxLen, size_t maxSize) {
    queueT *queue; 

    // alloco la memoria
    if ((queue = (queueT*) calloc(1, sizeof(queueT))) == NULL) {
        perror("Calloc createQueue");
        return (queueT*) NULL;
    }

    if ((queue->data = calloc(maxLen, sizeof(fileT*))) == NULL) {
        perror("Calloc data");
        cleanup(queue);
        return (queueT*) NULL;
    }

    // inizializzo la lock e le variabili di condizione
    if (pthread_mutex_init(&queue->m, NULL) != 0) {
        perror("pthread_mutex_init m");
        cleanup(queue);
        return (queueT*) NULL;
    }

    if (pthread_cond_init(&queue->full, NULL) != 0) {
        perror("pthread_cond_init full");
        cleanup(queue);
        return (queueT*) NULL;
    }

    if (pthread_cond_init(&queue->empty, NULL) != 0) {
        perror("pthread_cond_init empty");
        cleanup(queue);
        return (queueT*) NULL;
    }

    queue->head = queue->tail = 0;
    queue->maxLen = maxLen;
    queue->len = 0;
    queue->maxSize = maxSize;
    queue->size = 0;

    return queue;
}

fileT* readQueue(queueT *queue) {
    if (!queue) {
        errno = EINVAL;
        return NULL;
    }

    pthread_mutex_lock(&queue->m);

    // finché la coda è vuota, aspetto
    while (queue->len == 0) {
        pthread_cond_wait(&queue->empty, &queue->m);
    }

    fileT *data = queue->data[queue->head];
    queue->head += (queue->head + 1 >= queue->maxLen) ? (1 - queue->maxLen) : 1;
    queue->len--;
    assert(queue->len >= 0);

    pthread_cond_signal(&queue->full);  // segnalo cdhe la coda non è piena
    pthread_mutex_unlock(&queue->m);
    return data;
}

int writeQueue(queueT *queue, fileT* data) {
    if (!queue || !data) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&queue->m);

    // finché la coda è piena, aspetto
    while (queue->len == queue->maxLen) {
        pthread_cond_wait(&queue->full, &queue->m);
    }

    assert(queue->data[queue->tail] == NULL);
    queue->data[queue->tail] = data;
    queue->tail += (queue->tail + 1 >= queue->maxLen) ? (1 - queue->maxLen) : 1;
    queue->len++;

    pthread_cond_signal(&queue->empty);  // segnalo che la coda non è vuota
    pthread_mutex_unlock(&queue->m);
    return 0;
}

void destroyQueue(queueT *queue) {
    if (queue) {
        // se la coda non è vuota, libera la memoria per ogni elemento
        if (queue->len > 0) {
            fileT *data = NULL;
            while ((data = readQueue(queue))) {
                free(data);
            }
        }

        cleanup(queue);
    }
}

// funzione di pulizia
void cleanup(queueT *queue) {
    if (queue) {
        if (queue->data) {
            free(queue->data);
        }

        if (&queue->m) {
            pthread_mutex_destroy(&queue->m);
        }

        if (&queue->full) {
            pthread_cond_destroy(&queue->full);
        }

        if (&queue->empty) {
            pthread_cond_destroy(&queue->empty);
        }

        free(queue);
    }
}