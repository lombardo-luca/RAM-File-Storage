// struttura dati per gestire i file in memoria principale
typedef struct {
    char *filepath;     // path assoluto del file
    int O_LOCK;         // se = 1, il file e' in modalita' locked
    int owner;          // se O_LOCK = 1, contiene il file descriptor del client che possiede la lock sul file
    int open;           // se = 1, indica che il file e' stato aperto
    void *content;      // contenuto del file
    size_t size;        // dimensione del file in bytes
} fileT;

// nodo di una linked list
typedef struct node {
    fileT *data;        
    struct node *next;  // puntatore al prossimo elemento della lista
} nodeT;

// coda FIFO di fileT, implementata come una linked list
typedef struct {
    nodeT *head;        // puntatore al primo elemento della coda
    nodeT *tail;        // puntatore all'ultimo elemento della coda
    size_t maxLen;      // numero massimo di elementi supportati nella coda
    size_t len;         // numero attuale di elementi nella coda (<= maxLen)
    size_t maxSize;     // dimensione massima degli elementi nella coda
    size_t size;        // somma delle dimensioni degli elementi presenti in coda (<= maxSize)       
    pthread_mutex_t m;  // lock per rendere thread-safe le operazioni sulla coda
} queueT;

/**
 * Alloca ed inizializza un fileT.
 * \param filepath -> stringa che identifica il fileT tramite il suo path assoluto
 * \param O_LOCK -> se = 1, crea il fileT modalita' locked, se = 0 no
 * \param owner -> file descriptor del client che ha richiesto l'operazione
 * \param open -> se = 1, apre il file immediatamente dopo averlo creato, se = 0 no
 * \retval -> puntatore al fileT creato, NULL se errore (setta errno)
 */
fileT* createFileT(char *filepath, int O_LOCK, int owner, int open);

/**
 * Scrive del contenuto (in append) su un fileT creato con createFileT.
 * \param f -> fileT sul quale scrivere
 * \param content -> puntatore al buffer che contiene i dati da scrivere
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
 * Alloca ed inizializza una coda di fileT. Dev'essere chiamata da un solo thread.
 * \param maxLen -> lunghezza massima della coda (numero di file)
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
 *  Estrae un fileT dalla coda come la dequeue, ma invece di restituire il file estratto lo distrugge immediatamente, liberandone la memoria.
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
 * Imposta un fileT contenuto nella coda in modalita' locked. Fallisce se il file e' stato messo in modalita' locked da un client diverso.
 * \param queue -> puntatore alla coda che contiene il fileT
 * \param filepath -> path assoluto (identificatore) del fileT sul quale impostare la modalita' locked
 * \param owner -> file descriptor del client che ha richiesto l'operazione di lock
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int lockFileInQueue(queueT *queue, char *filepath, int owner);

/**
 * Resetta il flag O_LOCK di un fileT all'interno della coda. Fallisce se il file e' stato messo in modalita' locked da un client diverso.
 * \param queue -> puntatore alla coda che contiene il fileT
 * \param filepath -> path assoluto (identificatore) del fileT sul quale resettare il flag O_LOCK
 * \param owner -> file descriptor del client che ha richiesto l'operazione di unlock
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int unlockFileInQueue(queueT *queue, char *filepath, int owner);

/**
 * Apre un fileT contenuto nella coda. Fallisce se il file e' stato messo in modalita' locked da un client diverso.
 * \param queue -> puntatore alla coda che contiene il fileT da aprire
 * \param filepath -> path assoluto (identificatore) del fileT da aprire
 * \param O_LOCK -> se = 1, il file viene aperto in modalita' locked, se = 0 no
 * \param client -> file descriptor del client che ha richiesto l'apertura
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int openFileInQueue(queueT *queue, char *filepath, int O_LOCK, int client);

/**
 * Chiude un fileT contenuto nella coda. Se il file non era stato precedentemente aperto, termina comunque con successo.
 * Fallisce se il file e' stato messo in modalita' locked da un client diverso.
 * \param queue -> puntatore alla coda che contiene il fileT da chiudere
 * \param filepath -> path assoluto (identificatore) del fileT da chiudere
 * \param client -> file descriptor del client che ha richiesto la chiusura
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int closeFileInQueue(queueT *queue, char *filepath, int client);

/**
 * Scrive del contenuto su un fileT all'interno della coda. 
 * Fallisce se il file non e' stato precedente aperto, o se e' stato messo in modalita' locked da un client diverso.
 * \param queue -> puntatore alla coda che contiene il fileT su cui scrivere
 * \param filepath -> path assoluto (identificatore) del fileT su cui scrivere
 * \param content -> puntatore al buffer che contiene i dati da scrivere
 * \param size -> dimensione in bytes del contenuto da scrivere
 * \param client -> file descriptor del client che ha richiesto l'operazione di scrittura
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int writeFileInQueue(queueT *queue, char *filepath, void *content, size_t size, int client);

/**
 * Scrive del contenuto in append su un fileT all'interno della coda. 
 * Fallisce se il file non e' stato precedente aperto, o se e' stato messo in modalita' locked da un client diverso.
 * \param queue -> puntatore alla coda che contiene il fileT su cui effettuare l'append
 * \param filepath -> path assoluto (identificatore) del fileT su cui effettuare l'append
 * \param content -> puntatore al buffer che contiene i dati da scrivere in append
 * \param size -> dimensione in bytes del contenuto da scrivere in append
 * \param client -> file descriptor del client che ha richiesto l'operazione di append
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int appendFileInQueue(queueT *queue, char *filepath, void *content, size_t size, int client);

/**
 * Rimuove un fileT dalla coda e ne libera la memoria. 
 * Fallisce se il file non e' in modalita' locked, o se la lock e' posseduta da un client diverso.
 * \param queue -> puntatore alla coda che contiene il fileT da rimuovere
 * \param filepath -> path assoluto (identificatore) del fileT da rimuovere
 * \param client -> file descriptor del client che ha richiesto la rimozione
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int removeFileFromQueue(queueT *queue, char *filepath, int client);

/**
 * Cerca un fileT nella coda a partire dal suo path assoluto (identificatore) e ne restituisce una copia se trovato.
 * \param queue -> puntatore alla coda sulla quale cercare il fileT
 * \param filepath -> path assoluto del fileT da cercare 
 * \retval -> puntatore a una copia del fileT se trovato, NULL se non trovato o errore (setta errno)
 */
fileT* find(queueT *queue, char *filepath);

/**
 * Restituisce la lunghezza attuale della coda (ovvero il numero di elementi presenti).
 * \param queue -> puntatore alla coda della quale si vuole conoscere la lunghezza
 * \retval -> numero di elementi presenti, -1 se errore (setta errno)
*/
size_t getLen(queueT *queue);

/**
 * Restituisce la dimensione attuale della coda (ovvero la somma delle dimensioni in bytes dei fileT presenti).
 * \param queue -> puntatore alla coda della quale si vuole conoscere la dimensione
 * \retval -> dimensione della coda in bytes, -1 se errore (setta errno)
*/
size_t getSize(queueT *queue);

/**
 * Cancella una coda allocata con createQueue e ne libera la memoria. Dev'essere chiamata da un solo thread. 
 * Chiama al suo interno la destroyFile su ogni elemento della coda.
 * \param queue -> puntatore alla coda da distruggere
 */
void destroyQueue(queueT *queue);