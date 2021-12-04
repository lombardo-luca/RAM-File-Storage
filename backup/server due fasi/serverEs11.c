#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h> 	
#include <errno.h>
#include <pthread.h>
#include <sys/wait.h>
#include <ctype.h> 	
#include <signal.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>
#include <sys/syscall.h>

#include <threadpool.h>
#include <fileQueue.h>
#include <partialIO.h>

#define UNIX_PATH_MAX 108 
#define CMDSIZE 256
#define BUFSIZE 10000	// 10KB
#define LOGLINESIZE 512

// struttura dati che contiene gli argomenti da passare ai worker threads
typedef struct struct_thread {
	long *args;
	queueT *queue;
	FILE* logFile;
} threadT;

static void serverThread(void *par);
static void* sigThread(void *par);
int update(fd_set set, int fdmax);
int parser(char *command, queueT *queue, long fd_c);
void openFile(char *filepath, int flags, queueT *queue, long fd_c);
void writeFile(char *filepath, size_t size, queueT* queue, long fd_c);
void closeFile(char *filepath, queueT* queue, long fd_c);

// funzioni ausiliarie
int sendFile(fileT *f, long fd_c);

// funzioni di test
int testQueue(queueT *queue) {
	printf("TEST QUEUE\n");
	void *buf1 = malloc(256);
	fileT *f1, *f2;
	char str1[256] = "contenutofile1";
	memcpy(buf1, str1, 15);
	 
	void *buffer = malloc(256);
    FILE* ipf = fopen("test/file1.txt", "rb");
    int r = fread(buffer, 1, 256, ipf);
    fclose(ipf);

	if ((f1 = createFileT("test/file1.txt", 0, 0, 0)) == NULL) {
		perror("createFileT f1");
		return -1;
	}

	if (writeFileT(f1, buffer, r) == -1) {
		perror("writeFileT f1");
		return -1;
	}	

	printf("queue len = %ld\n", getLen(queue));

	if (enqueue(queue, f1) != 0) {
		perror("enqueue f1");
		return -1;
	}

	printf("queue len = %ld (dovrebbe essere 1)\n", getLen(queue));

	if ((f2 = createFileT("test/file2.txt", 1, 5, 0)) == NULL) {
		perror("createFileT f1");
		return -1;
	}

	if (enqueue(queue, f2) != 0) {
		perror("enqueue f2");
		return -1;
	}

	printf("queue len = %ld (dovrebbe essere 2)\n", getLen(queue));

	/*
	fileT *dequeueF;
	dequeueF = dequeue(queue);

	if (dequeueF == NULL) {
		printf("ERRORE??1\n");
	}

	destroyFile(dequeueF);
	*/

	free(buffer);
	free(buf1);

	if (printQueue(queue) == -1) {
		perror("printQueue");
		return -1;
	}
	printf("FINE TEST QUEUE\n");

	return 0;
}

int main(int argc, char *argv[]) {
	int fd_skt, fd_c, fd_max;
	struct sockaddr_un sa;
	sigset_t sigset;
	struct sigaction siga;
	char sockName[256] = "./mysock";		// nome del socket
	char logName[256] = "logs/log.txt";		// nome del file di log
	int threadpoolSize = 1;					// numero di thread workers nella threadPool
	size_t maxFiles = 1;					// massimo numero di file supportati
	size_t maxSize = 1;						// massima dimensione supportata (in bytes)
	int sigPipe[2], requestPipe[2];
	FILE *configFile;						// file di configurazione per il server
	FILE *logFile;							// file di log
	//char logString[LOGLINESIZE];			// stringa che contiene il singolo messaggio da scrivere sul logfile
	volatile long quit = 0;					// se = 1, termina il server il prima possibile
	sig_atomic_t numberOfConnections = 0;		// numero dei client attualmente connessi
	sig_atomic_t stopIncomingConnections = 0;	// se = 1, non accetta più nuove connessioni dai client

	// maschero tutti i segnali 
	if (sigfillset(&sigset) == -1) {
		perror("sigfillset.\n");
		return 1;
	}

	if (pthread_sigmask(SIG_SETMASK, &sigset, NULL) == -1) {
		perror("sigmask.\n");
		return 1;
	}

	memset(&siga, 0, sizeof(siga));

	siga.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &siga, NULL) == -1) {
		perror("sigaction.\n");
		return 1;
	} 

	// tolgo la maschera al solo SIGPIPE
	if (sigdelset(&sigset, SIGPIPE) == -1) {
		perror("sigdelset.\n");
		return 1;
	}

	if (pthread_sigmask(SIG_SETMASK, &sigset, NULL) == -1) {
		perror("sigmask.\n");
		return 1;
	}

	// stampo messaggio d'introduzione
	printf("File Storage Server avviato.\n");
	fflush(stdout);

	// apro il file di configurazione in sola lettura
	if ((configFile = fopen("config/config.txt", "r")) == NULL) {
		perror("configFile open");
		return 1;
	}

	char line[256];
	char *value;
	char *option = malloc(256);
	int len = 0;

	// leggo il file di configurazione una riga alla volta
	while ((fgets(line, 256, configFile))!= NULL) {
		// dalla riga opzione:valore estraggo solo il valore
		value = strchr(line, ':');	
		len = strlen(line) - strlen(value);
		value++;

		if (value != NULL) {
			option = strncpy(option, line, len);
			option[len] = '\0';
		}

		// configuro la dimensione della threadpool
		if (strcmp("threadpoolSize", option) == 0) {
			threadpoolSize = strtol(value, NULL, 0);

			if (threadpoolSize <= 0) {
				printf("Errore di configurazione: la dimensione della threadPool dev'essere maggiore o uguale a 1.\n");
				free(option);
				fclose(configFile);	// chiudo il file di configurazione
				return 1;
			}

			printf("CONFIG: Dimensione della threadPool = %d\n", threadpoolSize);
		}

		// configuro il nome del socket
		else if (strcmp("sockName", option) == 0) {
			strncpy(sockName, value, 256);
			sockName[strcspn(sockName, "\n")] = 0;	// rimuovo la newline dal nome del socket

			printf("CONFIG: Socket name = %s\n", sockName);
		}

		// configuro il numero massimo di file supportati
		else if (strcmp("maxFiles", option) == 0) {
			maxFiles = (size_t) strtol(value, NULL, 0);

			if (maxFiles <= 0) {
				printf("Errore di configurazione: il numero massimo di file dev'essere maggiore o uguale a 1.\n");
				free(option);
				fclose(configFile);	// chiudo il file di configurazione
				return 1;
			}

			printf("CONFIG: Numero massimo di file supportati = %ld\n", maxFiles);
		}

		// configuro la dimensione massima supportata (in KB)
		else if (strcmp("maxSize", option) == 0) {
			maxSize = (size_t) (strtol(value, NULL, 0) * 1000);

			if (maxSize <= 0) {
				printf("Errore di configurazione: la dimensione massima dev'essere maggiore o uguale a 1.\n");
				free(option);
				fclose(configFile);	// chiudo il file di configurazione
				return 1;
			}

			printf("CONFIG: Dimensione massima supportata = %lu KB (%lu Bytes)\n", maxSize/1000, maxSize);
		}

		// configuro il nome del file nel quale verranno scritti i logs
		else if (strcmp("logFile", option) == 0) {

			if (strcmp(value, "") == 0) {
				printf("Errore di configurazione: il nome del logFile non puo' essere vuoto.\n");
				free(option);
				fclose(configFile);	// chiudo il file di configurazione
				return 1;
			}

			strncpy(logName, "logs/", 6);
			strncat(logName, value, strlen(value)+1);
			char *ext = ".txt";
			strncat(logName, ext, 5);

			if ((logFile = fopen(logName, "w")) == NULL) {
				perror("logFile open");
				free(option);
				fclose(configFile);	// chiudo il file di configurazione
				return 1;
			}

			printf("CONFIG: logFile = %s\n", logName);
		}

		else {
			printf("Errore di configurazione: opzione non riconosciuta.\n");
			free(option);
			fclose(configFile);	// chiudo il file di configurazione
			return 1;
		}
	}

	free(option);
	fclose(configFile);	// chiudo il file di configurazione

	// scrivo sul logFile
	char servStartStr[256] = "Server avviato.\nMax files = ";
	char maxFilesStr[64];
	char maxSizeStr[64];
	char ss[13] = " Max size = ";
	snprintf(maxFilesStr, sizeof(size_t), "%ld", maxFiles);
	snprintf(maxSizeStr, sizeof(size_t), "%ld", maxSize);
	strncat(servStartStr, maxFilesStr, strlen(maxFilesStr)+1);
	strncat(servStartStr, ss, strlen(ss)+1);
	strncat(servStartStr, maxSizeStr, strlen(maxSizeStr)+1);
	strncat(servStartStr, ".\n", 3);
	if (fwrite(servStartStr, 1, strlen(servStartStr)+1, logFile) == -1) {
		perror("fwrite");
		return -1;
	}

	strncpy(sa.sun_path, sockName, UNIX_PATH_MAX);
	sa.sun_family = AF_UNIX;

	fd_skt = socket(AF_UNIX, SOCK_STREAM, 0);
	bind(fd_skt, (struct sockaddr *) &sa, sizeof(sa));
	listen(fd_skt, SOMAXCONN);

	// scrivo sul logFile
	char sockStr[512] = "Creato socket = ";
	strncat(sockStr, sockName, strlen(sockName)+1);
	strncat(sockStr, ".\n", 3);
	if (fwrite(sockStr, 1, strlen(sockStr)+1, logFile) == -1) {
		perror("fwrite");
		return -1;
	}

	// creo la pipe di comunicazione tra il sigThread e il thread manager
	if (pipe(sigPipe) == -1) {
		perror("sigPipe");
		return 1;
	}

	// creo la seconda pipe, ovvero quella tra i worker e i manager
	if (pipe(requestPipe) == -1) {
		perror("requestPipe");
		return 1;
	}

	// creo sigThread che farà la sigwait sui segnali
	pthread_t st;
	if (pthread_create(&st, NULL, &sigThread, (void*) &sigPipe[1]) == -1) {
		perror("pthread_create per st.\n");
		return 1;
	}

	// creo la coda di file
	queueT *queue = createQueue(maxFiles, maxSize);

	if (testQueue(queue) == -1) {
		perror("testQueue");
		return 1;
	}

	// creo la threadpool
	threadpool_t *pool = NULL;
	pool = createThreadPool(threadpoolSize, threadpoolSize);

	if (!pool) {
		perror("createThreadPool.\n");
		return 1;
	}

	// scrivo sul logFile
	char newTPoolStr[128] = "Creata threadpool di dimensione ";
	char tPoolSizeStr[64];
	snprintf(tPoolSizeStr, sizeof(int), "%d", threadpoolSize);
	strncat(newTPoolStr, tPoolSizeStr, strlen(tPoolSizeStr)+1);
	strncat(newTPoolStr, ".\n", 3);
	if (fwrite(newTPoolStr, 1, strlen(newTPoolStr)+1, logFile) == -1) {
		perror("fwrite");
		return -1;
	}

	fd_set set, tmpset;
	FD_ZERO(&set);
	FD_ZERO(&tmpset);
	FD_SET(fd_skt, &set);			// al set da ascoltare aggiungo: il listener,
	FD_SET(sigPipe[0], &set);		// l'fd di lettura della pipe del sigThread,

	// controllo quale fd ha id maggiore
	if (fd_skt > sigPipe[0]) {
		fd_max = fd_skt;
	}

	else {
		fd_max = sigPipe[0];
	}

	while (!quit) {
		// copio il set nella variabile temporanea. Bisogna inizializzare ogni volta perché select modifica tmpset
		tmpset = set;

		// fd_max+1 -> numero dei descrittori attivi, non l’indice massimo
		if (select(fd_max+1, &tmpset, NULL, NULL, NULL) == -1) {
			perror("select server main");
			return 1;
		}

		// select OK
		else { 
			// controllo da quale fd ho ricevuto la richiesta
			for (int fd = 0; fd <= fd_max; fd++) {
				if (FD_ISSET(fd, &tmpset)) {
					// se l'ho ricevuta da un sock connect, è una nuova richiesta di connessione
					if (fd == fd_skt) {
						if ((fd_c = accept(fd_skt, NULL, 0)) == -1) {
							perror("accept");
							return 1;
						}

						printf("Nuovo client connesso.\n");
						// Scrivo sul logFile
						char newConStr[25] = "Nuovo client connesso.\n";
					    if (fwrite(newConStr, 1, strlen(newConStr)+1, logFile) == -1) {
					    	perror("fwrite");
					    	return -1;
					    }

						// creo la struct da passare come argomento al thread worker
						threadT *t = malloc(sizeof(threadT));
						t->args = malloc(3*sizeof(long));
						t->args[0] = fd_c;
		    			t->args[1] = (long) &quit;
		    			t->args[2] = (long) requestPipe[1];
		    			t->queue = queue;
		    			t->logFile = logFile;
						int r = addToThreadPool(pool, serverThread, (void*) t);

						// task aggiunto alla pool con successo
						if (r == 0) {
							printf("SERVER: task aggiunto alla pool.\n");
							numberOfConnections++;
							continue;
						}

						// errore interno
						else if (r < 0) {
							perror("addToThreadPool");
						}

						// coda pendenti piena
						else {
							perror("coda pendenti piena");
						}

						//close(fd_c);

						continue;
					}

					/* se l'ho ricevuta dalla sigPipe, controllo se devo terminare immediatamente 
					o solo smettere di accettare nuove connessioni */
					else if (fd == sigPipe[0]) {
						printf("Ricevuto un segnale di terminazione immediata.\n");
						quit = 1;
						break;
					}

				}	
			}	
		}
	}

	destroyThreadPool(pool, 0);		// notifico a tutti i thread workers di terminare

	printf("Sto per distruggere la cache\n");
	if (printQueue(queue) == -1) {
		perror("printQueue");
		return 1;
	}
	destroyQueue(queue);			// distruggo la coda di file e libero la memoria

	if (pthread_join(st, NULL) != 0) {
		perror("pthread_join.\n");
		return 1;
	}

	fclose(logFile);	// chiudo il file di log
	unlink(sockName);

	return 0;
}

static void serverThread(void *par) {
	assert(par);
	threadT *t = (threadT*) par;
	long *args = t->args;
	long fd_c = args[0];
	long *quit = (long*) (args[1]);
	int pipe = (int) (args[2]);
	queueT *queue = t->queue;
	FILE *logFile = t->logFile;
	sigset_t sigset;
	fd_set set, tmpset;
	//pid_t tid = syscall(__NR_gettid);;	// identificatore del thread worker
	pthread_t tid = pthread_self();

	// libera la memoria del threadT passato come parametro
	free(par);
	
	// maschero tutti i segnali nel thread
	if (sigfillset(&sigset) == -1) {
		perror("sigfillset.\n");
		goto cleanup;
	}

	if (pthread_sigmask(SIG_SETMASK, &sigset, NULL) == -1) {
		perror("sigmask.\n");
		goto cleanup;
	}

	FD_ZERO(&set);
	FD_SET(fd_c, &set);

	while (*quit == 0) {
		tmpset = set;
		int r;
		struct timeval timeout = {0, 100000};	// ogni 100ms controllo se devo terminare

		if ((r = select(fd_c + 1, &tmpset, NULL, NULL, &timeout)) < 0) {
		    perror("select server thread");
		    goto cleanup;
		}

		// se il timeout è terminato, controllo se devo uscire o riprovare
		if (r == 0) {
		    if (*quit) {
		    	goto cleanup;
		    }
		}

		char buf[CMDSIZE];
		memset(buf, '\0', CMDSIZE);

		int n;
		if ((n = read(fd_c, buf, CMDSIZE)) == -1) {	// leggi il messaggio del client	
			perror("read");

			goto cleanup;
		}

		if (n == 0 || strcmp(buf, "quit\n") == 0) {
			printf("SERVER THREAD: chiudo la connessione col client\n");
			fflush(stdout);

			// scrivo sul logFile
			char closeConStr[36] = "Chiusa connessione con un client.\n";
			if (fwrite(closeConStr, 1, strlen(closeConStr)+1, logFile) == -1) {
				perror("fwrite");
				goto cleanup;
			}

			goto cleanup;
		}

		printf("SERVER THREAD: ho ricevuto %s!\n", buf);
		fflush(stdout);

		if (parser(buf, queue, fd_c) == -1) {
			printf("SERVER THREAD: errore parser.\n");
			fflush(stdout);
			goto cleanup;
		}

		memset(buf, '\0', CMDSIZE);

		// scrivo sul logFile
		char workStr[128] = "Il thread ";
		char tidStr[64];
		snprintf(tidStr, sizeof(pthread_t), "%ld", tid);
		strncat(workStr, tidStr, strlen(tidStr)+1);
		strncat(workStr, " ha servito una richiesta.\n", 31);
		if (fwrite(workStr, 1, strlen(workStr)+1, logFile) == -1) {
			perror("fwrite");
		}
	}

	//printf("SERVER THREAD: select ok.\n");
	//fflush(stdout);

	

	// ripulisci la memoria
	cleanup: {
		printf("cleanup SERVER THREAD\n");
		fflush(stdout);
		if (args) {
			free(args);
		}
	}

	close(fd_c);
}

static void* sigThread(void *par) {
	int *p = (int*) par;
	int fd_pipe = *p;

	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGQUIT);
	sigaddset(&set, SIGHUP);

	int sig;

	pthread_sigmask(SIG_SETMASK, &set, NULL);

	if (sigwait(&set, &sig) != 0) {
		perror("sigwait.\n");
		return (void*) 1;
	}

	printf("Ho ricevuto segnale %d\n", sig);
	close(fd_pipe);	// notifico il thread manager della ricezione del segnale

	return (void*) 0;
}

int update(fd_set set, int fdmax) {
	for (int i = fdmax-1; i >= 0; --i) {
		if (FD_ISSET(i, &set)) {
			return i;
		}
	}

	return -1;
}

// effettua il parsing dei comandi
int parser(char *command, queueT *queue, long fd_c) {
	if (!command || !queue) {
		errno = EINVAL;
		return -1;
	}

	// parso il comando ricevuto dal client
	char *token = NULL, *save = NULL, *token2 = NULL, *token3 = NULL;
	token = strtok_r(command, ":", &save);

	// controllo il comando ricevuto e chiamo la procedura opportuna
	if (token && strcmp(token, "openFile") == 0) {
		token2 = strtok_r(NULL, ":", &save);
		token3 = strtok_r(NULL, ":", &save);
		int arg = (int) strtol(token3, NULL, 0);

		openFile(token2, arg, queue, fd_c);
	}

	else if (token && strcmp(token, "writeFile") == 0) {
		token2 = strtok_r(NULL, ":", &save);
		token3 = strtok_r(NULL, ":", &save);
		size_t sz = (size_t) strtol(token3, NULL, 0);

		writeFile(token2, sz, queue, fd_c);
	}

	else if (token && strcmp(token, "closeFile") == 0) {
		token2 = strtok_r(NULL, ":", &save);
		closeFile(token2, queue, fd_c);
	}

	// comando non riconosciuto
	else {
		printf("PARSER: comando non riconosciuto.\n");
		fflush(stdout);
		return -1;
	}

	return 0;
}

void openFile(char *filepath, int flags, queueT* queue, long fd_c) {
	void *res = malloc(BUFSIZE);
	char ok[3] = "ok";		// messaggio che verra' mandato al client se l'operazione ha avuto successo
	char er[3] = "er";		// - - - - - - - - - - - - - - - - - - -  se c'e' stato un errore
	char es[3] = "es";		// - - - - - - - - - - - - - - - - - - -  se un file e' stato espulso dalla cache

	memcpy(res, ok, 3);
	fileT *espulso = NULL;

	// controllo la validita' degli argomenti
	if (!filepath || flags < 0 || flags > 3 || !queue) {
		errno = EINVAL;
		memcpy(res, er, 3);
		goto send;
	}

	int O_CREATE = 0, O_LOCK = 0, found = 0;

	/**
	 * flags = 0 -> !O_CREATE && !O_LOCK
	 *		 = 1 -> O_CREATE && !O_LOCK
	 *       = 2 -> !O_CREATE && O_LOCK
	 * 		 = 3 -> O_CREATE && O_LOCK
	 */
	if ((flags == 1 || flags == 3)) {
		O_CREATE = 1;
	}
	
	if ((flags == 2 || flags == 3)) {
		O_LOCK = 1;
	}

	// cerco se il file e' presente nel server
	fileT *findF = NULL;
	findF = find(queue, filepath);
	if (findF != NULL) {
		found = 1;
	}

	destroyFile(findF);

	printf("openFile: found = %d\n", found);
	fflush(stdout);

	// se il client richiede di creare un file che esiste già, errore
	if (O_CREATE && found) {
		errno = EEXIST;
		memcpy(res, er, 3);
		goto send;
	}
	
	// se il client cerca di aprire un file inesistente, errore
	else if (!O_CREATE && !found) {
		errno = ENOENT;
		memcpy(res, er, 3);
		goto send;
	}

	// il client vuole creare il file
	else if (O_CREATE && !found) {
		// se la cache e' piena, espelli un file secondo la politica FIFO
		if (getLen(queue) == queue->maxLen) {
			printf("openFile: coda piena (queue->len = %ld), espello un elemento.\n", getLen(queue));
			fflush(stdout);
			espulso = dequeue(queue);

			if (espulso == NULL) {
				perror("dequeue");
				memcpy(res, er, 3);
				goto send;
			}

			
			memcpy(res, es, 3);
		}

		// crea il file come richiesto dal client
		fileT *f = createFileT(filepath, O_LOCK, fd_c, 1);

		if (f == NULL) {
			perror("createFileT");
			memcpy(res, er, 3);
		}

		else if (enqueue(queue, f) != 0) {
			perror("enqueue");
			memcpy(res, er, 3);
		}
	}

	// il client vuole aprire un file gia' esistente
	else {
		if (openFileInQueue(queue, filepath, O_LOCK, fd_c) == -1) {
			perror("openFileInQueue");
			memcpy(res, er, 3);
		}
	}

	// invia risposta al client
	send:
		printf("openFile: mando risposta al client\n");
		fflush(stdout);
		void *buf = NULL;
		buf = malloc(BUFSIZE);
		memcpy(buf, res, 3);
		if (writen(fd_c, buf, 3) == -1) {
			perror("writen");
			goto cleanup;
		}

		// se un file è stato espulso dalla coda, lo invio al client
		if (strcmp(res, "es") == 0) {
			if (sendFile(espulso, fd_c) == -1) {
				perror("sendFile");
				goto cleanup;
			}
		}

		// se c'è stato un errore, invio errno al client
		else if(strcmp(res, "er") == 0) {			
			if (writen(fd_c, &errno, sizeof(int)) == -1) {
				perror("writen");
				goto cleanup;
			}
		}

		free(buf);

	// libera la memoria
	cleanup: 
		if (espulso) {
			destroyFile(espulso);
		}

		free(res);
}

void writeFile(char *filepath, size_t size, queueT* queue, long fd_c) {
	void *res = malloc(BUFSIZE);
	char ok[3] = "ok";		// messaggio che verra' mandato al client se l'operazione ha avuto successo
	char er[3] = "er";		// - - - - - - - - - - - - - - - - - - -  se c'e' stato un errore
	char es[3] = "es";		// - - - - - - - - - - - - - - - - - - -  se un file e' stato espulso dalla cache
	int found = 0;
	fileT *espulso = NULL;
	void *content = NULL;
	content = malloc(size);

	memcpy(res, ok, 3);
	
	// controllo la validita' degli argomenti
	if (!filepath || !queue) {
		errno = EINVAL;
		memcpy(res, er, 3);
		goto send;
	}

	// cerco se il file su cui si vuole scrivere e' presente nel server
	fileT *findF = NULL;
	findF = find(queue, filepath);
	if (findF != NULL) {
		found = 1;

		// se la dimensione del file scritto diventerebbe piu' grande della capacita' massima della cache, errore
		if (findF->size + size > queue->maxSize) {
			destroyFile(findF);
			errno = EFBIG;
			memcpy(res, er, 3);
			goto send;
		}
	}

	printf("writeFile: found = %d\n", found);	
	fflush(stdout);

	// il file e' presente nel server
	if (found) {
		printf("Il file e' locked? %d Owner = %d Client = %ld\n", findF->O_LOCK, findF->owner, fd_c);
		fflush(stdout);

		// controllo se il client ha i permessi per scrivere sul file
		if (!findF->open || (findF->O_LOCK && findF->owner != fd_c)) {
			errno = EPERM;
			memcpy(res, er, 3);
            goto send;
		}

		// se non c'e' abbastanza spazio nella cache, espelli un file secondo la politica FIFO
		if (getSize(queue) + size > queue->maxSize) {
			printf("writeFile: cache piena (queue->size = %ld), espello un elemento.\n", getSize(queue));
			fflush(stdout);
			espulso = dequeue(queue);

			if (espulso == NULL) {
				perror("dequeue");
				memcpy(res, er, 3);
				goto send;
			}

			memcpy(res, es, 3);
		}
	}

	// se il file non e' presente, errore
	else {
		errno = ENOENT;
		memcpy(res, er, 3);
		goto send;
	}

	destroyFile(findF);

	// invia risposta al client
	send:
		printf("writeFile: mando risposta al client\n");
		fflush(stdout);
		void *buf = NULL;
		buf = malloc(BUFSIZE);
		memcpy(buf, res, 3);

		if (writen(fd_c, buf, 3) == -1) {
			perror("writen");
			goto cleanup;
		}

		// se un file e' stato espulso dalla coda, lo invio al client
		if (strcmp(res, "es") == 0) {
			if (sendFile(espulso, fd_c) == -1) {
				perror("sendFile");
				goto cleanup;
			}

			// se ancora non c'e' abbastanza spazio nella cache, espelli altri file
			while (getSize(queue) + size > queue->maxSize) {
				if (getSize(queue) == 0 || getLen(queue) == 0) {
					// non dovrebbe mai accadere poiche' si controlla prima se il file puo' essere contenuto nella cache
					errno = EINVAL;
					goto cleanup;
				}

				printf("writeFile: cache ancora piena (queue->size = %ld), espello un elemento.\n", getSize(queue));
				fflush(stdout);

				// libero la memoria del file appena mandato
				if (espulso) {
					destroyFile(espulso);
				}

				espulso = dequeue(queue);

				if (espulso == NULL) {
					perror("dequeue");
					goto cleanup;
				}

				if (sendFile(espulso, fd_c) == -1) {
					perror("sendFile");
					goto cleanup;
				}
			}

			// avverto il client che ho finito di mandare file
			char fine[6] = ".FINE";
			printf("invio %s\n", fine);
			memset(buf, 0, BUFSIZE);
			memcpy(buf, fine, 6);

			if (writen(fd_c, buf, BUFSIZE) == -1) {
				perror("writen");
				goto cleanup;
			}

			// ricevo dal client il contenuto del file da scrivere
			if ((readn(fd_c, content, size)) == -1) {
				perror("readn");
				goto cleanup;
			}

			// scrivi il file
			// TO-DO: scrivere invece di append! 
			if (appendFileInQueue(queue, filepath, content, size, fd_c) == -1) {
				perror("writeFileT");
				memcpy(res, er, 3);
			}
		}

		// se c'è stato un errore, invio errno al client
		else if(strcmp(res, "er") == 0) {			
			if (writen(fd_c, &errno, sizeof(int)) == -1) {
				perror("writen");
			}
		}

		// se non ci sono stati errori e non ho dovuto espellere alcun file, eseguo la richiesta del client
		else {
			// ricevo dal client il contenuto del file da scrivere
			if ((readn(fd_c, content, size)) == -1) {
				perror("readn");
			}

			// scrivi il file
			if (appendFileInQueue(queue, filepath, content, size, fd_c) == -1) {
				perror("writeFileT");
			}
		}	

	// libera la memoria
	cleanup: 
		if (espulso) {
			destroyFile(espulso);
		}

		free(buf);
		free(content);
		free(res);
}

void closeFile(char *filepath, queueT* queue, long fd_c) {
	void *res = malloc(BUFSIZE);
	int found = 0;
	char ok[3] = "ok";		// messaggio che verra' mandato al client se l'operazione ha avuto successo
	char er[3] = "er";		// - - - - - - - - - - - - - - - - - - -  se c'e' stato un errore

	memcpy(res, ok, 3);

	// controllo la validita' degli argomenti
	if (!filepath || !queue) {
		errno = EINVAL;
		memcpy(res, er, 3);
		return;
	}

	// cerco se il file e' presente nel server
	fileT *findF = NULL;
	findF = find(queue, filepath);
	if (findF != NULL) {
		found = 1;
	}

	printf("openFile: found = %d\n", found);
	fflush(stdout);

	// se il file e' presente, chiudilo
	if (found) {
		// la funzione chiamata controlla se il client ha i permessi per poter chiudere il file
		if (closeFileInQueue(queue, filepath, fd_c) == -1) {
			perror("closeFileInQueue");
			memcpy(res, er, 3);
		}

		destroyFile(findF);
	}

	// se il file non e' presente, errore
	else {
		errno = ENOENT;
		memcpy(res, er, 3);
	}

	printf("closeFile: mando risposta al client\n");
	fflush(stdout);
	void *buf = NULL;
	buf = malloc(BUFSIZE);
	memcpy(buf, res, 3);

	// invio risposta al client
	if (writen(fd_c, buf, 3) == -1) {
		perror("writen");
	}

	// se c'è stato un errore, invio errno al client
	else if(strcmp(res, "er") == 0) {			
		if (writen(fd_c, &errno, sizeof(int)) == -1) {
			perror("writen");
		}
	}

	free(buf);
	free(res);
}

// funzione ausiliaria che invia un file al client
int sendFile(fileT *f, long fd_c) {
	void *buf = NULL;
	buf = malloc(BUFSIZE);

	// invio prima il filepath...
	printf("invio il filepath: %s\n", f->filepath);
	fflush(stdout);
	memset(buf, 0, BUFSIZE);
	memcpy(buf, f->filepath, strlen(f->filepath)+1);
	printf("memcpy fatta\n");
	fflush(stdout);
	if (writen(fd_c, buf, BUFSIZE) == -1) {
		perror("writen");
		free(buf);
		return -1;
	}

	// ... poi la dimensione del file...
	if (writen(fd_c, &f->size, sizeof(size_t)) == -1) {
		perror("writen");
		free(buf);
		return -1;
	}

	// ...e infine il contenuto
	memset(buf, 0, BUFSIZE);
	memcpy(buf, f->content, f->size);
	if (writen(fd_c, buf, f->size) == -1) {
		perror("writen");
		free(buf);
		return -1;
	}

	free(buf);
	return 0;
}