; Defines the exit sweep, then returns from the entry: libghul calls the
; sweep once as the program ends, before standard output is flushed.

declare void @say(ptr, i32)

@body = private constant [6 x i8] c"body\0A\00"
@swept = private constant [7 x i8] c"swept\0A\00"

define void @$ghul_exit_sweep() {
  call void @say(ptr @swept, i32 1)
  ret void
}

define i32 @$ghul_entry() {
  call void @say(ptr @body, i32 1)
  ret i32 0
}
