#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <fileQueue.h>

fileT* createFile(char *filepath, int O_LOCK, int owner) {
    fileT *f;

    if (!filepath || O_LOCK < 0) {
        errno = EINVAL;
        return (fileT*) NULL;
    }

    // alloco la memoria
    if ((f = (fileT*) calloc(1, sizeof(fileT))) == NULL) {
        perror("Calloc createFile");
        return (fileT*) NULL;
    }

    f->O_LOCK = O_LOCK;
    f->owner = owner;
    f->size = -1;

    if ((f->filepath = malloc(sizeof(char)*256)) == NULL) {
        perror("Malloc filepath");
        destroyFile(f);
        return (fileT*) NULL;
    }

    strncpy(f->filepath, filepath, 256);

    if (pthread_mutex_init(&f->m, NULL) != 0) {
        perror("pthread_mutex_init m");
        destroyFile(f);
        return (fileT*) NULL;
    }

    return f;
}

int writeFile(fileT *f, void *content, size_t size) {
    if (!f || !content || size < 0) {
        errno = EINVAL;
        return -1;
    }

    if ((f->content = malloc(size)) == NULL) {
        perror("Malloc content");
        return -1;
    }

    memcpy(f->content, content, size);

    printf("writeFile: ho scritto il file con contenuto: %s, di dimensione %ld\n", (char*) f->content, size);

    f->size = size;

    return 0;
}

void destroyFile(fileT *f) {
    if (f) {
        if (f->filepath) {
            free(f->filepath);
        }

        if (f->content) {
            free(f->content);
        }

        if (&f->m) {
            pthread_mutex_destroy(&f->m);
        }

        free(f);
    }
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
        cleanupQueue(queue);
        return (queueT*) NULL;
    }

    // inizializzo la lock e le variabili di condizione
    if (pthread_mutex_init(&queue->m, NULL) != 0) {
        perror("pthread_mutex_init m");
        cleanupQueue(queue);
        return (queueT*) NULL;
    }

    if (pthread_cond_init(&queue->full, NULL) != 0) {
        perror("pthread_cond_init full");
        cleanupQueue(queue);
        return (queueT*) NULL;
    }

    if (pthread_cond_init(&queue->empty, NULL) != 0) {
        perror("pthread_cond_init empty");
        cleanupQueue(queue);
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

    printf("readQueue: la dimensione del file rimosso e' %ld\n", data->size);

    queue->size -= data->size;

    assert(queue->len >= 0);

    pthread_cond_signal(&queue->full);  // segnalo che la coda non è piena
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

    printf("writeQueue: la dimensione del file aggiunto e' %ld\n", data->size);

    // se non c'è abbastanza spazio, errore
    if (queue->size + data->size > queue->maxSize) {
        errno = EFBIG;
        return -1;
    }

    // altrimenti inserisco l'elemento
    assert(queue->data[queue->tail] == NULL);
    queue->data[queue->tail] = data;
    queue->tail += (queue->tail + 1 >= queue->maxLen) ? (1 - queue->maxLen) : 1;
    queue->len++;
    queue->size += data->size;

    pthread_cond_signal(&queue->empty);  // segnalo che la coda non è vuota
    pthread_mutex_unlock(&queue->m);
    return 0;
}

size_t getLen(queueT *queue) {
    if (!queue) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&queue->m);

    size_t len = queue->len;

    pthread_mutex_unlock(&queue->m);

    return len;
}

size_t getSize(queueT *queue) {
    if (!queue) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&queue->m);

    size_t size = queue->size;

    pthread_mutex_unlock(&queue->m);

    return size;
}

void destroyQueue(queueT *queue) {
    if (queue) {
         // se la coda non è vuota, libera la memoria per ogni elemento
        while (queue->len > 0) {
             fileT *data = NULL;
            data = readQueue(queue);
            destroyFile(data);
        }

        cleanupQueue(queue);
    }
}

// funzione di pulizia
void cleanupQueue(queueT *queue) {
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