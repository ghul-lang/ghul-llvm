/*
 * The interface between a ghūl program compiled for the LLVM target and the
 * native runtime it is linked with (libghulrt.a). README.md describes it in
 * full; this header is the authority on the C signatures.
 *
 * Two directions:
 *
 * - The program defines ghul_entry, ghul_make_string and
 *   ghul_report_unhandled, which the runtime calls.
 * - The runtime defines main, ghul_alloc, ghul_alloc_atomic, ghul_throw and
 *   one ghul_host_<operation> function per host operation the core library
 *   declares with @intrinsic("host.<operation>").
 *
 * A string crosses the boundary as a pointer to its UTF-16 code units and a
 * count of them, never as the program's own string object, so the runtime
 * does not depend on how the program lays strings out.
 */

#ifndef GHULRT_H
#define GHULRT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The ABI version. A change to anything in this header that a compiled
 * program depends on raises it.
 */
#define GHUL_RUNTIME_ABI_VERSION 1

/*
 * Defined by the runtime, so a program can refer to it and fail to link
 * against a runtime of another ABI version rather than fail when it runs.
 */
extern const int32_t ghul_runtime_abi_1;

/* --- Defined by the program -------------------------------------------- */

/*
 * The program's entry point. Its result is the process's exit status. The
 * runtime has initialised the collector and kept the command-line arguments
 * before calling it.
 */
int32_t ghul_entry(void);

/*
 * Builds one of the program's strings from `length` UTF-16 code units. The
 * runtime calls it for every host operation that answers a string. The code
 * units are only valid for the duration of the call, so it copies them.
 */
void *ghul_make_string(const uint16_t *chars, int32_t length);

/*
 * Writes the first line .NET prints for an exception nothing caught,
 * "Unhandled exception. <Type>: <message>" and a newline, through
 * ghul_host_write to stream 2. The runtime calls it before ending the
 * process, so the runtime needs to know nothing of how an exception or its
 * message is laid out. A program that never throws need not define it.
 */
void ghul_report_unhandled(void *exception);

/* --- Allocation -------------------------------------------------------- */

/*
 * Allocates an object of `size` bytes, header included, from the garbage
 * collected heap. The memory is zeroed, then `descriptor` is stored in its
 * first word. The collector scans the object for pointers.
 */
void *ghul_alloc(const void *descriptor, size_t size);

/*
 * As ghul_alloc, for an object that holds no pointers to the heap, such as
 * an array of numbers or a string's code units. The collector does not scan
 * it.
 */
void *ghul_alloc_atomic(const void *descriptor, size_t size);

/* --- Exceptions --------------------------------------------------------- */

/*
 * Throws `exception`. Until the runtime can unwind to a handler, every
 * exception is unhandled: it is reported through ghul_report_unhandled,
 * output is flushed, and the process exits with status 134, as a .NET
 * process does.
 */
_Noreturn void ghul_throw(void *exception);

/* --- Host operations ----------------------------------------------------- */

/*
 * Writes `length` UTF-16 code units, as UTF-8, to standard output (stream
 * 1) or standard error (stream 2). Any other stream number is standard
 * output. Standard output is buffered and flushed at exit, before anything
 * is written to standard error, and before standard input is read.
 */
void ghul_host_write(const uint16_t *chars, int32_t length, int32_t stream);

/* The command-line arguments, not counting the program's own name. */
int32_t ghul_host_argument_count(void);
void *ghul_host_argument(int32_t index);

/*
 * The environment as it was when the program started. A lookup of a name
 * that is not set answers NULL.
 */
int32_t ghul_host_environment_count(void);
void *ghul_host_environment_name(int32_t index);
void *ghul_host_environment_lookup(const uint16_t *name, int32_t length);

/*
 * Standard input, decoded from UTF-8. read and peek answer one UTF-16 code
 * unit, or -1 at the end of the input. read_line answers the next line
 * without its ending, which is \n, \r\n or \r, or NULL at the end of the
 * input. read_to_end answers everything left, which may be empty.
 */
int32_t ghul_host_stdin_read(void);
int32_t ghul_host_stdin_peek(void);
void *ghul_host_stdin_read_line(void);
void *ghul_host_stdin_read_to_end(void);

/* Milliseconds since the program started, from a clock that never goes back. */
double ghul_host_monotonic_milliseconds(void);

/* 32 random bits from the operating system. */
int32_t ghul_host_random_u32(void);

/* The floating-point functions the core library does not compute itself. */
double ghul_host_math_sqrt(double value);
double ghul_host_math_cbrt(double value);
double ghul_host_math_floor(double value);
double ghul_host_math_ceiling(double value);
double ghul_host_math_truncate(double value);
double ghul_host_math_sin(double angle);
double ghul_host_math_cos(double angle);
double ghul_host_math_tan(double angle);
double ghul_host_math_asin(double value);
double ghul_host_math_acos(double value);
double ghul_host_math_atan(double value);
double ghul_host_math_atan2(double y, double x);
double ghul_host_math_sinh(double value);
double ghul_host_math_cosh(double value);
double ghul_host_math_tanh(double value);
double ghul_host_math_exp(double value);
double ghul_host_math_log(double value);
double ghul_host_math_log10(double value);
double ghul_host_math_log2(double value);
double ghul_host_math_pow(double x, double y);

#endif
