#include "future.h"
#include <stdlib.h>
#include <stdio.h>


typedef void *(*function_t)(void *);

typedef struct call {
    future_t *future;
    callable_t callable;
} call_t;

function_t call(void *args) {
    call_t *call = args;
    call->future->result = call->callable.function(call->callable.arg, call->callable.argsz, &call->future->ressz);
    call->future->ready |= 1;
    int err;
    if ((err = pthread_cond_broadcast(&call->future->wait)) != 0) {
        exit(EXIT_FAILURE);
    }
}

int async(thread_pool_t *pool, future_t *future, callable_t callable) {
    int err;
    future->ready = 0;
    future->result = NULL;
    if ((err = pthread_cond_init(&future->wait, NULL)) != 0) {
        fprintf(stderr, "%d: Cond init failure in async\n", err);
        exit(EXIT_FAILURE);
    }
    if ((err = pthread_mutex_init(&future->lock, NULL)) != 0) {
        fprintf(stderr, "%d: Mutex init failure in async\n", err);
        exit(EXIT_FAILURE);
    }
    call_t *arg = malloc(sizeof(call_t));
    if (arg == NULL) {
        goto Exception;
    }
    arg->callable = callable;
    arg->future = future;
    defer(pool, (runnable_t){.function=call, .arg=arg, .argsz=sizeof(call_t)});
    return 0;
    Exception: {
        if ((err = pthread_cond_destroy(&future->wait)) != 0) {
            fprintf(stderr, "%d: Cond destroy failure in async\n", err);
            exit(EXIT_FAILURE);
        }
        if ((err = pthread_mutex_destroy(&future->lock)) != 0) {
            fprintf(stderr, "%d: Mutex destroy failure in async\n", err);
            exit(EXIT_FAILURE);
        }
        return -1;
    }
}

int map(thread_pool_t *pool, future_t *future, future_t *from,
        void *(*function)(void *, size_t, size_t *)) {
  return 0;
}



void *await(future_t *future) {
    int err;
    pthread_mutex_t a = PTHREAD_MUTEX_INITIALIZER;
    void *result;
    if ((err = pthread_mutex_lock(&future->lock)) != 0) {
        fprintf(stderr, "%d: Mutex lock failure in await\n", err);
        exit(EXIT_FAILURE);
    }
    while (future->ready != 1) {
        if ((err = pthread_cond_wait(&future->wait, &future->lock)) != 0) {
            fprintf(stderr, "%d: Cond wait failure in await\n", err);
            exit(EXIT_FAILURE);
        }
    }
    if ((err = pthread_mutex_unlock(&future->lock)) != 0) {
        fprintf(stderr, "%d: Mutex unlock failure in await\n", err);
        exit(EXIT_FAILURE);
    }
    if ((err = pthread_cond_destroy(&future->wait)) != 0) {
        fprintf(stderr, "%d: Cond destroy failure in await\n", err);
        exit(EXIT_FAILURE);
    }
    if ((err = pthread_mutex_destroy(&future->lock)) != 0) {
        fprintf(stderr, "%d: Mutex destroy failure in await\n", err);
        exit(EXIT_FAILURE);
    }
    return future->result;
}

