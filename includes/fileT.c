#include <stdio.h>
#include <stdlib.h>
#include <fileT.h>

fileT* createFile(FILE *file, int O_LOCK, int owner) {
	fileT *f;

	if ((f = (fileT*) calloc(1, sizeof(fileT))) == NULL) {
		perror("Calloc createFile");
		return (fileT*) NULL;
	}

	if (O_LOCK != 0 && O_LOCK != 1) {
		perror("Flag O_LOCK invalido");
		return (fileT*) NULL;
	}

	f->file = file;
	f->O_LOCK = O_LOCK;
	f->owner = owner;

	return f;
}