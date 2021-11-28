// struttura dati per mantenere i file in memoria
typedef struct {
    FILE* file;
    char* filepath;
    int O_LOCK;
    pthread_mutex_t m;
} fileT;

// coda di fileT
typedef struct {
    size_t head;
    size_t tail;
    size_t maxLen;      // numero massimo di elementi supportati nella coda
    size_t len;         // numero attuale di elementi nella coda (<= maxLen)
    size_t maxSize;     // dimensione massima degli elementi nella coda
    size_t size;        // somma delle dimensioni degli elementi presenti in coda (<= maxSize)
    fileT** data;       // puntatore a un elemento fileT
    pthread_mutex_t m;
    pthread_cond_t full;
    pthread_cond_t empty;
} queueT;

fileT* createFile(FILE *file, char *filepath, int O_LOCK);

// fa la fclose sul file interno a f e poi chiama cleanupFile
void destroyFile(fileT *f);

void cleanupFile(fileT *f);

queueT* createQueue(size_t maxLen, size_t maxSize);

fileT* readQueue(queueT *queue);

int writeQueue(queueT *queue, fileT* data);

// se destroyData = 1, avvia destroyFile su ogni elemento della coda, il quale fa la fclose sui file
void destroyQueue(queueT *queue, int destroyData);

void cleanupQueue(queueT *queue);