#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "future.h"

#define POOLSIZE 4

typedef struct task {
    int time;
    int result;
} task_t;

static void *compute(void *args, size_t argsz __attribute__((unused)), size_t *result_size) {
    task_t *t = args;

    //usleep(t->time);

    *result_size = sizeof(int);
    return &t->result;
}

int main() {
    int k, n;
    int err;
    thread_pool_t pool;
    if ((err = thread_pool_init(&pool, POOLSIZE)) != 0) {
        fprintf(stderr, "%d: Pool init failure in macierz\n", err);
        return(-1);
    }
    scanf("%d", &k);
    scanf("%d", &n);

    future_t *computations = malloc(sizeof(future_t) * k * n);
    struct task *tasks = malloc(sizeof(struct task) * k * n);


    for (int i = 0; i < k * n; i++) {
        scanf("%d %d", &tasks[i].result, &tasks[i].time);
        async(&pool, &computations[i], (callable_t){.function = compute, .arg = &tasks[i], .argsz = sizeof(task_t)});

    }

    int sum;
    int i_nk = 0;
    for (int i_k = 0; i_k < k; i_k++) {
        sum = 0;
        for (int i_n = 0; i_n < n; i_n++) {
            int *val = await(&computations[i_nk]);

            sum += *val;
            i_nk++;
        }
        printf("%d\n", sum);
    }

    thread_pool_destroy(&pool);
}