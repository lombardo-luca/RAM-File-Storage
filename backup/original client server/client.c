#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h> 	
#include <errno.h>
#include <string.h>

#include <partialIO.h>

#define UNIX_PATH_MAX 108 
#define SOCKNAME "./mysock"
#define CMDSIZE 256
#define BUFSIZE 256

int main(int argc, char* argv[]) {
	int fd_skt;
	char buf[BUFSIZE] = "";
	struct sockaddr_un sa;
	char cmd[CMDSIZE] = "";

	printf("File Storage Client avviato.\n");
	fflush(stdout);

	strncpy(sa.sun_path, SOCKNAME, UNIX_PATH_MAX);
	sa.sun_family = AF_UNIX;

	fd_skt = socket(AF_UNIX, SOCK_STREAM, 0);
	while (connect(fd_skt, (struct sockaddr*) &sa, sizeof(sa)) == -1 ) {
		if ((errno == ENOENT)) {
			sleep(1); 	/* sock non esiste */
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
			close(fd_skt);	// chiudo la connessione al server ed esco dal ciclo
			break;
		}

		else {
			readn(fd_skt, buf, BUFSIZE);
			printf("Risultato: %s\n", buf);
		}

		memset(cmd, '\0', CMDSIZE);
	}

	return 0;
}