// struttura dati per gestire i file in memoria
typedef struct {
    FILE* file;
    char* filepath;     // path assoluto del file
    int O_LOCK;         // flag
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
 * \param file -> puntatore al file da contenere
 * \param filepath -> stringa che identifica il path assoluto del file
 * \param O_LOCK -> flag 
 */
fileT* createFile(FILE *file, char *filepath, int O_LOCK);

/* Cancella un fileT creato con createFile. Chiude (tramte fclose) il file in esso contenuto.
 * \param f -> file da distruggere
*/
void destroyFile(fileT *f);

/* Come la destroyFile, ma non chiude il file in esso contenuto.
 * \param f -> file da cancellare
*/
void cleanupFile(fileT *f);

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

/* Come la destroyQueue, ma non chiama la destroyFile sugli elementi della coda, non chiudendo quindi i file in essa contenuti.
 * \param queue -> puntatore alla coda da cancellare
*/
void cleanupQueue(queueT *queue);