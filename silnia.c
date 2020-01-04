#include <stdlib.h>
#include <stdio.h>
#include "future.h"

#define POOL_SIZE 3
#define TEST 10

typedef struct factorial {
    unsigned long long value;
    int n;
} factorial_t;


static void *multiply(void *args, size_t argsz __attribute__((unused)), size_t *result_size) {
    factorial_t *arg_fac = args;
    *result_size = sizeof(factorial_t);
    factorial_t *result = malloc(*result_size);
    result->value = arg_fac->value * arg_fac->n;
    result->n = arg_fac->n - 1;
    free(arg_fac);
    return result;
}

int main() {
    thread_pool_t pool;
    if (thread_pool_init(&pool, POOL_SIZE) != 0) {
        return -1;
    }
    int n;
    scanf("%d", &n);
    factorial_t *comp = malloc(sizeof(factorial_t));
    comp->n = n;
    comp->value = 1;
    future_t *future = malloc(sizeof(future_t) * n);
    if (async(&pool, future, (callable_t){.function = multiply, .arg = comp, .argsz = sizeof(factorial_t)}) != 0) {
        // TODO;
        return -1;
    }
    for (int i = 1; i < n; i++) {
        map(&pool, future + i, future + (i - 1), multiply);
    }
    factorial_t *result = await(&future[n - 1]);
    printf("%llu", result->value);
    thread_pool_destroy(&pool);
    free(result);
    free(future);
}