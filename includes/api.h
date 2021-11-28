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
