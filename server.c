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

#include <threadpool.h>

#define UNIX_PATH_MAX 108 
#define SOCKNAME "./mysock"
#define BUFSIZE 256

static void serverThread(void *par);
static void* sigThread(void *par);
int update(fd_set set, int fdmax);

int main(int argc, char *argv[]) {
	int fd_skt, fd_c, fd_max;
	struct sockaddr_un sa;
	sigset_t sigset;
	struct sigaction siga;
	volatile long quit = 0;
	int n = 0;
	int sigPipe[2], requestPipe[2];

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

	// verifico che l'argomento n sia corretto
	if (argc < 2) {
		perror("Inserisci come argomento il numero di thread nella pool.\n");
		return 1;
	}

	n = strtol(argv[1], NULL, 0);

	if (n <= 0) {
		perror("Il numero di thread nella pool dev'essere maggiore o uguale a 1.\n");
		return 1;
	}

	strncpy(sa.sun_path, SOCKNAME, UNIX_PATH_MAX);
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
	pool = createThreadPool(n, n);

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
						if ((fd_c = accept(fd_skt, NULL, 0)) == -1) {
							perror("accept");
							return 1;
						}

						printf("SERVER: nuova connessione richiesta.\n");

						long* args = malloc(3*sizeof(long));
						args[0] = fd_c;
		    			args[1] = (long) &quit;
		    			args[2] = (long) requestPipe[1];
						int r = addToThreadPool(pool, serverThread, (void*) args);

						// task aggiunto alla pool con successo
						if (r == 0) {
							printf("SERVER: task aggiunto alla pool.\n");
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
						continue;
					}

					// se l'ho ricevuta dalla requestPipe, una richiesta singola è stata servita
					else if (fd == requestPipe[0]) {
						// leggo il descrittore dalla pipe
						int fdr;
						read(requestPipe[0], &fdr, sizeof(int));
						// riaggiungo il descrittore al set, in modo che possa essere servito nuovamente
						FD_SET(fdr, &set);

						if (fdr > fd_max) {
							fd_max = fdr;
						}

						continue;	
					}

					// se l'ho ricevuta dalla sigPipe, è un segnale di terminazione
					else if (fd == sigPipe[0]) {
						printf("SERVER: ricevuto un segnale di terminazione dalla pipe.\n");
						quit = 1;
						break;
					}

					// altrimenti è una richiesta di I/O da un client già connesso
					else {
						FD_CLR(fd, &set);
						fd_max = update(set, fd_max);
						/*
						if (fd > fd_max) {
							fd_max = fd;
						}
						*/

						long* args = malloc(3*sizeof(long));
						args[0] = fd;
		    			args[1] = (long) &quit;
		    			args[2] = (long) requestPipe[1];
						int r = addToThreadPool(pool, serverThread, (void*) args);

						// task aggiunto alla pool con successo
						if (r == 0) {
							printf("SERVER: task aggiunto alla pool.\n");
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

	unlink(SOCKNAME);

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
		//break;
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