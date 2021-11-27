typedef struct {
    size_t head;
    size_t tail;
    size_t size;
    int len;
    FILE** data;
} queue_t;

FILE* queue_read(queue_t *queue);

int queue_write(queue_t *queue, FILE* handle);