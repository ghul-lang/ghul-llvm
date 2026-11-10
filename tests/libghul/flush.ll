; Writes a line and flushes, then a line and polls standard input, waiting
; after each so tests/smoke.sh can look at what has been written meanwhile:
; neither a flush nor a poll may leave output held back while the program
; does something else.

declare void @say(ptr, i32)
declare void @$ghul_host_flush(i32)
declare i1 @$ghul_host_stdin_pending()
declare void @$ghul_host_sleep(i32)

@flushed = private constant [9 x i8] c"flushed\0A\00"
@polled = private constant [8 x i8] c"polled\0A\00"

define i32 @$ghul_entry() {
  call void @say(ptr @flushed, i32 1)
  call void @$ghul_host_flush(i32 1)
  call void @$ghul_host_sleep(i32 1500)
  call void @say(ptr @polled, i32 1)
  %pending = call i1 @$ghul_host_stdin_pending()
  call void @$ghul_host_sleep(i32 1500)
  ret i32 0
}
