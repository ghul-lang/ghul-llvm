/* Shared between libghul's own source files; not part of the ABI. */

#ifndef LIBGHUL_INTERNAL_H
#define LIBGHUL_INTERNAL_H

#include "ghul.h"

/* .NET's exit status when a program ends abnormally: 128 + SIGABRT. */
#define LIBGHUL_FAILURE_STATUS 134

/* Called once, by main, before the program runs. */
void $libghul_arguments_init(int argc, char **argv);
void $libghul_environment_init(char **environ);
void $libghul_clock_init(void);
void $libghul_fault_init(void);
void $libghul_output_init(void);

/* Write the stack, innermost frame first, to standard error: from the caller,
 * or from the registers a signal handler was handed. */
void $libghul_write_backtrace(void);
void $libghul_write_signal_backtrace(void *signal_context);

/* Writes everything still buffered for standard output. */
void $libghul_flush_output(void);

/* The same, unless another thread is writing, as from a signal handler,
 * which may have interrupted a write on its own thread. */
void $libghul_flush_output_if_free(void);

/* Gives the calling thread its own stack for signal handlers, so a stack
 * overflow can be reported; answers it, or NULL where it could not. */
void *$libghul_signal_stack_start(void);
void $libghul_signal_stack_end(void *signal_stack);

/* The calling thread's id, 0 until it is given one, and the next id to give. */
extern _Thread_local int64_t $libghul_thread_id;
int64_t $libghul_next_thread_id(void);

/* Free what a thread kept for itself, as it ends. */
void $libghul_files_thread_exit(void);

/* Frees the calling thread's slot for the program's state, as it ends. */
void $libghul_thread_slot_end(void);
void $libghul_spawn_thread_exit(void);

/* Calls the program's $ghul_exit_sweep, where it defines one, once. */
void $libghul_exit_sweep(void);

/* Writes a message to standard error and ends the process with failure. */
_Noreturn void $libghul_fail(const char *message);

/*
 * A growable run of UTF-16 code units, held in memory the collector does
 * not manage, for building a string to hand to $ghul_make_string.
 */
typedef struct {
    uint16_t *chars;
    int32_t length;
    int32_t capacity;
} $libghul_utf16;

void $libghul_utf16_add($libghul_utf16 *text, uint16_t unit);
void *$libghul_utf16_make_string($libghul_utf16 *text);

/*
 * Decodes `length` bytes of UTF-8 into `text`. A byte sequence that is not
 * valid UTF-8 becomes U+FFFD, as .NET's decoder makes it.
 */
void $libghul_utf8_decode(const char *bytes, size_t length, $libghul_utf16 *text);

/*
 * Encodes UTF-16 code units as UTF-8 into `out`, which has room for at
 * least 3 bytes per code unit. An unpaired surrogate becomes U+FFFD.
 * Answers the number of bytes written. `pending` carries a high surrogate
 * left at the end of one call over to the next; start it at 0.
 */
size_t $libghul_utf8_encode(const uint16_t *chars, int32_t length, char *out, uint16_t *pending);

#endif
