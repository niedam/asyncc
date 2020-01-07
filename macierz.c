#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "future.h"

#define POOLSIZE 4

int n, k;

/** @brief Zadanie obliczeniowe związane z konkretną komórką macierzy.
 */
typedef struct task {
    int time; /**< Czas na ukończenie zadania. */
    int result; /**< Wynik zadania obliczeniowego. */
} task_t;


/** @brief Monitor zapewniający synchronizację wątków sumujących swoje wyniki.
 */
struct matrix_monitor {
    pthread_mutex_t lock; /**< Zamek na monitor. */
    pthread_cond_t wait; /**< Oczekiwanie na gotowy wynik. */
    int sum; /**< Wynik sumowania. */
    int count; /**< Liczba zsumowanych komórek. */
} matrix_monitor = {.lock = PTHREAD_MUTEX_INITIALIZER, .wait = PTHREAD_COND_INITIALIZER, .sum = 0, .count = 0};

/** @brief Pojedyńcze zadanie obliczeniowe.
 * @param[in] args - wskaźnik na opis zadania
 * @param[in] argsz - rozmiar `args`
 */
static void compute(void *args, size_t argsz __attribute__((unused))) {
    task_t *t = args;
    usleep(t->time * 1000);
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


/** @brief Blokuje wątek do czasu aż w monitorze nie zsumowane zostanie `n` liczb.
 * @result void
 */
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


/** @brief Zerowanie liczników w monitorze.
 * @return void
 */
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
    // Inicjacja puli.
    if ((err = thread_pool_init(&pool, POOLSIZE)) != 0) {
        fprintf(stderr, "%d: Pool init failure in macierz\n", err);
        return(-1);
    }
    // Przeczytanie rozmiaru macierzy.
    scanf("%d", &k);
    scanf("%d", &n);
    // Zaalokowanie zasobów.
    future_t *computations = malloc(sizeof(future_t) * k * n);
    task_t *tasks = malloc(sizeof(struct task) * k * n);
    // Przypisywanie zadań.
    for (int i = 0; i < k * n; i++) {
        scanf("%d %d", &tasks[i].result, &tasks[i].time);
    }
    // Obliczenia.
    int i_nk = 0;
    for (int i_k = 0; i_k < k; i_k++) {
        reset_monitor();
        for (int i_n = 0; i_n < n; i_n++) {
            defer(&pool, (runnable_t){.function = compute, .arg = &tasks[i_nk], .argsz = sizeof(task_t)});
            i_nk++;
        }
        // Oczekiwanie na wynik, wypisanie wyniku.
        wait_on_complete();
    }
    // Zwolnienie zasobów.
    thread_pool_destroy(&pool);
    pthread_cond_destroy(&matrix_monitor.wait);
    pthread_mutex_destroy(&matrix_monitor.lock);
    free(computations);
    free(tasks);
    return 0;
}