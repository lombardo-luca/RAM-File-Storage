// struttura dati per gestire i file in memoria principale
typedef struct {
    char *filepath;     // path assoluto del file
    int O_LOCK;         // se = 1, il file e' in modalita' locked
    int owner;          // se O_LOCK = 1, contiene il file descriptor del client che ha richiesto l'operazione di lock
    int open;           // ae = 1, indica che il file e' stato aperto in lettura/scrittura 
    void *content;      // contenuto del file
    size_t size;        // dimensione del file in bytes
} fileT;

// nodo di una linked list
typedef struct node {
    fileT *data;
    struct node *next;
} nodeT;

// coda FIFO di fileT, implementata come una linked list
typedef struct {
    nodeT *head;        // puntatore al primo elemento della coda
    nodeT *tail;        // puntatore all'ultimo elemento della coda
    size_t maxLen;      // numero massimo di elementi supportati nella coda
    size_t len;         // numero attuale di elementi nella coda (< maxLen)
    size_t maxSize;     // dimensione massima degli elementi nella coda
    size_t size;        // somma delle dimensioni degli elementi presenti in coda (<= maxSize)       
    pthread_mutex_t m;
} queueT;

/*
// coda FIFO di fileT. E' vuota quando head == tail, quindi può contenere al massimo maxLen-1 elementi.
typedef struct {
    size_t head;
    size_t tail;
    size_t maxLen;      // numero massimo di elementi supportati nella coda + 1
    size_t len;         // numero attuale di elementi nella coda (< maxLen)
    size_t maxSize;     // dimensione massima degli elementi nella coda
    size_t size;        // somma delle dimensioni degli elementi presenti in coda (<= maxSize)
    fileT** data;       
    pthread_mutex_t m;
    //pthread_cond_t full;
    //pthread_cond_t empty;
} queueT;
*/

/**
 * Alloca ed inizializza un fileT.
 * \param filepath -> stringa che identifica il path assoluto del fileT
 * \param O_LOCK -> se = 1, crea il fileT modalità locked, se = 0 no
 * \param owner -> file descriptor del client che ha richiesto la modalità locked
 * \param open -> se = 1, apre il file immediatamente dopo averlo creato, se = 0 no
 * \retval -> puntatore al fileT creato, NULL se errore (setta errno)
 */
fileT* createFileT(char *filepath, int O_LOCK, int owner, int open);

/**
 * Scrive del contenuto su un fileT creato con createFileT. L'operazione di scrittura avviene sempre in append.
 * \param f -> fileT sul quale scrivere
 * \param content -> contenuto da scrivere
 * \size -> dimensione in bytes del contenuto da scrivere
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int writeFileT(fileT *f, void *content, size_t size) ;

/**
 * Cancella un fileT creato con createFileT e ne libera la memoria.
 * \param f -> fileT da cancellare
*/
void destroyFile(fileT *f);

/**
 * Alloca ed inizializza una coda. Dev'essere chiamata da un solo thread.
 * \param maxLen -> massima lunghezza della coda (numero di file)
 * \param maxSize -> dimensione massima della coda (in bytes)
 * \retval -> puntatore alla coda allocata, NULL se errore
 */
queueT* createQueue(size_t maxLen, size_t maxSize);

/**
 * Estrae un fileT dalla coda.
 * \param queue -> puntatore alla coda dalla quale estrarre il fileT
 * \retval -> puntatore al file estratto, NULL se errore
 */
fileT* dequeue(queueT *queue);

/**
 *  Come la dequeue, ma non restituisce il fileT estratto.
 * \param queue -> puntatore alla coda dalla quale estrarre il fileT
*/
void voiDequeue(queueT *queue);

/**
 * Inserisce un fileT nella coda. 
 * \param queue -> puntatore alla coda nella quale inserire il fileT
 * \param data -> puntatore al fileT da inserire
 * \retval -> 0 se successo, -1 se errore (setta errno)  
 */
int enqueue(queueT *queue, fileT* data);

/**
 * Stampa su standard output l'intero contenuto della coda.
 * \param queue -> puntatore alla coda da stampare
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int printQueue(queueT *queue);

/**
 * Imposta un fileT all'interno della in modalita' locked.
 * \param queue -> puntatore alla coda che contiene il fileT
 * \param filepath -> path assoluto (identificatore) del fileT su cui impostare la modalita' locked
 * \param owner -> file descriptor del client che ha richiesto l'operazione di lock
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int lockFileInQueue(queueT *queue, char *filepath, int owner);

/**
 * Apre un fileT all'interno della coda. Fallisce se il file e' stato messo in modalita' locked da un client diverso.
 * \param queue -> puntatore alla coda che contiene il fileT
 * \param filepath -> path assoluto (identificatore) del fileT da aprire
 * \param O_LOCK -> se = 1, il file viene aperto in modalita' locked, se = 0 no
 * \param client -> file descriptor del client che ha richiesto l'apertura
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int openFileInQueue(queueT *queue, char *filepath, int O_LOCK, int client);

/**
 * Cerca un fileT nella coda a partire dal suo path assoluto (identificatore).
 * \param queue -> puntatore alla coda sulla quale cercare il fileT
 * \param filepath -> path assoluto del fileT da cercare 
 * \retval -> puntatore al fileT trovato, NULL se non trovato o errore (setta errno)
 */
fileT* find(queueT *queue, char *filepath);

/**
 * Restituisce la lunghezza attuale della coda (ovvero il numero di elementi presenti).
 * \param queue -> puntatore alla coda
 * \retval -> numero di elementi presenti, -1 se errore (setta errno)
*/
size_t getLen(queueT *queue);

/**
 * Restituisce la dimensione attuale della coda (ovvero la somma delle dimensioni dei fileT presenti).
 * \param queue -> puntatore alla coda
 * \retval -> dimensione della coda in bytes, -1 se errore (setta errno)
*/
size_t getSize(queueT *queue);

/**
 * Cancella una coda allocata con createQueue. Dev'essere chiamata da un solo thread. 
 * Chiama al suo interno la destroyFile su ogni elemento della coda e successivamente la cleanupQueue.
 * \param queue -> puntatore alla coda da distruggere
 */
void destroyQueue(queueT *queue);

/**
 * Funzione di pulizia ausiliaria che libera la memoria allocata da una coda. Da usare solo su code vuote.
 * \param queue -> puntatore alla coda da ripulire
*/
void cleanupQueue(queueT *queue);