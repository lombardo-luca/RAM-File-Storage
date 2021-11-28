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

#define UNIX_PATH_MAX 108 
#define BUFSIZE 256

static void serverThread(void *par);
static void* sigThread(void *par);
int update(fd_set set, int fdmax);

int main(int argc, char *argv[]) {
	int fd_skt, fd_c, fd_max;
	struct sockaddr_un sa;
	sigset_t sigset;
	struct sigaction siga;
	char sockName[256] = "./mysock";		// nome del socket
	int threadpoolSize = 1;					// numero di thread workers nella threadPool
	int maxFiles = 1;						// massimo numero di file supportati
	unsigned long maxSize = 1;				// massima dimensione supportata (in bytes)
	int sigPipe[2], requestPipe[2];
	FILE *configFile;						// file di configurazione per il server
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

	/*
	printf("TEST QUEUE\n");

	queueT *q = createQueue(5, 100);

	FILE *file1 = fopen("test/file1.txt", "r");
	FILE *file2 = fopen("test/file2.txt", "r");
	fileT *f1;
	fileT *f2;
	 
	if ((f1 = createFile(file1, "test/file1.txt", 0)) == NULL) {
		perror("createFile f1");
		return 1;
	}

	if ((f2 = createFile(file2, "test/file2.txt", 0)) == NULL) {
		perror("createFile f2");
		return 1;
	}

	if (writeQueue(q, f1) != 0) {
		perror("writeQueue f1");
		return 1;
	}

	if (writeQueue(q, f2) != 0) {
		perror("writeQueue f2");
		return 1;
	}

	for (int j = 0; j < 1; j++) {
		fileT *f3 = readQueue(q);
		if (f3 == NULL) {
			perror("readQueue");
			return 1;
		}

		else {
			printf("ho letto il file %s\n", f3->filepath);
		}
	}

	destroyFile(f1);
	destroyQueue(q, 1);

	printf("FINE TEST QUEUE\n");
	//return 0;
	*/

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
			maxFiles = (int) strtol(value, NULL, 0);

			if (maxFiles <= 0) {
				printf("Errore di configurazione: il numero massimo di file dev'essere maggiore o uguale a 1.\n");
				return 1;
			}

			printf("CONFIG: Numero massimo di file supportati = %d\n", maxFiles);
		}

		else if (strcmp("maxSize", option) == 0) {
			maxSize = (unsigned long) (strtol(value, NULL, 0) * 1000000);

			if (maxSize <= 0) {
				printf("Errore di configurazione: la dimensione massima dev'essere maggiore o uguale a 1.\n");
				return 1;
			}

			printf("CONFIG: Dimensione massima supportata = %lu MB (%lu bytes)\n", maxSize/1000000, maxSize);
		}

		else {
			printf("Errore di configuraizone: opzione non riconosciuta.\n");
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

							long* args = malloc(3*sizeof(long));
							args[0] = fd_c;
			    			args[1] = (long) &quit;
			    			args[2] = (long) requestPipe[1];
							int r = addToThreadPool(pool, serverThread, (void*) args);

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
						read(requestPipe[0], &fdr, sizeof(int));

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
						read(sigPipe[0], &code, sizeof(int));

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

						long* args = malloc(3*sizeof(long));
						args[0] = fd;
		    			args[1] = (long) &quit;
		    			args[2] = (long) requestPipe[1];
						int r = addToThreadPool(pool, serverThread, (void*) args);

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

	// notifico a tutti i thread di terminare
	destroyThreadPool(pool, 0);

	if (pthread_join(st, NULL) != 0) {
		perror("pthread_join.\n");
		return 1;
	}

	unlink(sockName);

	return 0;
}

static void serverThread(void *par) {
	assert(par);
	long *args = (long*) par;
	long fd_c = args[0];
	long *quit = (long*) (args[1]);
	int pipe = (int) (args[2]);
	free(par);
	sigset_t sigset;
	fd_set set, tmpset;
	
	// maschero tutti i segnali nel thread
	if (sigfillset(&sigset) == -1) {
		perror("sigfillset.\n");
		exit(EXIT_FAILURE);
	}

	if (pthread_sigmask(SIG_SETMASK, &sigset, NULL) == -1) {
		perror("sigmask.\n");
		exit(EXIT_FAILURE);
	}

	FD_ZERO(&set);
	FD_SET(fd_c, &set);

	while (*quit == 0) {
		tmpset = set;
		int r;
		struct timeval timeout = {0, 100000};	// ogni 100ms controllo se devo terminare

		if ((r = select(fd_c + 1, &tmpset, NULL, NULL, &timeout)) < 0) {
		    perror("select server thread");
		    return;
		}

		// se il timeout è terminato, controllo se devo uscire o riprovare
		if (r == 0) {
		    if (*quit) {
		    	return;
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
		write(pipe, &close, sizeof(int));	// comunico al manager che la richiesta è stata servita

		return;
	}

	printf("SERVER THREAD: ho ricevuto %s\n", buf);

	char str[BUFSIZE] = "";

	int i = 0;
	char chr;

	while (buf[i]) {
		chr = toupper(buf[i]);
		str[i] = chr;
		i++;
	}

	write(fd_c, str, strlen(str));
	printf("SERVER THREAD: ho mandato %s\n\n", str);
	fflush(stdout);

	memset(buf, '\0', BUFSIZE);

	write(pipe, &fd_c, sizeof(int));		// comunico al manager che la richiesta è stata servita
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
				write(fd_pipe, &code, sizeof(int));	
				break;
			case SIGINT:
			case SIGQUIT:
				code = 1;
				// notifico il thread manager di terminare il server il prima possibile
				write(fd_pipe, &code, sizeof(int));	
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