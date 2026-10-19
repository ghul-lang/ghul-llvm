/*
 * The cell host: a glibc executable that loads cells, shared objects
 * ghul-llvm --emit=cell writes, and runs each one's entry point.
 *
 * libghul, the collector, the unwinder and the big integer library are
 * linked into the host and every name they define is exported, so a cell
 * binds to them when it is loaded and carries none of them itself. A cell
 * defines what libghul calls back into ($ghul_entry, $ghul_make_string,
 * $ghul_make_bytes, $ghul_report_unhandled and $ghul_make_null_reference);
 * the host finds those in the cell it is running and forwards to them.
 *
 *     ghul-host <cell.so>... [-- <argument>...]
 *
 * runs each cell in turn in this one process, with the arguments after "--"
 * as the program's command line. The exit status is the last cell's entry
 * result.
 */

#include "internal.h"

#include <dlfcn.h>
#include <gc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern char **environ;

static int32_t (*cell_entry)(void);
static void *(*cell_make_string)(const uint16_t *, int32_t);
static void *(*cell_make_bytes)(const uint8_t *, int32_t);
static void (*cell_report_unhandled)(void *);
static void *(*cell_make_null_reference)(void);

int32_t $ghul_entry(void) { return cell_entry(); }

void *$ghul_make_string(const uint16_t *chars, int32_t length) {
    return cell_make_string(chars, length);
}

void *$ghul_make_bytes(const uint8_t *bytes, int32_t length) {
    return cell_make_bytes(bytes, length);
}

void $ghul_report_unhandled(void *exception) {
    if (cell_report_unhandled) {
        cell_report_unhandled(exception);
    }
}

void *$ghul_make_null_reference(void) { return cell_make_null_reference(); }

static void *symbol(void *cell, const char *name, int required) {
    void *found = dlsym(cell, name);

    if (!found && required) {
        fprintf(stderr, "ghul-host: the cell does not define %s\n", name);
        exit(2);
    }

    return found;
}

static double elapsed_ms(struct timespec from, struct timespec to) {
    return (double)(to.tv_sec - from.tv_sec) * 1000.0 + (double)(to.tv_nsec - from.tv_nsec) / 1e6;
}

int main(int argc, char **argv) {
    int cells = 1;

    while (cells < argc && strcmp(argv[cells], "--") != 0) {
        cells++;
    }

    if (cells < 2) {
        fprintf(stderr, "usage: ghul-host <cell.so>... [-- <argument>...]\n");
        return 2;
    }

    int first_argument = cells < argc ? cells + 1 : argc;

    GC_INIT();

    $libghul_fault_init();
    $libghul_clock_init();
    /* The entry before the first argument stands for the program name. */
    $libghul_arguments_init(argc - first_argument + 1, argv + first_argument - 1);
    $libghul_environment_init(environ);

    atexit($libghul_flush_output);

    int status = 0;
    int timing = getenv("GHUL_HOST_TIMING") != NULL;

    for (int i = 1; i < cells; i++) {
        struct timespec start, loaded, finished;

        clock_gettime(CLOCK_MONOTONIC, &start);

        /* Global, so a later cell binds to the names an earlier one defines.
         * Every cell also defines the entry points above; those are looked
         * up in the cell's own handle below, and the host's versions come
         * first in the global scope. */
        void *cell = dlopen(argv[i], RTLD_NOW | RTLD_GLOBAL);

        if (!cell) {
            fprintf(stderr, "ghul-host: %s\n", dlerror());
            return 2;
        }

        cell_entry = symbol(cell, "$ghul_entry", 0);
        cell_make_string = symbol(cell, "$ghul_make_string", 0);
        cell_make_bytes = symbol(cell, "$ghul_make_bytes", 0);
        cell_report_unhandled = symbol(cell, "$ghul_report_unhandled", 0);
        cell_make_null_reference = symbol(cell, "$ghul_make_null_reference", 0);

        clock_gettime(CLOCK_MONOTONIC, &loaded);

        /* A cell of definitions alone has nothing to run. */
        status = cell_entry ? $ghul_entry() : 0;

        $libghul_flush_output();

        clock_gettime(CLOCK_MONOTONIC, &finished);

        if (timing) {
            fprintf(stderr, "ghul-host: %s: load %.3f ms, run %.3f ms, status %d\n", argv[i],
                    elapsed_ms(start, loaded), elapsed_ms(loaded, finished), status);
        }
    }

    return status;
}
