/*
 * Threads, mutexes and a per-thread slot for the program's own state.
 *
 * Every thread is started through the collector, so its stack and registers
 * are scanned. The collector does not scan thread-local storage, so anything
 * a thread keeps that points into the heap is held in a block from
 * GC_MALLOC_UNCOLLECTABLE, which it does scan, and freed as the thread ends.
 */

#define GC_THREADS
#include "internal.h"

#include <gc.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>

/* Defined by the program: calls the function value `action`, which takes
   nothing and answers nothing. A program that starts no thread need not
   define it. */
extern void $ghul_thread_entry(void *action) __attribute__((weak));

/* --- The per-thread slot -------------------------------------------------- */

static _Thread_local void **slot;

void *$ghul_host_thread_state(void) {
    return slot ? *slot : NULL;
}

void $ghul_host_thread_set_state(void *state) {
    if (!slot) {
        slot = GC_MALLOC_UNCOLLECTABLE(sizeof(void *));

        if (!slot) {
            $libghul_fail("Out of memory.\n");
        }
    }

    *slot = state;
}

/* --- Starting and joining ------------------------------------------------- */

#define DEFAULT_STACK_SIZE ((size_t)8 << 20)

typedef struct {
    pthread_t thread;
} handle;

/* What a new thread is handed. Uncollectable, so the action stays reachable
   until the thread holds it on its own stack. */
typedef struct {
    void *action;
    int64_t id;
} start;

static void *run(void *argument) {
    start *given = argument;
    void *action = given->action;
    $libghul_thread_id = given->id;
    GC_FREE(given);

    /* SIGINT is for the main thread to take. */
    sigset_t interrupt;
    sigemptyset(&interrupt);
    sigaddset(&interrupt, SIGINT);
    pthread_sigmask(SIG_BLOCK, &interrupt, NULL);

    void *signal_stack = $libghul_signal_stack_start();

    $ghul_thread_entry(action);

    $libghul_files_thread_exit();
    $libghul_spawn_thread_exit();

    if (slot) {
        GC_FREE(slot);
        slot = NULL;
    }

    $libghul_signal_stack_end(signal_stack);
    return NULL;
}

void *$ghul_host_thread_start(void *action, int64_t stack_size) {
    if (!$ghul_thread_entry) {
        return NULL;
    }

    handle *started = GC_MALLOC_ATOMIC(sizeof(handle));
    start *given = GC_MALLOC_UNCOLLECTABLE(sizeof(start));

    if (!started || !given) {
        $libghul_fail("Out of memory.\n");
    }

    given->action = action;
    given->id = $libghul_next_thread_id();

    pthread_attr_t attributes;
    pthread_attr_init(&attributes);

    /* musl's own default is 128 KB, far less than a main thread has. */
    pthread_attr_setstacksize(&attributes, stack_size > 0 ? (size_t)stack_size : DEFAULT_STACK_SIZE);

    int failed = GC_pthread_create(&started->thread, &attributes, run, given);
    pthread_attr_destroy(&attributes);

    if (failed) {
        GC_FREE(given);
        return NULL;
    }

    return started;
}

void $ghul_host_thread_join(void *thread) {
    GC_pthread_join(((handle *)thread)->thread, NULL);
}

/* --- Mutexes -------------------------------------------------------------- */

typedef struct {
    pthread_mutex_t mutex;

    /* The thread holding it, or 0. */
    atomic_llong owner;
} mutex;

void *$ghul_host_mutex_new(void) {
    /* Nothing in a mutex points into the heap. */
    mutex *made = GC_MALLOC_ATOMIC(sizeof(mutex));

    if (!made) {
        $libghul_fail("Out of memory.\n");
    }

    pthread_mutex_init(&made->mutex, NULL);
    atomic_init(&made->owner, 0);
    return made;
}

bool $ghul_host_mutex_lock(void *locked) {
    mutex *m = locked;
    int64_t me = $ghul_host_thread_id();

    if (atomic_load(&m->owner) == me) {
        return false;
    }

    pthread_mutex_lock(&m->mutex);
    atomic_store(&m->owner, me);
    return true;
}

bool $ghul_host_mutex_try_lock(void *locked) {
    mutex *m = locked;
    int64_t me = $ghul_host_thread_id();

    if (atomic_load(&m->owner) == me || pthread_mutex_trylock(&m->mutex) != 0) {
        return false;
    }

    atomic_store(&m->owner, me);
    return true;
}

bool $ghul_host_mutex_unlock(void *locked) {
    mutex *m = locked;

    if (atomic_load(&m->owner) != $ghul_host_thread_id()) {
        return false;
    }

    atomic_store(&m->owner, 0);
    pthread_mutex_unlock(&m->mutex);
    return true;
}
