/* Shared between libghul's own source files; not part of the ABI. */

#ifndef LIBGHUL_INTERNAL_H
#define LIBGHUL_INTERNAL_H

#include "ghul.h"

/* .NET's exit status when a program ends abnormally: 128 + SIGABRT. */
#define LIBGHUL_FAILURE_STATUS 134

/* Called once, by main, before the program runs. */
void libghul_arguments_init(int argc, char **argv);
void libghul_environment_init(char **environ);
void libghul_clock_init(void);

/* Writes everything still buffered for standard output. */
void libghul_flush_output(void);

/* Writes a message to standard error and ends the process with failure. */
_Noreturn void libghul_fail(const char *message);

/*
 * A growable run of UTF-16 code units, held in memory the collector does
 * not manage, for building a string to hand to ghul_make_string.
 */
typedef struct {
    uint16_t *chars;
    int32_t length;
    int32_t capacity;
} libghul_utf16;

void libghul_utf16_add(libghul_utf16 *text, uint16_t unit);
void *libghul_utf16_make_string(libghul_utf16 *text);

/*
 * Decodes `length` bytes of UTF-8 into `text`. A byte sequence that is not
 * valid UTF-8 becomes U+FFFD, as .NET's decoder makes it.
 */
void libghul_utf8_decode(const char *bytes, size_t length, libghul_utf16 *text);

/*
 * Encodes UTF-16 code units as UTF-8 into `out`, which has room for at
 * least 3 bytes per code unit. An unpaired surrogate becomes U+FFFD.
 * Answers the number of bytes written. `pending` carries a high surrogate
 * left at the end of one call over to the next; start it at 0.
 */
size_t libghul_utf8_encode(const uint16_t *chars, int32_t length, char *out, uint16_t *pending);

#endif
