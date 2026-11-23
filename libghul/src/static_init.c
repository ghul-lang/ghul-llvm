/*
 * The guard that runs a static initialiser once however many threads reach
 * it. It is apart from thread.c, which only a program that starts threads
 * needs, because every program's static initialisers come through here.
 */

#include "internal.h"

#include <pthread.h>
#include <stdatomic.h>

/*
 * A guard is 0 before its initialiser has started and 2 once it has
 * finished. While it runs, the guard holds the running thread's id, shifted
 * left two with the low bit set, so the thread can tell its own re-entry
 * from another thread's arrival. Threads that arrive while another runs it
 * wait here; there is one condition for every guard, since a wait happens
 * only the first time a type is used.
 */
enum { NOT_STARTED = 0, DONE = 2 };

static pthread_mutex_t waiting = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t finished = PTHREAD_COND_INITIALIZER;

static void wake_all(void) {
    pthread_mutex_lock(&waiting);
    pthread_cond_broadcast(&finished);
    pthread_mutex_unlock(&waiting);
}

/* Runs as an initialiser that threw unwinds through: a later use runs it
   again, rather than waiting for it forever. */
static void abandon(_Atomic int32_t **guard) {
    if (*guard) {
        atomic_store_explicit(*guard, NOT_STARTED, memory_order_release);
        wake_all();
    }
}

void $ghul_static_init(int32_t *flag, void (*initialise)(void)) {
    _Atomic int32_t *guard = (_Atomic int32_t *)flag;
    int32_t mine = (int32_t)(((uint32_t)$ghul_host_thread_id() << 2) | 1);

    for (;;) {
        int32_t seen = NOT_STARTED;

        if (atomic_compare_exchange_strong_explicit(guard, &seen, mine, memory_order_acquire,
                                                    memory_order_acquire)) {
            _Atomic int32_t *running __attribute__((cleanup(abandon))) = guard;

            initialise();

            running = NULL;
            atomic_store_explicit(guard, DONE, memory_order_release);
            wake_all();
            return;
        }

        /* Done, or this thread is already running it. */
        if (seen == DONE || seen == mine) {
            return;
        }

        pthread_mutex_lock(&waiting);

        while ((seen = atomic_load_explicit(guard, memory_order_acquire)) != DONE && seen != NOT_STARTED) {
            pthread_cond_wait(&finished, &waiting);
        }

        pthread_mutex_unlock(&waiting);

        if (seen == DONE) {
            return;
        }
    }
}
