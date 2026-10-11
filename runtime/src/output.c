/*
 * Standard output and standard error. Output is buffered and written when
 * the buffer fills, before anything goes to standard error, before standard
 * input is read, and at exit, so the two streams interleave in the order
 * the program wrote them. Standard error is written as it comes.
 */

#include "internal.h"

#include <errno.h>
#include <unistd.h>

#define BUFFER_SIZE 65536

/* The most UTF-8 one UTF-16 code unit can encode to. */
#define BYTES_PER_UNIT 3

/* How many code units are encoded at a time into a stack buffer. */
#define CHUNK_UNITS 1024

static char buffer[BUFFER_SIZE];
static size_t buffered;

/* A high surrogate written at the end of one call, awaiting its low half. */
static uint16_t pending[3];

static void write_all(int fd, const char *bytes, size_t length) {
    while (length > 0) {
        ssize_t written = write(fd, bytes, length);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            /* Nowhere to report a failure to write: the output is lost. */
            return;
        }

        bytes += written;
        length -= (size_t)written;
    }
}

void ghulrt_flush_output(void) {
    write_all(1, buffer, buffered);
    buffered = 0;
}

static void write_stdout(const uint16_t *chars, int32_t length) {
    while (length > 0) {
        int32_t units = length < CHUNK_UNITS ? length : CHUNK_UNITS;

        if (buffered + (size_t)units * BYTES_PER_UNIT + 4 > BUFFER_SIZE) {
            ghulrt_flush_output();
        }

        buffered += ghulrt_utf8_encode(chars, units, buffer + buffered, &pending[1]);

        chars += units;
        length -= units;
    }
}

static void write_stderr(const uint16_t *chars, int32_t length) {
    char chunk[CHUNK_UNITS * BYTES_PER_UNIT + 4];

    ghulrt_flush_output();

    while (length > 0) {
        int32_t units = length < CHUNK_UNITS ? length : CHUNK_UNITS;
        size_t bytes = ghulrt_utf8_encode(chars, units, chunk, &pending[2]);

        write_all(2, chunk, bytes);

        chars += units;
        length -= units;
    }
}

void ghul_host_write(const uint16_t *chars, int32_t length, int32_t stream) {
    if (stream == 2) {
        write_stderr(chars, length);
    } else {
        write_stdout(chars, length);

        /* At a terminal, output is seen as it is written. */
        static int is_terminal = -1;

        if (is_terminal < 0) {
            is_terminal = isatty(1);
        }

        if (is_terminal) {
            ghulrt_flush_output();
        }
    }
}
