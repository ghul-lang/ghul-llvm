/*
 * The entry point of a program ghul-llvm links dynamically against glibc, in
 * place of the C library's own start files, which only the C library's
 * development package installs. It hands the arguments, the environment and
 * the dynamic loader's termination function to __libc_start_main, which
 * runs the program's constructors and then main, as the AArch64 psABI lays
 * them out at process start: argc at the stack pointer, then argv, and the
 * termination function in x0.
 */
    .text
    .globl _start
    .type _start, %function
_start:
    .cfi_startproc
    .cfi_undefined x30
    mov x29, #0
    mov x30, #0
    mov x5, x0
    ldr x1, [sp]
    add x2, sp, #8
    mov x6, sp
    mov x3, #0
    mov x4, #0
    adrp x0, :got:main
    ldr x0, [x0, :got_lo12:main]
    bl __libc_start_main
    bl abort
    .cfi_endproc
    .size _start, . - _start

    .section .note.GNU-stack, "", %progbits
