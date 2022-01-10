#define CMDSIZE 256
#define BUFSIZE 1000000 // 10KB
#define MAX_OPEN_FILES 50
#define O_CREATE 1
#define O_LOCK 2

/**
 * Apre una connessione AF_UNIX al socket file "sockname". Se il server non accetta immediatamente la richiesta di connessione, 
 * la connessione da parte del client viene ripetuta dopo "msec" millisecondi e fino allo scadere del tempo assoluto "abstime".
 * \param sockname -> nome del socket al quale connettersi
 * \param msec -> tempo in millisecondi da aspettare prima di ritentare la connessione
 * \param abstime -> tempo massimo, scaduto il quale interrompere il tentativo di connessione
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int openConnection(const char* sockname, int msec, const struct timespec abstime);

/**
 * Chiude la connessione AF_UNIX associata al socket file sockname. 
 * \param sockname -> nome del socket
 * \retval -> 0 se successo, -1 se errore (setta errno)
*/
int closeConnection(const char* sockname);

/**
 * Apre o crea un file sul server. La semantica dipende dai flags passati come secondo argomento (O_CREATE e/o O_LOCK). 
 * Se viene passato il flag O_CREATE ed il file esiste gia' memorizzato nel server, 
 * oppure il file non esiste ed il flag O_CREATE non e' stato specificato, viene ritornato un errore. 
 * In caso di successo, il file viene aperto in lettura e scrittura. Se viene passato il flag O_LOCK 
 * (eventualmente in OR con O_CREATE) il file viene aperto e/o creato in modalità locked, 
 * che vuol dire che l’unico che può leggere o scrivere il file "pathname" e' il processo che lo ha aperto. 
 * Il flag O_LOCK può essere esplicitamente resettato utilizzando la chiamata unlockFile.
 * \param pathname -> nome del file da aprire/creare
 * \param flags -> 0 -> !O_CREATE && !O_LOCK
 *                 1 -> O_CREATE && !O_LOCK
 *                 2 -> !O_CREATE && O_LOCK
 *                 3 -> O_CREATE && O_LOCK
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int openFile(const char* pathname, int flags);

/**
 * Legge tutto il contenuto del file dal server (se esiste) ritornando un puntatore ad un'area allocata sullo heap 
 * nel parametro "buf", mentre "size" conterrà la dimensione del buffer dati (ossia la dimensione in bytes del file letto). 
 * In caso di errore, "buf" e "size" non sono validi. 
 * \param pathfile -> nome del file da leggere
 * \param buf -> puntatore ad un'area allocata sullo heap dove verra' memorizzato il contenuto del file
 * \param size -> puntatore alla dimensione del buffer dati (ovvero del file letto)
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int readFile(const char* pathname, void** buf, size_t* size);

/**
 * Legge 'N' files qualsiasi dal server e li memorizza nella directory "dirname". 
 * Se il server ha meno di 'N' file disponibili, li invia tutti (esclusi quelli che il client non ha il permesso di leggere).
 * Se N <= 0, la richiesta al server e' quella di leggere tutti i file memorizzati al suo interno. 
 * \param N -> numero di files da leggere; se <=0, la richiesta e' quella di leggere tutti i file memorizzati nel server
 * \param dirname -> cartella lato client dove memorizzare i files letti
 * \retval -> numero >= 0 se successo (numero di file effettivamente letti), -1 se errore (setta errno)
 */
int readNFiles(int N, const char* dirname);

/**
 * Scrive tutto il file puntato da "pathname" nel file server. Ritorna successo solo se la precedente operazione, 
 * terminata con successo, e' stata openFile(pathname, O_CREATE| O_LOCK). 
 * Se "dirname" è diverso da NULL, i file eventualmente spediti dal server perche' espulsi dalla cache 
 * per far posto al file "pathname" vengono scritti nella cartella "dirname". 
 * \param pathname -> nome del file da scrivere sul server
 * \param dirname -> se != NULL, cartella dove scrivere i file eventualmente espulsi dal server in seguito a capacity misses
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int writeFile(const char* pathname, const char* dirname);

/**
 * Scrive in append al file "pathname" i "size" bytes contenuti nel buffer "buf". 
 * Se "dirname" è diverso da NULL, i file eventualmente spediti dal server perche' espulsi dalla cache 
 * per far posto ai nuovi dati di "pathname" vengono scritti in "dirname".
 * \param pathname -> nome del file su cui scrivere in append
 * \param buf -> buffer che contiene i dati da scrivere in append sul file
 * \param size -> dimensione (numero di bytes) del buffer da scrivere in append
 * \param dirname -> se != NULL, cartella dove scrivere i file espulsi dal server in seguito a capacity misses
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname);

/**
 * Imposta un file nel server in modalita' locked (ovvero setta il flag O_LOCK). 
 * Se il file era stato aperto/creato con il flag O_LOCK e la richiesta proviene dallo stesso processo, 
 * oppure se il file non ha il flag O_LOCK settato, l’operazione termina immediatamente con successo; 
 * altrimenti, l’operazione non viene completata fino a quando il flag O_LOCK non viene resettato dal detentore della lock. 
 * \param pathname -> nome del file da impostare in modalita' locked
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int lockFile(const char* pathname);

/**
 * Resetta il flag O_LOCK sul file "pathname". 
 * L’operazione ha successo solo se l’owner della lock e' il processo che ha richiesto l’operazione, altrimenti termina con errore. 
 * \param pathname -> nome del file sul quale resettare il flag O_LOCK
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int unlockFile(const char* pathname);

/**
 * Chiude il file "pathname" nel server. Eventuali operazioni sul file dopo la closeFile falliscono.
 * Quando un file viene chiuso con successo, la mutua esclusione viene rilasciata automaticamente.
 * \param pathname -> nome del file da chiudere
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int closeFile(const char *pathname);

/**
 * Rimuove il file cancellandolo dal server. L’operazione fallisce se il file non e' in stato locked, 
 * o e' in stato locked da parte di un processo client diverso da chi effettua la removeFile.
 * \param pathname -> nome del file da rimuovere dal server
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int removeFile(const char* pathname);

/**
 * A seconda del flag 'rw', imposta la cartella per le scritture dei file eventualmente espulsi dal server in seguito a capacity misses 
 * provocati dalle openFile(O_CREATE), oppure imposta la cartella dove scrivere i file letti con le readFile.
 * \param Dir -> cartella da impostare per le openFile o le readFile
 * \param rw -> se = 1, imposta la cartella per le scritture dei file espulsi in seguito a dalle openFile(O_CREATE)
 *              se = 0, imposta la cartella dove scrivere i file letti con le readFile
 */
int setDirectory(char* Dir, int rw);

/**
 * Abilita o disabilita la stampa delle informazioni interne sulle operazioni effettuate.
 * \param p -> se = 1, abilita la stampa
 *             se = 0, disabilita la stampa
 */
void printInfo(int p);

// Funzioni ausiliarie; vengono chiamate internamente alle altre funzioni della libreria
int receiveFile(const char *dirname, void **bufA, size_t *sizeA);
int receiveNFiles(const char *dirname);
int lockFile_aux(const char *pathname);

// Funzioni ausiliarie che operano sulla lista dei file aperti
int addOpenFile(const char *pathname);
int isOpen(const char *pathname);
int removeOpenFile(const char *pathname);
int closeEveryFile();