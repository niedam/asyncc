#include "threadpool.h"
#include <stdio.h>
#include <sys/unistd.h>

static void printX(void *args, size_t a) {
    printf("X");
    usleep(1000);
    return;
}

static void printY(void *args, size_t a) {
    printf("Y");
    //sleep(1);
    //printf("A");
    return;
}


static void printZ(void *args, size_t b) {
    printf("Z");
    usleep(1000);
    //sleep(1);
    //printf("A");
    return;
}


int main() {
    struct thread_pool a;
    struct thread_pool b;
    struct thread_pool c;
    int err;
    pthread_attr_t attr;
    pthread_t thread;
    /*if ((err = pthread_attr_init (&attr)) != 0) {
        fprintf(stderr, "%d: Pthread_attr init failure in thread_pool_init\n", err);
        return -1;
    }
    if ((err = pthread_attr_setdetachstate (&attr,PTHREAD_CREATE_JOINABLE)) != 0) {
        fprintf(stderr, "%d: Pthread_attr setdetachstate failure in thread_pool_init\n", err);
        return -1;
    }*/

    thread_pool_init(&a, 8);
    thread_pool_init(&b, 4);
    thread_pool_init(&c, 2);


    for (int i = 0; i < 1000 * 1000; i++) {
        int j = i % 3;
        if (j == 0) {
            if (defer(&a, (runnable_t) {.argsz=1, .function=printX, .arg=NULL}) != 0) {
                break;
            }
        } else if (j == 1) {
            if (defer(&b, (runnable_t) {.argsz=1, .function=printY, .arg=NULL}) != 0) {
                break;
            }
        } else {
            if (defer(&c, (runnable_t) {.argsz=1, .function=printZ, .arg=NULL}) != 0) {
                break;
            }
        }
    }
    printf("\n teraz to\n");
    thread_pool_join_signaled();
    thread_pool_destroy(&a);
    thread_pool_destroy(&b);
    thread_pool_destroy(&c);
    /*thread_pool_t d;
    thread_pool_init(&d, 3);
    for (int i = 0; i <= 1000 * 1000 * 1000; i++) {
        defer(&d, (runnable_t) {.argsz=1, .function=printZ, .arg=NULL});
    }


    thread_pool_t e;
    thread_pool_init(&e, 3);
    for (int i = 0; i <= 1000 * 1000; i++) {
        defer(&e, (runnable_t) {.argsz=1, .function=printX, .arg=NULL});
    }*/

    //thread_pool_destroy(&d);

    //thread_pool_join_signaled();
    //thread_pool_destroy(&e);
    //sleep(2);
    //thread_pool_destroy(&a);

    /*if ((err = pthread_create(&thread, &attr, stop, &a)) != 0) {
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
    }*/
    return 0;
}
