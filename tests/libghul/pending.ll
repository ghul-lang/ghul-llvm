; Waits for standard input and asks whether a line is pending before each
; read, writing W or w for what the wait answered and P or p for what
; pending answered, then > and the line. At the end of the input writes E,
; asks both again, and exits with 0. A wait that times out with nothing
; pending exits with 3.

declare void @say(ptr, i32)
declare void @write_string(ptr, i32)
declare ptr @$ghul_host_stdin_read_line()
declare i1 @$ghul_host_stdin_pending()
declare i1 @$ghul_host_stdin_wait(i32)

@upper_w = private constant [2 x i8] c"W\00"
@lower_w = private constant [2 x i8] c"w\00"
@upper_p = private constant [2 x i8] c"P\00"
@lower_p = private constant [2 x i8] c"p\00"
@end_mark = private constant [2 x i8] c"E\00"
@prefix = private constant [2 x i8] c">\00"
@newline = private constant [2 x i8] c"\0A\00"

define private i1 @wait() {
  %waited = call i1 @$ghul_host_stdin_wait(i32 500)
  %mark = select i1 %waited, ptr @upper_w, ptr @lower_w
  call void @say(ptr %mark, i32 1)
  ret i1 %waited
}

define private i1 @pending() {
  %pending = call i1 @$ghul_host_stdin_pending()
  %mark = select i1 %pending, ptr @upper_p, ptr @lower_p
  call void @say(ptr %mark, i32 1)
  ret i1 %pending
}

define i32 @$ghul_entry() {
entry:
  br label %loop

loop:
  %waited = call i1 @wait()
  %pending = call i1 @pending()
  %idle = or i1 %waited, %pending
  br i1 %idle, label %read, label %timed_out

read:
  %line = call ptr @$ghul_host_stdin_read_line()
  %done = icmp eq ptr %line, null
  br i1 %done, label %end, label %echo

echo:
  call void @say(ptr @prefix, i32 1)
  call void @write_string(ptr %line, i32 1)
  call void @say(ptr @newline, i32 1)
  br label %loop

end:
  call void @say(ptr @end_mark, i32 1)
  %after_pending = call i1 @pending()
  %after_wait = call i1 @wait()
  ret i32 0

timed_out:
  ret i32 3
}
