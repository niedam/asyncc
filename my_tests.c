#include "threadpool.h"
#include <stdio.h>
#include <sys/unistd.h>

static void *print(void *args) {
    printf("T");
    sleep(1);
    printf("A");
    return NULL;
}

int main() {
    struct thread_pool a;
    thread_pool_init(&a, 4);


    for (int i = 0; i < 100; i++) {
        defer(&a, (runnable_t) {.argsz=1, .function=print, .arg=NULL});
        //defer(&a, (runnable_t) {.argsz=1, .function=print, .arg=NULL});
        //defer(&a, (runnable_t) {.argsz=1, .function=print, .arg=NULL});
    }
    sleep(2);
    thread_pool_destroy(&a);

}
