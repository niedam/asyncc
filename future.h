#ifndef FUTURE_H
#define FUTURE_H

#include <pthread.h>

#include "threadpool.h"

/** @brief Struktura `callable`.
 * Argument dla funkcji `async(pool, fut, callable)` określający asynchroniczne obliczenie do wykonania.
 * Za zarządzanie pamięcią tej struktury odpowiada użytkownik biblioteki.
 */
typedef struct callable {
  void *(*function)(void *, size_t, size_t *);
  /**< @brief Funkcja do obliczenia.
   * @param arg[in, out] - wskaźnik do argumentu funkcji
   * @param size[in] - rozmiar argumentu
   * @param resz[out] - miejsce do zapisania rozmiaru wyniku funkcji
   * @return Wskaźnik do wyniku.
   */
  void *arg; /**< Wskaźnik na argumenty funkcji. */
  size_t argsz; /**< Rozmiar argumentu funkcji. */
} callable_t;


/** @brief Struktura mechanizmu Future.
 * Użytkownik biblioteki jest odpowiedzialny za zarządzanie pamięcią wskazywaną przez `*result`
 * oraz do zwolnienia mechanizmów pthread `lock` i `wait` po zakończeniu pracy z biblioteką.
 * Inne zmiany pól w strukturze przez użytkownika biblioteki może wywołać nieokreślone działanie programu.
 */
typedef struct future {
    pthread_mutex_t *lock; /**< Zamek ograniczający dostęp */
    pthread_cond_t *wait; /**< Zmienna warunkowa do oczekiwania na wynik */
    int ready; /**< Flaga oznaczająca gotowy/niegotowy wynik */
    void *result; /**< Wskaźnik na wynik obliczeń */
    size_t ressz; /**< Rozmiar wyniku (do odczytu dla użytkownika). */
} future_t;


/** @brief Zlecenie asynchronicznych obliczeń puli `thread_pool`.
 * Zleca puli `*pool` obliczenie reprezentowane przez `callable`.
 * Otrzymany wynik będzie dostępny poprzez `*future`.
 * @param pool[in, out] - wskaznik na pulę wątków roboczych
 * @param future[out] - struktura do odebrania wyniku
 * @param callable[in] - obliczenie do wykonania
 * @return Wartość `0` jeżeli zlecenie obliczenia powiodło się, w przeciwnym przypadku wartość ujemną.
 */
int async(thread_pool_t *pool, future_t *future, callable_t callable);


/** @brief Wykonanie obliczeń na wyniku zleconych wcześniej obliczeń.
 * Zlecenie puli `*pool` obliczenia wartości funkcji, której argumentem jest wynik odczytany z `*from`
 * i zapisanie wyniku w `*future`.
 * @param pool[in, out] - wskaznik na pulę wątków roboczych
 * @param future[out] - struktura do odebrania wyniku
 * @param from[in] - struktura z której odczytujemy argument
 * @param function[in] - funkcja do wykonania `fun(arg, args, resz)`, gdzie 'arg` to argument funkcji,
 *                          `args` to rozmiar argumentu, `resz` to miejsce do zapisania rozmiaru wyniku
 * @return Wartość `0` jeżeli zlecenie obliczenia powiodło się, w przeciwnym przypadku wartość ujemną.
 */
int map(thread_pool_t *pool, future_t *future, future_t *from,
        void *(*function)(void *, size_t, size_t *));


/** @brief Oczekiwanie na wynik asynchronicznego obliczenia.
 * Funkcja blokuje wątek do momentu w którym wynik ze struktury `*future` będzie gotowy do odczytania.
 * @param future[in] - struktura z której odebrany ma być wynik
 * @return Wskaźnik na wynik obliczeń.
 */
void *await(future_t *future);

#endif
