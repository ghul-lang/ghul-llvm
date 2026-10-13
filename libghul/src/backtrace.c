/*
 * The backtrace printed after an unhandled exception or a fatal signal: one
 * frame per line, innermost first, each named from the executable's own
 * symbol table, with its file and line where the program carries debug
 * information.
 *
 * The stack is walked with libunwind, from the frames still live when the
 * report is made: a raise that found no handler returns to $ghul_throw with
 * the stack as it was, and a signal handler is handed the interrupted
 * registers, which the walk is seeded with, since the unwinder cannot step
 * out of a signal frame on this target. libbacktrace names each address.
 * Nothing here runs until a report is made, and nothing allocates from the
 * collector or the C heap, so a report can be made from a signal handler.
 */

#include "internal.h"

#include <backtrace.h>
#include <libunwind.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

/* How many frames are named before the rest are only counted. */
#define FRAME_LIMIT 100

static struct backtrace_state *state;

static void write_error(const char *text, size_t length) {
    while (length > 0) {
        ssize_t written = write(2, text, length);

        if (written <= 0) {
            return;
        }

        text += written;
        length -= (size_t)written;
    }
}

static void write_text(const char *text) {
    write_error(text, strlen(text));
}

static void write_number(uintptr_t value) {
    char digits[24];
    size_t at = sizeof digits;

    do {
        digits[--at] = (char)('0' + value % 10);
        value /= 10;
    } while (value);

    write_error(digits + at, sizeof digits - at);
}

static void ignore_error(void *data, const char *message, int number) {
    (void)data;
    (void)message;
    (void)number;
}

typedef struct {
    const char *name;
    const char *file;
    int line;
    int lines;
} frame;

static void found_symbol(void *data, uintptr_t pc, const char *name, uintptr_t value, uintptr_t size) {
    (void)pc;
    (void)value;
    (void)size;

    ((frame *)data)->name = name;
}

static void write_frame(const char *name, const char *file, int line) {
    write_text("   at ");
    write_text(name ? name : "???");

    if (file && line > 0) {
        write_text(" in ");
        write_text(file);
        write_text(":line ");
        write_number((uintptr_t)line);
    }

    write_text("\n");
}

/*
 * Called once for each function the address is in, the innermost inlined
 * function first, where there is line information; without it, once with
 * nothing known.
 */
static int found_line(void *data, uintptr_t pc, const char *file, int line, const char *function) {
    (void)pc;
    frame *f = data;

    if (!function) {
        f->file = file;
        f->line = line;
        return 0;
    }

    write_frame(function, file, line);
    f->lines++;
    return 0;
}

static const char *symbol_name(uintptr_t pc) {
    frame f = {0};
    backtrace_syminfo(state, pc, found_symbol, ignore_error, &f);
    return f.name;
}

/* The throw and libghul's own frames, above the frame that threw or faulted. */
static int is_library_frame(const char *name) {
    return name && (!strncmp(name, "$ghul_throw", 11) || !strncmp(name, "$libghul_", 9));
}

static int is_outermost(const char *name) {
    return name && !strcmp(name, "main");
}

/*
 * Writes the frames from the cursor outwards. `exact` says the first frame's
 * address is the instruction that was running rather than a return
 * address; every later frame's is a return address, so the address one
 * before it is the call.
 */
static void write_frames(unw_cursor_t *cursor, int exact) {
    if (!state) {
        state = backtrace_create_state(NULL, 0, ignore_error, NULL);
    }

    int named = 0;
    uintptr_t more = 0;
    int leading = 1;

    do {
        unw_word_t ip;

        if (unw_get_reg(cursor, UNW_REG_IP, &ip) != 0 || ip == 0) {
            break;
        }

        uintptr_t pc = exact ? (uintptr_t)ip : (uintptr_t)ip - 1;
        exact = 0;

        const char *name = state ? symbol_name(pc) : NULL;

        // The entry wrapper is shown only where everything was inlined into
        // it, leaving no other frame.
        if (is_outermost(name) || (named > 0 && name && !strcmp(name, "$ghul_entry"))) {
            break;
        }

        if (leading && is_library_frame(name)) {
            continue;
        }

        leading = 0;

        if (named >= FRAME_LIMIT) {
            more++;
            continue;
        }

        named++;

        frame f = {0};

        if (state) {
            backtrace_pcinfo(state, pc, found_line, ignore_error, &f);
        }

        if (!f.lines) {
            write_frame(name, f.file, f.line);
        }
    } while (unw_step(cursor) > 0);

    if (more) {
        write_text("   ... ");
        write_number(more);
        write_text(" more frames\n");
    }
}

void $libghul_write_backtrace(void) {
    unw_context_t context;
    unw_cursor_t cursor;

    if (unw_getcontext(&context) != 0 || unw_init_local(&cursor, &context) != 0) {
        return;
    }

    write_frames(&cursor, 0);
}

void $libghul_write_signal_backtrace(void *signal_context) {
    greg_t *registers = ((ucontext_t *)signal_context)->uc_mcontext.gregs;

    unw_context_t context;
    unw_cursor_t cursor;

    if (unw_getcontext(&context) != 0 || unw_init_local(&cursor, &context) != 0) {
        return;
    }

    static const struct {
        int unwind;
        int signal;
    } map[] = {
        {UNW_X86_64_RAX, REG_RAX}, {UNW_X86_64_RDX, REG_RDX}, {UNW_X86_64_RCX, REG_RCX},
        {UNW_X86_64_RBX, REG_RBX}, {UNW_X86_64_RSI, REG_RSI}, {UNW_X86_64_RDI, REG_RDI},
        {UNW_X86_64_RBP, REG_RBP}, {UNW_X86_64_R8, REG_R8},   {UNW_X86_64_R9, REG_R9},
        {UNW_X86_64_R10, REG_R10}, {UNW_X86_64_R11, REG_R11}, {UNW_X86_64_R12, REG_R12},
        {UNW_X86_64_R13, REG_R13}, {UNW_X86_64_R14, REG_R14}, {UNW_X86_64_R15, REG_R15},
    };

    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
        unw_set_reg(&cursor, map[i].unwind, (unw_word_t)registers[map[i].signal]);
    }

    unw_set_reg(&cursor, UNW_REG_SP, (unw_word_t)registers[REG_RSP]);

    /* Last, since setting it looks up the frame the address is in. */
    if (unw_set_reg(&cursor, UNW_REG_IP, (unw_word_t)registers[REG_RIP]) != 0) {
        return;
    }

    write_frames(&cursor, 1);
}
