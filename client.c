#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h> 	
#include <errno.h>
#include <string.h>
#include <time.h>

#include <api.h>
#include <partialIO.h>

#define UNIX_PATH_MAX 108 
#define SOCKNAME "./mysock"
#define CMDSIZE 256
#define BUFSIZE 256

int cmd_f(char* socket);

int main(int argc, char* argv[]) {
	//char buf[BUFSIZE] = "";
	//char cmd[CMDSIZE] = "";
	char sock[256];

	printf("File Storage Client avviato.\n");
	fflush(stdout);

	if (argc < 2) {
		printf("Usage: ecc... Nessun argomento! TO-DO\n");
		return -1;
	}

	strncpy(sock, SOCKNAME, 9);

	int opt;
	int f = 0;
	// ciclo per il parsing dei comandi
	while ((opt = getopt(argc, argv, "hf:")) != -1) {
		switch (opt) {
			case 'h':
				printf("Ecco tutte le opzioni accettate ecc... TO-DO\n");
				return 0;
			case 'f':
				if (f) {
					printf("Il comando -f puo' essere usato solo una volta.\n");
					break;
				}

				printf("DEBUG Nome socket: %s\n", optarg);
				strncpy(sock, optarg, strlen(optarg)+1);
				f = 1;

				if (cmd_f(optarg) != 0) {
					printf("Errore nell'esecuzione del comando -f.\n");
				}

				break;
			case '?': default:
				break;
		}
	}

	if (openFile("fileNuovo.txt", 1) == -1) {
		perror("openFile");
		return -1;
	}

	/*
	strncpy(sa.sun_path, SOCKNAME, UNIX_PATH_MAX);
	sa.sun_family = AF_UNIX;

	fd_skt = socket(AF_UNIX, SOCK_STREAM, 0);
	while (connect(fd_skt, (struct sockaddr*) &sa, sizeof(sa)) == -1 ) {
		if ((errno == ENOENT)) {
			sleep(1); 
		}
	
		else {
			return 1;
		} 
	}

	memset(cmd, '\0', CMDSIZE);
	while (fgets(cmd, sizeof(cmd), stdin)) {
		writen(fd_skt, cmd, strlen(cmd));

		int cmp = 0;
		cmp = strcmp(cmd, "quit\n");

		if (!cmp) {
			close(fd_skt);	// chiudo la connessione al server ed esco dal ciclos
			break;
		}

		else {
			readn(fd_skt, buf, BUFSIZE);
			printf("Risultato: %s\n", buf);
		}

		memset(cmd, '\0', CMDSIZE);
	}
	*/

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