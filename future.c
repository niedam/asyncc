#include "future.h"
#include <stdlib.h>
#include <stdio.h>


typedef void *(*function_t)(void *);


static int future_init(future_t *future) {
    int err;
    future->ready = 0;
    future->result = NULL;
    future->ressz = 0;
    if ((err = pthread_cond_init(&future->wait, NULL)) != 0) {
        return -1;
    }
    if ((err = pthread_mutex_init(&future->lock, NULL)) != 0) {
        return -1;
    }
    return 0;
}


typedef struct future_call {
    future_t *future;
    callable_t callable;
} future_call_t;

typedef struct map_call {
    future_t *from;
    future_t *result;
    void *(*function)(void *, size_t, size_t *);
} map_call_t;

static function_t call(void *args) {
    future_call_t *call = args;
    call->future->result = call->callable.function(call->callable.arg, call->callable.argsz, &call->future->ressz);
    call->future->ready |= 1;
    int err;
    if ((err = pthread_cond_broadcast(&call->future->wait)) != 0) {
        exit(EXIT_FAILURE);
    }
}

static function_t call_map(void *args) {
    map_call_t *call = args;
    await(call->from);
    call->result->result = call->function(call->from->result, call->from->ressz, &call->result->ressz);
    call->result->ready |= 1;
    int err;
    if ((err = pthread_cond_broadcast(&call->result->wait)) != 0) {
        exit(EXIT_FAILURE);
    }
}



int async(thread_pool_t *pool, future_t *future, callable_t callable) {
    int err;
    if ((err = future_init(future)) != 0) {
        goto Exception;
    }

    future_call_t *fut_call = malloc(sizeof(future_call_t));
    if (fut_call == NULL) {
        goto Exception;
    }
    fut_call->callable = callable;
    fut_call->future = future;
    defer(pool, (runnable_t){.function=call, .arg=fut_call, .argsz=sizeof(future_call_t)});
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


int map(thread_pool_t *pool, future_t *future, future_t *from,
        void *(*function)(void *, size_t, size_t *)) {
    int err;
    if ((err = future_init(future)) != 0) {
        return -1;
    }
    map_call_t *map_call = (map_call_t*) malloc(sizeof(map_call));
    map_call->function = function;
    map_call->from = from;
    map_call->result = future;
    defer(pool,(runnable_t){.function=call_map, .arg=map_call, .argsz=sizeof(map_call_t)});

    return 0;
}