; A cell whose static initialiser runs through the host's $ghul_static_init,
; as every compiled cell's does. The initialiser runs once however often its
; guard is reached, so the entry answers 5.

declare void @"$ghul_static_init"(ptr, ptr)

@guard = global i32 0
@count = global i32 0

define void @initialise() {
  %count = load i32, ptr @count
  %more = add i32 %count, 5
  store i32 %more, ptr @count
  ret void
}

define i32 @"$ghul_entry"() {
  call void @"$ghul_static_init"(ptr @guard, ptr @initialise)
  call void @"$ghul_static_init"(ptr @guard, ptr @initialise)
  %count = load i32, ptr @count
  ret i32 %count
}
