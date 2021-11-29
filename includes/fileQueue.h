// struttura dati per gestire i file in memoria
typedef struct {
    char* filepath;     // path assoluto del file
    int O_LOCK;         // flag per la modalità locked
    int owner;          // file descriptor del client che ha richiesto l'operazione di lock sul file
    void *content;      // contenuto del file. (size == -1) => (content == NULL)
    size_t size;        // dimensione del file in bytes
    pthread_mutex_t m; 
} fileT;

// coda FIFO di fileT
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

/* Alloca ed inizializza un fileT.
 * \param filepath -> stringa che identifica il path assoluto del file
 * \param O_LOCK -> se 1, crea il file modalità locked
 * \param owner -> file descriptor del client che ha richiesto la modalità locked
 * \retval -> puntatore al fileT creato, NULL se errore (setta errno)
 */
fileT* createFile(char *filepath, int O_LOCK, int owner);

/* Scrive del contenuto su un fileT creato con createFile.
 * \param f -> fileT sul quale scrivere
 * \param content -> contenuto da scrivere
 * \size -> dimensione in bytes del contenuto da scrivere
 * \retval -> 1 se successo, 0 se errore (setta errno)
 */
int writeFile(fileT *f, void *content, size_t size) ;

/* Cancella un fileT creato con createFile
 * \param f -> file da cancellare
*/
void destroyFile(fileT *f);

/* Alloca ed inizializza una coda. Dev'essere chiamata da un solo thread.
 * \param maxLen -> massima lunghezza della coda (numero di file)
 * \param maxSize -> dimensione massima della coda (in bytes)
 * \retval -> puntatore alla coda allocata, NULL se errore
 */
queueT* createQueue(size_t maxLen, size_t maxSize);

/* Estrae un dato dalla coda.
 * \param queue -> puntatore alla coda dalla quale leggere
 * \retval -> puntatore al dato estratto, NULL se errore
 */
fileT* readQueue(queueT *queue);

/* Inserisce un elemento nella coda. 
 * \param queue -> puntatore alla coda sulla quale scrivere
 * \param data -> puntatore all'elemento da inserire
 * \retval -> 0 se successo, -1 se errore (setta errno)  
 */
int writeQueue(queueT *queue, fileT* data);

/* Restituisce la lunghezza attuale della coda (ovvero il numero di elementi presenti).
 * \param queue -> puntatore alla coda
 * \retval -> numero di elementi presenti, -1 se errore (setta errno)
*/
size_t getLen(queueT *queue);

/* Restituisce la dimensione attuale della coda (ovvero la somma delle dimensioni dei file presenti).
 * \param queue -> puntatore alla coda
 * \retval -> dimensione della coda in bytes, -1 se errore (setta errno)
*/
size_t getSize(queueT *queue);

/* Cancella una coda allocata con createQueue. Dev'essere chiamata da un solo thread. 
 * Chiama al suo interno la destroyFile su ogni elemento della coda, chiudendo tutti i file in essa contenuti.
 * \param queue -> puntatore alla coda da distruggere
 */
void destroyQueue(queueT *queue);

/* Come la destroyQueue, ma non chiama la destroyFile sugli elementi della coda.
 * \param queue -> puntatore alla coda da cancellare
*/
void cleanupQueue(queueT *queue);