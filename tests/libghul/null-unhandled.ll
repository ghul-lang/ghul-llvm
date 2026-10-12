; A read through null in a program that does not define
; $ghul_make_null_reference ends the process as it would without libghul.

@nothing = global ptr null

define i32 @$ghul_entry() {
  %p = load ptr, ptr @nothing
  %value = load volatile i32, ptr %p
  ret i32 %value
}

define void @$ghul_report_unhandled(ptr %exception) {
  ret void
}
