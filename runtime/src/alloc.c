/* Allocation from the garbage collected heap. */

#include "internal.h"

#include <gc.h>
#include <string.h>

static void *finish(void *object, const void *descriptor, size_t size) {
    if (!object) {
        ghulrt_fail("Out of memory.\n");
    }

    if (size >= sizeof(void *)) {
        *(const void **)object = descriptor;
    }

    return object;
}

void *ghul_alloc(const void *descriptor, size_t size) {
    /* GC_MALLOC answers zeroed memory. */
    return finish(GC_MALLOC(size), descriptor, size);
}

void *ghul_alloc_atomic(const void *descriptor, size_t size) {
    /* GC_MALLOC_ATOMIC does not zero what it answers. */
    void *object = GC_MALLOC_ATOMIC(size);

    if (object) {
        memset(object, 0, size);
    }

    return finish(object, descriptor, size);
}
