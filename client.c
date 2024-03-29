#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/un.h> 	
#include <errno.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <ctype.h>
#include <ftw.h>

// librerie in /includes
#include <api.h>
#include <partialIO.h>

#define UNIX_PATH_MAX 108 

// struttura dati della lista di comandi
typedef struct struct_cmd {		
	char cmd;					// nome del comando
	char *arg;					// (eventuale) argomento del comando
	struct struct_cmd *next;	// puntatore al prossimo comando nella lista
} cmdT;

// struttura dati necessaria per il comando -w
typedef struct struct_cmd_w {
	char *Directory;
	int print;
	int maxN;
	int currN;
}	cmd_w_T;

static cmd_w_T *wT;								// variabile globale di appoggio per il comando -w
static char globalSocket[UNIX_PATH_MAX] = "";	// variabile globale che contiene il nome del socket

// funzioni operanti sulla lista di comandi
int addCmd(cmdT **cmdList, char cmd, char *arg);
int execute(cmdT *cmdList, int print);
int destroyCmdList(cmdT *cmdList);

// funzioni corrispondenti ai comandi
int cmd_f(char* socket);
int cmd_w(char *dirname, char *Directory, int print);
int cmd_w_aux(const char *ftw_filePath, const struct stat *ptr, int flag);
int cmd_W(const char *filelist, char *Directory, int print);
int cmd_r(const char *filelist, char *directory, int print);
int cmd_R(const char *numStr, char *directory, int print);
int cmd_l(const char *filelist, int print);
int cmd_u(const char *filelist, int print);
int cmd_c(const char *filelist, int print);

// chiude la connessione con il server al termine dell'esecuzione del client
void cleanup();

int main(int argc, char* argv[]) {
	atexit(cleanup);

	struct sigaction siga;

	if (argc < 2) {
		printf("Errore: specificare almeno un comando come argomento.\n");
		return -1;
	}

	// ignoro il segnale SIGPIPE
	memset(&siga, 0, sizeof(siga));
	siga.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &siga, NULL) == -1) {
		perror("sigaction.\n");
		return 1;
	} 

	int opt;
	int f = 0, h = 0, p = 0;	// variabili per tenere traccia dei comandi che possono essere utilizzati solo una volta
	char args[256];

	// creo la lista di comandi
	cmdT *cmdList = NULL;
	cmdList = calloc(1, sizeof(cmdT));
	
	// ciclo per il parsing dei comandi
	while ((opt = getopt(argc, argv, ":hpf:t:w:W:D:r:R:d:l:u:c:")) != -1) {
		switch (opt) {
			// stampa la lista di tutte le opzioni accettate dal client e termina immediatamente
			case 'h':
				if (h) {
					fprintf(stderr, "Il comando -h puo' essere usato solo una volta.\n");
					goto cleanup;
				}

				else {
					addCmd(&cmdList, opt, "");
					h = 1;
				}
				break;
			
			// specifica il nome del socket AF_UNIX a cui connettersi
			case 'f':
				if (f) {
					fprintf(stderr, "Il comando -f puo' essere usato solo una volta.\n");
					goto cleanup;
				}

				else {
					if (optarg && strlen(optarg) > 0 && optarg[0] == '-') {
						fprintf(stderr, "Il comando -f necessita di un argomento.\n");
						goto cleanup;
					}

					memset(args, '\0', 256);
					strncpy(args, optarg, strlen(optarg)+1);
					addCmd(&cmdList, opt, optarg);
					f = 1;
				}
				break;

			// abilita le stampe su stdout per ogni operazione
			case 'p':
				if (p) {
					fprintf(stderr, "Il comando -p puo' essere usato solo una volta.\n");
					goto cleanup;
				}

				else {
					printInfo(1);
					p = 1;
				}
				break;

			case 'w':	// scrivi sul server 'n' file contenuti in una cartella
				if (optarg && strlen(optarg) > 0 && optarg[0] == '-') {
					fprintf(stderr, "Il comando -w necessita di un argomento.\n");
					optind -= 1;
					goto cleanup;
				}

				// alloca la memoria per la variabile globale che contiene le informazioni necessarie per cmd_w
				if (wT == NULL) {
					if ((wT = (cmd_w_T*) calloc(1, sizeof(cmd_w_T))) == NULL) {
						perror("calloc wT");
						goto cleanup;
					}
				}

				memset(args, '\0', 256);
				strncpy(args, optarg, strlen(optarg)+1);
				addCmd(&cmdList, opt, args);
				break;

			case 't':	// tempo in millisecondi che intercorre tra l’invio di due richieste successive al server
			case 'W':	// lista di nomi di file da scrivere nel server, separati da virgole
			case 'r': 	// lista di nomi di file da leggere dal server, separati da virgole
 			case 'D':	// cartella dove vengono scritti i file che il server rimuove a seguito di capacity misses in scrittura
 			case 'd':	// cartella dove vengono scritti i file letti dal server
 			case 'l':	// lista di nomi di file sui quali acquisire la mutua esclusione
 			case 'u':	// lista di nomi di file sui quali rilasciare la mutua esclusione
 			case 'c':	// lista di nomi di file da rimuovere dallo storage del server
 				if (optarg && strlen(optarg) > 0 && optarg[0] == '-') {
					fprintf(stderr, "Il comando -%c necessita di un argomento.\n", optopt);
					goto cleanup;
				}

				memset(args, '\0', 256);
				strncpy(args, optarg, strlen(optarg)+1);
				addCmd(&cmdList, opt, args);
				break;

			case 'R':	// legge 'n' file qualsiasi attualmente memorizzati nel server
			 	if (optarg && strlen(optarg) > 0 && optarg[0] == '-') {
					optind -= 1;
					addCmd(&cmdList, opt, NULL);
				}

				else {
					memset(args, '\0', 256);
					strncpy(args, optarg, strlen(optarg)+1);
					addCmd(&cmdList, opt, args);
				}

				break;

			// comando senza argomento
			case ':':
				switch(optopt) {
					case 'R':
						addCmd(&cmdList, optopt, NULL);
						break;
					default:
						fprintf(stderr, "Il comando -%c necessita di un argomento.\n", optopt);
	            		goto cleanup;
				}
				break;

			// argomento non riconosciuto
			case '?': default:
					fprintf(stderr, "Comando non riconosciuto: -%c.\n", optopt);
					goto cleanup;
				break;
		}
	}

	// eseguo singolarmente tutti i comandi inseriti dall'utente
	if (execute(cmdList, p) == -1) {
		perror("execute");
		
		goto cleanup;
	}

	// libero la memoria
	cleanup:
		if (cmdList) {
			if (destroyCmdList(cmdList) == -1) {
				perror("destroyCmdList");

				if (wT) {
					free(wT);
				}

				return 1;
			}
		}
		
		if (wT) {
			if (wT->Directory) {
				free(wT->Directory);
			}
			free(wT);
		}

	return 0;
}

// aggiunge un comando in fondo alla lista
int addCmd(cmdT **cmdList, char cmd, char *arg) {
	// controllo la validita' degli argomenti 
	if (!cmdList) {
		errno = EINVAL;
		return -1;
	}

	// creo il nuovo elemento allocandone la memoria
	cmdT *new = NULL;
	if ((new = calloc(1, sizeof(cmdT))) == NULL) {
		perror("malloc cmdT");
		return -1;
	}

	new->cmd = cmd;

	if (arg) {
		if ((new->arg = malloc(CMDSIZE)) == NULL) {
		perror("malloc arg");
		free(new);
		return -1;
	}

	strncpy(new->arg, arg, strlen(arg)+1);
	}

	new->next = NULL;

	cmdT *tail = *cmdList;

	// se la lista era vuota, il comando aggiunto diventa il primo della lista
	if (*cmdList == NULL) {
		*cmdList = new;
	}

	// altrimenti, scorro tutta la lista e aggiungo il comando come ultimo elemento
	else {
		while (tail->next) {
			tail = tail->next;
		}

		tail->next = new;
	}

	return 0;
}

// esegue tutti i comandi nella lista, uno alla volta
int execute(cmdT *cmdList, int print) {
	// controllo la validita' dell'argomento
	if (!cmdList) {
		errno = EINVAL;
		return -1;
	}

	int ok = 1;			// esito del comando
	cmdT *temp = NULL;
	temp = cmdList;
	int w = 0;				// se = 1, l'ultimo comando letto e' una scrittura (-w o -W)
	int r = 0;				// se = 1, l'ultimo comando letto e' una lettura (-r o -R)
	char *Dir = NULL;		// cartella in cui scrivere i file espulsi dal server a seguito di capacity misses in scrittura
	char *dir = NULL;		// cartella in cui scrivere i file letti dal server

	// variabili per gestire il comando -t
	struct timespec tim1, tim2;
	tim1.tv_sec = 0;
	tim1.tv_nsec = 0;
	int sec = 0;
	double msec = 0;
	long num = 0;

	while(temp) {
		switch (temp->cmd) {
			// stampa il messaggio di aiuto
			case 'h':
				w = 0;
				printf("\n-h: stampa il presente messaggio d'aiuto.");
				printf("\n-f filename: connettiti al socket AF_UNIX 'filename'.");
				printf("\n-w dirname[,n=0]: invia al server 'n' file nella cartella 'dirname'. Se n=0 o non e' specificato, tenta di inviare tutti i file al server.");
				printf("\n-W file1[,file2]: scrivi sul server una lista di file, separati da virgole.");
				printf("\n-D dirname: specifica la cartella dove scrivere i file espulsi dal server in seguito a capacity misses.");
				printf("\n-r file1[,file2]: leggi dal server una lista di nomi di file, separati da virgole.");
				printf("\n-R [n=0]: leggi dal server 'n' file qualsiasi. Se n=0 o non e' specificato, leggi tutti i file presenti nel server per i quali si hanno i permessi necessari.");
				printf("\n-d dirname: cartella dove scrivere i file letti dal server con i comandi '-r' o '-R'.");
				printf("\n-t time: se specificato, fra le richieste successive al server vi sara' un'attesa di 'time' millisecondi.");
				printf("\n-l file1[,file2]: acquisisci la mutua esclusione su una lista di file, separati da virgole.");
				printf("\n-u file1[,file2]: rilascia la mutua esclusione su una lista di file, separati da virgole.");
				printf("\n-c file1[,file2]: rimuovi dal server una lista di file (se presenti), separati da virgole.");
				printf("\n-p: stampa sullo standard output le informazioni riguardo ogni operazione effettuata.");
				break;

			// connettiti al socket AF_UNIX specificato
			case 'f':
				w = 0;
				r = 0;
				strncpy(globalSocket, temp->arg, strlen(temp->arg)+1);

				if (cmd_f(temp->arg) != 0) {
					if (print) {
						perror("-f");
					}
					ok = 0;
				}

				else {
					ok = 1;
				}

				// controllo se devo stampare su stdout
				if (print) {
					printf("\nf - Connessione al socket: %s\t", globalSocket);

					if (ok) {
						printf("Esito: ok\n");
					}

					else {
						printf("Esito: errore\n");
					}
				}
				break;

			// imposta il tempo che intercorre tra l’invio di due richieste successive al server
			case 't':
				w = 0;
				r = 0;
				num = strtol(temp->arg, NULL, 0);

				// converto i msec inseriti dall'utente in secondi e nanosecondi per la nanosleep
				if (num >= 1000) {
					sec = num/1000;
					msec = num % 1000;
				}
				
				else {
					msec = num;
				}

				tim1.tv_sec = sec;
				tim1.tv_nsec = msec * 1000000;

				// controllo se devo stampare su stdout
				if (print) {
					printf("\nt - Tempo fra due richieste: %ld ms\tEsito: ok\n", num);
				}
				break;

			// scrivi sul server 'n' file contenuti in una cartella
			case 'w':
				free(Dir);
				Dir = NULL;
				w = 1;
				r = 0;

				// controllo se il prossimo comando specifica una directory nella quale salvare i file espulsi
				if (temp->next) {
					if ((temp->next)->cmd == 'D') {
						Dir = malloc(strlen((temp->next)->arg) +1);
						strncpy(Dir, (temp->next)->arg, strlen((temp->next)->arg)+1);

						if (setDirectory(Dir, 1) == -1) {
							if (print) {
								perror("-D");
							}
						}

						if (print) {
							printf("\nD - Cartella per le scritture del comando -w: %s\tEsito: ok\n", Dir);
						}
					}
				}

				cmd_w(temp->arg, Dir, print);

				break;

			// lista di nomi di file da scrivere nel server separati da virgole
			case 'W':
				free(Dir);
				Dir = NULL;
				w = 1;
				r = 0;

				// controllo se il prossimo comando specifica una directory nella quale salvare i file espulsi
				if (temp->next) {
					if ((temp->next)->cmd == 'D') {
						Dir = malloc(strlen((temp->next)->arg) +1);
						strncpy(Dir, (temp->next)->arg, strlen((temp->next)->arg)+1);

						if (setDirectory(Dir, 1) == -1) {
							if (print) {
								perror("-D");
							}
						}

						if (print) {
							printf("\nD - Cartella per le scritture del comando -W: %s\tEsito: ok\n", Dir);
						}
					}
				}

				cmd_W(temp->arg, Dir, print);
				
				break;

			// imposta la cartella dove scrivere i file che il server rimuove a seguito di capacity misses in scrittura
			case 'D':
				// se il comando precedentemente non era una scrittura (-w o -W), errore
				if (!w) {
					printf("\nErrore: l'opzione -D deve essere usata congiuntamente all'opzione -w o -W.\n");
				}

				w = 0;
				r = 0;

				break;

			// leggi dal server una lista di file, separati da virgole
			case 'r':
				free(dir);
				dir = NULL;
				r = 1;
				w = 0;

				// controllo se il prossimo comando specifica una directory nella quale salvare i file espulsi
				if (temp->next) {
					if ((temp->next)->cmd == 'd') {
						dir = malloc(strlen((temp->next)->arg) +1);
						strncpy(dir, (temp->next)->arg, strlen((temp->next)->arg)+1);

						if (setDirectory(dir, 0) == -1) {
							if (print) {
								perror("-d");
							}
						}

						if (print) {
							printf("\nd - Cartella per le letture del comando -r: %s\tEsito: ok\n", dir);
						}
					}
				}

				cmd_r(temp->arg, dir, print);

				break;

			// leggi 'n' file qualsiasi attualmente memorizzati nel server
			case 'R':
				free(dir);
				dir = NULL;
				r = 1;
				w = 0;

				// controllo se il prossimo comando specifica una directory nella quale salvare i file espulsi
				if (temp->next) {
					if ((temp->next)->cmd == 'd') {
						dir = malloc(strlen((temp->next)->arg) +1);
						strncpy(dir, (temp->next)->arg, strlen((temp->next)->arg)+1);

						if (setDirectory(dir, 0) == -1) {
							if (print) {
								perror("-d");
							}
						}

						if (print) {
							printf("\nd - Cartella per le letture del comando -R: %s\tEsito: ok\n", dir);
						}
					}
				}

				cmd_R(temp->arg, dir, print);

				break;

			// imposta la cartella dove scrivere i file letti dal server
			case 'd':
				// se il comando precedentemente non era una scrittura (-w o -W), errore
				if (!r) {
					printf("\nErrore: l'opzione -d deve essere usata congiuntamente all'opzione -r o -R.\n");
				}

				w = 0;
				r = 0;

				break;

			// acquisisci la mutua esclusione su uno o piu' file
			case 'l':
				w = 0;
				r = 0;

				cmd_l(temp->arg, print);
				break;

			// rilascia la mutua esclusione su uno o piu' file
			case 'u':
				w = 0;
				r = 0;

				cmd_u(temp->arg, print);
				break;

			// rimuovi dallo storage del server uno o piu' file
			case 'c':
				w = 0;
				r = 0;

				cmd_c(temp->arg, print);
				break;
		}

		temp = temp->next;

		// attendo prima di mandare la prossima richiesta al server
		if (temp) {
			nanosleep(&tim1, &tim2);
		}
	}

	if (print) {
		printf("\n");
	}

	// libero la memoria
	if (dir) {
		free(dir);
	}

	if (Dir) {
		free(Dir);
	}

	// chiudo la connessione con il server
	if (strcmp(globalSocket, "") != 0) {
		closeConnection(globalSocket);
		strncpy(globalSocket, "", 2);
	}

	return 0;
}

// distrugge la lista di comandi, liberandone la memoria
int destroyCmdList(cmdT *cmdList) {
	// controllo la validita' dell'argomento
	if (!cmdList) {
		errno = EINVAL;
		return -1;
	}

	cmdT *temp;

	// scorro tutta la lista e libero la memoria
	while (cmdList) {
		temp = cmdList;

		free(cmdList->arg);
		cmdList = cmdList->next;

		free(temp);
	}

	return 0;
}

// connette al socket specificato
int cmd_f(char* socket) {
	// controllo la validita' dell argomento
	if (!socket) {
		errno = EINVAL;
	}

	struct timespec ts;
	ts.tv_sec = 2;
	ts.tv_nsec = 0;

	errno = 0;
	if (openConnection(socket, 100, ts) != 0) {
		return -1;
	}

	return 0;
}

// scrive sul server 'n' file contenuti in una cartella
int cmd_w(char *dirname, char *Directory, int print) {
	if (!dirname) {
		errno = EINVAL;
		return -1;
	}

	wT->maxN = 0;
	wT->currN = 0;
	char *token = NULL, *save = NULL;
	token = strtok_r(dirname, ",", &save);
	char Dir[256] = "";
	int cnt = 0;
	
	while (token != NULL && cnt < 2) {
		if (cnt == 0) {
			strncpy(Dir, token, strlen(token)+1);
		}

		// controlla se nel comando e' specificata l'opzione 'n'
		else if (cnt == 1) {
			if (strlen(token) > 2) {
				if (token[0] == 'n' && token[1] == '=') {
					char *number = token+2;
					for (int i = 0; i < strlen(number); i++) {
						if (!isdigit(number[i])) {
							errno = EINVAL;
							return -1;
						}
					}

					int num = (int) strtol(number, NULL, 0);
					if (num != 0) {
						wT->maxN = num;
					}
				}

				else {
					errno = EINVAL;
					return -1;
				}
			}
		}

		token = strtok_r(NULL, ",", &save);
		cnt++;
	}

	if (print) {
		wT->print = 2;
	}

	else {
		wT->print = 0;
	}

	if (Directory) {
		wT->Directory = realloc(wT->Directory, strlen(Dir)+1);
		strncpy(wT->Directory, Directory, strlen(Dir)+1);
	}

	else {
		free(wT->Directory);
	}

	if (print) {
		printf("\nw - Scrivo i seguenti file sul server:");
		fflush(stdout);
	}

	// invoca la funzione che visita ricorsivamente la cartella specificata e chiama la funzione ausiliaria su ogni file trovato
	ftw(Dir, cmd_w_aux, FOPEN_MAX);

	return 0;
}

// funzione ausiliaria del comando -w. Viene passata come argomento della funzione ftw
int cmd_w_aux(const char *ftw_filePath, const struct stat *ptr, int flag) {
	// controllo la validita' degli argomenti
	if (!ftw_filePath || !ptr) {
		errno = EINVAL;
		return -1;
	}

	if (flag != FTW_F) {
		return 0;
	}

	wT->currN++;

	if (wT->maxN != 0 && wT->currN > wT->maxN) {
		return -1;
	}

	cmd_W(ftw_filePath, wT->Directory, wT->print);

	return 0;
}

// scrive una lista di file sullo storage del server
int cmd_W(const char *filelist, char *Directory, int print) {
	// controllo la validita' degli argomenti
	if (!filelist) {
		errno = EINVAL;
		return -1;
	}

	// parso la lista di file da scrivere
	char *token = NULL, *save = NULL;
	char tokenList[256] = "";
	strncpy(tokenList, filelist, strlen(filelist)+1);
	token = strtok_r(tokenList, ",", &save);
	int ok = 1;

	if (print == 1) {
		printf("\nW - Scrivo i seguenti file sul server:\n");
		fflush(stdout);
	}

	// per ogni file nella lista...
	while (token != NULL) {
		ok = 1;
		int opened = 0;
		if (print != 0) {
			printf("\n%-20s", token); 
			fflush(stdout);
		}

		// creo il file in modalita' locked
		if (openFile(token, O_CREATE | O_LOCK) == -1) {
			ok = 0;
		}

		// se il file e' stato creato con successo...
		else {
			opened = 1;
		}
		
		// scrivo il contenuto del file sul server
		if (opened && writeFile(token, Directory) == -1) {
			ok = 0;
		}

		// se il file e' stato creato ma la scrittura non e' riuscita, elimina il file vuoto appena creato
		if (opened && !ok) {
			removeFile(token);
		}

		// chiudo il file
		else if (opened && closeFile(token) == -1) {
			ok = 0;
		}

		if (print != 0) {
			printf("Esito: "); 

			if (ok) {
				printf("ok");
			}
			
			else {
				printf("errore");

				if (print == 1) {
					perror("-W");
				}

				else {
					perror("-w");
				}
			}

			printf("\n");
			fflush(stdout);
		}

		token = strtok_r(NULL, ",", &save);
	}

	if (ok) {
		return 0;
	}

	else {
		return -1;
	}
}

// legge dal server una lista di file, separati da virgole
int cmd_r(const char *filelist, char *directory, int print) {
	if (!filelist) {
		errno = EINVAL;
		return -1;
	}

	// parso la lista di file da leggere
	char *token = NULL, *save = NULL;
	char tokenList[256] = "";
	strncpy(tokenList, filelist, strlen(filelist)+1);
	token = strtok_r(tokenList, ",", &save);
	int ok = 1;

	if (print) {
		printf("\nr - Leggo i seguenti file dal server:\n");
		fflush(stdout);
	}

	// per ogni file nella lista...
	while (token != NULL) {
		ok = 1;
		int opened = 0;
		if (print != 0) {
			printf("\n%-20s", token); 
			fflush(stdout);
		}

		// apro il file
		if (openFile(token, 0) == -1) {
			ok = 0;
		}

		// se il file e' stato aperto con successo...
		else {
			opened = 1;
		}
		
		void *buf = NULL;
		size_t size = -1;

		// leggo il contenuto del file
		printInfo(0);
		if (ok && readFile(token, &buf, &size) == -1) {
			ok = 0;
		}
		if (print) {
			printInfo(1);
		}

		if (ok && print) {
			printf("Dimensione: %zu B\t", size);
			fflush(stdout);
		}

		if (buf) {
			free(buf);
		}

		// chiudo il file
		if (opened && closeFile(token) == -1) {
			ok = 0;
		}

		if (print) {
			printf("Esito: "); 

			if (ok) {
				printf("ok");
			}
			
			else {
				printf("errore");
				perror("-r");
			}
		}

		if (print) {
			printf("\n");
			fflush(stdout);
		}

		token = strtok_r(NULL, ",", &save);
	}

	if (print && directory) {
		printf("\nI file letti sono stati scritti nella cartella %s.\n.", directory);
		fflush(stdout);
	}

	else if (print) {
		printf("\nI file letti non sono stati memorizzati sul disco.\n");
		fflush(stdout);
	}

	if (ok) {
		return 0;
	}

	else {
		return -1;
	}
}

// leggi 'n' file qualsiasi attualmente memorizzati nel server
int cmd_R(const char *numStr, char *directory, int print) {
	int n = 0;

	if (print) {
		printf("\nR - Leggo i seguenti file dal server:\n");
		fflush(stdout);
	}

	if (numStr) {
		// parso l'argomento
		char *token = NULL, *save = NULL;
		char tokenList[256] = "";
		strncpy(tokenList, numStr, strlen(numStr)+1);
		token = strtok_r(tokenList, ",", &save);

		// controllo se nel comando e' stato specificato il numero massimo di file da leggere
		if (token) {
			if (strlen(token) > 2) {
				if (token[0] == 'n' && token[1] == '=') {
					char *number = token+2;
					int valid = 1;

					for (int i = 0; i < strlen(number); i++) {
						if (!isdigit(number[i])) {
							valid = 0;
							break;
						}
					}

					if (valid) {
						int num = (int) strtol(number, NULL, 0);
						if (num > 0) {
							n = num;
						}
					}
				}
			}
		}
	}

	if (readNFiles(n, directory) == -1) {
		return -1;
	}

	if (print && directory) {
		printf("I file letti sono stati scritti nella cartella %s.\n", directory);
	}

	else if (print) {
		printf("I file letti non sono stati memorizzati sul disco.\n");
	}

	return 0;
}

// acquisisci la mutua esclusione su uno o piu' file
int cmd_l(const char *filelist, int print) {
	// controllo la validita' dell'argomento
	if (!filelist) {
		errno = EINVAL;
		return -1;
	}

	// parso la lista di file da leggere
	char *token = NULL, *save = NULL;
	char tokenList[256] = "";
	strncpy(tokenList, filelist, strlen(filelist)+1);
	token = strtok_r(tokenList, ",", &save);

	if (print) {
		printf("\nl - Acquisisco la mutua esclusione sui seguenti file:\n");
		fflush(stdout);
	}

	// per ogni file nella lista...
	while (token != NULL) {
		if (print != 0) {
			printf("\n%-20s", token); 
			fflush(stdout);
		}

		if (lockFile(token) == -1) {
			if (print) {
				printf("Esito: errore");
				perror("-l"); 
			}
		}

		else {
			if (print) {
				printf("Esito: ok"); 
			}
		}

		if (print) {
			printf("\n");
			fflush(stdout);
		}

		token = strtok_r(NULL, ",", &save);
	}

	return 0;
}

// rilascia la mutua esclusione su uno o piu' file
int cmd_u(const char *filelist, int print) {
	// controllo la validita' dell'argomento
	if (!filelist) {
		errno = EINVAL;
		return -1;
	}

	// parso la lista di file da leggere
	char *token = NULL, *save = NULL;
	char tokenList[256] = "";
	strncpy(tokenList, filelist, strlen(filelist)+1);
	token = strtok_r(tokenList, ",", &save);

	if (print) {
		printf("\nu - Rilascio la mutua esclusione sui seguenti file:\n");
		fflush(stdout);
	}

	// per ogni file nella lista...
	while (token != NULL) {
		if (print != 0) {
			printf("\n%-20s", token); 
			fflush(stdout);
		}

		if (unlockFile(token) == -1) {
			if (print) {
				printf("Esito: errore");
				perror("-l"); 
			}
		}

		else {
			if (print) {
				printf("Esito: ok"); 
			}
		}

		if (print) {
			printf("\n");
			fflush(stdout);
		}

		token = strtok_r(NULL, ",", &save);
	}

	return 0;
}

// cancella uno o piu' file dallo storage del server
int cmd_c(const char *filelist, int print) {
	// controllo la validita' dell'argomento
	if (!filelist) {
		errno = EINVAL;
		return -1;
	}

	// parso la lista di file da cancellare
	char *token = NULL, *save = NULL;
	char tokenList[256] = "";
	strncpy(tokenList, filelist, strlen(filelist)+1);
	token = strtok_r(tokenList, ",", &save);

	if (print) {
		printf("\nc - Cancello i seguenti file dal server:\n");
		fflush(stdout);
	}

	// per ogni file nella lista...
	while (token != NULL) {
		if (print != 0) {
			printf("\n%-20s", token); 
			fflush(stdout);
		}

		// acquisisci la lock, altrimenti la rimozione del file non puo' avvenire
		if (lockFile(token) == -1) {
			if (print) {
				printf("Esito: errore");
				perror("-c"); 
			}
		}

		else {
			if (removeFile(token) == -1) {
				if (print) {
					printf("Esito: errore");
					perror("-c"); 
				}
			}

			else if (print) {
				printf("Esito: ok"); 
			}
		}

		if (print) {
			printf("\n");
			fflush(stdout);
		}

		token = strtok_r(NULL, ",", &save);
	}

	return 0;
}

// chiudi la connessione con il server
void cleanup() {
	if (strcmp(globalSocket, "") != 0) {
		closeConnection(globalSocket);
	}
}