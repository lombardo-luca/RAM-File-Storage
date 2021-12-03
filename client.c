#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h> 	
#include <errno.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#include <api.h>
#include <partialIO.h>

#define UNIX_PATH_MAX 108 
//#define SOCKNAME "./mysock"
#define CMDSIZE 256
#define BUFSIZE 10000 	// 10KB

typedef struct struct_cmd {		
	char cmd;					// nome del comando
	char *arg;					// (eventuale) argomento del comando
	struct struct_cmd *next;	// puntatore al prossimo comando nella lista
} cmdT;

int addCmd(cmdT **cmdList, char cmd, char *arg);
int execute(cmdT *cmdList);
int destroyCmdList(cmdT *cmdList);
int cmd_f(char* socket);
int cmd_W(char *filelist, char *Directory);

void testOpenFile() {
	printf("INIZIO TEST OPENFILE\n");
	printf("Usare maxFiles = 3, un file gia' dentro il server\n");
	printf("Creo un file non lockato. Dovrebbe dare OK.\n");
	if (openFile("fileNONLOCKED", 1) == -1) {
		perror("openFile");
	}

	printf("Creo un file lockato. Dovrebbe dare OK.\n");
	if (openFile("fileLOCKED", 3) == -1) {
		perror("openFile");
	}

	printf("Creo un file gia' esistente. Dovrebbe dare ER.\n");
	if (openFile("test/file2.txt", 1) == -1) {
		perror("openFile");
	}

	printf("Apro un file che esiste ma e' lockato da un client diverso. Dovrebbe dare ER.\n");
	if (openFile("test/file2.txt", 2) == -1) {
		perror("openFile");
	}

	printf("Creo un file ma la coda e' piena. Dovrebbe dare ES.\n");
	if (openFile("fileLOCKED2", 3) == -1) {
		perror("openFile");
	}

	printf("Apro un file che esiste e non e' lockato. Dovrebbe dare OK.\n");
	if (openFile("fileNONLOCKED", 2) == -1) {
		perror("openFile");
	}

	printf("Creo un altro file ma la coda e' piena. Dovrebbe dare ES.\n");
	if (openFile("fileLOCKED3", 3) == -1) {
		perror("openFile");
	}

	printf("Creo l'ultimo file ma la coda e' piena. Dovrebbe dare ES.\n");
	if (openFile("fileLOCKED4", 3) == -1) {
		perror("openFile");
	}

	printf("FINE TEST OPENFILE\n");
}

void testWriteFile() {
	printf("INIZIO TEST WRITEFILE\n");
	printf("Usare maxFiles = 2, maxSize = 5, due file gia' dentro il server\n");
	printf("Scrivo un file che non ho aperto. Mi aspetto: perror\n");
	if (writeFile("test/filepesante", NULL) == -1) {
		perror("writeFile");
	}

	printf("Creo un file non lockato. Mi aspetto: ES\n");
	if (openFile("test/fileleggero", 1) == -1) {
		perror("openFile");
	}

	printf("Scrivo un file creato e aperto da me. Mi aspetto: OK\n");
	if (writeFile("test/fileleggero", NULL) == -1) {
		perror("writeFile");
	}

	printf("Creo un altro file prima di chiudere l'altro. Mi aspetto: perror\n");
	if (openFile("test/filepesante", 3) == -1) {
		perror("openFile");
	}
	printf("FINE TEST WRITEFILE\n");
}

int main(int argc, char* argv[]) {
	struct sigaction siga;

	printf("File Storage Client avviato.\n");
	fflush(stdout);

	if (argc < 2) {
		printf("Usage: ecc... Nessun argomento! TO-DO\n");
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
	int f = 0, h = 0;
	char args[256];

	// creo la lista di comandi
	cmdT *cmdList = NULL;
	cmdList = calloc(1, sizeof(cmdT));
	
	// ciclo per il parsing dei comandi
	while ((opt = getopt(argc, argv, "hf:t:W:D:")) != -1) {
		switch (opt) {
			// stampa la lista di tutte le opzioni accettate dal client e termina immediatamente
			case 'h':
				if (h) {
					printf("Il comando -h puo' essere usato solo una volta.\n");
					break;
				}

				addCmd(&cmdList, opt, "");
				h = 1;
				break;
			
			// specifica il nome del socket AF_UNIX a cui connettersi
			case 'f':
				if (f) {
					printf("Il comando -f puo' essere usato solo una volta.\n");
					break;
				}

				addCmd(&cmdList, opt, optarg);
				f = 1;
				break;

			case 't':	// tempo in millisecondi che intercorre tra l’invio di due richieste successive al server
			case 'W':	// lista di nomi di file da scrivere nel server separati da virgole
			case 'D':	// cartella dove vengono scritti i file che il server rimuove a seguito di capacity misses in scrittura
				memset(args, '\0', 256);
				strncpy(args, optarg, strlen(optarg)+1);
				addCmd(&cmdList, opt, args);
				break;

			// argomento non riconosciuto
			case '?': default:
				break;
		}
	}

	if (execute(cmdList) == -1) {
		perror("execute");
		return 1;
	}

	if (destroyCmdList(cmdList) == -1) {
		perror("destroyCmdList");
		return 1;
	}

	//testOpenFile();
	//testWriteFile();

	return 0;
}

// aggiunge un comando in fondo alla lista
int addCmd(cmdT **cmdList, char cmd, char *arg) {
	// controllo la validita' degli argomenti 
	if (!cmdList || !arg) {
		errno = EINVAL;
		return -1;
	}

	// creo il nuovo elemento allocandone la memoria
	cmdT *new = NULL;
	if ((new = malloc(sizeof(cmdT))) == NULL) {
		perror("malloc cmdT");
		return -1;
	}

	new->cmd = cmd;

	if ((new->arg = malloc(CMDSIZE)) == NULL) {
		perror("malloc arg");
		free(new);
		return -1;
	}

	strncpy(new->arg, arg, strlen(arg)+1);

	new->next = NULL;

	cmdT *tail = *cmdList;

	// se la lista era vuota, il comando aggiunto diventa il primo della lista
	if (*cmdList == NULL) {
		*cmdList = tail;
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
int execute(cmdT *cmdList) {
	// controllo la validita' dell'argomento
	if (!cmdList) {
		errno = EINVAL;
		return -1;
	}

	cmdT *temp = NULL;
	temp = cmdList;
	char sock[256];		// socket che viene impostato con il comando -f

	// variabili per gestire il comando -t
	struct timespec tim1, tim2;
	tim1.tv_sec = 0;
	tim1.tv_nsec = 0;
	int sec = 0;
	double msec = 0;
	long num = 0;

	int w = 0;	// se = 1, l'ultimo comando letto e' una scrittura (-w o -W)

	while(temp) {
		switch (temp->cmd) {
			// stampa il messaggio di aiuto
			case 'h':
				w = 0;
				printf("Ecco tutte le opzioni accettate ecc... TO-DO\n");
				break;

			// connettiti al socket AF_UNIX specificato
			case 'f':
				w = 0;
				printf("DEBUG: Nome socket: %s\n", temp->arg);
				strncpy(sock, temp->arg, strlen(temp->arg)+1);

				if (cmd_f(temp->arg) != 0) {
					perror("cmd_f");
				}

				break;

			// imposto il tempo che intercorre tra l’invio di due richieste successive al server
			case 't':
				w = 0;
				num = strtol(temp->arg, NULL, 0);

				// converto i msec inseriti dall'utente in secondi e nanosecondi per la nanosleep
				if (num > 1000) {
					sec = num/1000;
					msec = num % 1000;
				}
				
				else {
					msec = num;
				}

				tim1.tv_sec = sec;
				tim1.tv_nsec = msec * 1000000;
				printf("DEBUG: secondi = %ld nanosec = %ld\n", tim1.tv_sec, tim1.tv_nsec);
				break;

			// lista di nomi di file da scrivere nel server separati da virgole
			case 'W':
				printf("DEBUG: lista file da scrivere: %s\n", temp->arg);
				char Dir[256] = "";
				w = 1;

				// controllo se il prossimo comando specifica una directory nella quale salvare i file espulsi
				if (temp->next) {
					if ((temp->next)->cmd == 'D') {
						strncpy(Dir, (temp->next)->arg, strlen((temp->next)->arg)+1);
					}
				}

				if (cmd_W(temp->arg, Dir) != 0) {
					perror("cmd_w");
				}

				break;

			// imposta la cartella dove scrivere i file che il server rimuove a seguito di capacity misses in scrittura
			case 'D':
				// se il comando precedentemente non era una scrittura (-w o -W), errore
				if (!w) {
					printf("Errore: l'opzione -D deve essere usata congiuntamente all'opzione -w o -W.\n");
				}

				w = 0;

				break;
		}

		temp = temp->next;

		// attendo prima di mandare la prossima richiesta al server
		nanosleep(&tim1, &tim2);
	}

	return 0;
}

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

// connettiti al socket specificato
int cmd_f(char* socket) {
	if (!socket) {
		errno = EINVAL;
	}

	struct timespec ts;
	ts.tv_sec = 2;
	ts.tv_nsec = 550;

	errno = 0;
	if (openConnection(socket, 100, ts) != 0) {
		perror("openConnection");
		return -1;
	}

	else {
		printf("cmd_f: connessione avvenuta con successo.\n");
	}

	return 0;
}

// scrivi una lista di file sul server
int cmd_W(char *filelist, char *Directory) {
	if (!filelist || !Directory) {
		errno = EINVAL;
		return -1;
	}

	// parso la lista di file da scrivere
	char *token = NULL, *save = NULL;
	token = strtok_r(filelist, ",", &save);

	// per ogni file nella lista...
	while (token != NULL) {
		printf("cmd_w: file: %s\n", token);
		// apro il file
		if (openFile(token, 1) == -1) {
			perror("openFile");

			return -1;
		}

		// scrivo il contenuto del file sul server
		if (writeFile(token, Directory) == -1) {
			perror("writeFile");

			return -1;
		}

		// chiudo il file
		if (closeFile(token) == -1) {
			perror("closeFile");

			return -1;
		}

		token = strtok_r(NULL, ",", &save);
	}

	return 0;
}