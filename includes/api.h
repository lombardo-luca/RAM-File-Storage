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
 * Scrive tutto il file puntato da pathname nel file server. Ritorna successo solo se la precedente operazione,
 * terminata con successo, è stata openFile(pathname, O_CREATE| O_LOCK). Se ‘dirname’ è diverso da NULL, 
 * il file eventualmente spedito dal server perche' espulso dalla cache per far posto al file "pathname" dovra' essere
 * scritto in ‘dirname’; ritorna 0 in caso di successo, -1 in caso di fallimento, errno viene settato opportunamente.
*/
int writeFile(const char* pathname, const char* dirname);

/**
 * Richiesta di chiusura del file puntato da "pathname". Eventuali operazioni sul file dopo la closeFile falliscono.
 * Ritorna 0 in caso di successo, -1 in caso di fallimento, errno viene settato opportunamente.
 * Quando un file viene chiuso, la mutua esclusione viene rilasciata automaticamente.
 */
int closeFile(const char *pathname);

// Funzioni ausiliarie
int receiveFile(const char *dirname);
int receiveNFiles(const char *dirname);
void printInfo(int p);
int setDirectory(char* Dir);