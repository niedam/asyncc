#include <stdio.h>
#include "future.h"

#define POOLSIZE 4


int main() {
    int err;
    thread_pool_t pool;
    if ((err = thread_pool_init(&pool, POOLSIZE)) != 0) {
        fprintf(stderr, "%d: Pool init failure in macierz\n", err);
        return(-1);
    }


}