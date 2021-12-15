// libreria presa dalla soluzione dell'esercitazione 11

#ifndef THREADPOOL_H_
#define THREADPOOL_H_

#include <pthread.h>

/**
 *  @struct taskfun_t
 *  @brief generico task che un thread del threadpool deve eseguire
 *  @var fun Puntatore alla funzione da eseguire
 *  @var arg Argomento della funzione
 */
typedef struct taskfun_t {
    void (*fun)(void *);
    void *arg;
} taskfun_t;

/**
 *  @struct threadpool
 *  @brief Rappresentazione dell'oggetto threadpool
 */
typedef struct threadpool_t {
    pthread_mutex_t  lock;    // mutua esclusione nell'accesso all'oggetto
    pthread_cond_t   cond;    // usata per notificare un worker thread 
    pthread_t      * threads; // array di worker id
    int numthreads;           // numero di thread (size dell'array threads)
    taskfun_t *pending_queue; // coda interna per task pendenti
    int queue_size;     // massima size della coda, puo' essere anche -1 ad indicare che non si vogliono gestire task pendenti
    int taskonthefly;         // numero di task attualmente in esecuzione 
    int head, tail;           // riferimenti della coda
    int count;                // numero di task nella coda dei task pendenti
    int exiting;    // se > 0 e' iniziato il protocollo di uscita, se 1 il thread aspetta che non ci siano piu' lavori in coda
} threadpool_t;

/**
 * @function createThreadPool
 * @brief Crea un oggetto thread pool.
 * @param numthreads è il numero di thread del pool
 * @param pending_size è la size delle richieste che possono essere pendenti. 
 *        Questo parametro è 0 se si vuole utilizzare un modello per il pool con 1 thread 1 richiesta, 
 *        cioe' non ci sono richieste pendenti.
 * @return un nuovo thread pool oppure NULL ed errno settato opportunamente
 */
threadpool_t *createThreadPool(int numthreads, int pending_size);

/**
 * @function destroyThreadPool
 * @brief stoppa tutti i thread e distrugge l'oggetto pool
 * @param pool  oggetto da liberare
 * @param force se 1 forza l'uscita immediatamente di tutti i thread e libera subito le risorse, 
 *        se 0 aspetta che i thread finiscano tutti e soli i lavori pendenti (non accetta altri lavori).
 * @return 0 in caso di successo <0 in caso di fallimento ed errno viene settato opportunamente
 */
int destroyThreadPool(threadpool_t *pool, int force);

/**
 * @function addTaskToThreadPool
 * @brief aggiunge un task al pool, se ci sono thread liberi il task viene assegnato ad uno di questi, 
 *        se non ci sono thread liberi e pending_size > 0 allora si cerca di inserire il task come task pendente. 
 *        Se non c'e' posto nella coda interna allora la chiamata fallisce. 
 * @param pool oggetto thread pool
 * @param fun  funzione da eseguire per eseguire il task
 * @param arg  argomento della funzione
 * @return 0 se successo, 1 se non ci sono thread disponibili e/o la coda è piena, -1 in caso di fallimento, errno viene settato opportunamente.
 */
int addToThreadPool(threadpool_t *pool, void (*fun)(void *),void *arg);


/**
 * @function spawnThread
 * @brief lancia un thread che esegue la funzione fun passata come parametro, 
 *        il thread viene lanciato in modalità detached e non fa parte del pool.
 * @param fun  funzione da eseguire per eseguire il task
 * @param arg  argomento della funzione
 * @return 0 se successo, -1 in caso di fallimento, errno viene settato opportunamente.
 */
int spawnThread(void (*f)(void*), void* arg);

#endif /* THREADPOOL_H_ */