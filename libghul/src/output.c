/*
 * Standard output and standard error. Output is buffered and written when
 * the buffer fills, when the program flushes it, before anything goes to
 * standard error, before standard input is read or polled, and at exit, so
 * the two streams interleave in the order the program wrote them. Standard
 * error is written as it comes.
 *
 * Threads share the buffer under one lock, held for the whole of each write,
 * so one call's text is never split by another thread's.
 */

#include "internal.h"

#include <errno.h>
#include <pthread.h>
#include <unistd.h>

#define BUFFER_SIZE 65536

/* The most UTF-8 one UTF-16 code unit can encode to. */
#define BYTES_PER_UNIT 3

/* How many code units are encoded at a time into a stack buffer. */
#define CHUNK_UNITS 1024

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static char buffer[BUFFER_SIZE];
static size_t buffered;

/* A high surrogate written at the end of one of this thread's calls,
   awaiting its low half, by stream. */
static _Thread_local uint16_t pending[3];

/* Whether standard output is a terminal, set once before the program runs. */
static int is_terminal;

void $libghul_output_init(void) {
    is_terminal = isatty(1);
}

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

/* Writes the buffer out. The caller holds the lock. */
static void flush_locked(void) {
    write_all(1, buffer, buffered);
    buffered = 0;
}

void $libghul_flush_output(void) {
    pthread_mutex_lock(&lock);
    flush_locked();
    pthread_mutex_unlock(&lock);
}

void $libghul_flush_output_if_free(void) {
    if (pthread_mutex_trylock(&lock) == 0) {
        flush_locked();
        pthread_mutex_unlock(&lock);
    }
}

void $ghul_host_flush(int32_t stream) {
    /* Standard error is written as it comes, so only output waits. */
    if (stream != 2) {
        $libghul_flush_output();
    }
}

static void write_stdout(const uint16_t *chars, int32_t length) {
    while (length > 0) {
        int32_t units = length < CHUNK_UNITS ? length : CHUNK_UNITS;

        if (buffered + (size_t)units * BYTES_PER_UNIT + 4 > BUFFER_SIZE) {
            flush_locked();
        }

        buffered += $libghul_utf8_encode(chars, units, buffer + buffered, &pending[1]);

        chars += units;
        length -= units;
    }
}

static void write_stderr(const uint16_t *chars, int32_t length) {
    char chunk[CHUNK_UNITS * BYTES_PER_UNIT + 4];

    flush_locked();

    while (length > 0) {
        int32_t units = length < CHUNK_UNITS ? length : CHUNK_UNITS;
        size_t bytes = $libghul_utf8_encode(chars, units, chunk, &pending[2]);

        write_all(2, chunk, bytes);

        chars += units;
        length -= units;
    }
}

void $ghul_host_write(const uint16_t *chars, int32_t length, int32_t stream) {
    pthread_mutex_lock(&lock);

    if (stream == 2) {
        write_stderr(chars, length);
    } else {
        write_stdout(chars, length);

        /* At a terminal, output is seen as it is written. */
        if (is_terminal) {
            flush_locked();
        }
    }

    pthread_mutex_unlock(&lock);
}
