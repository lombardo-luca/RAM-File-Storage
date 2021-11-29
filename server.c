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

#include <threadpool.h>
#include <fileQueue.h>
#include <partialIO.h>

#define UNIX_PATH_MAX 108 
#define BUFSIZE 256

typedef struct struct_thread {
	long *args;
	queueT *queue;
} threadT;

static void serverThread(void *par);
static void* sigThread(void *par);
int update(fd_set set, int fdmax);
int parser(char *command, queueT *queue);
int openFile(char *pathname, int flags, queueT *queue);

int main(int argc, char *argv[]) {
	int fd_skt, fd_c, fd_max;
	struct sockaddr_un sa;
	sigset_t sigset;
	struct sigaction siga;
	char sockName[256] = "./mysock";		// nome del socket
	char logName[256] = "logs/log.txt";			// nome del file di log
	int threadpoolSize = 1;					// numero di thread workers nella threadPool
	size_t maxFiles = 1;					// massimo numero di file supportati
	size_t maxSize = 1;						// massima dimensione supportata (in bytes)
	int sigPipe[2], requestPipe[2];
	FILE *configFile;						// file di configurazione per il server
	FILE *logFile;							// file di log
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

		// se l'opzione letta è threadpoolSize, configuro la dimensione della threadpool
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

		else if (strcmp("sockName", option) == 0) {
			strncpy(sockName, value, 256);
			sockName[strcspn(sockName, "\n")] = 0;	// rimuovo la newline dal nome del socket

			printf("CONFIG: Socket name = %s\n", sockName);
		}

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

		else if (strcmp("maxSize", option) == 0) {
			maxSize = (size_t) (strtol(value, NULL, 0) * 1000000);

			if (maxSize <= 0) {
				printf("Errore di configurazione: la dimensione massima dev'essere maggiore o uguale a 1.\n");
				free(option);
				fclose(configFile);	// chiudo il file di configurazione
				return 1;
			}

			printf("CONFIG: Dimensione massima supportata = %lu MB (%lu bytes)\n", maxSize/1000000, maxSize);
		}

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

	strncpy(sa.sun_path, sockName, UNIX_PATH_MAX);
	sa.sun_family = AF_UNIX;

	fd_skt = socket(AF_UNIX, SOCK_STREAM, 0);
	bind(fd_skt, (struct sockaddr *) &sa, sizeof(sa));
	listen(fd_skt, SOMAXCONN);

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

	printf("TEST QUEUE\n");
	void *buf1 = malloc(256);
	void *buf2 = malloc(256);
	fileT *f1, *f2;
	char str1[256] = "contenutofile1";
	char str2[5] = "test";
	memcpy(buf1, str1, 15);
	memcpy(buf2, str2, 5);
	 
	if ((f1 = createFile("test/file1.txt", 0, 0)) == NULL) {
		perror("createFile f1");
		return 1;
	}

	if ((f2 = createFile("test/file2.txt", 0, 0)) == NULL) {
		perror("createFile f2");
		return 1;
	}

	if (writeFile(f1, buf1, 15) == -1) {
		perror("writeFile f1");
		return 1;
	}	

	if (writeFile(f1, buf1, 15) == -1) {
		perror("writeFile f1");
		return 1;
	}	

	if (writeFile(f2, buf2, 5) == -1) {
		perror("writeFile f2");
		return 1;
	}

	if (push(queue, f1) != 0) {
		perror("push f1");
		return 1;
	}

	if (push(queue, f2) != 0) {
		perror("push f2");
		return 1;
	}

	fileT *ff1;

	if ((ff1 = find(queue, "test/file1.txt")) == NULL) {
		printf("file non trovato\n");
	}

	else {
		printf("file trovato\n");
	}

	printf("TEST SCRITTURA SU FILE\n");
    FILE* opf = fopen("testscrittura.txt", "w");
    fwrite(ff1->content, 1, ff1->size, opf);
    fclose(opf);
    printf("FINE TEST SCRITTURA SU FILE\n");

	fileT *f3 = pop(queue);
	for (int j = 0; j < 1; j++) {
		if (f3 == NULL) {
			perror("pop");
			return 1;
		}

		else {
			printf("ho rimosso il file %s\n", f3->filepath);
		}
	}

	fileT *ff2;

	if ((ff2 = find(queue, "test/file1.txt")) == NULL) {
		printf("file non trovato\n");
	}

	else {
		printf("file trovato\n");
	}

	free(buf1);
	free(buf2);
	destroyFile(f3);
	destroyFile(ff1);
	destroyFile(ff2);
	printf("FINE TEST QUEUE\n");
	//return 0;

	// creo la threadpool
	threadpool_t *pool = NULL;
	pool = createThreadPool(threadpoolSize, threadpoolSize);

	if (!pool) {
		perror("createThreadPool.\n");
		return 1;
	}

	fd_set set, tmpset;
	FD_ZERO(&set);
	FD_ZERO(&tmpset);
	FD_SET(fd_skt, &set);			// al set da ascoltare aggiungo: il listener,
	FD_SET(sigPipe[0], &set);		// l'fd di lettura della pipe del sigThread,
	FD_SET(requestPipe[0], &set);	// e quello della pipe dei worker

	// controllo quale fd ha id maggiore
	fd_max = fd_skt;
	if (sigPipe[0] > fd_max) {
		fd_max = fd_skt;
	}

	if (requestPipe[0] > fd_max) {
		fd_max = requestPipe[0];
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
						if (!stopIncomingConnections) {
							if ((fd_c = accept(fd_skt, NULL, 0)) == -1) {
								perror("accept");
								return 1;
							}

							printf("Nuova connessione richiesta.\n");

							// creo la struct da passare come argomento al thread worker
							threadT *t = malloc(sizeof(threadT));
							t->args = malloc(3*sizeof(long));
							t->args[0] = fd_c;
			    			t->args[1] = (long) &quit;
			    			t->args[2] = (long) requestPipe[1];
			    			t->queue = queue;
							int r = addToThreadPool(pool, serverThread, (void*) t);

							/*
							long* args = malloc(3*sizeof(long));
							args[0] = fd_c;
			    			args[1] = (long) &quit;
			    			args[2] = (long) requestPipe[1];
							int r = addToThreadPool(pool, serverThread, (void*) args);
							*/

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

							close(fd_c);
						}

						else {
							printf("Nuova connessione rifiutata: il server è in fase di terminazione.\n");
							FD_CLR(fd, &set);
							close(fd);
						}

						continue;
					}

					// se l'ho ricevuta dalla requestPipe, una richiesta singola è stata servita
					else if (fd == requestPipe[0]) {
						// leggo il descrittore dalla pipe
						int fdr;
						readn(requestPipe[0], &fdr, sizeof(int));

						// se il worker thread ha chiuso la connessione...
						if (fdr == -1) {
							numberOfConnections--;
							// ...controllo se devo terminare il server
							if (stopIncomingConnections && numberOfConnections <= 0) {
								printf("Non ci sono altri client connessi, termino...\n");
								quit = 1;
								pthread_cancel(st);	// termino il signalThread
							}

							break;
						}

						// altrimenti riaggiungo il descrittore al set, in modo che possa essere servito nuovamente
						FD_SET(fdr, &set);

						if (fdr > fd_max) {
							fd_max = fdr;
						}

						continue;	
					}

					/* se l'ho ricevuta dalla sigPipe, controllo se devo terminare immediatamente 
					o solo smettere di accettare nuove connessioni */
					else if (fd == sigPipe[0]) {
						int code;
						readn(sigPipe[0], &code, sizeof(int));

						if (code == 0) {
							printf("Ricevuto un segnale di stop alle nuove connessioni.\n");
							stopIncomingConnections = 1;
						}

						else if (code == 1) {
							printf("Ricevuto un segnale di terminazione immediata.\n");
							quit = 1;
						}

						else {
							perror("Errore: codice inviato dal sigThread invalido.\n");
						}
						
						break;
					}

					// altrimenti è una richiesta di I/O da un client già connesso
					else {
						FD_CLR(fd, &set);
						fd_max = update(set, fd_max);

						// creo la struct da passare come argomento al thread worker
						threadT *t = malloc(sizeof(threadT));
						t->args = malloc(3*sizeof(long));
						t->args[0] = fd_c;
			    		t->args[1] = (long) &quit;
			    		t->args[2] = (long) requestPipe[1];
			    		t->queue = queue;
						int r = addToThreadPool(pool, serverThread, (void*) t);

						/*
						long* args = malloc(3*sizeof(long));
						args[0] = fd;
		    			args[1] = (long) &quit;
		    			args[2] = (long) requestPipe[1];
						int r = addToThreadPool(pool, serverThread, (void*) args);
						*/

						// task aggiunto alla pool con successo
						if (r == 0) {
							printf("Task aggiunto alla pool.\n");
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

						close(fd);

						continue;
					}
				}	
			}	
		}
	}

	destroyThreadPool(pool, 0);		// notifico a tutti i thread workers di terminare
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
	sigset_t sigset;
	fd_set set, tmpset;

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

		else {
			break;
		}
	}

	printf("SERVER THREAD: select ok.\n");

	char buf[BUFSIZE];
	memset(buf, '\0', BUFSIZE);

	int n;
	n = read(fd_c, buf, BUFSIZE);	// leggi il messaggio del client	

	if (n == 0 || strcmp(buf, "quit\n") == 0) {
		printf("SERVER THREAD: chiudo la connessione col client\n");
		close(fd_c);

		int close = -1;
		writen(pipe, &close, sizeof(int));	// comunico al manager che la richiesta è stata servita

		goto cleanup;
	}

	printf("SERVER THREAD: ho ricevuto %s\n", buf);

	if (parser(buf, queue) == -1) {
		printf("SERVER THREAD: errore parser.");
		goto cleanup;
	}

	
	/*
	char str[BUFSIZE] = "";

	int i = 0;
	char chr;

	while (buf[i]) {
		chr = toupper(buf[i]);
		str[i] = chr;
		i++;
	}

	//write(fd_c, str, strlen(str));
	writen(fd_c, str, BUFSIZE);
	printf("SERVER THREAD: ho mandato %s\n\n", str);
	fflush(stdout);
	*/

	memset(buf, '\0', BUFSIZE);

	writen(pipe, &fd_c, sizeof(int));	// comunico al manager che la richiesta è stata servita

	// ripulisci la memoria
	cleanup: {
		printf("cleanup\n");
		if (args) {
			free(args);
		}
	}
}

static void* sigThread(void *par) {
	int *p = (int*) par;
	int fd_pipe = *p;

	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGQUIT);
	sigaddset(&set, SIGHUP);

	while (1) {
		int sig;
		int code;
		pthread_sigmask(SIG_SETMASK, &set, NULL);

		if (sigwait(&set, &sig) != 0) {
			perror("sigwait.\n");
			return (void*) 1;
		}

		printf("Ho ricevuto segnale %d\n", sig);

		switch (sig) {
			case SIGHUP:
				code = 0;
				// notifico il thread manager di smettere di accettare nuove connessioni in entrata
				writen(fd_pipe, &code, sizeof(int));	
				break;
			case SIGINT:
			case SIGQUIT:
				code = 1;
				// notifico il thread manager di terminare il server il prima possibile
				writen(fd_pipe, &code, sizeof(int));	
				return NULL;
			default:
				break;
		}
	}
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
int parser(char *command, queueT *queue) {
	if (!command) {
		errno = EINVAL;
		return -1;
	}

	// parso il comando ricevuto dal client
	char *token = NULL, *save = NULL, *token2 = NULL, *token3 = NULL;
	token = strtok_r(command, ":", &save);

	// il comando ricevuto è openFile
	if (strcmp(token, "openFile") == 0) {
		token2 = strtok_r(NULL, ":", &save);
		token3 = strtok_r(NULL, ":", &save);
		int arg = (int) strtol(token3, NULL, 0);

		if (openFile(token2, arg, queue) == -1) {
			perror("openFile");
			return -1;
		}
	}

	// comando non riconosciuto
	else {
		printf("PARSER: comando non riconosciuto.\n");
		return -1;
	}

	return 0;
}

int openFile(char *pathname, int flags, queueT* queue) {
	if (!pathname || flags < 0 || flags > 3) {
		errno = EINVAL;
		return -1;
	}

	printf("OpenFile: ho ricevuto pathname = %s! flags = %d!\n", pathname, flags);

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

	fileT *f = find(queue, pathname);
	if (f != NULL) {
		found = 1;
	}

	printf("OpenFile: found = %d\n", found);

	// se il client richiede di creare un file che esiste già, errore
	if (O_CREATE && found) {
		errno = EEXIST;
		return -1;
	}
	
	// se il client cerca di aprire un file inesistente, errore
	if (!O_CREATE && !found) {
		errno = ENOENT;
		return -1;
	}



	destroyFile(f);

	return 0;
}