/*
 * What a program ghul-llvm links dynamically against glibc takes from the
 * C library's static part, libc_nonshared.a, in a program a C compiler
 * links: that archive is in the C library's development package, which a
 * dynamic link does not need. Of what it holds on glibc 2.34 and later,
 * libghul calls only atexit; at_quick_exit is here beside it for a program
 * that declares it with @extern.
 */

/* The handle that ties functions registered with atexit to this executable:
   zero, as the C compiler's own start files define it in an executable. */
void *__dso_handle __attribute__((visibility("hidden"))) = 0;

/* The first word of the data segment, which the collector scans from, and
   the version tag the C library looks for in an executable's read-only
   data, as glibc's own start files define them. The tag is kept by the
   linker's --gc-sections although nothing refers to it, because libghul's
   ghul.ld places the fault map after the read-only data, which there must
   then be, even in a program that has none of its own. */
int __data_start __attribute__((section(".data"))) = 0;
extern int data_start __attribute__((weak, alias("__data_start")));
const int _IO_stdin_used __attribute__((used, retain)) = 0x20001;

int __cxa_atexit(void (*function)(void *), void *argument, void *handle);
int __cxa_at_quick_exit(void (*function)(void *), void *handle);

int atexit(void (*function)(void)) {
    return __cxa_atexit((void (*)(void *))function, 0, __dso_handle);
}

int at_quick_exit(void (*function)(void)) {
    return __cxa_at_quick_exit((void (*)(void *))function, __dso_handle);
}
