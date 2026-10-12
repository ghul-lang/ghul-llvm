/*
 * memcpy, memmove and memset, in place of musl's.
 *
 * musl's x86-64 versions move everything with rep movsq and rep stosq,
 * whose start-up cost dominates a copy of a few dozen bytes, and a ghūl
 * program copies strings that short all the time. These move up to 256
 * bytes with unaligned 8- and 16-byte loads and stores, overlapping the
 * last with the one before rather than finishing a byte at a time, and
 * leave longer moves to rep movsb, which is fast for those.
 *
 * Every copy of up to 32 bytes loads everything before it stores anything,
 * so it is also a correct memmove. Being in libghul, which is linked
 * before the C library, these are the definitions every caller gets,
 * the C library's own included.
 */

#include <stddef.h>
#include <stdint.h>

typedef uint8_t bytes16 __attribute__((vector_size(16), aligned(1)));
typedef uint64_t bytes8 __attribute__((aligned(1)));
typedef uint32_t bytes4 __attribute__((aligned(1)));
typedef uint16_t bytes2 __attribute__((aligned(1)));

/* A copy of `n` bytes, n at most 32, all loaded before any is stored. */
static inline void copy_small(unsigned char *to, const unsigned char *from, size_t n) {
    if (n >= 16) {
        bytes16 head = *(const bytes16 *)from;
        bytes16 tail = *(const bytes16 *)(from + n - 16);
        *(bytes16 *)to = head;
        *(bytes16 *)(to + n - 16) = tail;
    } else if (n >= 8) {
        bytes8 head = *(const bytes8 *)from;
        bytes8 tail = *(const bytes8 *)(from + n - 8);
        *(bytes8 *)to = head;
        *(bytes8 *)(to + n - 8) = tail;
    } else if (n >= 4) {
        bytes4 head = *(const bytes4 *)from;
        bytes4 tail = *(const bytes4 *)(from + n - 4);
        *(bytes4 *)to = head;
        *(bytes4 *)(to + n - 4) = tail;
    } else if (n >= 2) {
        bytes2 head = *(const bytes2 *)from;
        bytes2 tail = *(const bytes2 *)(from + n - 2);
        *(bytes2 *)to = head;
        *(bytes2 *)(to + n - 2) = tail;
    } else if (n == 1) {
        *to = *from;
    }
}

static inline void move_forward(unsigned char *to, const unsigned char *from, size_t n) {
    __asm__ volatile("rep movsb" : "+D"(to), "+S"(from), "+c"(n) : : "memory");
}

static inline void move_backward(unsigned char *to, const unsigned char *from, size_t n) {
    to += n - 1;
    from += n - 1;
    __asm__ volatile("std\n\trep movsb\n\tcld" : "+D"(to), "+S"(from), "+c"(n) : : "memory");
}

void *memcpy(void *restrict destination, const void *restrict source, size_t n) {
    unsigned char *to = destination;
    const unsigned char *from = source;

    if (n <= 32) {
        copy_small(to, from, n);
    } else if (n <= 256) {
        bytes16 tail_a = *(const bytes16 *)(from + n - 32);
        bytes16 tail_b = *(const bytes16 *)(from + n - 16);

        for (size_t at = 0; at < n - 32; at += 32) {
            bytes16 a = *(const bytes16 *)(from + at);
            bytes16 b = *(const bytes16 *)(from + at + 16);
            *(bytes16 *)(to + at) = a;
            *(bytes16 *)(to + at + 16) = b;
        }

        *(bytes16 *)(to + n - 32) = tail_a;
        *(bytes16 *)(to + n - 16) = tail_b;
    } else {
        move_forward(to, from, n);
    }

    return destination;
}

void *memmove(void *destination, const void *source, size_t n) {
    unsigned char *to = destination;
    const unsigned char *from = source;

    if (n <= 32) {
        copy_small(to, from, n);
    } else if ((uintptr_t)to - (uintptr_t)from >= n) {
        /* `to` is before `from`, or the two do not overlap, so a forward
         * move reads each byte before it is overwritten. */
        move_forward(to, from, n);
    } else {
        move_backward(to, from, n);
    }

    return destination;
}

void *memset(void *destination, int value, size_t n) {
    unsigned char *to = destination;
    uint64_t pattern = (uint64_t)(unsigned char)value * 0x0101010101010101ULL;

    if (n >= 16 && n <= 256) {
        bytes16 wide = (bytes16){0} + (unsigned char)value;

        for (size_t at = 0; at < n - 16; at += 16) {
            *(bytes16 *)(to + at) = wide;
        }

        *(bytes16 *)(to + n - 16) = wide;
    } else if (n > 256) {
        __asm__ volatile("rep stosb" : "+D"(to), "+c"(n) : "a"(value) : "memory");
    } else if (n >= 8) {
        *(bytes8 *)to = pattern;
        *(bytes8 *)(to + n - 8) = pattern;
    } else if (n >= 4) {
        *(bytes4 *)to = (uint32_t)pattern;
        *(bytes4 *)(to + n - 4) = (uint32_t)pattern;
    } else if (n >= 2) {
        *(bytes2 *)to = (uint16_t)pattern;
        *(bytes2 *)(to + n - 2) = (uint16_t)pattern;
    } else if (n == 1) {
        *to = (unsigned char)value;
    }

    return destination;
}
