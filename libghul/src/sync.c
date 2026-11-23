/*
 * The per-thread slot for the program's own state, mutexes and condition
 * variables. None of them starts a thread, so the cell host builds this
 * file as well as the static and dynamic links: a program uses them for
 * state it keeps whether or not it starts threads.
 *
 * The collector does not scan thread-local storage, so the slot is a block
 * from GC_MALLOC_UNCOLLECTABLE, which it does scan, freed as a thread ends.
 */

#include "internal.h"

#include <errno.h>
#include <gc.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

/* --- The per-thread slot -------------------------------------------------- */

static _Thread_local void **slot;

void $libghul_thread_slot_end(void) {
    if (slot) {
        GC_FREE(slot);
        slot = NULL;
    }
}

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

/* --- Condition variables -------------------------------------------------- */

typedef struct {
    pthread_cond_t condition;
} condition;

void *$ghul_host_condition_new(void) {
    /* Nothing in a condition variable points into the heap. */
    condition *made = GC_MALLOC_ATOMIC(sizeof(condition));
    pthread_condattr_t attributes;

    if (!made) {
        $libghul_fail("Out of memory.\n");
    }

    /* Timeouts are measured on the monotonic clock, so setting the
       system clock neither cuts a wait short nor stretches it. */
    pthread_condattr_init(&attributes);
    pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    pthread_cond_init(&made->condition, &attributes);
    pthread_condattr_destroy(&attributes);
    return made;
}

int32_t $ghul_host_condition_wait(void *waited, void *locked, int64_t timeout_ms) {
    condition *c = waited;
    mutex *m = locked;
    int64_t me = $ghul_host_thread_id();
    int result = 0;

    if (atomic_load(&m->owner) != me) {
        return -1;
    }

    atomic_store(&m->owner, 0);

    if (timeout_ms < 0) {
        pthread_cond_wait(&c->condition, &m->mutex);
    } else {
        struct timespec until;

        clock_gettime(CLOCK_MONOTONIC, &until);
        until.tv_sec += timeout_ms / 1000;
        until.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;

        if (until.tv_nsec >= 1000000000L) {
            until.tv_sec += 1;
            until.tv_nsec -= 1000000000L;
        }

        result = pthread_cond_timedwait(&c->condition, &m->mutex, &until);
    }

    atomic_store(&m->owner, me);
    return result == ETIMEDOUT ? 0 : 1;
}

void $ghul_host_condition_signal(void *signalled) {
    condition *c = signalled;

    pthread_cond_signal(&c->condition);
}

void $ghul_host_condition_broadcast(void *signalled) {
    condition *c = signalled;

    pthread_cond_broadcast(&c->condition);
}
