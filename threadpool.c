#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include "threadpool.h"


static struct thread_pool_monitor {
    pthread_mutex_t lock;
    pthread_cond_t wait;
    int used;
    void *queue;
    int flag[2];
    int turn;
    int signaled;
    int count_threads;
} monitor = {.lock = PTHREAD_MUTEX_INITIALIZER, .queue = NULL, .used = 0, .flag = {0, 0}, .signaled = 0,
        .wait = PTHREAD_COND_INITIALIZER, .count_threads = 0};

typedef struct circ_queue_node {
    thread_pool_t *pool;
    struct circ_queue_node *next;
    struct circ_queue_node *prev;
} circ_queue_node_t;

typedef struct join_queue_node {
    pthread_t thread;
    struct join_queue_node *next;
} join_queue_node_t;


void destroy_monitor() {
    if (pthread_mutex_destroy(&monitor.lock) != 0  ||
        pthread_cond_destroy(&monitor.wait) != 0) {
        goto really_fatal_exception;
    }
    return;
    really_fatal_exception:
    _Exit(EXIT_FAILURE);
}


static void thread_pool_signal_done() {
    join_queue_node_t *node = malloc(sizeof(join_queue_node_t));
    if (node == NULL) {
        exit(EXIT_FAILURE);
    }
    monitor.count_threads++;
    if (monitor.signaled == monitor.count_threads) {
        if (pthread_cond_signal(&monitor.wait) != 0) {
            exit(EXIT_FAILURE);
        }
    }
    if (pthread_mutex_unlock(&monitor.lock) != 0) {
        exit(EXIT_FAILURE);
    }
}

static circ_queue_node_t *new_circ_node(thread_pool_t *pool) {
    circ_queue_node_t *result = malloc(sizeof(circ_queue_node_t));
    if (result == NULL) {
        return NULL;
    }
    result->pool = pool;
    return result;
}


static void catch(int sig __attribute__((unused))) {
    monitor.flag[1] = 1;
    monitor.turn = 0;
    while (monitor.flag[0] == 1 && monitor.turn == 0) {
        // busy wait
    }
    circ_queue_node_t *curr = monitor.queue;
    circ_queue_node_t *start = monitor.queue;
    if (monitor.queue != 0) {
        do {
            curr->pool->signal = 1;
            curr = curr->next;
        } while (curr != start);
    }
    monitor.queue = NULL;
    monitor.flag[1] = 0;
}

static circ_queue_node_t *thread_pool_monitor(thread_pool_t *pool) {
    if (pthread_mutex_lock(&monitor.lock) != 0) {
        goto exception;
    }
    monitor.flag[0] = 1;
    monitor.turn = 1;
    while (monitor.flag[1] == 1 && monitor.turn == 1) {
        // busy wait
    }
    if (monitor.used == 0) {
        atexit(destroy_monitor);

        struct sigaction action;
        sigset_t block_mask;
        sigemptyset (&block_mask);

        action.sa_handler = catch;
        action.sa_mask = block_mask;
        action.sa_flags = SA_RESTART | SA_NODEFER;

        if (sigaction (SIGINT, &action, 0) == -1) {
            // TODO
        }
        if (sigprocmask(SIG_BLOCK, &block_mask, 0) == -1) {
            // TODO
        }
    }
    circ_queue_node_t *new_node = new_circ_node(pool);
    if (new_node == NULL) {
        if (pthread_mutex_unlock(&monitor.lock) != 0) {
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
        goto fatal_exception;
    }
    return new_node;
    exception:
    return NULL;
    fatal_exception:
    exit(EXIT_FAILURE);
}


static void thread_pool_unmonitor(void *node) {
    if (pthread_mutex_lock(&monitor.lock) != 0) {
        goto fatal_exception;
    }
    monitor.flag[0] = 1;
    monitor.turn = 1;
    while (monitor.flag[1] == 1 && monitor.turn == 1) {
        // busy wait
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
    free(node);
    if (pthread_mutex_unlock(&monitor.lock) != 0) {
        goto fatal_exception;
    }
    return;
    fatal_exception:
    exit(EXIT_FAILURE);
}


/**
 *
 */
typedef struct queue_node {
    struct queue_node *next; /**< Pointer to next element in queue */
    runnable_t runnable; /**< `runnable` task assigment to node */
} queue_node_t;


/**
 *
 * @param[in] runnable - function to run
 * @return Pointer to allocated `queue_node`
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



static void *workers(void *arg) {
    thread_pool_t *pool = (struct thread_pool*) arg;
    queue_node_t *node = NULL;
    int err = 0;
    int first_thread = (pthread_equal(pthread_self(), pool->workers[0]) != 0?1:0);
    while (1) {
        if ((err = pthread_mutex_lock(&pool->lock)) != 0) {
            fprintf(stderr, "%d: Mutex lock failure in workers\n", err);
            goto Exception;
        }
        pool->count_waiting_workers++;
        while (pool->defered_tasks < 1) {
            if (pool->destroy == 1 || pool->signal == 1) {
                pool->count_waiting_workers--;
                if ((err = pthread_mutex_unlock(&pool->lock)) != 0) {
                    fprintf(stderr, "%d: Mutex unlock failure in workers\n", err);
                    goto Exception;
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
            if ((err = pthread_cond_wait(&pool->waiting_workers, &pool->lock)) != 0) {
                fprintf(stderr, "%d: Cond wait failure in workers\n", err);
                goto Exception;
            }
        }
        pool->count_waiting_workers--;
        node = pool->head;
        pool->defered_tasks--;
        pool->head = node->next;
        if (pool->count_waiting_workers > 0) {
            if ((err = pthread_cond_broadcast(&pool->waiting_workers)) != 0) {
                // TODO
            }
        }


        if ((err = pthread_mutex_unlock(&pool->lock)) != 0) {
            fprintf(stderr, "%d: Mutex unlock failure in workers\n", err);
            goto Exception;
        }
        node->runnable.function(node->runnable.arg, node->runnable.argsz);
        free(node);
    }
    Exception: {
    exit(EXIT_FAILURE);
}
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
        //fprintf(stderr, "%d: Mutex init failure in thread_pool_init\n", err);
        thread_pool_unmonitor(node);
        goto exception;
    }
    if (pthread_cond_init(&pool->waiting_workers, NULL) != 0) {
        //fprintf(stderr, "%d: Condition init failure in thread_pool_init\n", err);
        thread_pool_unmonitor(node);
        if (pthread_mutex_destroy(&pool->lock) != 0) {
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
        //fprintf(stderr, "%d: Pthread_attr init failure in thread_pool_init\n", err);
        thread_pool_unmonitor(node);
        if (pthread_mutex_destroy(&pool->lock) != 0 ||
            pthread_cond_destroy(&pool->waiting_workers) != 0) {
            goto fatal_exception;
        }
        goto exception;
    }
    if (pthread_attr_setdetachstate (&attr,PTHREAD_CREATE_JOINABLE) != 0) {
        //fprintf(stderr, "%d: Pthread_attr setdetachstate failure in thread_pool_init\n", err);
        thread_pool_unmonitor(node);
        if (pthread_mutex_destroy(&pool->lock) != 0 ||
            pthread_cond_destroy(&pool->waiting_workers) != 0 ||
            pthread_attr_destroy(&attr) != 0) {
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
            goto fatal_exception;
        }
        goto exception;
    }
    for (size_t i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->workers[i], &attr, workers, pool) != 0) {
            //fprintf(stderr, "%d: Pthread_t create failure in thread_pool_init\n", err);
            goto fatal_exception;
        }
    }
    return 0;
    exception:
    return -1;
    fatal_exception:
    exit(EXIT_FAILURE);
}

void thread_pool_destroy(struct thread_pool *pool) {
    void *res;
    int err = 0;
    thread_pool_unmonitor(pool->circ_node);
    pool->destroy |= 1;
    if ((err = pthread_cond_broadcast(&pool->waiting_workers)) != 0) {
        fprintf(stderr, "%d: Cond broadcast failure in thread_pool_destroy\n", err);
        goto fatal_exception;
    }
    for (size_t i = pool->signal; i < pool->pool_size; i++) {
        if ((err = pthread_join(pool->workers[i], &res)) != 0) {
            fprintf(stderr, "%d: Pthread_t join failure in thread_pool_destroy\n", err);
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
    if ((err = pthread_mutex_destroy(&pool->lock)) != 0) {
        fprintf(stderr, "%d: Mutex destroy failure in thread_pool_destroy\n", err);
        goto fatal_exception;
    }
    if ((err = pthread_cond_destroy(&pool->waiting_workers)) != 0) {
        fprintf(stderr, "%d: Cond waiting_workers destroy failure in thread_pool_destroy\n", err);
        goto fatal_exception;
    }
    return;
    fatal_exception: {
    exit(EXIT_FAILURE);
}
}

void thread_pool_join_signaled() {
    if (pthread_mutex_lock(&monitor.lock) != 0) {
        goto fatal_exception;
    }
    while (monitor.signaled != monitor.count_threads) {
        if (pthread_cond_wait(&monitor.wait, &monitor.lock) != 0) {
            goto fatal_exception;
        }
    }
    monitor.signaled = 0;
    monitor.count_threads = 0;
    if (pthread_mutex_unlock(&monitor.lock) != 0) {
        goto fatal_exception;
    }
    fatal_exception:
        exit(EXIT_FAILURE);
}


int defer(struct thread_pool *pool, runnable_t runnable) {
    int err = 0;
    if (pool->destroy == 1)
        return -1;
    if (pthread_mutex_lock(&pool->lock) != 0) {
        // Mutex lock failure, task didn't defer
        return -1;
    }

    if (pool->destroy == 1 || pool->signal == 1) {
        if ((err = pthread_mutex_unlock(&pool->lock)) != 0) {
            // Unlocked mutex can cause errors.
            fprintf(stderr, "%d: Mutex unlock failure in defer\n", err);
            goto Exception;
        }
        return -1;
    }
    queue_node_t *node = new_queue_node(runnable);
    if (node == NULL) {
        if ((err = pthread_mutex_unlock(&pool->lock)) != 0) {
            // Unlocked mutex can cause errors.
            fprintf(stderr, "%d: Mutex unlock failure in defer\n", err);
            goto Exception;
        }
        // Memory allocation failure, task didn't defer
        return -1;
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
    if ((err = pthread_mutex_unlock(&pool->lock)) != 0) {
        // Unlocked mutex can cause errors.
        fprintf(stderr, "%d: Mutex unlock failure in defer\n", err);
        goto Exception;
    }

    if ((err = pthread_cond_broadcast(&pool->waiting_workers)) != 0) {
        // TODO
    }

    return 0;
    Exception: {
    if (pool->destroy != 1) {
        exit(EXIT_FAILURE);
    } else {
        //fprintf(stderr, "Can not defer on destroyed thread pool\n");
        return -1;
    }
}
}
