#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "future.h"
#include "minunit.h"

int tests_run = 0;
static thread_pool_t pool;
static future_t future;

static void *squared(void *arg, size_t argsz __attribute__((unused)),
                     size_t *retsz __attribute__((unused))) {
  int n = *(int *)arg;
  int *ret = malloc(sizeof(int));
  *ret = n * n;
  return ret;
}

static char *test_await_simple() {
  thread_pool_init(&pool, 2);

  int n = 16;
  async(&pool, &future,
        (callable_t){.function = squared, .arg = &n, .argsz = sizeof(int)});
  int *m = await(&future);

  mu_assert("expected 256", *m == 256);
  free(m);

  thread_pool_destroy(&pool);
  return 0;
}

static void *add_four(void *arg, size_t argsz, size_t *ressz) {
    int *x = arg;
    *ressz = sizeof(int);
    int *res = malloc(sizeof(int));
    *res = *x + 4;
    free(arg);
    return res;
}

static char *test_map_simple() {
    thread_pool_init(&pool, 3);
    int *n = (int*) malloc(sizeof(int));
    *n = 0;
    future_t *last = malloc(sizeof(future_t));
    async(&pool, last, (callable_t){.function = add_four, .arg = n, .argsz = sizeof(int)});

    future_t *fut = malloc(sizeof(future_t));
    map(&pool, fut, last, add_four);

    int *m = await(fut);
    mu_assert("expected 4", *m == 8);
    thread_pool_destroy(&pool);
    free(m);
    free(last);
    free(fut);
    return 0;
}


static char *test_100map_simple() {
    thread_pool_init(&pool, 3);
    int *n = (int*) malloc(sizeof(int));
    *n = 0;
    future_t *last = malloc(sizeof(future_t));
    async(&pool, last, (callable_t){.function = add_four, .arg = n, .argsz = sizeof(int)});

    for (int i = 0; i < 99; i++) {
        future_t *fut = malloc(sizeof(future_t));
        map(&pool, fut, last, add_four);
        last = fut;
    }

    int *m = await(last);
    mu_assert("expected 400", *m == 400);
    thread_pool_destroy(&pool);
    return 0;
}

static char *all_tests() {
  //mu_run_test(test_await_simple);
  mu_run_test(test_map_simple);
  //mu_run_test(test_100map_simple);

  return 0;
}

int main() {
  char *result = all_tests();
  if (result != 0) {
    printf(__FILE__ " %s\n", result);
  } else {
    printf(__FILE__ " ALL TESTS PASSED\n");
  }
  printf(__FILE__ " Tests run: %d\n", tests_run);

  return result != 0;
}
