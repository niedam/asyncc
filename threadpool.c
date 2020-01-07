#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include "threadpool.h"

/** @brief Globalny monitor zarządzający przekazywaniem sygnałów.
 */
static struct thread_pool_monitor {
    pthread_mutex_t lock; /**< Zamek do monitora realizujący wzajemne wykluczenie (nie dotyczy obsługi sygnałów). */
    pthread_cond_t wait; /**< Zmienna warunkowa do czekania na zakończenie wszystkich wątków, którym wysłano sygnał. */
    int used; /**< Flaga czy zostało zlecone zniszczenie monitora na `exit` lub `return` w main. */
    void *queue; /**< Kolejka pul, które mają otrzymać sygnał */
    int flag[2]; /**< Flagi do algorytmu Petersona. */
    int turn; /**< Czyja kolej w algorytmie Petersona. */
    int signaled; /**< Liczba pul, którym wysłano sygnał. */
    int count_threads; /**< Liczba pul, które już zakończyły pracę po sygnale */
} monitor = {.lock = PTHREAD_MUTEX_INITIALIZER, .queue = NULL, .used = 0, .flag = {0, 0}, .signaled = 0,
        .wait = PTHREAD_COND_INITIALIZER, .count_threads = 0};

/** @brief Węzeł kolejki cyklicznej pul oczekujących na sygnał w monitorze.
 */
typedef struct circ_queue_node {
    thread_pool_t *pool; /**< Wskaźnik do puli */
    struct circ_queue_node *next; /**< Wskaźnik do następnego węzła. */
    struct circ_queue_node *prev; /**< Wskaźnik do poprzedniego węzła. */
} circ_queue_node_t;

/** @brief Węzeł kolejki zadań w `thread_pool`.
 */
typedef struct queue_node {
    struct queue_node *next; /**< Wskaźnik na następny węzeł kolejki. */
    runnable_t runnable; /**< Zlecone zadanie `runnable`  */
} queue_node_t;


/** @brief Funkcja do wykonania w `atexit`.
 * @return void
 */
static void destroy_monitor() {
    if (pthread_mutex_destroy(&monitor.lock) != 0  ||
        pthread_cond_destroy(&monitor.wait) != 0) {
        fprintf(stderr, "Mutex & cond destroy failure in destroy_monitor\n");
        goto really_fatal_exception;
    }
    return;
    really_fatal_exception:
        _Exit(EXIT_FAILURE);
}


/** @brief Przekazanie informacji o zniszczeniu puli przez sygnał do monitora.
 * @return void
 */
static void thread_pool_signal_done() {
    if (pthread_mutex_lock(&monitor.lock) != 0) {
        fprintf(stderr, "Mutex lock failure in thread_pool_signal_done\n");
        goto fatal_exception;
    }
    monitor.flag[0] = 1;
    monitor.turn = 1;
    while (monitor.flag[1] == 1 && monitor.turn == 1) {
        // aktywne oczekiwanie
    }
    monitor.count_threads++;
    if (monitor.signaled == monitor.count_threads) {
        if (pthread_cond_signal(&monitor.wait) != 0) {
            fprintf(stderr, "Cond signal failure in thread_pool_signal_done\n");
            goto fatal_exception;
        }
    }
    monitor.flag[0] = 0;
    if (pthread_mutex_unlock(&monitor.lock) != 0) {
        fprintf(stderr, "Mutex unlock failure in thread_pool_signal_done\n");
        goto fatal_exception;
    }
    return;
    fatal_exception:
        exit(EXIT_FAILURE);
}


/** @brief Stworzenie nowego `circ_queue_node`.
 * @param pool[in] - pula z którą ma być związany węzeł
 * @return Węzeł do kolejki cyklicznej dla wątków w monitorze.
 */
static circ_queue_node_t *new_circ_node(thread_pool_t *pool) {
    circ_queue_node_t *result = malloc(sizeof(circ_queue_node_t));
    if (result == NULL) {
        return NULL;
    }
    result->pool = pool;
    return result;
}


/** @brief Handler sygnału SIGINT.
 * Funkcja przetwarzająca sygnał SIGINT wysłany do procesu.
 * Na potrzeby funkcji konieczne było zapewnienie dodatkowego wzajemnego wykluczenia w dostępie do monitora
 * przy użyciu algorytmu Petersona (w `sigaction` nie można korzystać z mechanizmów synchronizujących pthreads).
 * @param sig[] - kod sygnału (nieużywany)
 */
static void catch(int sig __attribute__((unused))) {
    monitor.flag[1] = 1;
    monitor.turn = 0;
    while (monitor.flag[0] == 1 && monitor.turn == 0) {
        // aktywne oczekiwanie
    }
    circ_queue_node_t *curr = monitor.queue;
    circ_queue_node_t *start = monitor.queue;
    // Ustawienie pulom flag o sygnale SIGINT.
    if (monitor.queue != NULL) {
        do {
            curr->pool->signal = 1;
            curr = curr->next;
        } while (curr != start);
    }
    monitor.queue = NULL;
    monitor.flag[1] = 0;
}


/** @brief Zgłoszenie puli do odbierania sygnałów.
 * @param pool[in] - wskaźnik na zgłaszaną pulę
 * @return Reprezentant puli w monitorze
 */
static circ_queue_node_t *thread_pool_monitor(thread_pool_t *pool) {
    if (pthread_mutex_lock(&monitor.lock) != 0) {
        goto exception;
    }

    monitor.flag[0] = 1;
    monitor.turn = 1;
    while (monitor.flag[1] == 1 && monitor.turn == 1) {
        // aktywne oczekiwanie
    }
    if (monitor.used == 0) {
        atexit(destroy_monitor);

        struct sigaction action;
        sigset_t block_mask;
        sigemptyset (&block_mask);

        action.sa_handler = catch;
        action.sa_mask = block_mask;
        action.sa_flags = SA_RESTART | SA_NODEFER;

        if (sigaction (SIGINT, &action, 0) != 0) {
            fprintf(stderr, "Sigaction failure in thread_pool_monitor\n");
            goto fatal_exception;
        }
        if (sigprocmask(SIG_BLOCK, &block_mask, 0) != 0) {
            fprintf(stderr, "Sigprocmask failure in thread_pool_monitor\n");
            goto fatal_exception;
        }
    }
    circ_queue_node_t *new_node = new_circ_node(pool);
    if (new_node == NULL) {
        if (pthread_mutex_unlock(&monitor.lock) != 0) {
            fprintf(stderr, "Mutex lock failure in thread_pool_monitor\n");
            goto fatal_exception;
        }
        goto exception;
    }
    if (monitor.queue == NULL) {
        new_node->next = new_node;
        new_node->prev = new_node;
        monitor.queue = new_node;
    } else {
        circ_queue_node_t *first = monitor.queue;
        circ_queue_node_t *second = first->next;
        first->next = new_node;
        second->prev = new_node;
        new_node->next = second;
        new_node->prev = first;
    }
    monitor.flag[0] = 0;
    if (pthread_mutex_unlock(&monitor.lock) != 0) {
        fprintf(stderr, "Mutex unlock failure in thread_pool_monitor\n");
        goto fatal_exception;
    }
    return new_node;
    exception:
        return NULL;
    fatal_exception:
        exit(EXIT_FAILURE);
}


/** @brief Anulowanie odbierania sygnału SIGINT przez pulę.
 * @param node[in] - reprezentant puli w monitorze utworzony przez `thread_pool_monitor(...)`
 * @return void
 */
static void thread_pool_unmonitor(void *node) {
    if (pthread_mutex_lock(&monitor.lock) != 0) {
        fprintf(stderr, "Mutex lock failure in thread_pool_unmonitor\n");
        goto fatal_exception;
    }
    monitor.flag[0] = 1;
    monitor.turn = 1;
    while (monitor.flag[1] == 1 && monitor.turn == 1) {
        // aktywne oczekiwanie
    }
    circ_queue_node_t *circ_node = node;
    circ_queue_node_t *next = circ_node->next;
    circ_queue_node_t *prev = circ_node->prev;
    if (monitor.queue == node) {
        if (next == prev) {
            monitor.queue = NULL;
        } else {
            monitor.queue = next;
        }
    }
    next->prev = prev;
    prev->next = next;
    monitor.flag[0] = 0;
    if (pthread_mutex_unlock(&monitor.lock) != 0) {
        fprintf(stderr, "Mutex unlock failure in thread_pool_unmonitor\n");
        goto fatal_exception;
    }
    free(node);
    return;
    fatal_exception:
        exit(EXIT_FAILURE);
}


/** @brief Stworzenie nowego węzła do kolejki zadań w puli.
 * @param runnable[in] - zadanie do wykonania w puli
 * @return Wskaźnik na zaalokowany węzeł kolejki
 */
static queue_node_t *new_queue_node(runnable_t runnable) {
    queue_node_t *result = (queue_node_t*) malloc(sizeof(queue_node_t));
    if (result == NULL) {
        return NULL;
    }
    result->next = NULL;
    result->runnable = runnable;
    return result;
}


/** @brief Funkcja wątków roboczych puli `thread_pool`.
 * @param arg[in, out] - wskaznik na pulę z którą związany jest wątek
 * @return NULL
 */
static void *workers(void *arg) {
    thread_pool_t *pool = (struct thread_pool*) arg;
    queue_node_t *node = NULL;
    int first_thread = (pthread_equal(pthread_self(), pool->workers[0]) != 0?1:0);
    while (1) {
        if (pthread_mutex_lock(&pool->lock) != 0) {
            fprintf(stderr, "Mutex lock failure in workers\n");
            goto fatal_exception;
        }
        pool->count_waiting_workers++;
        while (pool->defered_tasks < 1) {
            if (pool->destroy == 1 || pool->signal == 1) {
                pool->count_waiting_workers--;
                if (pthread_mutex_unlock(&pool->lock) != 0) {
                    fprintf(stderr, "Mutex unlock failure in workers\n");
                    goto fatal_exception;
                }
                if (pool->signal == 0 || (pool->signal == 1 && !first_thread)) {
                    return NULL;
                } else {
                    pthread_detach(pthread_self());
                    thread_pool_destroy(pool);
                    thread_pool_signal_done();
                    pthread_exit(EXIT_SUCCESS);
                }
            }
            if (pthread_cond_wait(&pool->waiting_workers, &pool->lock) != 0) {
                fprintf(stderr, "Cond wait failure in workers\n");
                goto fatal_exception;
            }
        }
        pool->count_waiting_workers--;
        node = pool->head;
        pool->defered_tasks--;
        pool->head = node->next;
        if (pool->count_waiting_workers > 0) {
            if (pthread_cond_broadcast(&pool->waiting_workers) != 0) {
                fprintf(stderr, "Cond broadcast failure in workers\n");
                goto fatal_exception;
            }
        }


        if (pthread_mutex_unlock(&pool->lock) != 0) {
            fprintf(stderr, "Mutex unlock failure in workers\n");
            goto fatal_exception;
        }
        node->runnable.function(node->runnable.arg, node->runnable.argsz);
        free(node);
    }
    fatal_exception:
        exit(EXIT_FAILURE);
}


int thread_pool_init(thread_pool_t *pool, size_t num_threads) {
    if (pool == NULL) {
        goto exception;
    }
    circ_queue_node_t *node = thread_pool_monitor(pool);
    if (node == NULL) {
        goto exception;
    }
    if (pthread_mutex_init(&pool->lock, 0) != 0) {
        thread_pool_unmonitor(node);
        goto exception;
    }
    if (pthread_cond_init(&pool->waiting_workers, NULL) != 0) {
        thread_pool_unmonitor(node);
        if (pthread_mutex_destroy(&pool->lock) != 0) {
            fprintf(stderr, "Mutex destroy fail in thread_pool_init\n");
            goto fatal_exception;
        }
        goto exception;
    }

    pool->pool_size = num_threads;
    pool->head = NULL;
    pool->tail = NULL;
    pool->defered_tasks = 0;
    pool->count_waiting_workers = 0;
    pool->destroy = 0;
    pool->signal = 0;
    pool->circ_node = node;
    pthread_attr_t attr;
    if (pthread_attr_init (&attr) != 0) {
        thread_pool_unmonitor(node);
        if (pthread_mutex_destroy(&pool->lock) != 0 ||
            pthread_cond_destroy(&pool->waiting_workers) != 0) {
            fprintf(stderr, "Attr init failure in thread_pool_init\n");
            goto fatal_exception;
        }
        goto exception;
    }
    if (pthread_attr_setdetachstate (&attr,PTHREAD_CREATE_JOINABLE) != 0) {
        thread_pool_unmonitor(node);
        if (pthread_mutex_destroy(&pool->lock) != 0 ||
            pthread_cond_destroy(&pool->waiting_workers) != 0 ||
            pthread_attr_destroy(&attr) != 0) {
            fprintf(stderr, "Attr set failure in thread_pool_init\n");
            goto fatal_exception;
        }
        goto exception;
    }
    pool->workers = (pthread_t*) malloc(sizeof(pthread_t) * num_threads);
    if (pool->workers == NULL) {
        thread_pool_unmonitor(node);
        if (pthread_mutex_destroy(&pool->lock) != 0 ||
            pthread_cond_destroy(&pool->waiting_workers) != 0 ||
            pthread_attr_destroy(&attr) != 0) {
            fprintf(stderr, "Workers malloc failure in thread_pool_init\n");
            goto fatal_exception;
        }
        goto exception;
    }
    for (size_t i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->workers[i], &attr, workers, pool) != 0) {
            fprintf(stderr, "Pthread_t create failure in thread_pool_init\n");
            goto fatal_exception;
        }
    }
    return 0;
    exception:
        return -1;
    fatal_exception:
        exit(EXIT_FAILURE);
}

void thread_pool_destroy(thread_pool_t *pool) {
    void *res;
    if (pthread_mutex_lock(&pool->lock) != 0) {
        fprintf(stderr, "Mutex lock failure in thread_pool_destroy\n");
        goto fatal_exception;
    }
    pool->destroy = 1;
    if (pthread_mutex_unlock(&pool->lock) != 0) {
        fprintf(stderr, "Mutex unlock failure in thread_pool_destroy\n");
        goto fatal_exception;
    }
    if (pthread_cond_broadcast(&pool->waiting_workers) != 0) {
        fprintf(stderr, "Cond broadcast failure in thread_pool_destroy\n");
        goto fatal_exception;
    }
    for (size_t i = pool->signal; i < pool->pool_size; i++) {
        if (pthread_join(pool->workers[i], &res) != 0) {
            fprintf(stderr, "Pthread_t join failure in thread_pool_destroy\n");
            goto fatal_exception;
        }
    }

    free(pool->workers);
    queue_node_t *it = pool->head, *next_node;
    while (it != NULL) {
        next_node = it->next;
        free(it);
        it = next_node;
    }
    if (pthread_mutex_destroy(&pool->lock) != 0) {
        fprintf(stderr, "Mutex destroy failure in thread_pool_destroy\n");
        goto fatal_exception;
    }
    thread_pool_unmonitor(pool->circ_node);
    if (pthread_cond_destroy(&pool->waiting_workers) != 0) {
        fprintf(stderr, "Cond waiting_workers destroy failure in thread_pool_destroy\n");
        goto fatal_exception;
    }
    return;
    fatal_exception:
        exit(EXIT_FAILURE);
}

void thread_pool_join_signaled() {
    if (pthread_mutex_lock(&monitor.lock) != 0) {
        fprintf(stderr, "Mutex lock failure in thread_poll_join_signaled\n");
        goto fatal_exception;
    }
    while (monitor.signaled != monitor.count_threads) {
        if (pthread_cond_wait(&monitor.wait, &monitor.lock) != 0) {
            fprintf(stderr, "Cond wait failure in thread_poll_join_signaled\n");
            goto fatal_exception;
        }
    }
    monitor.signaled = 0;
    monitor.count_threads = 0;
    if (pthread_mutex_unlock(&monitor.lock) != 0) {
        fprintf(stderr, "Mutex unlock failure in thread_poll_join_signaled\n");
        goto fatal_exception;
    }
    fatal_exception:
        exit(EXIT_FAILURE);
}


int defer(thread_pool_t *pool, runnable_t runnable) {
    if (pool->destroy == 1)
        return -1;
    if (pthread_mutex_lock(&pool->lock) != 0) {
        goto exception;
    }

    if (pool->destroy == 1 || pool->signal == 1) {
        if (pthread_mutex_unlock(&pool->lock) != 0) {
            fprintf(stderr, "Mutex unlock failure in defer\n");
            goto fatal_exception;
        }
        goto exception;
    }
    queue_node_t *node = new_queue_node(runnable);
    if (node == NULL) {
        if (pthread_mutex_unlock(&pool->lock) != 0) {
            fprintf(stderr, "Mutex unlock failure in defer\n");
            goto fatal_exception;
        }
        goto exception;
    }
    pool->defered_tasks++;
    queue_node_t *tail;
    if (pool->defered_tasks == 1) {
        pool->head = node;
        pool->tail = node;
    } else {
        tail = pool->tail;
        tail->next = node;
        pool->tail = node;
    }
    if (pthread_mutex_unlock(&pool->lock) != 0) {
        fprintf(stderr, "Mutex unlock failure in defer\n");
        goto fatal_exception;
    }

    if (pthread_cond_broadcast(&pool->waiting_workers) != 0) {
        goto fatal_exception;
    }

    return 0;
    exception:
        return -1;
    fatal_exception:
        exit(EXIT_FAILURE);
        /*
    Exception: {
    if (pool->destroy != 1) {
        exit(EXIT_FAILURE);
    } else {
        //fprintf(stderr, "Can not defer on destroyed thread pool\n");
        return -1;
    }
}*/
}
