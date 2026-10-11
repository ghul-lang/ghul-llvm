/*
 * Standard input, read as the program asks for it, decoded from UTF-8 into
 * UTF-16 code units. Lines end at \n, \r\n or \r, as .NET's ReadLine ends
 * them.
 */

#include "internal.h"

#include <errno.h>
#include <unistd.h>

#define READ_SIZE 4096

/* The longest UTF-8 sequence, which is never split between decodes. */
#define MAX_SEQUENCE 4

static char bytes[READ_SIZE + MAX_SEQUENCE];
static size_t byte_count;

/* Code units decoded and not yet taken. */
static libghul_utf16 decoded;
static int32_t taken;

static int at_end;

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
    libghul_flush_output();

    decoded.length = 0;
    taken = 0;

    while (decoded.length == 0) {
        ssize_t got = read(0, bytes + byte_count, READ_SIZE);

        if (got < 0 && errno == EINTR) {
            continue;
        }

        if (got <= 0) {
            at_end = 1;

            /* What is left was cut off by the end of the input. */
            libghul_utf8_decode(bytes, byte_count, &decoded);
            byte_count = 0;

            return decoded.length > 0;
        }

        byte_count += (size_t)got;

        size_t keep = incomplete_tail();

        libghul_utf8_decode(bytes, byte_count - keep, &decoded);

        for (size_t i = 0; i < keep; i++) {
            bytes[i] = bytes[byte_count - keep + i];
        }

        byte_count = keep;
    }

    return 1;
}

int32_t ghul_host_stdin_peek(void) {
    if (taken == decoded.length && !fill()) {
        return -1;
    }

    return decoded.chars[taken];
}

int32_t ghul_host_stdin_read(void) {
    int32_t unit = ghul_host_stdin_peek();

    if (unit >= 0) {
        taken++;
    }

    return unit;
}

void *ghul_host_stdin_read_line(void) {
    libghul_utf16 line = {0};

    int32_t unit = ghul_host_stdin_read();

    if (unit < 0) {
        return NULL;
    }

    while (unit >= 0 && unit != '\n' && unit != '\r') {
        libghul_utf16_add(&line, (uint16_t)unit);
        unit = ghul_host_stdin_read();
    }

    if (unit == '\r' && ghul_host_stdin_peek() == '\n') {
        ghul_host_stdin_read();
    }

    return libghul_utf16_make_string(&line);
}

void *ghul_host_stdin_read_to_end(void) {
    libghul_utf16 rest = {0};

    for (int32_t unit = ghul_host_stdin_read(); unit >= 0; unit = ghul_host_stdin_read()) {
        libghul_utf16_add(&rest, (uint16_t)unit);
    }

    return libghul_utf16_make_string(&rest);
}
