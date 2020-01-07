#include "future.h"
#include <stdlib.h>
#include <stdio.h>


typedef void *(*function_t)(void *);

typedef struct future_call {
    future_t *future;
    callable_t callable;
} future_call_t;

typedef struct map_call {
    future_t *from;
    future_t *result;
    void *(*function)(void *, size_t, size_t *);
} map_call_t;


/** @brief Inicjalizacja struktury `future`.
 * @param future[out] - struktura do zainicjalizowania
 * @return Wartość `0` jeżeli zlecenie obliczenia powiodło się, w przeciwnym przypadku wartość ujemną.
 */
static int future_init(future_t *future) {
    if (future == NULL) {
        return -1;
    }
    future->ready = 0;
    future->result = NULL;
    future->ressz = 0;
    if (pthread_cond_init(&future->wait, NULL) != 0) {
        return -1;
    }
    if (pthread_mutex_init(&future->lock, NULL) != 0) {
        return -1;
    }
    return 0;
}


/** @brief Funkcja argument dla defer przy wywołaniu asynch.
 * @param arg[in, out] - argument (`callable` i `future` do zapisania wyniku)
 * @param args[in] - rozmiar argumentu (nieużywany)
 * @return void
 */
static void call_comp(void *arg, size_t args __attribute__((unused))) {
    future_call_t *call = arg;
    call->future->result = call->callable.function(call->callable.arg, call->callable.argsz, &call->future->ressz);
    // Bez robienia locka, bo nie narusza bezpieczeństwa.
    call->future->ready = 1;
    if (pthread_cond_broadcast(&call->future->wait) != 0) {
        exit(EXIT_FAILURE);
    }
    free(arg);
}


/** @brief Funkcja argument dla defer przy wywołaniu map.
 * @param arg[in, out] - argument (`from` i `future`)
 * @param args[in] - rozmiar argumentu (nieużywany)
 * @return void
 */
static void call_map(void *arg, size_t argss __attribute__((unused))) {
    map_call_t *call = arg;
    await(call->from);
    call->result->result = call->function(call->from->result, call->from->ressz, &call->result->ressz);
    // Bez robienia locka, ponieważ nie narusza bezpieczeństwa.
    call->result->ready = 1;
    if (pthread_cond_broadcast(&call->result->wait) != 0) {
        exit(EXIT_FAILURE);
    }
    free(arg);
}


int async(thread_pool_t *pool, future_t *future, callable_t callable) {
    if (future_init(future) != 0) {
        goto Exception;
    }
    future_call_t *fut_call = malloc(sizeof(future_call_t));
    if (fut_call == NULL) {
        goto Exception;
    }
    fut_call->callable = callable;
    fut_call->future = future;
    defer(pool, (runnable_t){.function= call_comp, .arg=fut_call, .argsz=sizeof(future_call_t)});
    return 0;
    Exception: {
        if (pthread_cond_destroy(&future->wait) != 0) {
            //fprintf(stderr, "%d: Cond destroy failure in async\n", err);
            exit(EXIT_FAILURE);
        }
        if (pthread_mutex_destroy(&future->lock) != 0) {
            //fprintf(stderr, "%d: Mutex destroy failure in async\n", err);
            exit(EXIT_FAILURE);
        }
        return -1;
    }
}


void *await(future_t *future) {
    int err;
    if ((err = pthread_mutex_lock(&future->lock)) != 0) {
        //fprintf(stderr, "%d: Mutex lock failure in await\n", err);
        exit(EXIT_FAILURE);
    }
    while (future->ready != 1) {
        if ((err = pthread_cond_wait(&future->wait, &future->lock)) != 0) {
            //fprintf(stderr, "%d: Cond wait failure in await\n", err);
            exit(EXIT_FAILURE);
        }
    }
    if ((err = pthread_mutex_unlock(&future->lock)) != 0) {
        //fprintf(stderr, "%d: Mutex unlock failure in await\n", err);
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
    if (future_init(future) != 0) {
        return -1;
    }
    map_call_t *map_call = (map_call_t*) malloc(sizeof(map_call_t));
    map_call->function = function;
    map_call->from = from;
    map_call->result = future;
    defer(pool,(runnable_t){.function=call_map, .arg=map_call, .argsz=sizeof(map_call_t)});
    return 0;
}