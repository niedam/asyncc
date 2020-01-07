#include <stdlib.h>
#include <stdio.h>
#include "future.h"

#define POOL_SIZE 3
#define TEST 10

typedef struct factorial {
    unsigned long long value;
    int n;
} factorial_t;


/** @brief Funkcja wykonująca mnożenie iloczynu
 * @param args[in] - argumenty (typu `factorial_t`)
 * @param argsz[in] - rozmiar argumentu (nieużywany)
 * @param result_size[out] - rozmiar wyniku
 * @return Wskaźnik na wynik mnożenia args.value * n
 */
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
    // Wczytanie liczby z której obliczamy silnie.
    int n;
    scanf("%d", &n);
    // Zainicjowanie pierwszego obliczenia.
    factorial_t *comp = malloc(sizeof(factorial_t));
    comp->n = n;
    comp->value = 1;
    future_t *future = malloc(sizeof(future_t) * n);
    if (async(&pool, future, (callable_t){.function = multiply, .arg = comp, .argsz = sizeof(factorial_t)}) != 0) {
        fprintf(stderr, "Nie udało się zlecić zadania pierwszego.\n");
        return -1;
    }
    // Powiązanie future w łańcuch obliczający silnię.
    for (int i = 1; i < n; i++) {
        if (map(&pool, future + i, future + (i - 1), multiply) != 0) {
            fprintf(stderr, "Błąd przy zleceniu obliczenia nr. %d", i + 1);
            return -1;
        }
    }
    // Odebranie wyniku i wypisanie go.
    factorial_t *result = await(&future[n - 1]);
    printf("%llu", result->value);
    // Zwolnienie zasobów.
    thread_pool_destroy(&pool);
    free(result);
    free(future);
    return 0;
}