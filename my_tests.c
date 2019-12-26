#include "threadpool.h"


static void *print(void *args) {
    print("Test1");
}

int main() {
    struct thread_pool a;
    thread_pool_init(&a, 4);

    defer(&a, (runnable_t){.argsz=1, .function=print, .arg=NULL});
    defer(&a, (runnable_t){.argsz=1, .function=print, .arg=NULL});
    defer(&a, (runnable_t){.argsz=1, .function=print, .arg=NULL});

    thread_pool_destroy(&a);

}
