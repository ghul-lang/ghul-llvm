/*
 * Standard input, read as the program asks for it, decoded from UTF-8 into
 * UTF-16 code units. Lines end at \n, \r\n or \r, as .NET's ReadLine ends
 * them.
 */

#include "internal.h"

#include <errno.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

#define READ_SIZE 4096

/* The longest UTF-8 sequence, which is never split between decodes. */
#define MAX_SEQUENCE 4

static char bytes[READ_SIZE + MAX_SEQUENCE];
static size_t byte_count;

/* Code units decoded and not yet taken. */
static $libghul_utf16 decoded;
static int32_t taken;

/*
 * How far has_line has looked: the code units from taken up to here hold no
 * line ending, so a poll while a long line arrives looks at each unit once.
 */
static int32_t scanned;

static int at_end;

/*
 * Whether a read has answered the end of the input to the program, after
 * which nothing is pending. A line cut off by the end does not count: the
 * read after it answers the end without waiting, so the end is still pending.
 */
static int end_read;

/* How many bytes at the end of `bytes` start a UTF-8 sequence not yet complete. */
static size_t incomplete_tail(void) {
    for (size_t back = 1; back <= MAX_SEQUENCE - 1 && back <= byte_count; back++) {
        unsigned char byte = (unsigned char)bytes[byte_count - back];

        if ((byte & 0xC0) == 0x80) {
            continue;
        }

        size_t needed = byte >= 0xF0 ? 4 : byte >= 0xE0 ? 3 : byte >= 0xC0 ? 2 : 1;

        return needed > back ? back : 0;
    }

    return 0;
}

/* Decodes more input; answers false at the end of the input. */
static int fill(void) {
    if (at_end) {
        return 0;
    }

    /* A prompt written without a newline is seen before the read waits. */
    $libghul_flush_output();

    decoded.length = 0;
    taken = 0;
    scanned = 0;

    while (decoded.length == 0) {
        ssize_t got = read(0, bytes + byte_count, READ_SIZE);

        if (got < 0 && errno == EINTR) {
            continue;
        }

        if (got <= 0) {
            at_end = 1;

            /* What is left was cut off by the end of the input. */
            $libghul_utf8_decode(bytes, byte_count, &decoded);
            byte_count = 0;

            return decoded.length > 0;
        }

        byte_count += (size_t)got;

        size_t keep = incomplete_tail();

        $libghul_utf8_decode(bytes, byte_count - keep, &decoded);

        for (size_t i = 0; i < keep; i++) {
            bytes[i] = bytes[byte_count - keep + i];
        }

        byte_count = keep;
    }

    return 1;
}

/* The next code unit, or -1 at the end of the input, without taking it. */
static int32_t peek_unit(void) {
    if (taken == decoded.length && !fill()) {
        return -1;
    }

    return decoded.chars[taken];
}

static int32_t read_unit(void) {
    int32_t unit = peek_unit();

    if (unit >= 0) {
        taken++;
    }

    return unit;
}

/* An answer of the end of the input, after which the end is no longer pending. */
static int32_t answer(int32_t unit) {
    if (unit < 0) {
        end_read = 1;
    }

    return unit;
}

int32_t $ghul_host_stdin_peek(void) {
    return answer(peek_unit());
}

int32_t $ghul_host_stdin_read(void) {
    return answer(read_unit());
}

void *$ghul_host_stdin_read_line(void) {
    $libghul_utf16 line = {0};

    int32_t unit = read_unit();

    if (unit < 0) {
        end_read = 1;
        return NULL;
    }

    while (unit >= 0 && unit != '\n' && unit != '\r') {
        $libghul_utf16_add(&line, (uint16_t)unit);
        unit = read_unit();
    }

    if (unit == '\r' && peek_unit() == '\n') {
        read_unit();
    }

    return $libghul_utf16_make_string(&line);
}

void *$ghul_host_stdin_read_to_end(void) {
    $libghul_utf16 rest = {0};

    for (int32_t unit = read_unit(); unit >= 0; unit = read_unit()) {
        $libghul_utf16_add(&rest, (uint16_t)unit);
    }

    return $libghul_utf16_make_string(&rest);
}

/* Whether the code units not yet taken hold a whole line. */
static int has_line(void) {
    for (int32_t i = scanned > taken ? scanned : taken; i < decoded.length; i++) {
        if (decoded.chars[i] == '\n' || decoded.chars[i] == '\r') {
            scanned = i;
            return 1;
        }
    }

    scanned = decoded.length;

    return 0;
}

/*
 * Whether a read would return without waiting for more input: a whole line
 * is decoded, or the input has ended and a read has not yet answered so.
 */
static int ready(void) {
    return has_line() || (at_end && !end_read);
}

/*
 * Waits up to `timeout_ms` for standard input to be readable, without
 * reading it; negative waits indefinitely. Answers whether it is readable,
 * which includes the input having ended.
 */
static int readable(int timeout_ms) {
    struct pollfd fd = { .fd = 0, .events = POLLIN };

    for (;;) {
        int got = poll(&fd, 1, timeout_ms);

        if (got < 0 && errno == EINTR) {
            continue;
        }

        /* An error on the descriptor is for the read to report as the end. */
        return got != 0;
    }
}

/*
 * Reads what one read of standard input gives, which poll has said will not
 * wait, and decodes it after what is not yet taken.
 */
static void take_available(void) {
    if (taken > 0) {
        for (int32_t i = taken; i < decoded.length; i++) {
            decoded.chars[i - taken] = decoded.chars[i];
        }

        decoded.length -= taken;
        scanned = scanned > taken ? scanned - taken : 0;
        taken = 0;
    }

    ssize_t got;

    do {
        got = read(0, bytes + byte_count, READ_SIZE);
    } while (got < 0 && errno == EINTR);

    if (got <= 0) {
        at_end = 1;

        $libghul_utf8_decode(bytes, byte_count, &decoded);
        byte_count = 0;

        return;
    }

    byte_count += (size_t)got;

    size_t keep = incomplete_tail();

    $libghul_utf8_decode(bytes, byte_count - keep, &decoded);

    for (size_t i = 0; i < keep; i++) {
        bytes[i] = bytes[byte_count - keep + i];
    }

    byte_count = keep;
}

static int64_t now_ms(void) {
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);

    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

bool $ghul_host_stdin_pending(void) {
    while (!ready() && !at_end && readable(0)) {
        take_available();
    }

    return ready();
}

bool $ghul_host_stdin_wait(int32_t timeout_ms) {
    /* A prompt written without a newline is seen before the wait. */
    $libghul_flush_output();

    int64_t deadline = timeout_ms > 0 ? now_ms() + timeout_ms : 0;

    for (;;) {
        if (ready() || at_end) {
            return true;
        }

        int remaining = -1;

        if (timeout_ms > 0) {
            int64_t left = deadline - now_ms();

            if (left <= 0) {
                return false;
            }

            remaining = (int)left;
        }

        if (!readable(remaining)) {
            return false;
        }

        take_available();
    }
}
