#define CMDSIZE 256
#define BUFSIZE 10000 // 10KB
#define MAX_OPEN_FILES 50
#define O_CREATE 1
#define O_LOCK 2

/**
 * Viene aperta una connessione AF_UNIX al socket file sockname. Se il server non accetta immediatamente la
 * richiesta di connessione, la connessione da parte del client viene ripetuta dopo ‘msec’ millisecondi e fino allo
 * scadere del tempo assoluto ‘abstime’ specificato come terzo argomento. Ritorna 0 in caso di successo, -1 in caso
 * di fallimento, errno viene settato opportunamente.
 * \param sockname -> nome del socket al quale connettersi
 * \param msec -> numero di millisecondi da aspettare prima di ritentare la connessione
 * \param abstime -> tempo massimo, scaduto il quale interrompere il tentativo di connessione
 * \retval -> 0 se successo, -1 se errore (setta errno)
 */
int openConnection(const char* sockname, int msec, const struct timespec abstime);

/**
 * Chiude la connessione AF_UNIX associata al socket file sockname. 
 * Ritorna 0 in caso di successo, -1 in caso di fallimento, errno viene settato opportunamente
 * \param sockname -> nome del socket
 * \retval -> 0 se successo, -1 se errore (setta errno)
*/
int closeConnection(const char* sockname);

/**
 * Richiesta di apertura o di creazione di un file. La semantica della openFile dipende dai flags passati come secondo
 * argomento che possono essere O_CREATE ed O_LOCK. Se viene passato il flag O_CREATE ed il file esiste già
 * memorizzato nel server, oppure il file non esiste ed il flag O_CREATE non è stato specificato, viene ritornato un
 * errore. In caso di successo, il file viene sempre aperto in lettura e scrittura, ed in particolare le scritture possono
 * avvenire solo in append. Se viene passato il flag O_LOCK (eventualmente in OR con O_CREATE) il file viene
 * aperto e/o creato in modalità locked, che vuol dire che l’unico che può leggere o scrivere il file ‘pathname’ è il
 * processo che lo ha aperto. Il flag O_LOCK può essere esplicitamente resettato utilizzando la chiamata unlockFile,
 * descritta di seguito.
 * Ritorna 0 in caso di successo, -1 in caso di fallimento, errno viene settato opportunamente.
 *  flags = 0 -> !O_CREATE && !O_LOCK
 *        = 1 -> O_CREATE && !O_LOCK
 *        = 2 -> !O_CREATE && O_LOCK
 *        = 3 -> O_CREATE && O_LOCK
 */
int openFile(const char* pathname, int flags);

/**
 * Legge tutto il contenuto del file dal server (se esiste) ritornando un puntatore ad un'area allocata sullo heap nel
 * parametro ‘buf’, mentre ‘size’ conterrà la dimensione del buffer dati (ossia la dimensione in bytes del file letto). 
 * In caso di errore, ‘buf‘e ‘size’ non sono validi. 
 * Ritorna 0 in caso di successo, -1 in caso di fallimento, errno viene settato opportunamente.
 */
int readFile(const char* pathname, void** buf, size_t* size);

/**
 * Richiede al server la lettura di ‘N’ files qualsiasi da memorizzare nella directory ‘dirname’ lato client. 
 * Se il server ha meno di ‘N’ file disponibili, li invia tutti. 
 * Se N <= 0 la richiesta al server è quella di leggere tutti i file memorizzati al suo interno. 
 * Ritorna un valore maggiore o uguale a 0 in caso di successo (cioè ritorna il n. di file effettivamente letti), 
 * -1 in caso di fallimento, errno viene settato opportunamente.
 */
int readNFiles(int N, const char* dirname);

/**
 * Scrive tutto il file puntato da pathname nel file server. Ritorna successo solo se la precedente operazione,
 * terminata con successo, è stata openFile(pathname, O_CREATE| O_LOCK). Se ‘dirname’ è diverso da NULL, 
 * il file eventualmente spedito dal server perche' espulso dalla cache per far posto al file "pathname" dovra' essere
 * scritto in ‘dirname’; ritorna 0 in caso di successo, -1 in caso di fallimento, errno viene settato opportunamente.
*/
int writeFile(const char* pathname, const char* dirname);

/**
 * Richiesta di scrivere in append al file ‘pathname‘ i ‘size‘ bytes contenuti nel buffer ‘buf’. 
 * L’operazione di append nel file è garantita essere atomica dal file server. 
 * Se ‘dirname’ è diverso da NULL, il file eventualmente spedito dal server perchè 
 * espulso dalla cache per far posto ai nuovi dati di ‘pathname’ dovrà essere scritto in ‘dirname’.
 * Ritorna 0 in caso di successo, -1 in caso di fallimento, errno viene settato opportunamente. 
 */
int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname);

/**
 * In caso di successo setta il flag O_LOCK al file. Se il file era stato aperto/creato con il flag O_LOCK 
 * e la richiesta proviene dallo stesso processo, oppure se il file non ha il flag O_LOCK settato, 
 * l’operazione termina immediatamente con successo, altrimenti l’operazione non viene completata 
 * fino a quando il flag O_LOCK non viene resettato dal detentore della lock. 
 * L’ordine di acquisizione della lock sul file non è specificato. 
 * Ritorna 0 in caso di successo, -1 in caso di fallimento, errno viene settato opportunamente.
 */
int lockFile(const char* pathname);

/**
 * Resetta il flag O_LOCK sul file ‘pathname’. L’operazione ha successo solo se l’owner della lock 
 * è il processo che  ha richiesto l’operazione, altrimenti l’operazione termina con errore. 
 * Ritorna 0 in caso di successo, -1 in caso di fallimento, errno viene settato opportunamente.
 */
int unlockFile(const char* pathname);

/**
 * Richiesta di chiusura del file puntato da "pathname". Eventuali operazioni sul file dopo la closeFile falliscono.
 * Ritorna 0 in caso di successo, -1 in caso di fallimento, errno viene settato opportunamente.
 * Quando un file viene chiuso, la mutua esclusione viene rilasciata automaticamente.
 */
int closeFile(const char *pathname);

/**
 * Rimuove il file cancellandolo dal file storage server. L’operazione fallisce se il file non è in stato locked, 
 * o è in stato locked da parte di un processo client diverso da chi effettua la removeFile.
 */
int removeFile(const char* pathname);


// Funzioni ausiliarie
int receiveFile(const char *dirname, void **bufA, size_t *sizeA);
int receiveNFiles(const char *dirname);
int lockFile_aux(const char *pathname);
int addOpenFile(const char *pathname);
int isOpen(const char *pathname);
int removeOpenFile(const char *pathname);
int closeEveryFile();
void printInfo(int p);
int setDirectory(char* Dir, int rw);