/* main, which starts the collector and the program, and leaving. */

#include "internal.h"

#include <gc.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

const int32_t $ghul_abi_4 = GHUL_ABI_VERSION;

/* The collector calls this where it would otherwise answer null, so an
   allocation either succeeds or ends the program, and the program may take
   every allocation it is answered to be an object. */
static void *out_of_memory(size_t size) {
    (void)size;

    $libghul_fail("Out of memory.\n");
}

int main(int argc, char **argv) {
    GC_INIT();
    GC_set_oom_fn(out_of_memory);

    $libghul_null_init();
    $libghul_clock_init();
    $libghul_arguments_init(argc, argv);
    $libghul_environment_init(environ);

    /* A program that ends by calling exit still has its output written. */
    atexit($libghul_flush_output);

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
