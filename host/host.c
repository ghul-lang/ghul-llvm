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
 *
 *     ghul-host --serve <commands> [-- <argument>...]
 *
 * keeps the process, and so every cell's code and data, for as long as it
 * is asked to: it reads one line at a time from the file or named pipe
 * <commands>, and a line naming a cell loads it and runs it. The program's
 * standard input and output stay the program's. When a cell has run, the
 * host flushes what it printed and then writes a line to standard output
 * that begins with the byte 0x01, "cell-done" and the cell's status, so a
 * driver knows where that cell's output ends. An exception nothing in the
 * cell catches is reported, and ends that cell, not the session. A line
 * "quit", or the end of the commands, ends the host.
 */

#include "internal.h"

#include <dlfcn.h>
#include <gc.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern char **environ;

extern void *$libghul_unhandled_jump;

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

static double elapsed_ms(struct timespec from, struct timespec to) {
    return (double)(to.tv_sec - from.tv_sec) * 1000.0 + (double)(to.tv_nsec - from.tv_nsec) / 1e6;
}

/* The status of a cell that did not load. */
enum { LOAD_FAILED = 127, UNHANDLED = 134, INTERRUPTED = 130 };

/*
 * A SIGINT while a cell runs ends that cell, as an exception nothing catches
 * would, and leaves the session as the cell left it; one that arrives between
 * cells is ignored. The cell is abandoned where it stands, so whatever it was
 * in the middle of, including an allocation, is not finished.
 */
static void interrupt_cell(int signal_number) {
    (void)signal_number;

    if ($libghul_unhandled_jump) {
        siglongjmp(*(sigjmp_buf *)$libghul_unhandled_jump, 2);
    }
}

/*
 * Loads the cell at `path` and runs its entry point. A cell that has none,
 * one of definitions alone, has nothing to run. Lazy, so a call into a cell
 * loaded later binds when it is first made; global, so a later cell binds to
 * the names an earlier one defines. Every cell defines the entry points
 * above; those are looked up in the cell's own handle, and the host's
 * versions come first in the global scope.
 */
static int run_cell(const char *path, int timing) {
    struct timespec start, loaded, finished;

    clock_gettime(CLOCK_MONOTONIC, &start);

    void *cell = dlopen(path, RTLD_LAZY | RTLD_GLOBAL);

    if (!cell) {
        fprintf(stderr, "ghul-host: %s\n", dlerror());
        return LOAD_FAILED;
    }

    void *found;

    /* What a cell does not define stays as the cell before it left it. */
    if ((found = dlsym(cell, "$ghul_make_string"))) cell_make_string = found;
    if ((found = dlsym(cell, "$ghul_make_bytes"))) cell_make_bytes = found;
    if ((found = dlsym(cell, "$ghul_report_unhandled"))) cell_report_unhandled = found;
    if ((found = dlsym(cell, "$ghul_make_null_reference"))) cell_make_null_reference = found;

    cell_entry = dlsym(cell, "$ghul_entry");

    clock_gettime(CLOCK_MONOTONIC, &loaded);

    int status = 0;
    sigjmp_buf boundary;

    /* An exception nothing catches comes back here once it is reported. */
    int jumped = sigsetjmp(boundary, 1);

    if (jumped == 0) {
        $libghul_unhandled_jump = &boundary;

        status = cell_entry ? $ghul_entry() : 0;
    } else {
        status = jumped == 2 ? INTERRUPTED : UNHANDLED;
    }

    $libghul_unhandled_jump = NULL;

    $libghul_flush_output();

    clock_gettime(CLOCK_MONOTONIC, &finished);

    if (timing) {
        fprintf(stderr, "ghul-host: %s: load %.3f ms, run %.3f ms, status %d\n", path,
                elapsed_ms(start, loaded), elapsed_ms(loaded, finished), status);
    }

    return status;
}

static int serve(const char *commands, int timing) {
    FILE *input = fopen(commands, "r");

    if (!input) {
        perror(commands);
        return 2;
    }

    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;

    while ((length = getline(&line, &capacity, input)) >= 0) {
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }

        if (length == 0) {
            continue;
        }

        if (strcmp(line, "quit") == 0) {
            break;
        }

        int status = run_cell(line, timing);

        $libghul_flush_output();

        printf("\001" "cell-done %d\n", status);
        fflush(stdout);
    }

    free(line);
    fclose(input);

    return 0;
}

int main(int argc, char **argv) {
    int first = 1;
    int serving = argc > 2 && strcmp(argv[1], "--serve") == 0;

    if (serving) {
        first = 3;
    }

    int cells = first;

    while (cells < argc && strcmp(argv[cells], "--") != 0) {
        cells++;
    }

    if (!serving && cells < 2) {
        fprintf(stderr, "usage: ghul-host <cell.so>... [-- <argument>...]\n"
                        "       ghul-host --serve <commands> [-- <argument>...]\n");
        return 2;
    }

    int first_argument = cells < argc ? cells + 1 : argc;

    GC_INIT();

    struct sigaction action;

    memset(&action, 0, sizeof action);
    action.sa_handler = interrupt_cell;
    action.sa_flags = SA_RESTART;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);

    $libghul_fault_init();
    $libghul_clock_init();
    /* The entry before the first argument stands for the program name. */
    $libghul_arguments_init(argc - first_argument + 1, argv + first_argument - 1);
    $libghul_environment_init(environ);

    atexit($libghul_flush_output);

    int timing = getenv("GHUL_HOST_TIMING") != NULL;

    if (serving) {
        return serve(argv[2], timing);
    }

    int status = 0;

    for (int i = 1; i < cells; i++) {
        status = run_cell(argv[i], timing);

        if (status == LOAD_FAILED) {
            return 2;
        }
    }

    return status;
}
