#include "future.h"
#include <stdlib.h>
#include <stdio.h>


/** @brief Struktura będąca argumentem dla `async`.
 */
typedef struct future_call {
    future_t *future; /**< Future gdzie ma być zapisany wynik. */
    callable_t callable; /**< Przypisane zadanie do wykonania. */
} future_call_t;


/** @brief Struktura będąca argumentem dla `async`.
 */
typedef struct map_call {
    future_t *from; /**< Future z którego ma pochodzić argument. */
    future_t *result; /**< Future gdzie mamy zapisać wynik. */
    void *(*function)(void *, size_t, size_t *);
    /**< @brief Funkcja do wykonania.
     *  @param p1[in, out] - wskaźnik na argument funkcji
     *  @param p2[in] - rozmiar argumentu funkcji
     *  @param p3[out] - wskaźnik gdzie funkcja może wpisać rozmiar wyniku
     *  @return Wskaźnik do wyniku funkcji.
     */
} map_call_t;


/** @brief Inicjalizacja struktury `future`.
 * @param future[out] - struktura do zainicjalizowania
 * @return Wartość `0` jeżeli zlecenie obliczenia powiodło się, w przeciwnym przypadku wartość ujemną.
 */
static int future_init(future_t *future) {
    if (future == NULL) {
        return -1;
    }
    int mutex_cond = 0;
    future->lock = malloc(sizeof(pthread_mutex_t));
    if (future->lock == NULL) {
        goto exception;
    }
    future->wait = malloc(sizeof(pthread_cond_t));
    if (future->wait == NULL) {
        goto exception;
    }
    mutex_cond = 1;
    future->ready = 0;
    future->result = NULL;
    future->ressz = 0;
    if (pthread_cond_init(future->wait, NULL) != 0) {
        goto exception;
    }
    if (pthread_mutex_init(future->lock, NULL) != 0) {
        if (pthread_cond_destroy(future->wait) != 0) {
            fprintf(stderr, "Cond destroy failure in future_init\n");
            goto fatal_exception;
        }
        goto exception;
    }
    return 0;
    exception:
        if (mutex_cond == 1) {
            free(future->lock);
            free(future->wait);
        }
        return -1;
    fatal_exception:
        exit(EXIT_FAILURE);
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
    if (pthread_cond_broadcast(call->future->wait) != 0) {
        fprintf(stderr, "Cond broadcast failure in call_com\n");
        goto fatal_exception;
    }
    free(arg);
    return;
    fatal_exception:
        exit(EXIT_FAILURE);
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
    if (pthread_cond_broadcast(call->result->wait) != 0) {
        fprintf(stderr, "Cond broadcast failure in call_map\n");
        goto fatal_exception;
    }
    free(arg);
    return;
    fatal_exception:
        exit(EXIT_FAILURE);
}


int async(thread_pool_t *pool, future_t *future, callable_t callable) {
    if (future_init(future) != 0) {
        goto exception;
    }
    future_call_t *fut_call = malloc(sizeof(future_call_t));
    if (fut_call == NULL) {
        goto exception;
    }
    fut_call->callable = callable;
    fut_call->future = future;
    if (defer(pool, (runnable_t){.function= call_comp, .arg=fut_call, .argsz=sizeof(future_call_t)}) != 0) {
        goto exception;
    }
    return 0;
    exception: {
        if (pthread_cond_destroy(future->wait) != 0) {
            fprintf(stderr, "Cond destroy failure in async\n");
            goto fatal_exception;
        }
        if (pthread_mutex_destroy(future->lock) != 0) {
            fprintf(stderr, "Mutex destroy failure in async\n");
            goto fatal_exception;
        }
        return -1;
    }
    fatal_exception:
        exit(EXIT_FAILURE);
}


void *await(future_t *future) {
    if (pthread_mutex_lock(future->lock) != 0) {
        fprintf(stderr, "Mutex lock failure in await\n");
        goto fatal_exception;
    }
    while (future->ready != 1) {
        if (pthread_cond_wait(future->wait, future->lock) != 0) {
            fprintf(stderr, "Cond wait failure in await\n");
            goto fatal_exception;
        }
    }
    if (pthread_mutex_unlock(future->lock) != 0) {
        fprintf(stderr, "Mutex unlock failure in await\n");
        goto fatal_exception;
    }
    if (pthread_cond_destroy(future->wait) != 0) {
        fprintf(stderr, "Cond destroy failure in await\n");
        goto fatal_exception;
    }
    if (pthread_mutex_destroy(future->lock) != 0) {
        fprintf(stderr, "Mutex destroy failure in await\n");
        goto fatal_exception;
    }
    free(future->lock);
    free(future->wait);
    return future->result;
    fatal_exception:
        exit(EXIT_FAILURE);
}


int map(thread_pool_t *pool, future_t *future, future_t *from,
        void *(*function)(void *, size_t, size_t *)) {
    if (future_init(future) != 0) {
        goto exception;
    }
    map_call_t *map_call = (map_call_t*) malloc(sizeof(map_call_t));
    if (map_call == NULL) {
        goto exception;
    }
    map_call->function = function;
    map_call->from = from;
    map_call->result = future;
    if (defer(pool,(runnable_t){.function=call_map, .arg=map_call, .argsz=sizeof(map_call_t)}) != 0) {
        goto exception;
    }
    return 0;
    exception:
        return -1;
}