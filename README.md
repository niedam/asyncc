# Asynchronous C - Thread pool and Future mechanism

Implementation of libraries to manage a group of worker threads, assign them concurrent tasks asynchronously and await for computations' results using Future mechanism.
 
Project for the course 
"[Concurrent programming](https://usosweb.mimuw.edu.pl/kontroler.php?_action=katalog2%2Fprzedmioty%2FpokazPrzedmiot&prz_kod=1000-213bPW&lang=en)" 
at my Computer Science studies. 


## Using Thread pool details (`threadpool.h`)

````
typedef struct runnable {
  void (*function)(void *, size_t);
  void *arg;
  size_t argsz;
} runnable_t;

typedef struct thread_pool thread_pool_t;

int thread_pool_init(thread_pool_t *pool, size_t pool_size);

void thread_pool_destroy(thread_pool_t *pool);

int defer(thread_pool_t *pool, runnable_t runnable);
````

* Execution `thread_pool_init(*pool, pool_size)` initialize resource and run `pool_size` worker-threads related with thread pool referenced as `pool`.
* Execution `thread_pool_destroy(*pool)` block adding new tasks to `pool`, finish working worker-thread and free all resource associated with the `pool`.
* Execution `defer(*pool, runnable)` add a new task to `pool` to asynchronous realisation.

##### Signal handling

Programs using the Thread pool have setting SIGINT signal handling. After receiving a signal, all working pools block deferring new tasks and finish their current tasks.

## Using Future details (`future.h`)

````
typedef struct callable {
  void *(*function)(void *, size_t, size_t *);
  void *arg;
  size_t argsz;
} callable_t;

typedef struct future future_t;

int async(thread_pool_t *pool, future_t *future, callable_t callable);

int map(thread_pool_t *pool, future_t *future, future_t *from,
        void *(*function)(void *, size_t, size_t *));

void *await(future_t *future);
````

* Execution `async(*pool, *future, callable)` defer a new task to realisation for `pool` and initialize `future` to receipt result of a computation.
* Execution `map(*pool, *future, *from, *(*function)(void*, size_t, size_t*))` defer a task to realisation for `pool` with argument received from `from` future related with previous defered task.
* Execution `await(future_t *future)` block the current thread until receive result of deferred computation related with `future`.

Getting result from `future_t` object more than once is undefined behaviour (similar to C++ thread pool)

## Examples of usage

Part of assigment was write two programs uses own implementation of thread pool.

#### Matrix (`macierz.c`)

Program read two numbers `k` and `n` from standard input. Then read `n*k` lines which represent cells of a matrix with `k` rows and `n` columns.
Every line has two number: the value of cell and time needed for computing value in cell (in milliseconds).
The program "computes" every value in cell (by waiting), sums all values in each row and print them to standard output.
 
Example:
````
2 
3
1 2
1 5
12 4
23 9
3 11
7 2
````

_This data represents that matrix_:

````
|  1  1 12 |
| 23  3  7 |
````

_and gives as result:_

````
14
33
````

#### Factorial (`silnia.c`)

The factorial program read a single number `n` from the standard input, and then calculate the number `n!` using a pool of 3 threads. After calculating print result to standard output. The program MUST (according to requirements) calculate the factorial using the `map` function and passing it as partial products. 

Example:
````
6
````
gives:
````
720
````