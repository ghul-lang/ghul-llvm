/* main, which starts the collector and the program, and leaving. */

#include "internal.h"

#include <gc.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

const int32_t ghul_runtime_abi_2 = GHUL_RUNTIME_ABI_VERSION;

int main(int argc, char **argv) {
    GC_INIT();

    ghulrt_clock_init();
    ghulrt_arguments_init(argc, argv);
    ghulrt_environment_init(environ);

    /* A program that ends by calling exit still has its output written. */
    atexit(ghulrt_flush_output);

    return ghul_entry();
}

_Noreturn void ghulrt_fail(const char *message) {
    ghulrt_flush_output();

    size_t length = strlen(message);

    while (length > 0) {
        ssize_t written = write(2, message, length);

        if (written <= 0) {
            break;
        }

        message += written;
        length -= (size_t)written;
    }

    _exit(GHULRT_FAILURE_STATUS);
}
