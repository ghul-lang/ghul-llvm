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
 * Every other fault, and every fault in a program that does not define
 * $ghul_make_null_reference, goes to the handler installed before this one,
 * which by default ends the process.
 */

#include "internal.h"

#include <signal.h>
#include <string.h>
#include <ucontext.h>

/* Where a folded test's read can fault: LLVM folds only offsets below this. */
#define FOLDED_REGION 4096

/* Where an untested read through null can fault. */
#define NULL_REGION 65536

/* Defined by the program, which a program that never reads through null
 * need not do. */
extern void *$ghul_make_null_reference(void) __attribute__((weak));

/* Weak, so a program that throws nothing need not link the unwinder or
 * define $ghul_report_unhandled. A program that defines
 * $ghul_make_null_reference throws it, so has it. */
extern _Noreturn void $ghul_throw(void *exception) __attribute__((weak));

/* Bound .llvm_faultmaps; defined by ghul.ld, and absent from a link without it. */
extern const uint8_t __ghul_faultmaps_start[] __attribute__((weak));
extern const uint8_t __ghul_faultmaps_end[] __attribute__((weak));

static struct sigaction previous;

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

_Noreturn void $libghul_throw_null_reference(void) {
    $ghul_throw($ghul_make_null_reference());
}

/*
 * Entered with the faulting instruction's address plus one pushed as if it
 * were a call's return address, so the unwinder finds the faulting function
 * as this frame's caller and looks up the faulting instruction itself.
 */
void $libghul_null_dereference(void);

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
    "    call \"$libghul_throw_null_reference\"\n"
    "    ud2\n"
    "    .cfi_endproc\n"
    "    .size \"$libghul_null_dereference\", . - \"$libghul_null_dereference\"\n");

static void pass_on(int signal, siginfo_t *info, void *context) {
    if (previous.sa_flags & SA_SIGINFO) {
        previous.sa_sigaction(signal, info, context);
    } else if (previous.sa_handler == SIG_DFL || previous.sa_handler == SIG_IGN) {
        /* Returning re-runs the faulting instruction, which now ends the process. */
        struct sigaction standard = {.sa_handler = SIG_DFL};
        sigemptyset(&standard.sa_mask);
        sigaction(signal, &standard, NULL);
    } else {
        previous.sa_handler(signal);
    }
}

static void on_fault(int signal, siginfo_t *info, void *context) {
    greg_t *registers = ((ucontext_t *)context)->uc_mcontext.gregs;
    uintptr_t address = (uintptr_t)info->si_addr;
    uintptr_t pc = (uintptr_t)registers[REG_RIP];

    if (!$ghul_make_null_reference || address >= NULL_REGION) {
        pass_on(signal, info, context);
        return;
    }

    uintptr_t handler = address < FOLDED_REGION ? resume_at(pc) : 0;

    if (handler) {
        registers[REG_RIP] = (greg_t)handler;
        return;
    }

    uintptr_t *stack = (uintptr_t *)registers[REG_RSP];
    *--stack = pc + 1;

    registers[REG_RSP] = (greg_t)stack;
    registers[REG_RIP] = (greg_t)(uintptr_t)$libghul_null_dereference;
}

void $libghul_null_init(void) {
    if (!$ghul_make_null_reference || !$ghul_throw) {
        return;
    }

    struct sigaction handler = {.sa_sigaction = on_fault, .sa_flags = SA_SIGINFO};
    sigemptyset(&handler.sa_mask);
    sigaction(SIGSEGV, &handler, &previous);
}
