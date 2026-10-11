/* Throwing. There is no unwinding yet, so every exception is unhandled. */

#include "internal.h"

#include <unistd.h>

_Noreturn void ghul_throw(void *exception) {
    ghulrt_flush_output();
    ghul_report_unhandled(exception);
    ghulrt_flush_output();

    _exit(GHULRT_FAILURE_STATUS);
}
