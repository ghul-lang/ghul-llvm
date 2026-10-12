; A read that faults outside the first 64KB is not a read through null, and
; ends the process as it would without libghul, though the program tests
; for null and can make null-reference exceptions.

declare void @$ghul_throw_null_reference()

@null_reference = constant { ptr, i32 } { ptr null, i32 7 }
@wild = global i64 1099511627776

define ptr @$ghul_make_null_reference() {
  ret ptr @null_reference
}

define i32 @$ghul_entry() {
entry:
  %address = load i64, ptr @wild
  %p = inttoptr i64 %address to ptr
  %null = icmp eq ptr %p, null
  br i1 %null, label %is_null, label %read

is_null:
  call void @$ghul_throw_null_reference()
  unreachable

read:
  %value = load volatile i32, ptr %p
  ret i32 %value
}

define void @$ghul_report_unhandled(ptr %exception) {
  ret void
}
