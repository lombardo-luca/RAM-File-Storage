// struttura dati per mantenere i file in memoria
typedef struct {
    FILE* file;
    int O_LOCK;
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

fileT* createFile(FILE *file, int O_LOCK);

queueT* createQueue(size_t maxLen, size_t maxSize);

fileT* readQueue(queueT *queue);

int writeQueue(queueT *queue, fileT* data);

void destroyQueue(queueT *queue);

void cleanup(queueT *queue);