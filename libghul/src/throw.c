/*
 * Throwing and catching, through LLVM's libunwind and the Itanium two-phase
 * unwinding protocol.
 *
 * $ghul_throw wraps the exception in an unwinder header and raises it. The
 * personality routine, $ghul_personality, answers for every frame the
 * unwinder passes: it reports a handler wherever the frame has a landing
 * pad for the call being unwound through, and nothing anywhere else. Which
 * exceptions a landing pad handles is decided by the generated code in it,
 * which re-raises with $ghul_throw what it does not handle. When no frame has
 * a landing pad, the raise returns, and the exception is unhandled.
 */

#include "internal.h"

#include <gc.h>
#include <setjmp.h>
#include <unwind.h>
#include <unistd.h>

/*
 * Where an exception nothing catches is taken to, after it has been reported,
 * instead of ending the process: a host that runs the program in pieces sets
 * it around each, so one that fails leaves the others' state in place.
 * Null ends the process, as it does for a program that is one piece.
 */
void *$libghul_unhandled_jump;

/* "GHUL" as the vendor and "GHUL" as the language, as the protocol asks. */
#define EXCEPTION_CLASS 0x4748554C4748554CULL

typedef struct {
    struct _Unwind_Exception unwind;
    void *exception;
} header;

_Noreturn void $ghul_throw(void *exception) {
    /*
     * From the collected heap, so the exception it holds stays reachable
     * while the unwinder holds only the header.
     */
    header *raised = GC_MALLOC(sizeof(header));

    if (!raised) {
        $libghul_fail("Out of memory.\n");
    }

    raised->unwind.exception_class = EXCEPTION_CLASS;
    raised->unwind.exception_cleanup = NULL;
    raised->exception = exception;

    _Unwind_RaiseException(&raised->unwind);

    /* The raise returns only when no frame will handle the exception. */
    $libghul_flush_output();
    $ghul_report_unhandled(exception);
    $libghul_write_backtrace();
    $libghul_flush_output();

    if ($libghul_unhandled_jump) {
        siglongjmp(*(sigjmp_buf *)$libghul_unhandled_jump, 1);
    }

    _exit(LIBGHUL_FAILURE_STATUS);
}

void *$ghul_caught(void *unwind_header) {
    return ((header *)unwind_header)->exception;
}

/* --- Reading the language-specific data area ---------------------------- */

/* The pointer encodings DWARF exception tables use. */
enum {
    PE_ABSPTR = 0x00,
    PE_ULEB128 = 0x01,
    PE_UDATA2 = 0x02,
    PE_UDATA4 = 0x03,
    PE_UDATA8 = 0x04,
    PE_SLEB128 = 0x09,
    PE_SDATA2 = 0x0A,
    PE_SDATA4 = 0x0B,
    PE_SDATA8 = 0x0C,
    PE_PCREL = 0x10,
    PE_INDIRECT = 0x80,
    PE_OMIT = 0xFF,
};

static uintptr_t read_uleb128(const uint8_t **p) {
    uintptr_t result = 0;
    unsigned shift = 0;
    uint8_t byte;

    do {
        byte = *(*p)++;
        result |= (uintptr_t)(byte & 0x7F) << shift;
        shift += 7;
    } while (byte & 0x80);

    return result;
}

static intptr_t read_sleb128(const uint8_t **p) {
    uintptr_t result = 0;
    unsigned shift = 0;
    uint8_t byte;

    do {
        byte = *(*p)++;
        result |= (uintptr_t)(byte & 0x7F) << shift;
        shift += 7;
    } while (byte & 0x80);

    if ((byte & 0x40) && shift < sizeof(result) * 8) {
        result |= ~(uintptr_t)0 << shift;
    }

    return (intptr_t)result;
}

#define READ(type, p) ({ type value; __builtin_memcpy(&value, *(p), sizeof value); *(p) += sizeof value; value; })

/*
 * Reads one value in `encoding`. Answers false for an encoding a table LLVM
 * emits for this target never uses.
 */
static int read_encoded(const uint8_t **p, uint8_t encoding, uintptr_t *out) {
    const uint8_t *start = *p;
    uintptr_t value;

    switch (encoding & 0x0F) {
    case PE_ABSPTR: value = READ(uintptr_t, p); break;
    case PE_ULEB128: value = read_uleb128(p); break;
    case PE_UDATA2: value = READ(uint16_t, p); break;
    case PE_UDATA4: value = READ(uint32_t, p); break;
    case PE_UDATA8: value = (uintptr_t)READ(uint64_t, p); break;
    case PE_SLEB128: value = (uintptr_t)read_sleb128(p); break;
    case PE_SDATA2: value = (uintptr_t)(intptr_t)READ(int16_t, p); break;
    case PE_SDATA4: value = (uintptr_t)(intptr_t)READ(int32_t, p); break;
    case PE_SDATA8: value = (uintptr_t)READ(int64_t, p); break;
    default: return 0;
    }

    if (value != 0) {
        switch (encoding & 0x70) {
        case 0: break;
        case PE_PCREL: value += (uintptr_t)start; break;
        default: return 0;
        }

        if (encoding & PE_INDIRECT) {
            value = *(const uintptr_t *)value;
        }
    }

    *out = value;
    return 1;
}

/*
 * The landing pad for the call the frame is stopped at, or 0 where the frame
 * has none: no table, a call the table lists without one, or a call it does
 * not list.
 */
static uintptr_t landing_pad(struct _Unwind_Context *context) {
    const uint8_t *lsda = (const uint8_t *)_Unwind_GetLanguageSpecificData(context);

    if (!lsda) {
        return 0;
    }

    int before_instruction = 0;
    uintptr_t ip = _Unwind_GetIPInfo(context, &before_instruction);

    /*
     * The return address is the instruction after the call; the call itself
     * is what the table describes.
     */
    if (!before_instruction) {
        ip--;
    }

    uintptr_t function = _Unwind_GetRegionStart(context);
    uintptr_t offset = ip - function;

    const uint8_t *p = lsda;
    uintptr_t base = function;

    uint8_t start_encoding = *p++;

    if (start_encoding != PE_OMIT && !read_encoded(&p, start_encoding, &base)) {
        return 0;
    }

    uint8_t type_encoding = *p++;

    if (type_encoding != PE_OMIT) {
        read_uleb128(&p);
    }

    uint8_t call_site_encoding = *p++;
    uintptr_t table_length = read_uleb128(&p);
    const uint8_t *end = p + table_length;

    while (p < end) {
        uintptr_t start, length, pad;

        if (!read_encoded(&p, call_site_encoding, &start) ||
            !read_encoded(&p, call_site_encoding, &length) ||
            !read_encoded(&p, call_site_encoding, &pad)) {
            return 0;
        }

        read_uleb128(&p);

        /* The table is sorted by start address. */
        if (offset < start) {
            return 0;
        }

        if (offset < start + length) {
            return pad ? base + pad : 0;
        }
    }

    return 0;
}

_Unwind_Reason_Code $ghul_personality(int version, _Unwind_Action actions, uint64_t exception_class,
                                     struct _Unwind_Exception *exception,
                                     struct _Unwind_Context *context) {
    (void)exception_class;

    if (version != 1) {
        return _URC_FATAL_PHASE1_ERROR;
    }

    uintptr_t pad = landing_pad(context);

    if (!pad) {
        return _URC_CONTINUE_UNWIND;
    }

    if (actions & _UA_SEARCH_PHASE) {
        return _URC_HANDLER_FOUND;
    }

    _Unwind_SetGR(context, __builtin_eh_return_data_regno(0), (uintptr_t)exception);
    _Unwind_SetGR(context, __builtin_eh_return_data_regno(1), 1);
    _Unwind_SetIP(context, pad);

    return _URC_INSTALL_CONTEXT;
}

/* Defined by a program that tests for null; see null.c. */
extern void *$ghul_make_null_reference(void) __attribute__((weak));

_Noreturn __attribute__((cold, noinline)) void $ghul_throw_null_reference(void) {
    $ghul_throw($ghul_make_null_reference ? $ghul_make_null_reference() : NULL);
}
