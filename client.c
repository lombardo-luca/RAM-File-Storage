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
#define SOCKNAME "./mysock"
#define CMDSIZE 256
#define BUFSIZE 10000 	// 10KB

int cmd_f(char* socket);
int cmd_W(char *filelist);

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
	char sock[256];
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

	strncpy(sock, SOCKNAME, 9);

	struct timespec tim1, tim2;
	tim1.tv_sec = 0;
	tim1.tv_nsec = 0;
	int opt;
	int f = 0, sec = 0;
	double msec = 0;
	long num = 0;
	// ciclo per il parsing dei comandi
	while ((opt = getopt(argc, argv, "hf:t:W:")) != -1) {
		switch (opt) {
			// stampa la lista di tutte le opzioni accettate dal client e termina immediatamente
			case 'h':
				printf("Ecco tutte le opzioni accettate ecc... TO-DO\n");
				return 0;

			// specifica il nome del socket AF_UNIX a cui connettersi
			case 'f':
				if (f) {
					printf("Il comando -f puo' essere usato solo una volta.\n");
					break;
				}

				printf("DEBUG: Nome socket: %s\n", optarg);
				strncpy(sock, optarg, strlen(optarg)+1);
				f = 1;

				if (cmd_f(optarg) != 0) {
					printf("Errore nell'esecuzione del comando -f.\n");
				}

				break;

			// tempo in millisecondi che intercorre tra l’invio di due richieste successive al server
			case 't':
				num = strtol(optarg, NULL, 0);

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
				printf("DEBUG: lista file da scrivere: %s\n", optarg);

				if (cmd_W(optarg) != 0) {
					printf("Errore nell'esecuzione del comando -W\n");
				}

				break;

			// argomento non riconosciuto
			case '?': default:
				break;
		}

		// attento prima di mandare la prossima richiesta al server
		nanosleep(&tim1, &tim2);
	}

	//testOpenFile();
	testWriteFile();

	return 0;
}

// connettiti al socket specificato
int cmd_f(char* socket) {
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

int cmd_W(char *filelist) {
	// parso la lista di file da scrivere
	char *token = NULL, *save = NULL;
	token = strtok_r(filelist, ",", &save);

	while (token != NULL) {
		printf("cmd_w: file: %s\n", token);
		if (openFile(token, 1) == -1) {
			perror("openFile");
			return -1;
		}

		if (writeFile(token, NULL) == -1) {
			perror("writeFile");
			return -1;
		}

		token = strtok_r(NULL, ",", &save);
	}

	// TO-DO

	return 0;
}