; A cell whose entry point never returns.

@spin = internal global i32 0

define i32 @"$ghul_entry"() {
entry:
  br label %loop

loop:
  store volatile i32 1, ptr @spin
  br label %loop
}
