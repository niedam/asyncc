#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "future.h"

#define POOLSIZE 4

int n, k;

/**
 * Task connected with cell from matrix.
 */
typedef struct task {
    int time; /**< Time to complete task */
    int result; /**< Result of task */
} task_t;

struct matrix_monitor {
    pthread_mutex_t lock;
    pthread_cond_t wait;
    int sum;
    int count;
} matrix_monitor = {.lock = PTHREAD_MUTEX_INITIALIZER, .wait = PTHREAD_COND_INITIALIZER, .sum = 0, .count = 0};

/**
 * Do single task and return result.
 * @param[in] args - pointer to task
 * @param[in] argsz - size of pointed memory by `args`
 * @param[out] resz - size of memory returned as result
 */
static void compute(void *args, size_t argsz __attribute__((unused))) {
    task_t *t = args;
    usleep(t->time);
    if (pthread_mutex_lock(&matrix_monitor.lock) != 0) {
        exit(EXIT_FAILURE);
    }
    matrix_monitor.sum += t->result;
    matrix_monitor.count++;
    if (matrix_monitor.count == n) {
        if (pthread_cond_broadcast(&matrix_monitor.wait) != 0) {
            exit(EXIT_FAILURE);
        }
    }
    if (pthread_mutex_unlock(&matrix_monitor.lock) != 0) {
        exit(EXIT_FAILURE);
    }
}

void wait_on_complete() {
    if (pthread_mutex_lock(&matrix_monitor.lock) != 0) {
        exit(EXIT_FAILURE);
    }
    while (matrix_monitor.count < n) {
        if (pthread_cond_wait(&matrix_monitor.wait, &matrix_monitor.lock) != 0) {
            exit(EXIT_FAILURE);
        }
    }
    printf("%d ", matrix_monitor.sum);
    if (pthread_mutex_unlock(&matrix_monitor.lock) != 0) {
        exit(EXIT_FAILURE);
    }
}

void reset_monitor() {
    if (pthread_mutex_lock(&matrix_monitor.lock) != 0) {
        exit(EXIT_FAILURE);
    }
    matrix_monitor.sum = 0;
    matrix_monitor.count = 0;
    if (pthread_mutex_unlock(&matrix_monitor.lock) != 0) {
        exit(EXIT_FAILURE);
    }
}


int main() {
    int err;
    thread_pool_t pool;
    // Init thread pool.
    if ((err = thread_pool_init(&pool, POOLSIZE)) != 0) {
        fprintf(stderr, "%d: Pool init failure in macierz\n", err);
        return(-1);
    }
    // Read size of matrix.
    scanf("%d", &k);
    scanf("%d", &n);
    // Allocate resource for computations.
    future_t *computations = malloc(sizeof(future_t) * k * n);
    task_t *tasks = malloc(sizeof(struct task) * k * n);
    // Assign tasks
    for (int i = 0; i < k * n; i++) {
        scanf("%d %d", &tasks[i].result, &tasks[i].time);
    }
    // Wait for results.
    int sum;
    int i_nk = 0;
    for (int i_k = 0; i_k < k; i_k++) {
        reset_monitor();
        for (int i_n = 0; i_n < n; i_n++) {
            defer(&pool, (runnable_t){.function = compute, .arg = &tasks[i_nk], .argsz = sizeof(task_t)});
            i_nk++;
        }
        // Print sum in `i_k` row.
        wait_on_complete();
    }
    // Free resources.
    thread_pool_destroy(&pool);
    free(computations);
    free(tasks);
    return 0;
}