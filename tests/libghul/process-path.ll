; Writes the running executable's path on a line, then "now" if the wall
; clock reads later than 2020-01-01, which an unset clock would not.

declare void @say(ptr, i32)
declare void @write_string(ptr, i32)
declare ptr @$ghul_host_process_path()
declare double @$ghul_host_wall_clock_milliseconds()

@newline = private constant [2 x i8] c"\0A\00"
@absent = private constant [8 x i8] c"absent\0A\00"
@now = private constant [5 x i8] c"now\0A\00"
@then = private constant [6 x i8] c"then\0A\00"

define i32 @$ghul_entry() {
entry:
  %path = call ptr @$ghul_host_process_path()
  %is_absent = icmp eq ptr %path, null
  br i1 %is_absent, label %no_path, label %has_path

no_path:
  call void @say(ptr @absent, i32 1)
  br label %clock

has_path:
  call void @write_string(ptr %path, i32 1)
  call void @say(ptr @newline, i32 1)
  br label %clock

clock:
  %ms = call double @$ghul_host_wall_clock_milliseconds()
  %later = fcmp ogt double %ms, 1.5778368e12
  br i1 %later, label %is_now, label %is_then

is_now:
  call void @say(ptr @now, i32 1)
  ret i32 0

is_then:
  call void @say(ptr @then, i32 1)
  ret i32 0
}
