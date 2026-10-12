/* Allocation from the garbage collected heap. */

#include "internal.h"

#include <gc.h>
#include <gc/gc_inline.h>
#include <gc/gc_mark.h>
#include <stdint.h>
#include <string.h>

static void *checked(void *object) {
    if (!object) {
        $libghul_fail("Out of memory.\n");
    }

    return object;
}

/* Always inlined, as every allocation goes through one of these two and each
   is no more than the call it makes. */
__attribute__((always_inline)) void *$ghul_alloc(size_t size) {
    /* GC_MALLOC answers zeroed memory. */
    return checked(GC_MALLOC(size));
}

__attribute__((always_inline)) void *$ghul_alloc_atomic(size_t size) {
    /* GC_MALLOC_ATOMIC does not zero what it answers, and the program
       stores everything the object holds. */
    return checked(GC_MALLOC_ATOMIC(size));
}

void *$ghul_clone(const void *object) {
    size_t size = 0;
    int kind = GC_get_kind_and_size(object, &size);

    /* The copy is the same kind as the original, so an object the collector
       does not scan for pointers stays one it does not scan. */
    void *copy = kind == GC_I_PTRFREE ? GC_MALLOC_ATOMIC(size) : GC_MALLOC(size);

    if (!copy) {
        $libghul_fail("Out of memory.\n");
    }

    memcpy(copy, object, size);

    return copy;
}

int32_t $ghul_identity_hash(const void *object) {
    /* The collector never moves an object, so its address is its identity
       for as long as it lives. Objects of one size sit at a fixed stride, so
       their addresses share their low bits, and a map picks a bucket from
       the low bits of the hash: the address is put through MurmurHash3's
       64-bit finaliser, which leaves every bit of the result depending on
       every bit of the address. */
    uint64_t x = (uint64_t)(uintptr_t)object;

    x ^= x >> 33;
    x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 33;
    x *= UINT64_C(0xc4ceb9fe1a85ec53);
    x ^= x >> 33;

    return (int32_t)(uint32_t)x;
}
