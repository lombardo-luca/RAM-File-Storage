/*
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

/*
 * Chiude la connessione AF_UNIX associata al socket file sockname. 
 * Ritorna 0 in caso di successo, -1 in caso di fallimento, errno viene settato opportunamente
 * \param sockname -> nome del socket
 * \retval -> 0 se successo, -1 se errore (setta errno)
*/
int closeConnection(const char* sockname);

/*
 * Richiesta di apertura o di creazione di un file. La semantica della openFile dipende dai flags passati come secondo
 * argomento che possono essere O_CREATE ed O_LOCK. Se viene passato il flag O_CREATE ed il file esiste già
 * memorizzato nel server, oppure il file non esiste ed il flag O_CREATE non è stato specificato, viene ritornato un
 * errore. In caso di successo, il file viene sempre aperto in lettura e scrittura, ed in particolare le scritture possono
 * avvenire solo in append. Se viene passato il flag O_LOCK (eventualmente in OR con O_CREATE) il file viene
 * aperto e/o creato in modalità locked, che vuol dire che l’unico che può leggere o scrivere il file ‘pathname’ è il
 * processo che lo ha aperto. Il flag O_LOCK può essere esplicitamente resettato utilizzando la chiamata unlockFile,
 * descritta di seguito.
 * Ritorna 0 in caso di successo, -1 in caso di fallimento, errno viene settato opportunamente.
 */
int openFile(const char* pathname, int flags);