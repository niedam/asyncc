#include <stdlib.h>
#include <stdio.h>
#include "threadpool.h"


static struct thread_pool_monitor {
    pthread_mutex_t lock;
    int used;
    void *queue;
} monitor = {.lock = PTHREAD_MUTEX_INITIALIZER, .queue = NULL, .used = 0};

void destroy_monitor() {
    pthread_mutex_destroy(&monitor.lock);
}

typedef struct circ_queue_node {
    thread_pool_t *pool;
    struct circ_queue_node *next;
    struct circ_queue_node *prev;
} circ_queue_node_t;

static circ_queue_node_t *new_circ_node(thread_pool_t *pool) {
    circ_queue_node_t *result = malloc(sizeof(circ_queue_node_t));
    if (result == NULL) {
        return NULL;
    }
    result->pool = pool;
    return result;
}


static circ_queue_node_t *thread_pool_monitor(thread_pool_t *pool) {
    if (pthread_mutex_lock(&monitor.lock) != 0) {
        goto exception;
    }
    if (monitor.used == 0) {
        atexit(destroy_monitor);
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
    while (1) {
        if ((err = pthread_mutex_lock(&pool->lock)) != 0) {
            fprintf(stderr, "%d: Mutex lock failure in workers\n", err);
            goto Exception;
        }
        pool->count_waiting_workers++;
        while (pool->defered_tasks < 1) {
            if (pool->destroy) {
                pool->count_waiting_workers--;
                if ((err = pthread_mutex_unlock(&pool->lock)) != 0) {
                    fprintf(stderr, "%d: Mutex unlock failure in workers\n", err);
                    goto Exception;
                }
                return NULL;
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
    if ((err = pthread_mutex_lock(&pool->lock)) != 0) {
        fprintf(stderr, "%d: Mutex lock failure in thread_pool_destroy\n", err);
        goto Exception;
    }
    if (pool->destroy) {
        if ((err = pthread_mutex_unlock(&pool->lock)) != 0) {
            fprintf(stderr, "%d: Mutex unlock failure in thread_pool_destroy\n", err);
            goto Exception;
        }
        return;
    }
    pool->destroy |= 1;
    if ((err = pthread_mutex_unlock(&pool->lock)) != 0) {
        fprintf(stderr, "%d: Mutex unlock failure in thread_pool_destroy\n", err);
        goto Exception;
    }
    if ((err = pthread_cond_broadcast(&pool->waiting_workers)) != 0) {
        fprintf(stderr, "%d: Cond broadcast failure in thread_pool_destroy\n", err);
        goto Exception;
    }
    for (size_t i = 0; i < pool->pool_size; i++) {
        if ((err = pthread_join(pool->workers[i], &res)) != 0) {
            fprintf(stderr, "%d: Pthread_t join failure in thread_pool_destroy\n", err);
            goto Exception;
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
        goto Exception;
    }
    if ((err = pthread_cond_destroy(&pool->waiting_workers)) != 0) {
        fprintf(stderr, "%d: Cond waiting_workers destroy failure in thread_pool_destroy\n", err);
        goto Exception;
    }
    return;
    Exception: {
        exit(EXIT_FAILURE);
    }
}

int defer(struct thread_pool *pool, runnable_t runnable) {
    int err = 0;
    if (pool->destroy == 1)
        return -1;
    if (pthread_mutex_lock(&pool->lock) != 0) {
        // Mutex lock failure, task didn't defer
        return -1;
    }

    if (pool->destroy) {
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
