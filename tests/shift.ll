; A variable shift, which x86-64-v3's BMI2 does in one instruction (shlx)
; where the generic CPU shifts by %cl.
define i64 @shift(i64 %value, i64 %places) {
  %result = shl i64 %value, %places
  ret i64 %result
}
