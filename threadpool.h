#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <stddef.h>
#include <pthread.h>


typedef struct runnable {
  void (*function)(void *, size_t);
  void *arg;
  size_t argsz;
} runnable_t;

typedef struct thread_pool {
    size_t pool_size;
    pthread_t *workers;
    void *head, *tail;
    void *circ_node;
    pthread_mutex_t lock;
    pthread_cond_t waiting_workers;
    size_t defered_tasks; /**<  */
    size_t count_waiting_workers; /**<  */
    int destroy;
    int signal;
} thread_pool_t;

int thread_pool_init(thread_pool_t *pool, size_t pool_size);

void thread_pool_destroy(thread_pool_t *pool);

int defer(thread_pool_t *pool, runnable_t runnable);

#endif
