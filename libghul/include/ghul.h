/*
 * The interface between a ghūl program compiled for the LLVM target and the
 * native support library it is linked with (libghul.a). README.md describes it in
 * full; this header is the authority on the C signatures.
 *
 * Two directions:
 *
 * - The program defines $ghul_entry, $ghul_make_string, $ghul_make_bytes,
 *   $ghul_report_unhandled and $ghul_make_null_reference, which libghul
 *   calls.
 * - libghul defines main, $ghul_alloc, $ghul_alloc_atomic, $ghul_throw,
 *   $ghul_caught, $ghul_personality and one ghul_host_<operation> function per
 *   host operation the core library declares with
 *   @intrinsic("host.<operation>").
 *
 * A string crosses the boundary as a pointer to its UTF-16 code units and a
 * count of them, never as the program's own string object, so libghul
 * does not depend on how the program lays strings out.
 */

#ifndef GHUL_H
#define GHUL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The ABI version. A change to anything in this header that a compiled
 * program depends on raises it.
 */
#define GHUL_ABI_VERSION 3

/*
 * Defined by libghul, so a program can refer to it and fail to link
 * against a libghul of another ABI version rather than fail when it runs.
 * Version 2 added exceptions that a landing pad can catch; version 3 put a
 * `$` in front of every name on either side of the boundary, which no ghūl
 * identifier can contain.
 */
extern const int32_t $ghul_abi_3;

/* --- Defined by the program -------------------------------------------- */

/*
 * The program's entry point. Its result is the process's exit status.
 * libghul has initialised the collector and kept the command-line arguments
 * before calling it.
 */
int32_t $ghul_entry(void);

/*
 * Builds one of the program's strings from `length` UTF-16 code units.
 * libghul calls it for every host operation that answers a string. The code
 * units are only valid for the duration of the call, so it copies them.
 */
void *$ghul_make_string(const uint16_t *chars, int32_t length);

/*
 * Builds one of the program's ubyte[] arrays from `length` bytes, for every
 * host operation that answers one. The bytes are only valid for the
 * duration of the call, so it copies them. A ubyte[] argument crosses the
 * other way as two parameters: `const uint8_t *bytes, int32_t length`.
 */
void *$ghul_make_bytes(const uint8_t *bytes, int32_t length);

/*
 * Writes the first line .NET prints for an exception nothing caught,
 * "Unhandled exception. <Type>: <message>" and a newline, through
 * $ghul_host_write to stream 2. libghul calls it before ending the
 * process, so libghul needs to know nothing of how an exception or its
 * message is laid out. A program that never throws need not define it.
 */
void $ghul_report_unhandled(void *exception);

/*
 * Makes the exception a read through null throws. libghul calls it when a read
 * through null faults, and throws what it answers, so the program raises the
 * same exception it throws for null anywhere else. A program that does
 * not define it ends on a read through null with SIGSEGV.
 */
void *$ghul_make_null_reference(void);

/* --- Allocation -------------------------------------------------------- */

/*
 * Allocates an object of `size` bytes, header included, from the garbage
 * collected heap. The memory is zeroed, then `descriptor` is stored in its
 * first word. The collector scans the object for pointers.
 */
void *$ghul_alloc(const void *descriptor, size_t size);

/*
 * As $ghul_alloc, for an object that holds no pointers to the heap, such as
 * an array of numbers or a string's code units. The collector does not scan
 * it.
 */
void *$ghul_alloc_atomic(const void *descriptor, size_t size);

/*
 * A shallow copy of `object`: every byte of its allocation, descriptor
 * included, in a new allocation of the same kind, so a copy of an atomic
 * object is atomic too. The collector may give the copy a larger block.
 */
void *$ghul_clone(const void *object);

/*
 * A hash of `object`'s identity, the same for as long as it lives, since
 * the collector never moves it.
 */
int32_t $ghul_identity_hash(const void *object);

/* --- Exceptions --------------------------------------------------------- */

/*
 * Throws `exception`, unwinding to the nearest landing pad for a call it is
 * thrown through. A landing pad that does not handle the exception throws it
 * again with $ghul_throw. Where there is no landing pad left, the exception
 * is unhandled: it is reported through $ghul_report_unhandled, output is
 * flushed, and the process exits with status 134, as a .NET process does.
 */
_Noreturn void $ghul_throw(void *exception);

/*
 * The exception a landing pad was entered for, from the first value of its
 * landingpad instruction.
 */
void *$ghul_caught(void *unwind_header);

/*
 * $ghul_personality is the personality routine every function with a landing
 * pad names. Only the unwinder calls it, with the signature unwind.h gives
 * a personality routine, so it is not declared here.
 */

/* --- Host operations ----------------------------------------------------- */

/*
 * Writes `length` UTF-16 code units, as UTF-8, to standard output (stream
 * 1) or standard error (stream 2). Any other stream number is standard
 * output. Standard output is buffered and flushed at exit, before anything
 * is written to standard error, and before standard input is read.
 */
void $ghul_host_write(const uint16_t *chars, int32_t length, int32_t stream);

/* The command-line arguments, not counting the program's own name. */
int32_t $ghul_host_argument_count(void);
void *$ghul_host_argument(int32_t index);

/*
 * The environment as it was when the program started. A lookup of a name
 * that is not set answers NULL.
 */
int32_t $ghul_host_environment_count(void);
void *$ghul_host_environment_name(int32_t index);
void *$ghul_host_environment_lookup(const uint16_t *name, int32_t length);

/*
 * Standard input, decoded from UTF-8. read and peek answer one UTF-16 code
 * unit, or -1 at the end of the input. read_line answers the next line
 * without its ending, which is \n, \r\n or \r, or NULL at the end of the
 * input. read_to_end answers everything left, which may be empty.
 */
int32_t $ghul_host_stdin_read(void);
int32_t $ghul_host_stdin_peek(void);
void *$ghul_host_stdin_read_line(void);
void *$ghul_host_stdin_read_to_end(void);

/* Milliseconds since the program started, from a clock that never goes back. */
double $ghul_host_monotonic_milliseconds(void);

/*
 * Running another program. process_begin starts a command with the program
 * to run, found on the PATH where it names no directory, and
 * process_add_argument adds each argument. process_run runs the command to
 * completion, sharing standard input and capturing standard output and
 * standard error, and answers its exit status, or -1 where it could not be
 * started, with the reason in process_failure: 1 the program was not found,
 * 2 any other. process_output and process_error answer what the last run
 * captured.
 */
void $ghul_host_process_begin(const uint16_t *file_name, int32_t length);
void $ghul_host_process_add_argument(const uint16_t *argument, int32_t length);
int32_t $ghul_host_process_run(void);
int32_t $ghul_host_process_failure(void);
void *$ghul_host_process_output(void);
void *$ghul_host_process_error(void);

/* Milliseconds since 1970-01-01T00:00:00Z, from the system clock. */
double $ghul_host_wall_clock_milliseconds(void);

/* Ends the process with `code` as its status, after flushing standard output. */
_Noreturn void $ghul_host_exit(int32_t code);

/* The path of the running executable, or NULL where it cannot be read. */
void *$ghul_host_process_path(void);

/* 32 random bits from the operating system. */
int32_t $ghul_host_random_u32(void);

/* The floating-point functions the core library does not compute itself. */
double $ghul_host_math_sqrt(double value);
double $ghul_host_math_cbrt(double value);
double $ghul_host_math_floor(double value);
double $ghul_host_math_ceiling(double value);
double $ghul_host_math_truncate(double value);
double $ghul_host_math_sin(double angle);
double $ghul_host_math_cos(double angle);
double $ghul_host_math_tan(double angle);
double $ghul_host_math_asin(double value);
double $ghul_host_math_acos(double value);
double $ghul_host_math_atan(double value);
double $ghul_host_math_atan2(double y, double x);
double $ghul_host_math_sinh(double value);
double $ghul_host_math_cosh(double value);
double $ghul_host_math_tanh(double value);
double $ghul_host_math_exp(double value);
double $ghul_host_math_log(double value);
double $ghul_host_math_log10(double value);
double $ghul_host_math_log2(double value);
double $ghul_host_math_pow(double x, double y);

/*
 * bigint, with .NET's BigInteger semantics: division truncates, a remainder
 * takes the dividend's sign, shifts are arithmetic and a negative count
 * shifts the other way, and the bitwise operations act on two's complement.
 * A bigint crosses as a handle: a pointer into the collected heap that
 * libghul makes and never changes, kept alive by whatever refers to it. A
 * handle the program keeps must be in an object the collector scans.
 *
 * Division by zero is the caller's to check: the core library throws before
 * calling. An argument these cannot take (a double that is not finite, a
 * negative exponent, a zero modulus, text that is not a whole number) ends
 * the process as an unhandled exception.
 */
void *$ghul_host_bigint_from_int(int32_t value);
void *$ghul_host_bigint_from_long(int64_t value);
void *$ghul_host_bigint_from_ulong(uint64_t value);
void *$ghul_host_bigint_from_double(double value);
bool $ghul_host_bigint_fits_long(const void *value);
int64_t $ghul_host_bigint_to_long(const void *value);
double $ghul_host_bigint_to_double(const void *value);
void *$ghul_host_bigint_add(const void *a, const void *b);
void *$ghul_host_bigint_subtract(const void *a, const void *b);
void *$ghul_host_bigint_multiply(const void *a, const void *b);
void *$ghul_host_bigint_divide(const void *a, const void *b);
void *$ghul_host_bigint_remainder(const void *a, const void *b);
void *$ghul_host_bigint_negate(const void *a);
void *$ghul_host_bigint_not(const void *a);
void *$ghul_host_bigint_and(const void *a, const void *b);
void *$ghul_host_bigint_or(const void *a, const void *b);
void *$ghul_host_bigint_xor(const void *a, const void *b);
void *$ghul_host_bigint_shift_left(const void *a, int32_t places);
void *$ghul_host_bigint_shift_right(const void *a, int32_t places);
void *$ghul_host_bigint_pow(const void *value, int32_t exponent);
void *$ghul_host_bigint_mod_pow(const void *value, const void *exponent, const void *modulus);
void *$ghul_host_bigint_gcd(const void *a, const void *b);
int32_t $ghul_host_bigint_compare(const void *a, const void *b);
bool $ghul_host_bigint_equals(const void *a, const void *b);
int32_t $ghul_host_bigint_hash(const void *value);
int32_t $ghul_host_bigint_bit_length(const void *value);
bool $ghul_host_bigint_is_integer_text(const uint16_t *chars, int32_t length);
void *$ghul_host_bigint_parse(const uint16_t *chars, int32_t length);
void *$ghul_host_bigint_to_string(const void *value);
void *$ghul_host_bigint_to_hex(const void *value, bool upper);

/*
 * Files and directories on the real file system. Paths are as the program
 * gives them; the core library makes them full first. An operation that can
 * fail answers one of these codes, and file_read, which answers the bytes,
 * leaves its code for file_last_error:
 *
 *   0 done, 1 the file is not there, 2 a directory on the way is not there,
 *   3 access is denied, 4 any other I/O error
 *
 * from which the core library throws FileNotFoundException,
 * DirectoryNotFoundException, UnauthorizedAccessException or IOException,
 * as .NET does.
 */
void *$ghul_host_file_current_directory(void);

/* 0 for nothing there, 1 for a file, 2 for a directory. */
int32_t $ghul_host_file_kind(const uint16_t *path, int32_t length);

/* The whole of the file, or NULL with the reason in file_last_error. */
void *$ghul_host_file_read(const uint16_t *path, int32_t length);
int32_t $ghul_host_file_last_error(void);

/* Replaces the file's content with `bytes`, or adds them to its end. */
int32_t $ghul_host_file_write(const uint16_t *path, int32_t length, const uint8_t *bytes,
                             int32_t count, bool append);

/* Deletes the file; one that is not there is not an error. */
int32_t $ghul_host_file_delete(const uint16_t *path, int32_t length);

/* Moves a file or directory; nothing may be at the destination already. */
int32_t $ghul_host_file_rename(const uint16_t *from, int32_t from_length, const uint16_t *to,
                              int32_t to_length);

/* Makes the directory the process's current directory. */
int32_t $ghul_host_directory_set_current(const uint16_t *path, int32_t length);

/* Creates the directory and every directory on the way to it. */
int32_t $ghul_host_directory_create(const uint16_t *path, int32_t length);

/* Deletes the directory: with what is in it, or only when it is empty. */
int32_t $ghul_host_directory_delete(const uint16_t *path, int32_t length, bool recursive);

/*
 * Lists the files, or the directories, directly in the directory, answering
 * how many, or a code negated. Each name, without the directory, is then
 * answered by directory_entry. Names are sorted, so a listing is the same
 * from one run to the next.
 */
int32_t $ghul_host_directory_entries(const uint16_t *path, int32_t length, bool directories);
void *$ghul_host_directory_entry(int32_t index);

#endif
