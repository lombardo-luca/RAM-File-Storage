#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <api.h>
#include <partialIO.h>

#define UNIX_PATH_MAX 108 
#define SOCKNAME_MAX 100
#define CMDSIZE 256
#define BUFSIZE 10000 // 10KB

static char socketName[SOCKNAME_MAX] = "";	// nome del socket al quale il client e' connesso
static int fd_skt;							// file descriptor per le operazioni di lettura e scrittura sul server
static int print = 0;						// se = 1, stampa su stdout informazioni sui comandi eseguiti
static char openedFile[256] = "";			// filepath del file attualmente aperto

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

	// controllo che il client non abbia gia' un file aperto
	if (strcmp(openedFile, "") != 0) {
		errno = EMFILE;
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
	//printf("openFile: invio %s!\n", cmd);

	if (writen(fd_skt, cmd, CMDSIZE) == -1) {
		errno = EREMOTEIO;
		return -1;
	}

	//printf("openFile: fatta la writen\n");

	// ricevo la risposta dal server
	void *buf = malloc(BUFSIZE);
	int r = readn(fd_skt, buf, 3);
	if (r == -1 || r == 0) {
		free(buf);
		errno = EREMOTEIO;
		return -1;
	}

	//printf("openFile: fatta la readn\n");

	char res[3];
	memcpy(res, buf, 3);

	//printf("openFile: ho ricevuto: %s!\n", res);

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
	strncpy(openedFile, pathname, strlen(pathname)+1);

	return 0;
}

int writeFile(const char* pathname, const char* dirname) {
	// controllo la validità degli argomenti
	if (!pathname || strlen(pathname) >= BUFSIZE) {
		errno = EINVAL;
		return -1;
	}

	// controllo che il client sia effettivamente connesso al server
	if (strcmp(socketName, "") == 0) {
		errno = ENOTCONN;
		return -1;
	}

	// se il file su cui si vuole scrivere non e' stato precedentemente creato o aperto, errore
	if (strcmp(openedFile, pathname) != 0) {
		errno = EPERM;
		return -1;
	}

	//printf("Sono dentro la writeFile\n");
	//fflush(stdout);

	void *buf = malloc(BUFSIZE);
	void *content = malloc(BUFSIZE);
	size_t size = 0;
	char cmd[CMDSIZE] = "";

	int fdi = -1, lung = 0;
	// leggo il file da scrivere sul server
	if ((fdi = open(pathname, O_RDONLY)) == -1) {
		//perror("open");
		free(buf);
		free(content);
		return -1;
	}

	//printf("provo la read...\n");
	//fflush(stdout);
	while ((lung = read(fdi, content, BUFSIZE)) > 0) {
		size += lung;
	}

	if (lung == -1) {
		free(buf);
		free(content);
		return -1;
	}

	printf("Dimensione: %ldB\t", size);
	fflush(stdout);

	if (size >= BUFSIZE) {
		if (print) {
			printf("\nLa dimensione del file supera il limite (%dB). Solo i primi %dB saranno inviati.\n", BUFSIZE, BUFSIZE);
		}
	}

	if (close(fdi) == -1) {
		free(buf);
		free(content);
		return -1;
	}

	/*
	// leggo il file da scrivere sul server
	FILE* ipf = NULL;
	if ((ipf = fopen(pathname, "rb")) == NULL) {
		perror("fopen");
		free(buf);
		free(content);
		return -1;
	}

	char cnt[BUFSIZE];
    cnt[0] = '\0';
    char line[BUFSIZE];

    printf("faccio la fgets...\n");
    while (fgets(line, BUFSIZE, ipf)) {
      	// leggiamo riga per riga il contenuto del file (ogni riga è registrata in line) e ne facciamo la append in cnt
        strcat(cnt, line);
    }

    size = strlen(cnt)+1;
	//printf("provo la fread...\n");
	//size = fread(content, 1, BUFSIZE, ipf);
	//printf("Dimensione: %ldB\t", size);

	if (size == BUFSIZE) {
		if (print) {
			printf("\nLa dimensione del file supera il limite (%dB). Solo i primi %dB saranno inviati.\n", BUFSIZE, BUFSIZE);
		}
	}

	fclose(ipf);
	*/

	// preparo il comando da inviare al server in formato writeFile:pathname:size
	memset(cmd, '\0', CMDSIZE);
	strncpy(cmd, "writeFile:", 11);
	strncat(cmd, pathname, strlen(pathname) + 1);
	strncat(cmd, ":", 2);
	char sizeStr[BUFSIZE];
	snprintf(sizeStr, BUFSIZE, "%ld", size);
	strncat(cmd, sizeStr, strlen(sizeStr) + 1);

	// invio il comando al server
	//printf("writeFile: invio %s!\n", cmd);
	//fflush(stdout);

	//printf("writeFile: aspetto la writen...\n");
	//fflush(stdout);
	if (writen(fd_skt, cmd, CMDSIZE) == -1) {
		free(buf);
		free(content);
		errno = EREMOTEIO;
		return -1;
	}

	//printf("writeFile: aspetto la readn...\n");
	//fflush(stdout);
	// ricevo la risposta dal server
	int r = readn(fd_skt, buf, 3);
	if (r == -1 || r == 0) {
		free(buf);
		free(content);
		errno = EREMOTEIO;
		return -1;
	}

	char res[3];
	memcpy(res, buf, 3);

	//printf("writeFile: ho ricevuto: %s!\n", res);
	//fflush(stdout);

	// se il server mi ha risposto con un errore...
	if (strcmp(res, "er") == 0) {
		memset(buf, 0, BUFSIZE);

		// ...ricevo l'errno
		if ((readn(fd_skt, buf, sizeof(int))) == -1) {
			free(buf);
			free(content);
			errno = EREMOTEIO;
			return -1;
		}

		// setto il mio errno uguale a quello che ho ricevuto dal server
		memcpy(&errno, buf, sizeof(int));
		free(buf);
		free(content);
		return -1;
	}

	// se il server ha dovuto espellere dei file per fare spazio, li ricevo tutti
	else if (strcmp(res, "es") == 0) {
		if (print && dirname) {
			if (strcmp(dirname, "") != 0) {
				printf("\nIl server ha espulso i seguenti file:\n");
				fflush(stdout);
			}
		}

		if (receiveNFiles(dirname) == -1) {
			free(buf);
			free(content);
			return -1;
		}
	}

	// invio il contenuto del file al server
	if (writen(fd_skt, content, size) == -1) {
		free(buf);
		free(content);
		errno = EREMOTEIO;
		return -1;
	}

	free(buf);
	free(content);
	return 0;
}

int closeFile(const char* pathname) {
	// controllo la validita' dell'argomento
	if (!pathname) {
		errno = EINVAL;
		return -1;
	}

	// controllo che il file da chiudere sia effettivamente aperto
	if (strcmp(openedFile, pathname) != 0) {
		errno = ENOENT;
		return -1;
	}

	char cmd[CMDSIZE] = "";

	// preparo il comando da inviare al server in formato closeFile:pathname
	memset(cmd, '\0', CMDSIZE);
	strncpy(cmd, "closeFile:", 11);
	strncat(cmd, pathname, strlen(pathname) + 1);

	// invio il comando al server
	//printf("closeFile: invio %s!\n", cmd);
	//fflush(stdout);


	//printf("closeFile: provo la writen...\n");
	//fflush(stdout);
	if (writen(fd_skt, cmd, CMDSIZE) == -1) {
		errno = EREMOTEIO;
		return -1;
	}

	//printf("closeFile: provo la prima readn...\n");
	//fflush(stdout);
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

	//printf("closeFile: ho ricevuto: %s!\n", res);

	// se il server mi ha risposto con un errore...
	if (strcmp(res, "er") == 0) {
		memset(buf, 0, BUFSIZE);

		printf("closeFile: provo la seconda readn...\n");
		fflush(stdout);

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

	// aggiorno la variabile globale che tiene traccia del file aperto
	strncpy(openedFile, "", 1);

	free(buf);
	return 0;
}

// funzione ausiliaria che riceve un file dal server
int receiveFile(const char *dirname) {
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
	//printf("Ho ricevuto filepath = %s\n", filepath);
	//fflush(stdout);

	// ...poi la dimensione del file...
	memset(buf, 0, BUFSIZE);
	if ((readn(fd_skt, buf, sizeof(size_t))) == -1) {
		free(buf);
		errno = EREMOTEIO;
		return -1;
	}

	memcpy(&size, buf, sizeof(size_t));
	//printf("Ho ricevuto size = %ld\n", size);
	//fflush(stdout);

	content = malloc(size);

	// ...e infine il contenuto
	if ((readn(fd_skt, content, size)) == -1) {
		free(buf);
		free(content);
		errno = EREMOTEIO;
		return -1;
	}

	//printf("Ho ricevuto il contenuto!\n");

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

// funzione ausiliaria che riceve N files dal server
int receiveNFiles(const char *dirname) {
	void *buf = NULL;
	buf = malloc(BUFSIZE);
	char filepath[BUFSIZE];
	size_t size;
	int fine = 0;
	char dir[256] = "";
	char *filename = malloc(256);

	// se e' stata passata una directory, usala per scriverci dentro i file ricevuti dal serevr
	if (dirname && strcmp(dirname, "") != 0) {
		strncpy(dir, dirname, strlen(dirname)+1);

		if (dir[strlen(dir)] != '/') {
			dir[strlen(dir)] = '/';
		}
	}
	
	while (!fine) {
		memset(buf, 0, BUFSIZE);
		void *content = NULL;

		// ricevo prima il filepath
		if ((readn(fd_skt, buf, BUFSIZE)) == -1) {
			free(buf);
			errno = EREMOTEIO;
			return -1;
		}

		strncpy(filepath, buf, BUFSIZE);

		// se ho finito di ricevere file, esci
		if (strcmp(filepath, ".FINE") == 0) {
			fine = 1;
			break;
		}

		if (print && strcmp(dir, "") != 0) {
			printf("%s", filepath);
		}

		// poi ricevo la dimensione del file...
		memset(buf, 0, BUFSIZE);
		if ((readn(fd_skt, buf, sizeof(size_t))) == -1) {
			free(buf);
			errno = EREMOTEIO;
			return -1;
		}

		memcpy(&size, buf, sizeof(size_t));

		if (print && strcmp(dir, "") != 0) {
			printf("\tDimensione: %ldB\t", size);
		}
		//printf("Ho ricevuto size = %ld\n", size);

		content = malloc(size);

		// ...e infine il contenuto
		if ((readn(fd_skt, content, size)) == -1) {
			free(buf);
			free(content);
			errno = EREMOTEIO;
			return -1;
		}

		//printf("Ho ricevuto il contenuto!\n");

		// se il client ha specificato una cartella, vi scrivo dentro il file appena ricevuto
		if (strcmp(dir, "") != 0) {
			//printf("SCRITTURA SU FILE\n");
			memset(filename, '\0', 256);
			strncpy(filename, filepath, strlen(filepath)+1);
			char fullpath[512] = "";
			strncpy(fullpath, dir, 256);

			/**
			 * Se il nome del file ricevuto contiene dei caratteri "/", li sostituisco con "-".
			 * Questo può accadere poiché il server memorizza i file utilizzando il loro path assoluto come identificatore.
			*/
			int i = 0;
			while (filename[i]) {
				if (filename[i] == '/') {
					filename[i] = '-';
				}

				i++;
			}

			//printf("Dir: %s Filename: %s Fullpath: %s\n", dir, filename, fullpath);
			strncat(fullpath, filename, 256);
			//printf("Voglio scrivere %ld bytes in %s\n", size, fullpath);

			// Apro file di output
			int fdo;
			if ((fdo = open(fullpath, O_CREAT | O_WRONLY, 0666)) == -1) {
				perror("errore secondo argomento in apertura\n");
				free(content);
		    	free(filename);
		    	free(buf);
		    	return -1;
			}

			if (write(fdo, content, size) == -1) {
				perror("errore secondo argomento in scrittura\n");
				free(content);
		    	free(filename);
		    	free(buf);
		    	return -1;
			}

			if (close(fdo) == -1) {
				free(content);
		    	free(filename);
		    	free(buf);
		    	return -1;
			}

			/*
		    FILE* opf;
		    if ((opf = fopen(fullpath, "w")) == NULL) {
		    	//perror("fopen");
		    	free(content);
		    	free(filename);
		    	free(buf);
		    	return -1;
		    }
		    
		    if (fwrite(content, 1, size, opf) == -1) {
		    	//perror("fwrite");
		    	free(content);
		    	free(filename);
		    	free(buf);
		    	return -1;
		    }

		    fclose(opf);
		    */
		}
		
	    free(content);
	}

	free(filename);
	free(buf);

	return 0;
}

void printInfo(int p) {
	if (p) {
		print = 1;
	}

	else {
		print = 0;
	}
}