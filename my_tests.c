#include "threadpool.h"
#include <stdio.h>
#include <sys/unistd.h>

static void *printX(void *args) {
    printf("X");
    //sleep(1);
    return NULL;
}

static void *printY(void *args) {
    printf("Y");
    //sleep(1);
    //printf("A");
    return NULL;
}


void *stop(void *parm) {
    thread_pool_destroy((thread_pool_t*)parm);
}

int main() {
    struct thread_pool a;
    int err;
    pthread_attr_t attr;
    pthread_t thread;
    if ((err = pthread_attr_init (&attr)) != 0) {
        fprintf(stderr, "%d: Pthread_attr init failure in thread_pool_init\n", err);
        return -1;
    }
    if ((err = pthread_attr_setdetachstate (&attr,PTHREAD_CREATE_JOINABLE)) != 0) {
        fprintf(stderr, "%d: Pthread_attr setdetachstate failure in thread_pool_init\n", err);
        return -1;
    }

    thread_pool_init(&a, 4);


    for (int i = 0; i < 100; i++) {
        defer(&a, (runnable_t) {.argsz=1, .function=printX, .arg=NULL});

    }

    if ((err = pthread_create(&thread, &attr, stop, &a)) != 0) {
        fprintf(stderr, "%d: Pthread_t create failure in thread_pool_init\n", err);
        return -1;
    }


    for (int i = 0; i < 300; i++) {
        defer(&a, (runnable_t) {.argsz=1, .function=printY, .arg=NULL});
    }
    sleep(1);
    void * res;
    if ((err = pthread_join(thread, &res)) != 0) {
        return 1;
    }
    return 0;
}
