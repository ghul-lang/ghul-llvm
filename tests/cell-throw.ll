; A cell whose entry point throws an exception nothing catches.

declare void @"$ghul_throw"(ptr) noreturn

define i32 @"$ghul_entry"() {
  call void @"$ghul_throw"(ptr null)
  unreachable
}
