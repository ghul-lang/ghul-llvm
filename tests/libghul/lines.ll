; Reads standard input a line at a time and writes each line back after a
; ">", until the end of the input.

declare void @say(ptr, i32)
declare void @write_string(ptr, i32)
declare ptr @$ghul_host_stdin_read_line()

@prefix = private constant [2 x i8] c">\00"
@newline = private constant [2 x i8] c"\0A\00"

define i32 @$ghul_entry() {
entry:
  br label %loop

loop:
  %line = call ptr @$ghul_host_stdin_read_line()
  %done = icmp eq ptr %line, null
  br i1 %done, label %end, label %echo

echo:
  call void @say(ptr @prefix, i32 1)
  call void @write_string(ptr %line, i32 1)
  call void @say(ptr @newline, i32 1)
  br label %loop

end:
  ret i32 0
}
