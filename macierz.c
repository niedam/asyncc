#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "future.h"

#define POOLSIZE 4

/**
 * Task connected with cell from matrix.
 */
typedef struct task {
    int time; /**< Time to complete task */
    int result; /**< Result of task */
} task_t;


/**
 * Do single task and return result.
 * @param[in] args - pointer to task
 * @param[in] argsz - size of pointed memory by `args`
 * @param[out] resz - size of memory returned as result
 * @return Result of task
 */
static void *compute(void *args, size_t argsz __attribute__((unused)), size_t *resz) {
    task_t *t = args;
    usleep(t->time);
    *resz = sizeof(int);
    return &t->result;
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
    int k, n;
    scanf("%d", &k);
    scanf("%d", &n);
    // Allocate resource for computations.
    future_t *computations = malloc(sizeof(future_t) * k * n);
    task_t *tasks = malloc(sizeof(struct task) * k * n);
    // Assign tasks
    for (int i = 0; i < k * n; i++) {
        scanf("%d %d", &tasks[i].result, &tasks[i].time);
        async(&pool, &computations[i], (callable_t){.function = compute, .arg = &tasks[i], .argsz = sizeof(task_t)});
    }
    // Wait for results.
    int sum;
    int i_nk = 0;
    for (int i_k = 0; i_k < k; i_k++) {
        sum = 0;
        for (int i_n = 0; i_n < n; i_n++) {
            int *val = await(&computations[i_nk]);
            sum += *val;
            i_nk++;
        }
        // Print sum in `i_k` row.
        printf("%d\n", sum);
    }
    // Free resources.
    thread_pool_destroy(&pool);
    free(computations);
    free(tasks);
    return 0;
}