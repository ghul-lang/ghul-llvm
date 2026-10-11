; 128-bit division has no x86-64 instruction, so code generation calls the
; compiler-rt builtins for it. Exits with status 42 when the quotient and
; remainder are right; the divisor comes from argc so nothing folds away.

define i32 @main(i32 %argc, ptr %argv) {
  %wide = zext i32 %argc to i128
  %divisor = add i128 %wide, 6
  %dividend = shl i128 1, 100
  %quotient = udiv i128 %dividend, %divisor
  %remainder = urem i128 %dividend, %divisor
  %back = mul i128 %quotient, %divisor
  %whole = add i128 %back, %remainder
  %same = icmp eq i128 %whole, %dividend
  %small = icmp ult i128 %remainder, %divisor
  %ok = and i1 %same, %small
  %status = select i1 %ok, i32 42, i32 1
  ret i32 %status
}
