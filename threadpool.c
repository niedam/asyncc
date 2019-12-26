#include <stdlib.h>
#include <stdio.h>
#include "threadpool.h"

struct queue_node {
    queue_node *next; /**< Pointer to next element in queue */
    runnable_t runnable; /**< `runnable` assigment to node */
};


/**
 *
 * @param[in] runnable - function to run
 * @return Pointer to allocated `queue_node`
 */
static queue_node *new_queue_node(runnable_t runnable) {
    queue_node *result = (queue_node*) malloc(sizeof(queue_node));
    if (result == NULL) {
        fprintf(stderr, "Malloc failure in new_queueu_node\n");
        goto Exception;
    }
    result->next = NULL;
    result->runnable = runnable;
    return result;
    Exception: {
        exit(EXIT_FAILURE);
    }
}

static void *workers(void *arg) {
    thread_pool_t *pool = (struct thread_pool*) arg;
    queue_node *node = NULL;
    int err = 0;
    while (1) {
        if ((err = pthread_mutex_init(&pool->lock, 0)) != 0) {
            fprintf(stderr, "%d: Mutex lock failure in workers\n", err);
            goto Exception;
        }

        pool->count_waiting_workers++;
        while (pool->defered_tasks < 1) {
            if ((err = pthread_cond_wait(&pool->waiting_workers, &pool->lock)) != 0) {
                fprintf(stderr, "%d: Cond wait failure in workers\n", err);
                goto Exception;
            }
        }
        pool->count_waiting_workers--;

        node = pool->head;
        pool->defered_tasks--;
        pool->head = node->next;

        node->runnable.function(node->runnable.arg, node->runnable.argsz);
        free(node);
    }
    return NULL;
    Exception: {
        exit(EXIT_FAILURE);
    }
}

int thread_pool_init(thread_pool_t *pool, size_t num_threads) {
    int err = 0;
    if ((err = pthread_mutex_init(&pool->lock, 0)) != 0) {
        fprintf(stderr, "%d: Mutex init failure in thread_pool_init\n", err);
        goto Exception;
    }
    if ((err = pthread_cond_init(&pool->waiting_workers, NULL)) != 0) {
        fprintf(stderr, "%d: Condition init failure in thread_pool_init\n", err);
        goto Exception;
    }
    pool->pool_size = num_threads;
    pool->head = NULL;
    pool->tail = NULL;
    pool->defered_tasks = 0;
    pool->count_waiting_workers = 0;
    pthread_attr_t attr;
    if ((err = pthread_attr_init (&attr)) != 0) {
        fprintf(stderr, "%d: Pthread_attr init failure in thread_pool_init\n", err);
        goto Exception;
    }
    if ((err = pthread_attr_setdetachstate (&attr,PTHREAD_CREATE_JOINABLE)) != 0) {
        fprintf(stderr, "%d: Pthread_attr setdetachstate failure in thread_pool_init\n", err);
        goto Exception;
    }
    pool->workers = (pthread_t*) malloc(sizeof(pthread_t) * num_threads);
    for (size_t i = 0; i < num_threads; i++) {
        if ((err = pthread_create(&pool->workers[i], &attr, workers, pool)) != 0) {
            fprintf(stderr, "%d: Pthread_t create failure in thread_pool_init\n", err);
            goto Exception;
        }
    }
    return 0;
    Exception: {
        exit(EXIT_FAILURE);
    }
}

void thread_pool_destroy(struct thread_pool *pool) {
    void *res;
    int err = 0;
    for (size_t i = 0; i < pool->pool_size; i++) {
        if ((err = pthread_join(pool->workers[i], &res)) != 0) {
            fprintf(stderr, "%d: Pthread_t join failure in thread_pool_destroy\n", err);
            goto Exception;
        }
    }
    queue_node *it = pool->head, *next_node;
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
    if ((err = pthread_mutex_lock(&pool->lock)) != 0) {
        fprintf(stderr, "%d: Mutex lock failure in defer\n", err);
        goto Exception;
    }
    pool->defered_tasks++;
    queue_node *node = new_queue_node(runnable);
    if (pool->defered_tasks == 1) {
        pool->head = node;
        pool->tail = node;
    } else {
        pool->tail->next = node;
        pool->tail = node;
    }
    if ((err = pthread_mutex_unlock(&pool->lock)) != 0) {
        fprintf(stderr, "%d: Mutex unlock failure in defer\n", err);
        goto Exception;
    }
    return 0;
    Exception: {
        exit(EXIT_FAILURE);
    }
}
