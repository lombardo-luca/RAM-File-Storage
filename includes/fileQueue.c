#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <fileQueue.h>

fileT* createFileT(char *filepath, int O_LOCK, int owner, int open) {
    // controllo la validità degli argomenti
    if (!filepath || O_LOCK < 0 || O_LOCK > 1 || open < 0 || open > 1) {
        errno = EINVAL;
        return (fileT*) NULL;
    }

    fileT *f;

    // alloco la memoria
    if ((f = (fileT*) calloc(1, sizeof(fileT))) == NULL) {
        perror("Calloc createFileT");
        return (fileT*) NULL;
    }

    f->O_LOCK = O_LOCK;
    f->owner = owner;
    f->open = open;  
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

    return f;
}

int writeFileT(fileT *f, void *content, size_t size) {
    // controllo la validità degli argomenti
    if (!f || !content || size < 0) {
        errno = EINVAL;
        return -1;
    }

    if (size != 0) {
        if ((f->content = realloc(f->content, f->size + size)) == NULL) {
            perror("Malloc content");
            return -1;
        } 
    }

    // scrittura in append
    memcpy(f->content+f->size, content, size);
    f->size += (size);

    //printf("writeFileT: ho scritto il file %s, di dimensione %ld\n", f->filepath, f->size);

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

    // inizializzo la lock
    if (pthread_mutex_init(&queue->m, NULL) != 0) {
        perror("pthread_mutex_init m");
        cleanupQueue(queue);
        return (queueT*) NULL;
    }

    queue->head = NULL;
    queue->tail = NULL;
    queue->maxLen = maxLen;
    queue->len = 0;
    queue->maxSize = maxSize;
    queue->size = 0;

    return queue;
}

int enqueue(queueT *queue, fileT* data) {
    // controllo la validità degli argomenti
    if (!queue || !data) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&queue->m);

    // se la coda è piena, errore
    if (queue->len == queue->maxLen) {
        errno = ENFILE;
        pthread_mutex_unlock(&queue->m);
        return -1;
    }

    printf("enqueue: voglio aggiungere il file %s di dimensione %ld\n", data->filepath, data->size);

    // se non c'è abbastanza spazio, errore
    if (queue->size + data->size > queue->maxSize) {
        errno = EFBIG;
        pthread_mutex_unlock(&queue->m);
        return -1;
    }

    // altrimenti inserisco l'elemento alla fine della coda
    nodeT *newNode = NULL;
    if ((newNode = malloc(sizeof(nodeT))) == NULL) {
        perror("malloc newNode");
        pthread_mutex_unlock(&queue->m);
        return -1;
    }

    newNode->data = data;
    newNode->next = NULL;

    // se è il primo elemento della coda
    if (queue->head == NULL && queue->tail == NULL) {
        queue->head = queue->tail = newNode;
    }

    // se non è il primo elemento della coda
    else {
        (queue->tail)->next = newNode;
        queue->tail = newNode;
    }

    queue->len++;
    queue->size += data->size;

    printf("enqueue: aggiunto.\n");

    pthread_mutex_unlock(&queue->m);
    return 0;
}

void voiDequeue(queueT *queue) {
    // controllo la validità dell'argomento
    if (!queue) {
        errno = EINVAL;
        return;
    }

    pthread_mutex_lock(&queue->m);

    // se la coda e' vuota, errore
    if (queue->head == NULL || queue->len == 0) {
        errno = ENOENT;
        pthread_mutex_unlock(&queue->m);
        return;
    }

    nodeT *temp = NULL;
    temp = queue->head;

    printf("voiDequeue: rimuovo il file %s di dimensione %ld\n", (temp->data)->filepath, (temp->data)->size);

    queue->head = (queue->head)->next;

    if (queue->head == NULL) {
        queue->tail = NULL;
    }
   
    queue->len--;
    queue->size -= (temp->data)->size;
    assert(queue->len >= 0);

    // libero la memoria
    destroyFile(temp->data);
    free(temp);

    pthread_mutex_unlock(&queue->m);
}

// attenzione: il fileT* restituito va distrutto manualmente per liberarne la memoria
fileT* dequeue(queueT *queue) {
    // controllo la validità dell'argomento
    if (!queue) {
        errno = EINVAL;
        return NULL;
    }

    pthread_mutex_lock(&queue->m);

    // se la coda e' vuota, errore
    if (queue->head == NULL || queue->len == 0) {
        errno = ENOENT;
        pthread_mutex_unlock(&queue->m);
        return NULL;
    }

    /*
    // creo una copia del primo elemento della coda (in modo da restituirlo come risultato) prima di rimuoverlo
    fileT *data = NULL;
    data = createFileT(((queue->head)->data)->filepath, ((queue->head)->data)->O_LOCK, ((queue->head)->data)->owner);
    if (data == NULL) {
        perror("createFileT");
        pthread_mutex_unlock(&queue->m);
        return NULL;
    }

    if (writeFileT(data, ((queue->head)->data)->content, ((queue->head)->data)->size) == -1) {
        perror("writeFileT");
        pthread_mutex_unlock(&queue->m);
        return NULL;
    }
    */

    if ((queue->head)->data == NULL) {
        perror("WTF???");
    }

    fileT *data = (queue->head)->data;

    nodeT *temp = NULL;
    temp = queue->head;
    queue->head = (queue->head)->next;

    if (queue->head == NULL) {
        queue->tail = NULL;
    }

    if (data->filepath == NULL) {
        printf("WTF?");
    }
   
    queue->len--;
    queue->size -= data->size;
    assert(queue->len >= 0);

    //destroyFile(temp->data);
    free(temp);

    printf("dequeue: ho rimosso il file %s di dimensione %ld\n", data->filepath, data->size);

    pthread_mutex_unlock(&queue->m);
    return data;
}

int printQueue(queueT *queue) {
    // controllo la validità dell'argomento
    if (!queue) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&queue->m);

    printf("Lista dei file contenuti nello storage al momento della chiusura del server:\n");
    //printf("Lunghezza attuale della coda: %ld\n", queue->len);
    //printf("Dimensione attuale della coda: %ld\n", queue->size);

    nodeT *temp = queue->head;

    // scorro tutta la lista e stampo ogni elemento
    while (temp) {
        double res = (temp->data)->size/(double) 1000000;
        printf("File: %s Dimensione %lf MB\n", (temp->data)->filepath, res);
        temp = temp->next;
    }

    pthread_mutex_unlock(&queue->m);

    return 0;
}

int lockFileInQueue(queueT *queue, char *filepath, int owner) {
    // controllo la validità degli argomenti
    if (!queue || !filepath) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&queue->m);

    // se la coda e' vuota, errore
    if (queue->len == 0) {
        errno = ENOENT;
        pthread_mutex_unlock(&queue->m);
        return -1;
    }

    int found = 0;
    nodeT *temp = queue->head;

    // scorro tutta la coda
    while (temp && !found) {
        // se trovo l'elemento cercato...
        if (strcmp(filepath, (temp->data)->filepath) == 0) {
            found = 1;

            printf("LOCKFILE: locked? %d Owner = %d Client = %d\n", (temp->data)->O_LOCK, (temp->data)->owner, owner);

            // se il file e' stato messo in modalita' locked da un client diverso, errore
            if ((temp->data)->O_LOCK && (temp->data)->owner != owner) {
                errno = EPERM;
                pthread_mutex_unlock(&queue->m);
                return -1;
            }

            // imposta flag e owner
            (temp->data)->O_LOCK = 1;
            (temp->data)->owner = owner;
        }

        temp = temp->next;
    }

    pthread_mutex_unlock(&queue->m);

    if (!found) {
        return -1;
    }

    return 0;
}

int openFileInQueue(queueT *queue, char *filepath, int O_LOCK, int client) {
    // controllo la validità degli argomenti
    if (!queue || !filepath || O_LOCK < 0 || O_LOCK > 3) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&queue->m);

    // se la coda e' vuota, errore
    if (queue->len == 0) {
        errno = ENOENT;
        pthread_mutex_unlock(&queue->m);
        return -1;
    }

    int found = 0;
    nodeT *temp = queue->head;

    // scorro tutta la coda
    while (temp && !found) {
        if (strcmp(filepath, (temp->data)->filepath) == 0) {
            // ho trovato il file che cercavo
            found = 1;

            printf("openFile: file locked? %d Owner = %d Client = %d\n", (temp->data)->O_LOCK, (temp->data)->owner, client);

            // se il file e' stato messo in modalita' locked da un client diverso, errore
            if ((temp->data)->O_LOCK && (temp->data)->owner != client) {
                errno = EPERM;
                pthread_mutex_unlock(&queue->m);
                return -1;
            }

            (temp->data)->open = 1;
            (temp->data)->O_LOCK = O_LOCK;

            if (O_LOCK) {
                (temp->data)->owner = client;
            }
        }

        temp = temp->next;
    }

    pthread_mutex_unlock(&queue->m);

    if (!found) {
        return -1;
    }

    return 0;
}

int closeFileInQueue(queueT *queue, char *filepath, int client) {
    // controllo la validità degli argomenti
    if (!queue || !filepath) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&queue->m);

    // se la coda e' vuota, errore
    if (queue->len == 0) {
        errno = ENOENT;
        pthread_mutex_unlock(&queue->m);
        return -1;
    }

    int found = 0;
    nodeT *temp = queue->head;

    // scorro tutta la coda
    while (temp && !found) {
        if (strcmp(filepath, (temp->data)->filepath) == 0) {
            // ho trovato il file che cercavo
            found = 1;

            printf("closeFile: file locked? %d Owner = %d Client = %d\n", (temp->data)->O_LOCK, (temp->data)->owner, client);

            // se il file e' stato messo in modalita' locked da un client diverso, errore
            if ((temp->data)->O_LOCK && (temp->data)->owner != client) {
                errno = EPERM;
                pthread_mutex_unlock(&queue->m);
                return -1;
            }

            // chiudi il file e togli la modalita' locked
            (temp->data)->open = 0;
            (temp->data)->O_LOCK = 0;
            (temp->data)->owner = client;
        }

        temp = temp->next;
    }

    pthread_mutex_unlock(&queue->m);

    if (!found) {
        return -1;
    }

    return 0;
}

int appendFileInQueue(queueT *queue, char *filepath, void *content, size_t size, int client) {
    // controllo la validità degli argomenti
    if (!queue || !filepath || !content) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&queue->m);

    // se la coda e' vuota, errore
    if (queue->len == 0) {
        errno = ENOENT;
        pthread_mutex_unlock(&queue->m);
        return -1;
    }

    // se non c'e' abbastanza spazio nella coda, errore
    if (queue->size + size > queue->maxSize) {
        errno = EFBIG;
        pthread_mutex_unlock(&queue->m);
        return -1;
    }

    int found = 0;
    nodeT *temp = queue->head;

    // scorro tutta la coda
    while (temp && !found) {
        if (strcmp(filepath, (temp->data)->filepath) == 0) {
            // ho trovato il file che cercavo
            found = 1;

            printf("Il file e' locked? %d Owner = %d Client = %d\n", (temp->data)->O_LOCK, (temp->data)->owner, client);

            // controllo se il client ha i permessi per scrivere sul file
            if ((temp->data)->open == 0 || ((temp->data)->O_LOCK && (temp->data)->owner != client)) {
                errno = EPERM;
                pthread_mutex_unlock(&queue->m);
                return -1;
            }

            if (size != 0) {
                // allora la memoria
                if (((temp->data)->content = realloc((temp->data)->content, (temp->data)->size + size)) == NULL) {
                    perror("Malloc content");
                    pthread_mutex_unlock(&queue->m);
                    return -1;
                } 
            }

            // scrittura in append
            memcpy((temp->data)->content + (temp->data)->size, content, size);
            (temp->data)->size += size;
            queue->size += size;
        }

        temp = temp->next;
    }

    pthread_mutex_unlock(&queue->m);

    if (!found) {
        return -1;
    }

    return 0;
}

// attenzione: il fileT* restituito va distrutto manualmente per liberarne la memoria
fileT* find(queueT *queue, char *filepath) {
    // controllo la validità degli argomenti
    if (!queue || !filepath) {
        errno = EINVAL;
        return NULL;
    }

    pthread_mutex_lock(&queue->m);

    if (queue->len == 0) {
        pthread_mutex_unlock(&queue->m);
        return NULL;
    }

    fileT *res = NULL;
    int found = 0;
    nodeT *temp = queue->head;

    // scorro tutta la coda
    while (temp && !found) {
        // se trovo l'elemento cercato...
        if (strcmp(filepath, (temp->data)->filepath) == 0) {
            found = 1;

            // ...ne creo una copia e la restituisco
            res = createFileT((temp->data)->filepath, (temp->data)->O_LOCK, (temp->data)->owner, (temp->data)->open);
            if (writeFileT(res, (temp->data)->content, (temp->data)->size) == -1) {
                perror("writeFileT res");
                pthread_mutex_unlock(&queue->m);
                return NULL;
            }
        }

        temp = temp->next;
    }

    pthread_mutex_unlock(&queue->m);

    return res;
}

size_t getLen(queueT *queue) {
    // controllo la validità dell'argomento
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
    // controllo la validità dell'argomento
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
         // se la coda non è vuota, svuotala e libera la memoria per ogni elemento
        while (queue->len > 0) {
            printf("DestroyQueue: queue len = %ld\n", queue->len);

            voiDequeue(queue);
        }

        cleanupQueue(queue);
    }
}

// funzione di pulizia
void cleanupQueue(queueT *queue) {
    if (queue) {
        if (queue->head) {
            free(queue->head);
        }

        if (queue->tail) {
            free(queue->tail);
        }

        if (&queue->m) {
            pthread_mutex_destroy(&queue->m);
        }

        free(queue);
    }
}