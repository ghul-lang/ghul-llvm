/*
 * Reading through null, as the null-reference exception the program
 * throws for it.
 *
 * Generated code tests a reference for null before reading through it, and
 * branches to a block that throws when it is. Where LLVM can fold the test
 * into the read, it leaves the read to fault instead, and records in the
 * fault map the block the test would have branched to. A fault at such a
 * read, in the first page, resumes at that block, which throws through an
 * ordinary call: a try in the same function catches it.
 *
 * Any other fault in the first 64KB is a read through null that nothing
 * tested. It is made to look as though the faulting instruction had called
 * $libghul_null_dereference, which throws from there, so the exception
 * unwinds out of the faulting function to its callers.
 *
 * Every other fault, every fault in a program that does not define
 * $ghul_make_null_reference, and a bus error, an arithmetic fault or an
 * illegal instruction, is reported, with a backtrace, after standard output
 * is flushed, and then goes to the handler installed before this one, which
 * by default ends the process with the signal. The handler runs on a stack
 * of its own, so the stack running out is reported too.
 */

#include "internal.h"

#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

/* Where a folded test's read can fault: LLVM folds only offsets below this. */
#define FOLDED_REGION 4096

/* Where an untested read through null can fault. */
#define NULL_REGION 65536

/* Defined by the program, which a program that never reads through null
 * need not do. */
extern void *$ghul_make_null_reference(void) __attribute__((weak));

/* Weak, so a program that throws nothing need not link the unwinder or
 * define $ghul_report_unhandled. A program that tests for null calls it,
 * so has it. */
extern _Noreturn void $ghul_throw_null_reference(void) __attribute__((weak));

/* Bound .llvm_faultmaps; defined by ghul.ld, and absent from a link without it. */
extern const uint8_t __ghul_faultmaps_start[] __attribute__((weak));
extern const uint8_t __ghul_faultmaps_end[] __attribute__((weak));



#define READ(type, p) ({ type value; memcpy(&value, (p), sizeof value); (p) += sizeof value; value; })

/*
 * The block a folded test resumes at for a fault at `pc`, or 0 where `pc` is
 * not a folded read. The section holds one map per object, each a header,
 * then per function its address and its faulting reads.
 */
static uintptr_t resume_at(uintptr_t pc) {
    const uint8_t *p = __ghul_faultmaps_start;
    const uint8_t *end = __ghul_faultmaps_end;

    if (!p || !end) {
        return 0;
    }

    while (end - p >= 8) {
        uint8_t version = *p;

        if (version != 1) {
            return 0;
        }

        p += 4;
        uint32_t functions = READ(uint32_t, p);

        for (uint32_t i = 0; i < functions; i++) {
            uintptr_t function = (uintptr_t)READ(uint64_t, p);
            uint32_t faults = READ(uint32_t, p);
            p += 4;

            for (uint32_t j = 0; j < faults; j++) {
                p += 4; /* the kind: a load, a store or both, all handled alike */
                uint32_t faulting = READ(uint32_t, p);
                uint32_t handler = READ(uint32_t, p);

                if (function + faulting == pc) {
                    return function + handler;
                }
            }
        }
    }

    return 0;
}

/*
 * Entered with the faulting instruction's address plus one pushed as if it
 * were a call's return address, so the unwinder finds the faulting function
 * as this frame's caller and looks up the faulting instruction itself.
 */
void $libghul_null_dereference(void);

#if defined(__x86_64__)
__asm__(
    "    .text\n"
    "    .p2align 4\n"
    "    .type \"$libghul_null_dereference\", @function\n"
    "\"$libghul_null_dereference\":\n"
    "    .cfi_startproc\n"
    "    pushq %rbp\n"
    "    .cfi_def_cfa_offset 16\n"
    "    .cfi_offset %rbp, -16\n"
    "    movq %rsp, %rbp\n"
    "    .cfi_def_cfa_register %rbp\n"
    "    andq $-16, %rsp\n"
    "    call \"$ghul_throw_null_reference\"\n"
    "    ud2\n"
    "    .cfi_endproc\n"
    "    .size \"$libghul_null_dereference\", . - \"$libghul_null_dereference\"\n");
#endif

/* The handler installed before this one, for each signal handled here. */
static struct sigaction previous[NSIG];

/*
 * Where the handler runs: a stack of its own, so a fault that is the stack
 * running out can still be reported. Reserved, not committed, until used.
 */
#define SIGNAL_STACK_SIZE (512 * 1024)

static const int fatal_signals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL};

static void pass_on(int signal, siginfo_t *info, void *context) {
    struct sigaction *before = &previous[signal];

    if (before->sa_flags & SA_SIGINFO) {
        before->sa_sigaction(signal, info, context);
    } else if (before->sa_handler == SIG_DFL || before->sa_handler == SIG_IGN) {
        /* Returning re-runs the faulting instruction, which now ends the process. */
        struct sigaction standard = {.sa_handler = SIG_DFL};
        sigemptyset(&standard.sa_mask);
        sigaction(signal, &standard, NULL);
    } else {
        before->sa_handler(signal);
    }
}

static void write_text(const char *text) {
    size_t length = strlen(text);

    while (length > 0) {
        ssize_t written = write(2, text, length);

        if (written <= 0) {
            return;
        }

        text += written;
        length -= (size_t)written;
    }
}

static void write_address(uintptr_t address) {
    char digits[19] = "0x";
    int shift = 60;

    while (shift > 0 && !((address >> shift) & 0xF)) {
        shift -= 4;
    }

    size_t at = 2;

    for (; shift >= 0; shift -= 4) {
        digits[at++] = "0123456789abcdef"[(address >> shift) & 0xF];
    }

    digits[at] = 0;
    write_text(digits);
}

/*
 * A fault within this distance below the stack pointer, or a little above
 * it, is the stack running out: the guard page under the stack is hit by a
 * push or a store into a new frame.
 */
#define OVERFLOW_REACH (1024 * 1024)

static void report(int signal, uintptr_t address, uintptr_t sp, void *context) {
    static volatile sig_atomic_t reporting;

    if (reporting) {
        return;
    }

    reporting = 1;

    $libghul_flush_output();

    if (signal == SIGSEGV && address <= sp + 4096 && address + OVERFLOW_REACH >= sp) {
        write_text("Stack overflow.\n");
    } else {
        switch (signal) {
        case SIGSEGV:
            write_text("Fatal error. SIGSEGV: invalid memory access at address ");
            write_address(address);
            break;
        case SIGBUS:
            write_text("Fatal error. SIGBUS: bus error at address ");
            write_address(address);
            break;
        case SIGFPE:
            write_text("Fatal error. SIGFPE: arithmetic fault");
            break;
        default:
            write_text("Fatal error. SIGILL: illegal instruction");
            break;
        }

        write_text("\n");
    }

    $libghul_write_signal_backtrace(context);
}

static void on_fault(int signal, siginfo_t *info, void *context) {
    uintptr_t address = (uintptr_t)info->si_addr;

#if defined(__x86_64__)
    /* The interrupted pc and sp, written back to resume past a null
     * dereference. x86_64 keeps them in the greg array; musl's arm64
     * mcontext is struct sigcontext, with the 31 general registers in
     * regs[], then sp and pc separately. On arm64 the resume is not
     * attempted yet: LLVM's implicit-null-check pass is x86-only, so the
     * folded-read path has no fault map on arm64, and the trampoline's
     * throw does not unwind from its fake frame there. Both are to come. */
    greg_t *registers = ((ucontext_t *)context)->uc_mcontext.gregs;
    uintptr_t pc = (uintptr_t)registers[REG_RIP];
    uintptr_t sp = (uintptr_t)registers[REG_RSP];

    int is_null = signal == SIGSEGV && $ghul_make_null_reference && $ghul_throw_null_reference &&
                  address < NULL_REGION;

    if (!is_null) {
        report(signal, address, sp, context);
        pass_on(signal, info, context);
        return;
    }

    uintptr_t handler = address < FOLDED_REGION ? resume_at(pc) : 0;

    if (handler) {
        registers[REG_RIP] = (greg_t)handler;
        return;
    }

    uintptr_t *stack = (uintptr_t *)sp;
    *--stack = pc + 1;

    registers[REG_RSP] = (greg_t)stack;
    registers[REG_RIP] = (greg_t)(uintptr_t)$libghul_null_dereference;
#else
    report(signal, address, (uintptr_t)context, context);
    pass_on(signal, info, context);
#endif
}

void $libghul_fault_init(void) {
    void *signal_stack = mmap(NULL, SIGNAL_STACK_SIZE, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

    int flags = SA_SIGINFO;

    if (signal_stack != MAP_FAILED) {
        stack_t alternate = {.ss_sp = signal_stack, .ss_size = SIGNAL_STACK_SIZE};

        if (sigaltstack(&alternate, NULL) == 0) {
            flags |= SA_ONSTACK;
        }
    }

    for (size_t i = 0; i < sizeof fatal_signals / sizeof fatal_signals[0]; i++) {
        struct sigaction handler = {.sa_sigaction = on_fault, .sa_flags = flags};
        sigemptyset(&handler.sa_mask);
        sigaction(fatal_signals[i], &handler, &previous[fatal_signals[i]]);
    }
}
