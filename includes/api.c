#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>

#include <api.h>

#define UNIX_PATH_MAX 108 
#define SOCKNAME_MAX 100

static char socketName[SOCKNAME_MAX];
static int fd_skt;

int openConnection(const char* sockname, int msec, const struct timespec abstime) {
	struct sockaddr_un sa;
	struct timespec ts;
	ts.tv_sec = msec/1000;
	ts.tv_nsec = (msec % 1000) * 1000000;
	struct timeval t1, t2;
	double start, end;
	double elapsedTime;

	// il tempo passato come parametro non può essere negativo
	if (msec <= 0) {
		errno = EINVAL;
		return -1;
	}

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

	return 0;
}

int closeConnection(const char* sockname) {
	// se il nome del socket non corrisponde a quello nella variabile globale, errore
	if (strcmp(socketName, sockname) != 0) {
		errno = EINVAL;
		return -1;
	}

	// chiudi la connessione
	if (close(fd_skt) == -1) {
		errno = EREMOTEIO;
		return -1;
	}

	return 0;
}