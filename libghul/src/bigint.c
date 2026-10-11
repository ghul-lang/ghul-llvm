/*
 * The host side of bigint, over libtommath. A bigint crosses the boundary
 * as a handle: a pointer to an mp_int in the collected heap, whose digits
 * are a collected allocation of their own, so a handle needs no freeing and
 * lives as long as something refers to it. A handle is never changed once
 * it has been answered.
 *
 * The semantics are .NET's BigInteger, as the Wasm target's loader gives
 * them: division truncates, a remainder takes the dividend's sign, shifts
 * are arithmetic, and the bitwise operations act on two's complement.
 */

#include "internal.h"

#include <gc.h>
#include <math.h>
#include <string.h>
#include <tommath.h>

/* --- libtommath's allocator: digits in the collected heap --------------- */

void *$libghul_mp_malloc(size_t size) {
    return GC_MALLOC_ATOMIC(size);
}

void *$libghul_mp_calloc(size_t count, size_t size) {
    void *memory = GC_MALLOC_ATOMIC(count * size);

    if (memory) {
        memset(memory, 0, count * size);
    }

    return memory;
}

void *$libghul_mp_realloc(void *memory, size_t old_size, size_t new_size) {
    (void)old_size;
    return GC_REALLOC(memory, new_size);
}

void $libghul_mp_free(void *memory, size_t size) {
    (void)size;
    GC_FREE(memory);
}

/* --- Handles ------------------------------------------------------------ */

static void check(mp_err error) {
    if (error == MP_MEM) {
        $libghul_fail("Out of memory.\n");
    }

    if (error != MP_OKAY) {
        $libghul_fail("Unhandled exception. Ghul.ArithmeticException: bigint operation failed.\n");
    }
}

/* A new handle holding zero. */
static mp_int *fresh(void) {
    mp_int *value = GC_MALLOC(sizeof(mp_int));

    if (!value) {
        $libghul_fail("Out of memory.\n");
    }

    check(mp_init(value));

    return value;
}

void *$ghul_host_bigint_from_int(int32_t value) {
    mp_int *result = fresh();
    mp_set_i32(result, value);
    return result;
}

void *$ghul_host_bigint_from_long(int64_t value) {
    mp_int *result = fresh();
    mp_set_i64(result, value);
    return result;
}

void *$ghul_host_bigint_from_ulong(uint64_t value) {
    mp_int *result = fresh();
    mp_set_u64(result, value);
    return result;
}

void *$ghul_host_bigint_from_double(double value) {
    if (!isfinite(value)) {
        $libghul_fail("Unhandled exception. Ghul.OverflowException: "
                     "Value was either too large or too small for a bigint.\n");
    }

    mp_int *result = fresh();
    check(mp_set_double(result, trunc(value)));
    return result;
}

/* Whether the value fits in an i64, so the program can hold it there. */
bool $ghul_host_bigint_fits_long(const void *handle) {
    const mp_int *value = handle;
    int bits = mp_count_bits(value);

    if (bits < 64) {
        return true;
    }

    /* The one 64-bit magnitude that fits: the lowest long, -2^63. */
    if (bits == 64 && mp_isneg(value)) {
        mp_int lowest;
        check(mp_init(&lowest));
        mp_set_i64(&lowest, INT64_MIN);
        bool fits = mp_cmp(value, &lowest) == MP_EQ;
        mp_clear(&lowest);
        return fits;
    }

    return false;
}

int64_t $ghul_host_bigint_to_long(const void *value) {
    /* The low 64 bits of the two's complement, as a cast wraps. */
    return mp_get_i64(value);
}

double $ghul_host_bigint_to_double(const void *handle) {
    const mp_int *value = handle;
    int bits = mp_count_bits(value);

    if (bits <= 63) {
        return (double)mp_get_i64(value);
    }

    /*
     * Rounded to the nearest double, ties to even, as .NET converts: the
     * top 64 bits of the magnitude, with whether any bit below them is set.
     */
    mp_int top, below, magnitude;
    check(mp_init_multi(&top, &below, &magnitude, NULL));
    check(mp_abs(value, &magnitude));
    check(mp_div_2d(&magnitude, bits - 64, &top, &below));

    uint64_t high = mp_get_mag_u64(&top);
    int sticky = !mp_iszero(&below);

    mp_clear_multi(&top, &below, &magnitude, NULL);

    uint64_t mantissa = high >> 11;
    uint64_t rest = high & 0x7FF;

    if (rest > 0x400 || (rest == 0x400 && (sticky || (mantissa & 1)))) {
        mantissa++;
    }

    double result = ldexp((double)mantissa, bits - 53);

    return mp_isneg(value) ? -result : result;
}

/* --- Arithmetic ----------------------------------------------------------- */

#define BINARY(name, operation)                                     \
    void *$ghul_host_bigint_##name(const void *a, const void *b) {   \
        mp_int *result = fresh();                                   \
        check(operation(a, b, result));                             \
        return result;                                              \
    }

BINARY(add, mp_add)
BINARY(subtract, mp_sub)
BINARY(multiply, mp_mul)
BINARY(and, mp_and)
BINARY(or, mp_or)
BINARY(xor, mp_xor)
BINARY(gcd, mp_gcd)

void *$ghul_host_bigint_divide(const void *a, const void *b) {
    mp_int *result = fresh();
    check(mp_div(a, b, result, NULL));
    return result;
}

void *$ghul_host_bigint_remainder(const void *a, const void *b) {
    mp_int *result = fresh();
    check(mp_div(a, b, NULL, result));
    return result;
}

void *$ghul_host_bigint_negate(const void *a) {
    mp_int *result = fresh();
    check(mp_neg(a, result));
    return result;
}

void *$ghul_host_bigint_not(const void *a) {
    mp_int *result = fresh();
    check(mp_complement(a, result));
    return result;
}

static void *shift(const void *a, int64_t places) {
    mp_int *result = fresh();

    if (places >= 0) {
        check(mp_mul_2d(a, (int)places, result));
    } else {
        check(mp_signed_rsh(a, (int)-places, result));
    }

    return result;
}

/* A shift by a negative count shifts the other way, as .NET's does. */
void *$ghul_host_bigint_shift_left(const void *a, int32_t places) {
    return shift(a, places);
}

void *$ghul_host_bigint_shift_right(const void *a, int32_t places) {
    return shift(a, -(int64_t)places);
}

void *$ghul_host_bigint_pow(const void *value, int32_t exponent) {
    if (exponent < 0) {
        $libghul_fail("Unhandled exception. Ghul.ArgumentOutOfRangeException: "
                     "The exponent must not be negative.\n");
    }

    mp_int *result = fresh();
    check(mp_expt_n(value, exponent, result));
    return result;
}

/*
 * Square and multiply with truncating remainders, so a negative value gives
 * a result of its sign, as .NET's ModPow does.
 */
void *$ghul_host_bigint_mod_pow(const void *value, const void *exponent, const void *modulus) {
    if (mp_isneg((const mp_int *)exponent)) {
        $libghul_fail("Unhandled exception. Ghul.ArgumentOutOfRangeException: "
                     "The exponent must not be negative.\n");
    }

    if (mp_iszero((const mp_int *)modulus)) {
        $libghul_fail("Unhandled exception. Ghul.DivideByZeroException: "
                     "Attempted to divide by zero.\n");
    }

    mp_int *result = fresh();
    mp_int base, rest, product;
    check(mp_init_multi(&base, &rest, &product, NULL));

    mp_set(result, 1);
    check(mp_div(result, modulus, NULL, result));
    check(mp_div(value, modulus, NULL, &base));
    check(mp_copy(exponent, &rest));

    while (mp_cmp_d(&rest, 0) == MP_GT) {
        if (mp_isodd(&rest)) {
            check(mp_mul(result, &base, &product));
            check(mp_div(&product, modulus, NULL, result));
        }

        check(mp_mul(&base, &base, &product));
        check(mp_div(&product, modulus, NULL, &base));
        check(mp_div_2d(&rest, 1, &rest, NULL));
    }

    mp_clear_multi(&base, &rest, &product, NULL);

    return result;
}

/* --- Comparison and hashing ---------------------------------------------- */

int32_t $ghul_host_bigint_compare(const void *a, const void *b) {
    mp_ord order = mp_cmp(a, b);
    return order == MP_LT ? -1 : order == MP_GT ? 1 : 0;
}

bool $ghul_host_bigint_equals(const void *a, const void *b) {
    return mp_cmp(a, b) == MP_EQ;
}

/*
 * The low 32 bits of value ^ (value >> 32), as the Wasm target hashes. .NET's
 * own BigInteger hash differs from one process to the next, so no other
 * target can match it; this one only has to agree with equality.
 */
int32_t $ghul_host_bigint_hash(const void *value) {
    mp_int high, mixed;
    check(mp_init_multi(&high, &mixed, NULL));
    check(mp_signed_rsh(value, 32, &high));
    check(mp_xor(value, &high, &mixed));

    int32_t hash = (int32_t)(uint32_t)mp_get_i64(&mixed);

    mp_clear_multi(&high, &mixed, NULL);

    return hash;
}

/* The bits a two's complement value needs besides its sign. */
int32_t $ghul_host_bigint_bit_length(const void *value) {
    mp_int magnitude;
    check(mp_init(&magnitude));

    if (mp_isneg((const mp_int *)value)) {
        check(mp_complement(value, &magnitude));
    } else {
        check(mp_copy(value, &magnitude));
    }

    int32_t bits = mp_iszero(&magnitude) ? 0 : mp_count_bits(&magnitude);

    mp_clear(&magnitude);

    return bits;
}

/* --- Text ------------------------------------------------------------------ */

/* What JavaScript's \s matches, which the Wasm target's parse trims. */
static int is_space(uint16_t unit) {
    return (unit >= 0x09 && unit <= 0x0D) || unit == 0x20 || unit == 0xA0 || unit == 0x1680 ||
           (unit >= 0x2000 && unit <= 0x200A) || unit == 0x2028 || unit == 0x2029 ||
           unit == 0x202F || unit == 0x205F || unit == 0x3000 || unit == 0xFEFF;
}

/*
 * Copies an optional sign and the decimal digits of text, trimmed, into
 * `ascii`, which has room for length + 1 bytes. Answers false where text is
 * not a whole number.
 */
static int integer_text(const uint16_t *chars, int32_t length, char *ascii) {
    int32_t start = 0, end = length;

    while (start < end && is_space(chars[start])) {
        start++;
    }

    while (end > start && is_space(chars[end - 1])) {
        end--;
    }

    size_t out = 0;

    if (start < end && (chars[start] == '+' || chars[start] == '-')) {
        if (chars[start] == '-') {
            ascii[out++] = '-';
        }

        start++;
    }

    if (start == end) {
        return 0;
    }

    for (int32_t i = start; i < end; i++) {
        if (chars[i] < '0' || chars[i] > '9') {
            return 0;
        }

        ascii[out++] = (char)chars[i];
    }

    ascii[out] = 0;
    return 1;
}

bool $ghul_host_bigint_is_integer_text(const uint16_t *chars, int32_t length) {
    char *ascii = GC_MALLOC_ATOMIC((size_t)length + 1);

    if (!ascii) {
        $libghul_fail("Out of memory.\n");
    }

    return integer_text(chars, length, ascii);
}

void *$ghul_host_bigint_parse(const uint16_t *chars, int32_t length) {
    char *ascii = GC_MALLOC_ATOMIC((size_t)length + 1);

    if (!ascii) {
        $libghul_fail("Out of memory.\n");
    }

    if (!integer_text(chars, length, ascii)) {
        $libghul_fail("Unhandled exception. Ghul.FormatException: "
                     "The value could not be parsed.\n");
    }

    mp_int *result = fresh();
    check(mp_read_radix(result, ascii, 10));
    return result;
}

static void *string_of(const char *ascii, size_t length) {
    $libghul_utf16 text = {0};

    for (size_t i = 0; i < length; i++) {
        $libghul_utf16_add(&text, (uint16_t)ascii[i]);
    }

    return $libghul_utf16_make_string(&text);
}

/* `value` in `radix`, lower case, with a '-' for a negative value. */
static char *radix_text(const mp_int *value, int radix, size_t *length) {
    int size;
    check(mp_radix_size(value, radix, &size));

    char *text = GC_MALLOC_ATOMIC((size_t)size);

    if (!text) {
        $libghul_fail("Out of memory.\n");
    }

    size_t written;
    check(mp_to_radix(value, text, (size_t)size, &written, radix));

    /* written counts the terminating NUL. */
    *length = written - 1;

    for (size_t i = 0; i < *length; i++) {
        if (text[i] >= 'A' && text[i] <= 'Z') {
            text[i] = (char)(text[i] - 'A' + 'a');
        }
    }

    return text;
}

void *$ghul_host_bigint_to_string(const void *value) {
    size_t length;
    char *text = radix_text(value, 10, &length);
    return string_of(text, length);
}

/*
 * Two's complement in as few hex digits as hold the value and its sign, as
 * .NET writes a BigInteger under `X`: a leading 0 keeps a positive value
 * whose top digit is 8 or more positive, and a leading f keeps a negative
 * value whose top digit is below 8 negative.
 */
void *$ghul_host_bigint_to_hex(const void *handle, bool upper) {
    const mp_int *value = handle;

    if (mp_iszero(value)) {
        return string_of(upper ? "0" : "0", 1);
    }

    mp_int shown;
    check(mp_init(&shown));

    size_t digits;

    if (mp_isneg(value)) {
        /* 2^(4 * digits) + value, where digits is how many ~value takes. */
        mp_int complement, power;
        check(mp_init_multi(&complement, &power, NULL));
        check(mp_complement(value, &complement));

        digits = 1;

        if (!mp_iszero(&complement)) {
            radix_text(&complement, 16, &digits);
        }

        check(mp_2expt(&power, (int)(4 * digits)));
        check(mp_add(&power, value, &shown));

        mp_clear_multi(&complement, &power, NULL);
    } else {
        check(mp_copy(value, &shown));
        radix_text(&shown, 16, &digits);
    }

    size_t length;
    char *text = radix_text(&shown, 16, &length);

    mp_clear(&shown);

    /* Padded to `digits` with zeros, and one more digit to hold the sign. */
    char *out = GC_MALLOC_ATOMIC(digits + 2);

    if (!out) {
        $libghul_fail("Out of memory.\n");
    }

    size_t at = 0;
    int top = (int)(length < digits ? '0' : text[0]);
    int top_value = top <= '9' ? top - '0' : top - 'a' + 10;

    if (mp_isneg(value) ? top_value < 8 : top_value >= 8) {
        out[at++] = mp_isneg(value) ? 'f' : '0';
    }

    for (size_t pad = length; pad < digits; pad++) {
        out[at++] = '0';
    }

    memcpy(out + at, text, length);
    at += length;

    if (upper) {
        for (size_t i = 0; i < at; i++) {
            if (out[i] >= 'a' && out[i] <= 'f') {
                out[i] = (char)(out[i] - 'a' + 'A');
            }
        }
    }

    return string_of(out, at);
}
