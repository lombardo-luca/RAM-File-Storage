#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>

#include <api.h>
#include <partialIO.h>

#define UNIX_PATH_MAX 108 
#define SOCKNAME_MAX 100
#define CMDSIZE 256
#define BUFSIZE 512

static char socketName[SOCKNAME_MAX] = "";	// nome del socket al quale il client e' connesso
static int fd_skt;							// file descriptor per le operazioni di lettura e scrittura sul server

/**
 * lastOp = 1 se e solo se l'ultima operazione e' stata una openFile terminata con successo.
 * Se lastOp = 1, openedFile contiene il filepath del file attualmente aperto. 
 */
//static int lastOp = 0;
static char openedFile[256] = "";	

int openConnection(const char* sockname, int msec, const struct timespec abstime) {
	// controllo la validità dei parametri
	if (!sockname || msec <= 0) {
		errno = EINVAL;
		return -1;
	}

	struct sockaddr_un sa;
	struct timespec ts;
	ts.tv_sec = msec/1000;
	ts.tv_nsec = (msec % 1000) * 1000000;
	struct timeval t1, t2;
	double start, end;
	double elapsedTime;

	strncpy(sa.sun_path, sockname, UNIX_PATH_MAX);
	sa.sun_family = AF_UNIX;

	if ((fd_skt = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		errno = EINVAL;
		perror("socket");
		return -1;
	}

	gettimeofday(&t1, NULL);	// avvio il timer
	start = (double) t1.tv_sec * 1000000000 + (double) t1.tv_usec * 1000;
	while (connect(fd_skt, (struct sockaddr*) &sa, sizeof(sa)) == -1) {
		// controllo quanto tempo è passato
		gettimeofday(&t2, NULL);
		end = (double) t2.tv_sec * 1000000000 + (double) t2.tv_usec * 1000;
		elapsedTime = (end - start);

		// se è passato troppo tempo, errore
		if (elapsedTime > (abstime.tv_sec * 1000000000) + abstime.tv_nsec) {
			errno = ETIMEDOUT;
			return -1;
		}

		// attendi prima di riprovare
		nanosleep(&ts, &ts);
	}

	// copio il nome del socket nella variabile globale
	strncpy(socketName, sockname, SOCKNAME_MAX);

	//lastOp = 0;
	return 0;
}

int closeConnection(const char* sockname) {
	// se il nome del socket non corrisponde a quello nella variabile globale, errore
	if (!sockname || strcmp(socketName, sockname) != 0) {
		errno = EINVAL;
		return -1;
	}

	// chiudi la connessione
	if (close(fd_skt) == -1) {
		errno = EREMOTEIO;
		return -1;
	}

	// resetto la variabile globale che mantiene il nome del socket
	strncpy(socketName, "", SOCKNAME_MAX);

	//lastOp = 0;
	return 0;
}

int openFile(const char* pathname, int flags) {
	// controllo la validità degli argomenti
	if (!pathname || strlen(pathname) >= (CMDSIZE-10) || flags < 0 || flags > 3) {
		errno = EINVAL;
		return -1;
	}

	// controllo che il client sia effettivamente connesso al server
	if (strcmp(socketName, "") == 0) {
		errno = ENOTCONN;
		return -1;
	}

	char cmd[CMDSIZE] = "";

	// preparo il comando da inviare al server in formato openFile:pathname:flags
	memset(cmd, '\0', CMDSIZE);
	strncpy(cmd, "openFile:", 10);
	strncat(cmd, pathname, strlen(pathname) + 1);
	strncat(cmd, ":", 2);
	char flagStr[2];
	snprintf(flagStr, 2, "%d", flags);
	strncat(cmd, flagStr, strlen(flagStr) + 1);

	// invio il comando al server
	printf("openFile: invio %s!\n", cmd);

	if (writen(fd_skt, cmd, CMDSIZE) == -1) {
		errno = EREMOTEIO;
		return -1;
	}

	// ricevo la risposta dal server
	void *buf = malloc(BUFSIZE);
	int r = readn(fd_skt, buf, 3);
	if (r == -1 || r == 0) {
		free(buf);
		errno = EREMOTEIO;
		return -1;
	}

	char res[3];
	memcpy(res, buf, 3);

	printf("openFile: ho ricevuto: %s!\n", res);

	// se il server mi ha risposto con un errore...
	if (strcmp(res, "er") == 0) {
		memset(buf, 0, BUFSIZE);

		// ...ricevo l'errno
		if ((readn(fd_skt, buf, sizeof(int))) == -1) {
			free(buf);
			errno = EREMOTEIO;
			return -1;
		}

		// setto il mio errno uguale a quello che ho ricevuto dal server
		memcpy(&errno, buf, sizeof(int));
		free(buf);
		return -1;
	}

	// se il server ha dovuto espellere un file per fare spazio, lo ricevo
	else if (strcmp(res, "es") == 0) {

		if (receiveFile(NULL) == -1) {
			free(buf);
			return -1;
		}
	}

	free(buf);
	
	// tengo traccia del file aperto
	//lastOp = 1;
	strncpy(openedFile, pathname, strlen(pathname)+1);

	return 0;
}

int writeFile(const char* pathname, const char* dirname) {
	// controllo la validità degli argomenti
	if (!pathname || strlen(pathname) >= BUFSIZE) {
		errno = EINVAL;
		return -1;
	}

	// se il file su cui si vuole scrivere non e' stato precedentemente creato o aperto, errore
	if (strcmp(openedFile, pathname) != 0) {
		errno = EPERM;
		return -1;
	}

	void *buf = malloc(BUFSIZE);
	void *content = malloc(BUFSIZE);
	size_t size;
	char cmd[CMDSIZE] = "";

	// leggo il file da scrivere sul server
	FILE* ipf = NULL;
	if ((ipf = fopen(pathname, "rb")) == NULL) {
		perror("fopen");
		free(buf);
		return -1;
	}

	size = fread(content, 1, BUFSIZE, ipf);
	printf("writeFile: ho letto %ld bytes\n", size);
	fclose(ipf);

	// preparo il comando da inviare al server in formato writeFile:pathname:size
	memset(cmd, '\0', CMDSIZE);
	strncpy(cmd, "writeFile:", 11);
	strncat(cmd, pathname, strlen(pathname) + 1);
	strncat(cmd, ":", 2);
	char sizeStr[BUFSIZE];
	snprintf(sizeStr, BUFSIZE, "%ld", size);
	strncat(cmd, sizeStr, strlen(sizeStr) + 1);

	// invio il comando al server
	printf("writeFile: invio %s!\n", cmd);

	if (writen(fd_skt, cmd, CMDSIZE) == -1) {
		errno = EREMOTEIO;
		return -1;
	}

	// ricevo la risposta dal server
	int r = readn(fd_skt, buf, 3);
	if (r == -1 || r == 0) {
		free(buf);
		errno = EREMOTEIO;
		return -1;
	}

	char res[3];
	memcpy(res, buf, 3);

	printf("writeFile: ho ricevuto: %s!\n", res);

	// TO-DO

	free(buf);
	free(content);
	return 0;
}

// funzione ausiliaria che riceve un file dal server
int receiveFile(char *dirname) {
	void *buf = malloc(BUFSIZE);
	memset(buf, 0, BUFSIZE);
	char filepath[BUFSIZE];
	size_t size;
	void *content = NULL;

	// ricevo prima il filepath...
	if ((readn(fd_skt, buf, BUFSIZE)) == -1) {
		free(buf);
		errno = EREMOTEIO;
		return -1;
	}

	strncpy(filepath, buf, BUFSIZE);
	printf("Ho ricevuto filepath = %s\n", filepath);

	// ...poi la dimensione del file...
	memset(buf, 0, BUFSIZE);
	if ((readn(fd_skt, buf, sizeof(size_t))) == -1) {
		free(buf);
		errno = EREMOTEIO;
		return -1;
	}

	memcpy(&size, buf, sizeof(size_t));
	printf("Ho ricevuto size = %ld\n", size);

	content = malloc(size);

	// ...e infine il contenuto
	if ((readn(fd_skt, content, size)) == -1) {
		free(buf);
		free(content);
		errno = EREMOTEIO;
		return -1;
	}

	printf("Ho ricevuto il contenuto!\n");

	printf("TEST SCRITTURA SU FILE\n");
    FILE* opf = fopen("testscritturaCLIENT", "w");
    printf("Voglio scrivere %ld bytes\n", size);
    fwrite(content, 1, size, opf);
    fclose(opf);
    printf("FINE TEST SCRITTURA SU FILE\n");

	free(content);
	free(buf);

	return 0;
}