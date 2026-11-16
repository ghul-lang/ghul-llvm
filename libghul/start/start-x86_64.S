/*
 * The entry point of a program ghul-llvm links dynamically against glibc, in
 * place of the C library's own start files, which only the C library's
 * development package installs. It hands the arguments, the environment and
 * the dynamic loader's termination function to __libc_start_main, which
 * runs the program's constructors and then main, as the x86-64 psABI lays
 * them out at process start: argc at the stack pointer, then argv, and the
 * termination function in rdx.
 */
    .text
    .globl _start
    .type _start, @function
_start:
    .cfi_startproc
    .cfi_undefined rip
    xorl %ebp, %ebp
    movq %rdx, %r9
    popq %rsi
    movq %rsp, %rdx
    andq $-16, %rsp
    pushq %rax
    pushq %rsp
    xorl %r8d, %r8d
    xorl %ecx, %ecx
    leaq main(%rip), %rdi
    call *__libc_start_main@GOTPCREL(%rip)
    hlt
    .cfi_endproc
    .size _start, . - _start

    .section .note.GNU-stack, "", @progbits
