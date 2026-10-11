; Runs every host.bigint_* operation and writes each answer on a line, for
; tests/libghul/bigint.expected, which holds what the Wasm target's loader
; answers for the same operations: the semantics libghul is to match.

declare void @say(ptr, i32)
declare void @write_string(ptr, i32)

declare ptr @ghul_host_bigint_from_int(i32)
declare ptr @ghul_host_bigint_from_long(i64)
declare ptr @ghul_host_bigint_from_ulong(i64)
declare ptr @ghul_host_bigint_from_double(double)
declare i64 @ghul_host_bigint_to_long(ptr)
declare i1 @ghul_host_bigint_fits_long(ptr)
declare double @ghul_host_bigint_to_double(ptr)
declare ptr @ghul_host_bigint_add(ptr, ptr)
declare ptr @ghul_host_bigint_subtract(ptr, ptr)
declare ptr @ghul_host_bigint_multiply(ptr, ptr)
declare ptr @ghul_host_bigint_divide(ptr, ptr)
declare ptr @ghul_host_bigint_remainder(ptr, ptr)
declare ptr @ghul_host_bigint_negate(ptr)
declare ptr @ghul_host_bigint_not(ptr)
declare ptr @ghul_host_bigint_and(ptr, ptr)
declare ptr @ghul_host_bigint_or(ptr, ptr)
declare ptr @ghul_host_bigint_xor(ptr, ptr)
declare ptr @ghul_host_bigint_shift_left(ptr, i32)
declare ptr @ghul_host_bigint_shift_right(ptr, i32)
declare ptr @ghul_host_bigint_pow(ptr, i32)
declare ptr @ghul_host_bigint_mod_pow(ptr, ptr, ptr)
declare ptr @ghul_host_bigint_gcd(ptr, ptr)
declare i32 @ghul_host_bigint_compare(ptr, ptr)
declare i1 @ghul_host_bigint_equals(ptr, ptr)
declare i32 @ghul_host_bigint_hash(ptr)
declare i32 @ghul_host_bigint_bit_length(ptr)
declare i1 @ghul_host_bigint_is_integer_text(ptr, i32)
declare ptr @ghul_host_bigint_parse(ptr, i32)
declare ptr @ghul_host_bigint_to_string(ptr)
declare ptr @ghul_host_bigint_to_hex(ptr, i1)

@newline = private constant [2 x i8] c"\0A\00"

; Copies NUL-terminated ASCII into `buffer` as UTF-16; answers the length.
define i32 @widen(ptr %text, ptr %buffer) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %copy ]
  %byte_at = getelementptr i8, ptr %text, i32 %i
  %byte = load i8, ptr %byte_at
  %done = icmp eq i8 %byte, 0
  br i1 %done, label %end, label %copy

copy:
  %unit = zext i8 %byte to i16
  %unit_at = getelementptr i16, ptr %buffer, i32 %i
  store i16 %unit, ptr %unit_at
  %next = add i32 %i, 1
  br label %loop

end:
  ret i32 %i
}

define ptr @big(ptr %text) {
  %buffer = alloca [512 x i16]
  %length = call i32 @widen(ptr %text, ptr %buffer)
  %value = call ptr @ghul_host_bigint_parse(ptr %buffer, i32 %length)
  ret ptr %value
}

define i1 @is_integer(ptr %text) {
  %buffer = alloca [512 x i16]
  %length = call i32 @widen(ptr %text, ptr %buffer)
  %answer = call i1 @ghul_host_bigint_is_integer_text(ptr %buffer, i32 %length)
  ret i1 %answer
}

define void @show(ptr %value) {
  %text = call ptr @ghul_host_bigint_to_string(ptr %value)
  call void @write_string(ptr %text, i32 1)
  call void @say(ptr @newline, i32 1)
  ret void
}

define void @show_int(i32 %value) {
  %big = call ptr @ghul_host_bigint_from_int(i32 %value)
  call void @show(ptr %big)
  ret void
}

define void @show_long(i64 %value) {
  %big = call ptr @ghul_host_bigint_from_long(i64 %value)
  call void @show(ptr %big)
  ret void
}

define void @show_bool(i1 %value) {
  %number = zext i1 %value to i32
  call void @show_int(i32 %number)
  ret void
}

; A double, shown as the bigint of its integer value.
define void @show_double(double %value) {
  %big = call ptr @ghul_host_bigint_from_double(double %value)
  call void @show(ptr %big)
  ret void
}

define void @show_hex(ptr %value, i1 %upper) {
  %text = call ptr @ghul_host_bigint_to_hex(ptr %value, i1 %upper)
  call void @write_string(ptr %text, i32 1)
  call void @say(ptr @newline, i32 1)
  ret void
}

@a_text = private constant [32 x i8] c"1267650600228229401496703217721\00"
@b_text = private constant [26 x i8] c"-717897987691852588770249\00"
@c_text = private constant [11 x i8] c"9876543211\00"
@d_text = private constant [5 x i8] c"-937\00"
@spaced = private constant [10 x i8] c"  +42 \09\0D\0A\00"
@not_number = private constant [4 x i8] c"12a\00"
@negative_spaced = private constant [5 x i8] c" -7 \00"
@empty = private constant [1 x i8] c"\00"
@sign_only = private constant [2 x i8] c"-\00"
@two_53_plus_1 = private constant [17 x i8] c"9007199254740993\00"
@two_53_plus_3 = private constant [17 x i8] c"9007199254740995\00"
@two_64_plus_1 = private constant [21 x i8] c"18446744073709551617\00"
@sticky = private constant [21 x i8] c"18446744073709553665\00"
@huge = private constant [41 x i8] c"-123456789012345678901234567890123456789\00"
@one_two_eight = private constant [4 x i8] c"128\00"
@minus_one_two_eight = private constant [5 x i8] c"-128\00"
@minus_one = private constant [3 x i8] c"-1\00"
@zero = private constant [2 x i8] c"0\00"
@two_five_five = private constant [4 x i8] c"255\00"
@minus_two_five_six = private constant [5 x i8] c"-256\00"
@minus_two_five_seven = private constant [5 x i8] c"-257\00"
@nbsp_five = private constant [3 x i16] [i16 160, i16 32, i16 53]

define i32 @ghul_entry() {
  %a = call ptr @big(ptr @a_text)
  %b = call ptr @big(ptr @b_text)
  %c = call ptr @big(ptr @c_text)
  %d = call ptr @big(ptr @d_text)

  ; arithmetic
  %sum = call ptr @ghul_host_bigint_add(ptr %a, ptr %b)
  call void @show(ptr %sum)
  %difference = call ptr @ghul_host_bigint_subtract(ptr %b, ptr %a)
  call void @show(ptr %difference)
  %product = call ptr @ghul_host_bigint_multiply(ptr %a, ptr %b)
  call void @show(ptr %product)
  %quotient = call ptr @ghul_host_bigint_divide(ptr %a, ptr %b)
  call void @show(ptr %quotient)
  %remainder = call ptr @ghul_host_bigint_remainder(ptr %a, ptr %b)
  call void @show(ptr %remainder)
  %quotient2 = call ptr @ghul_host_bigint_divide(ptr %b, ptr %c)
  call void @show(ptr %quotient2)
  %remainder2 = call ptr @ghul_host_bigint_remainder(ptr %b, ptr %c)
  call void @show(ptr %remainder2)
  %negated = call ptr @ghul_host_bigint_negate(ptr %b)
  call void @show(ptr %negated)

  ; bitwise, on two's complement
  %not_a = call ptr @ghul_host_bigint_not(ptr %a)
  call void @show(ptr %not_a)
  %not_b = call ptr @ghul_host_bigint_not(ptr %b)
  call void @show(ptr %not_b)
  %and = call ptr @ghul_host_bigint_and(ptr %a, ptr %b)
  call void @show(ptr %and)
  %or = call ptr @ghul_host_bigint_or(ptr %a, ptr %b)
  call void @show(ptr %or)
  %xor = call ptr @ghul_host_bigint_xor(ptr %a, ptr %b)
  call void @show(ptr %xor)
  %and_d = call ptr @ghul_host_bigint_and(ptr %b, ptr %d)
  call void @show(ptr %and_d)

  ; shifts, including negative counts
  %left = call ptr @ghul_host_bigint_shift_left(ptr %a, i32 70)
  call void @show(ptr %left)
  %right = call ptr @ghul_host_bigint_shift_right(ptr %b, i32 33)
  call void @show(ptr %right)
  %right_neg = call ptr @ghul_host_bigint_shift_right(ptr %d, i32 3)
  call void @show(ptr %right_neg)
  %back_right = call ptr @ghul_host_bigint_shift_right(ptr %c, i32 -5)
  call void @show(ptr %back_right)
  %back_left = call ptr @ghul_host_bigint_shift_left(ptr %b, i32 -7)
  call void @show(ptr %back_left)

  ; powers and divisors
  %cube = call ptr @ghul_host_bigint_pow(ptr %b, i32 3)
  call void @show(ptr %cube)
  %one = call ptr @ghul_host_bigint_pow(ptr %a, i32 0)
  call void @show(ptr %one)
  %mod_pow = call ptr @ghul_host_bigint_mod_pow(ptr %b, ptr %c, ptr %a)
  call void @show(ptr %mod_pow)
  %mod_pow_small = call ptr @ghul_host_bigint_mod_pow(ptr %d, ptr %c, ptr %c)
  call void @show(ptr %mod_pow_small)
  %gcd = call ptr @ghul_host_bigint_gcd(ptr %b, ptr %c)
  call void @show(ptr %gcd)
  %product_bc = call ptr @ghul_host_bigint_multiply(ptr %b, ptr %c)
  %gcd2 = call ptr @ghul_host_bigint_gcd(ptr %product_bc, ptr %b)
  call void @show(ptr %gcd2)

  ; comparison, equality and hashing
  %order = call i32 @ghul_host_bigint_compare(ptr %a, ptr %b)
  call void @show_int(i32 %order)
  %order2 = call i32 @ghul_host_bigint_compare(ptr %b, ptr %d)
  call void @show_int(i32 %order2)
  %a_again = call ptr @big(ptr @a_text)
  %order3 = call i32 @ghul_host_bigint_compare(ptr %a, ptr %a_again)
  call void @show_int(i32 %order3)
  %same = call i1 @ghul_host_bigint_equals(ptr %a, ptr %a_again)
  call void @show_bool(i1 %same)
  %different = call i1 @ghul_host_bigint_equals(ptr %a, ptr %b)
  call void @show_bool(i1 %different)
  %hash_a = call i32 @ghul_host_bigint_hash(ptr %a)
  call void @show_int(i32 %hash_a)
  %hash_b = call i32 @ghul_host_bigint_hash(ptr %b)
  call void @show_int(i32 %hash_b)
  %hash_d = call i32 @ghul_host_bigint_hash(ptr %d)
  call void @show_int(i32 %hash_d)
  %hash_c = call i32 @ghul_host_bigint_hash(ptr %c)
  call void @show_int(i32 %hash_c)

  ; bit lengths
  %bits_a = call i32 @ghul_host_bigint_bit_length(ptr %a)
  call void @show_int(i32 %bits_a)
  %bits_b = call i32 @ghul_host_bigint_bit_length(ptr %b)
  call void @show_int(i32 %bits_b)
  %minus_one = call ptr @big(ptr @minus_one)
  %bits_m1 = call i32 @ghul_host_bigint_bit_length(ptr %minus_one)
  call void @show_int(i32 %bits_m1)
  %zero = call ptr @big(ptr @zero)
  %bits_0 = call i32 @ghul_host_bigint_bit_length(ptr %zero)
  call void @show_int(i32 %bits_0)
  %minus_256 = call ptr @big(ptr @minus_two_five_six)
  %bits_m256 = call i32 @ghul_host_bigint_bit_length(ptr %minus_256)
  call void @show_int(i32 %bits_m256)

  ; hex
  call void @show_hex(ptr %a, i1 true)
  call void @show_hex(ptr %b, i1 false)
  call void @show_hex(ptr %minus_one, i1 false)
  call void @show_hex(ptr %zero, i1 false)
  %v128 = call ptr @big(ptr @one_two_eight)
  call void @show_hex(ptr %v128, i1 false)
  %vm128 = call ptr @big(ptr @minus_one_two_eight)
  call void @show_hex(ptr %vm128, i1 false)
  %v255 = call ptr @big(ptr @two_five_five)
  call void @show_hex(ptr %v255, i1 true)
  call void @show_hex(ptr %minus_256, i1 false)
  %vm257 = call ptr @big(ptr @minus_two_five_seven)
  call void @show_hex(ptr %vm257, i1 false)

  ; whether each fits in an i64, at both ends of its range
  %fits_a = call i1 @ghul_host_bigint_fits_long(ptr %a)
  call void @show_bool(i1 %fits_a)
  %fits_d = call i1 @ghul_host_bigint_fits_long(ptr %d)
  call void @show_bool(i1 %fits_d)
  %highest = call ptr @ghul_host_bigint_from_long(i64 9223372036854775807)
  %fits_highest = call i1 @ghul_host_bigint_fits_long(ptr %highest)
  call void @show_bool(i1 %fits_highest)
  %lowest = call ptr @ghul_host_bigint_from_long(i64 -9223372036854775808)
  %fits_lowest = call i1 @ghul_host_bigint_fits_long(ptr %lowest)
  call void @show_bool(i1 %fits_lowest)
  %unit = call ptr @ghul_host_bigint_from_int(i32 1)
  %above = call ptr @ghul_host_bigint_add(ptr %highest, ptr %unit)
  %fits_above = call i1 @ghul_host_bigint_fits_long(ptr %above)
  call void @show_bool(i1 %fits_above)
  %below = call ptr @ghul_host_bigint_subtract(ptr %lowest, ptr %unit)
  %fits_below = call i1 @ghul_host_bigint_fits_long(ptr %below)
  call void @show_bool(i1 %fits_below)

  ; conversions out
  %long_a = call i64 @ghul_host_bigint_to_long(ptr %a)
  call void @show_long(i64 %long_a)
  %long_b = call i64 @ghul_host_bigint_to_long(ptr %b)
  call void @show_long(i64 %long_b)
  %long_d = call i64 @ghul_host_bigint_to_long(ptr %d)
  call void @show_long(i64 %long_d)
  %t1 = call ptr @big(ptr @two_53_plus_1)
  %d1 = call double @ghul_host_bigint_to_double(ptr %t1)
  call void @show_double(double %d1)
  %t3 = call ptr @big(ptr @two_53_plus_3)
  %d3 = call double @ghul_host_bigint_to_double(ptr %t3)
  call void @show_double(double %d3)
  %t64 = call ptr @big(ptr @two_64_plus_1)
  %d64 = call double @ghul_host_bigint_to_double(ptr %t64)
  call void @show_double(double %d64)
  %ts = call ptr @big(ptr @sticky)
  %dsticky = call double @ghul_host_bigint_to_double(ptr %ts)
  call void @show_double(double %dsticky)
  %th = call ptr @big(ptr @huge)
  %dhuge = call double @ghul_host_bigint_to_double(ptr %th)
  call void @show_double(double %dhuge)
  %dneg = call double @ghul_host_bigint_to_double(ptr %b)
  call void @show_double(double %dneg)

  ; conversions in
  %from_min_int = call ptr @ghul_host_bigint_from_int(i32 -2147483648)
  call void @show(ptr %from_min_int)
  %from_min_long = call ptr @ghul_host_bigint_from_long(i64 -9223372036854775808)
  call void @show(ptr %from_min_long)
  %from_ulong = call ptr @ghul_host_bigint_from_ulong(i64 -1)
  call void @show(ptr %from_ulong)
  call void @show_double(double -12.7)
  call void @show_double(double 1.0e30)

  ; text
  %spaced = call ptr @big(ptr @spaced)
  call void @show(ptr %spaced)
  %is1 = call i1 @is_integer(ptr @not_number)
  call void @show_bool(i1 %is1)
  %is2 = call i1 @is_integer(ptr @negative_spaced)
  call void @show_bool(i1 %is2)
  %is3 = call i1 @is_integer(ptr @empty)
  call void @show_bool(i1 %is3)
  %is4 = call i1 @is_integer(ptr @sign_only)
  call void @show_bool(i1 %is4)
  %is5 = call i1 @ghul_host_bigint_is_integer_text(ptr @nbsp_five, i32 3)
  call void @show_bool(i1 %is5)
  %nbsp = call ptr @ghul_host_bigint_parse(ptr @nbsp_five, i32 3)
  call void @show(ptr %nbsp)

  ret i32 0
}
