; Writes a line, then throws an exception nothing catches. libghul has
; the program describe it, flushes the line, and exits with status 134.

declare void @say(ptr, i32)
declare void @$ghul_throw(ptr)

@before = private constant [8 x i8] c"before\0A\00"
@description = private constant [45 x i8] c"Unhandled exception. Test.Failure: it broke\0A\00"
@exception = private constant i8 0

define void @$ghul_report_unhandled(ptr %exception) {
  call void @say(ptr @description, i32 2)
  ret void
}

define i32 @$ghul_entry() {
  call void @say(ptr @before, i32 1)
  call void @$ghul_throw(ptr @exception)
  unreachable
}
