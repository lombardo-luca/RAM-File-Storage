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
    f->size = 0;

    if ((f->filepath = malloc(sizeof(char)*256)) == NULL) {
        perror("Malloc filepath");
        destroyFile(f);
        return (fileT*) NULL;
    }

    strncpy(f->filepath, filepath, 256);

    if ((f->content = malloc(f->size+1)) == NULL) {
        perror("Malloc content");
        return (fileT*) NULL;
    }

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

    pthread_mutex_lock(&f->m);

    if ((f->content = realloc(f->content, f->size + size)) == NULL) {
        perror("Malloc content");
        return -1;
    }

    // scrittura su un file vuoto
    if (f->size == 0) {
        memcpy(f->content, content, size);
         f->size += (size);
    }

    // scrittura in append sul file
    else {
        memcpy(f->content+size-1, content, size);
         f->size += (size-1);
    }

    printf("writeFile: ho scritto il file %s, adesso ha contenuto: %s! di dimensione %ld\n", f->filepath, (char*) f->content, f->size);

    pthread_mutex_unlock(&f->m);

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

// attenzione: il fileT* restituito va distrutto manualmente per liberarne la memoria
fileT* pop(queueT *queue) {
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

    printf("pop: la dimensione del file rimosso e' %ld\n", data->size);

    queue->size -= data->size;

    assert(queue->len >= 0);

    pthread_cond_signal(&queue->full);  // segnalo che la coda non è piena
    pthread_mutex_unlock(&queue->m);
    return data;
}

int push(queueT *queue, fileT* data) {
    if (!queue || !data) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&queue->m);

    // finché la coda è piena, aspetto
    while (queue->len == queue->maxLen) {
        pthread_cond_wait(&queue->full, &queue->m);
    }

    printf("push: la dimensione del file aggiunto e' %ld\n", data->size);

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

// attenzione: il fileT* restituito va distrutto manualmente per liberarne la memoria
fileT* find(queueT *queue, char *filepath) {
    if (!queue || !filepath) {
        errno = EINVAL;
        return NULL;
    }

    pthread_mutex_lock(&queue->m);

    if (queue->len == 0) {
        return NULL;
    }

    fileT *res = NULL;
    int cnt = 0, found = 0;
    size_t temp = queue->head;
    //printf("cerco: %s!\n", filepath);
    //printf("el: %s!\n", (queue->data[temp])->filepath);

    // scorro tutta la coda
    while (cnt < queue->len && !found) {
        // se trovo l'elemento cercato...
        if (strcmp(filepath, (queue->data[temp])->filepath) == 0) {
            //printf("find: trovato!\n");
            found = 1;

            // ...creane una copia e restituiscila
            res = createFile((queue->data[temp])->filepath, (queue->data[temp])->O_LOCK, (queue->data[temp])->owner);
            if (writeFile(res, (queue->data[temp])->content, (queue->data[temp])->size) == -1) {
                perror("writeFile res");
                return NULL;
            }
        }

        temp += (temp + 1 >= queue->maxLen) ? (1 - queue->maxLen) : 1;
        if (queue->data[temp]) {
            //printf("el: %s!\n", (queue->data[temp])->filepath);
        }

        cnt++;
    }

    pthread_mutex_unlock(&queue->m);

    return res;
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
            data = pop(queue);
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