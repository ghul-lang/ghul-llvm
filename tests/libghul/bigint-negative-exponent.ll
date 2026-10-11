; mod_pow with a negative exponent, which .NET refuses: the process ends as
; an unhandled exception rather than answering.

declare ptr @$ghul_host_bigint_from_int(i32)
declare ptr @$ghul_host_bigint_mod_pow(ptr, ptr, ptr)

define i32 @$ghul_entry() {
  %value = call ptr @$ghul_host_bigint_from_int(i32 3)
  %exponent = call ptr @$ghul_host_bigint_from_int(i32 -2)
  %modulus = call ptr @$ghul_host_bigint_from_int(i32 7)
  %result = call ptr @$ghul_host_bigint_mod_pow(ptr %value, ptr %exponent, ptr %modulus)
  ret i32 0
}
