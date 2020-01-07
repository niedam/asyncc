#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <stddef.h>
#include <pthread.h>


/** @brief Struktura `runnable`.
 * Argument dla funkcji `defer(pool, runnable)` określający asynchroniczne zadanie do wykonania.
 * Za zarządzanie pamięcią tej struktury odpowiada użytkownik biblioteki.
 */
typedef struct runnable {
  void (*function)(void *, size_t);
  /**< @brief Funkcja do asynchronicznego wykonania.
   * @param arg[in, out] - wskaźnik na argument funkcji
   * @param args[in] - rozmiar argumentu funkcji
   * @result void
   */
  void *arg; /**< Wskaźnik na argument. */
  size_t argsz; /**< Rozmiar pamięci na którą wskazuje `arg`. */
} runnable_t;


/** @brief Struktura `thread_pool`.
 * Zmiana pól w strukturze przez użytkownika biblioteki może wywołać nieokreślone działanie programu.
 */
typedef struct thread_pool {
    pthread_mutex_t *lock; /**< Zamek realizujący wzajemne wykluczenie wątków używających puli. */
    size_t pool_size; /**< Liczba wątków działająca w puli. */
    pthread_t *workers; /**< Tablica identyfikatorów wątków z puli. */
    void *head; /**< Wskaźnik na początek kolejki zleconych zadań. */
    void *tail; /**< Wskaźnik na koniec kolejki zleconych zadań. */
    void *circ_node; /**< Wskaźnik do odbierania sygnału SIGINT. */
    pthread_cond_t *waiting_workers; /**< Zmienna warunkowa dla procesów z puli do czekania na zadania. */
    size_t defered_tasks; /**< Liczba zleconych zadań. */
    size_t count_waiting_workers; /**< Liczba wątków czekających na zadanie. */
    int destroy; /**< Flaga dotycząca zniszczenia puli. */
    int signal; /**< Flaga dotycząca otrzymania sygnału przez pulę. */
} thread_pool_t;


/** @brief Inicjacja puli wątków `thread_pool`
 * Funkcja inicjuje pulę wątków `pool` i uruchamia grupę procesów
 * z nią związaną o wielkości `pool_size`.
 * @param pool[in, out] - wskaźnik na pulę do zainicjowania
 * @param pool_size[in] - rozmiar inicjowanej puli
 * @return Wartość `0` jeżeli inicjacja zakończyła się sukcesem, w przeciwnym wypadku liczbę ujemną
 */
int thread_pool_init(thread_pool_t *pool, size_t pool_size);


/** @brief Zwolnienie puli wątków `thread_pool`
 * Funkcja blokuje możliwość dodawania do puli nowych zadań i po zakończeniu
 * wszystkich zleconych zadań zwalnia zasoby związane z pulą.
 * Blokuje aktualny wątek do czasu zakończenia wszystkich wątków w puli.
 * @param pool[in, out] - wskaźnik na pulę do zwolnienia
 * @return void
 */
void thread_pool_destroy(thread_pool_t *pool);


/** @brief Czeka na wątki przerwane sygnałem SIGINT.
 * Funkcja blokuje aktualny proces do czasu, aż wszystkie pule
 * przerwane sygnałem SIGINT zakończą swoją pracę.
 * Do poprawnego działania wymagane jest wysłanie wygnału SIGINT przez użytkownika biblioteki.
 * @return void
 */
void thread_pool_join_signaled();


/** @brief Zlecenie nowego zadania dla puli `thread_pool`
 * Funkcja zleca do asynchronicznego wykonania przez pulę zadanie `runnable`.
 * @param pool[in, out] - wskaznik na pulę wątków roboczych
 * @param runnable[in, out] - zadanie do wykonania
 * @return Wartość `0` jeżeli udało się pomyślnie zlecić zadanie, w przeciwnym wypadku liczbę ujemną
 */
int defer(thread_pool_t *pool, runnable_t runnable);

#endif
