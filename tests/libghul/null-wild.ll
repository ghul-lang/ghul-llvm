; A read that faults outside the first 64KB is not a read through null, and
; ends the process as it would without libghul, though the program can make
; null-reference exceptions.

@null_reference = constant { ptr, i32 } { ptr null, i32 7 }
@wild = global i64 1099511627776

define ptr @$ghul_make_null_reference() {
  ret ptr @null_reference
}

define i32 @$ghul_entry() {
  %address = load i64, ptr @wild
  %p = inttoptr i64 %address to ptr
  %value = load volatile i32, ptr %p
  ret i32 %value
}

define void @$ghul_report_unhandled(ptr %exception) {
  ret void
}
