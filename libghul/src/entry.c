/* main, which starts the collector and the program, and leaving. */

#include "internal.h"

#include <gc.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

const int32_t $ghul_abi_4 = GHUL_ABI_VERSION;

/* Defined by a program whose core library keeps state that has to be flushed
   or closed as the program ends; absent from one that keeps none. */
extern void $ghul_exit_sweep(void) __attribute__((weak));

void $libghul_exit_sweep(void) {
    static int swept;

    /* Once: the sweep can end in an exception nothing catches, whose path
       out sweeps too. */
    if (swept || !$ghul_exit_sweep) {
        return;
    }

    swept = 1;
    $ghul_exit_sweep();
}

/* Defined only by the collector built with marker threads, and declared by
   gc.h only for a program built for threads, which libghul is not. */
extern void GC_set_markers_count(unsigned markers) __attribute__((weak));

/*
 * Under the collector built with marker threads, at most four markers unless
 * GC_MARKERS says otherwise: more than four gained little on the heaps
 * measured, and each costs a thread. The threads start here rather than at
 * the program's first pthread_create, which a ghūl program never calls.
 * Under the single-threaded collector, starting them does nothing.
 */
static void start_collector(void) {
    if (GC_set_markers_count && !getenv("GC_MARKERS")) {
        long processors = sysconf(_SC_NPROCESSORS_ONLN);

        GC_set_markers_count(processors < 1 ? 1 : processors > 4 ? 4 : (unsigned)processors);
    }

    GC_INIT();
    GC_start_mark_threads();
}

int main(int argc, char **argv) {
    start_collector();

    $libghul_fault_init();
    $libghul_clock_init();
    $libghul_arguments_init(argc, argv);
    $libghul_environment_init(environ);

    /* A program that ends by calling exit still has its output written, after
       the sweep has written what the program left open: atexit runs the two
       in the reverse of this order. */
    atexit($libghul_flush_output);
    atexit($libghul_exit_sweep);

    return $ghul_entry();
}

_Noreturn void $libghul_fail(const char *message) {
    $libghul_flush_output();

    size_t length = strlen(message);

    while (length > 0) {
        ssize_t written = write(2, message, length);

        if (written <= 0) {
            break;
        }

        message += written;
        length -= (size_t)written;
    }

    _exit(LIBGHUL_FAILURE_STATUS);
}
