; Defines the exit sweep, then throws an exception nothing catches: the
; exception is reported, the sweep runs once, and the status is 134.

declare void @say(ptr, i32)
declare void @$ghul_throw(ptr)

@body = private constant [6 x i8] c"body\0A\00"
@swept = private constant [7 x i8] c"swept\0A\00"
@description = private constant [42 x i8] c"Unhandled exception. Test.Failure: swept\0A\00"
@exception = private constant i8 0

define void @$ghul_report_unhandled(ptr %exception) {
  call void @say(ptr @description, i32 2)
  ret void
}

define void @$ghul_exit_sweep() {
  call void @say(ptr @swept, i32 1)
  ret void
}

define internal void @Test.thrower() noinline {
  call void @$ghul_throw(ptr @exception)
  unreachable
}

define i32 @$ghul_entry() {
  call void @say(ptr @body, i32 1)
  call void @Test.thrower()
  unreachable
}
