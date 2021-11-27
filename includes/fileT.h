typedef struct {
    FILE* file;
    int O_LOCK;
    int owner;
} fileT;

fileT* createFile(FILE *file, int O_LOCK, int owner);