#include "future.h"
#include <stdlib.h>
#include <stdio.h>

typedef void *(*function_t)(void *);

int async(thread_pool_t *pool, future_t *future, callable_t callable) {
  return 0;
}

int map(thread_pool_t *pool, future_t *future, future_t *from,
        void *(*function)(void *, size_t, size_t *)) {
  return 0;
}

void *await(future_t *future) {
    int err;
    void *result;
    if (future->ready != 1) {
        if ((err = pthread_mutex_lock(&future->lock)) != 0) {
            fprintf(stderr, "%d: Mutex lock failure in await\n", err);
            exit(EXIT_FAILURE);
        }
        future->count_waiting++;
        while (future->ready != 1) {
            if ((err = pthread_cond_wait(&future->wait, &future->lock)) != 0) {
                fprintf(stderr, "%d: Cond wait failure in await\n", err);
                exit(EXIT_FAILURE);
            }
        }
        future->count_waiting--;
        if ((err = pthread_mutex_unlock(&future->lock)) != 0) {
            fprintf(stderr, "%d: Mutex unlock failure in await\n", err);
            exit(EXIT_FAILURE);
        }
        if ((err = pthread_mutex_trylock(&future->lock)) == 0) {
            if (future->count_waiting == 0) {
                if ((err = pthread_mutex_unlock(&future->lock)) != 0) {
                    fprintf(stderr, "%d: Mutex unlock failure in await\n", err);
                    exit(EXIT_FAILURE);
                }
                if ((err = pthread_mutex_destroy(&future->lock)) != 0) {
                    fprintf(stderr, "%d: Mutex destroy failure in await\n", err);
                    exit(EXIT_FAILURE);
                }
                if ((err = pthread_cond_destroy(&future->wait)) != 0) {
                    fprintf(stderr, "%d: Cond destroy failure in await\n", err);
                    exit(EXIT_FAILURE);
                }
            }
        } else if (err != -4082) {
            fprintf(stderr, "%d: Mutex trylock failure in await\n", err);
            exit(EXIT_FAILURE);
        }
    }
    return future->result;
}
