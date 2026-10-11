; Runs a shell command that writes to standard output and standard error
; and exits 4, then writes what was captured from each on a line, and exits
; with the command's status.

declare void @say(ptr, i32)
declare void @write_string(ptr, i32)
declare void @$ghul_host_process_begin(ptr, i32)
declare void @$ghul_host_process_add_argument(ptr, i32)
declare i32 @$ghul_host_process_run()
declare ptr @$ghul_host_process_output()
declare ptr @$ghul_host_process_error()

@newline = private constant [2 x i8] c"\0A\00"
@shell = private constant [7 x i16] [i16 47, i16 98, i16 105, i16 110, i16 47, i16 115, i16 104]
@flag = private constant [2 x i16] [i16 45, i16 99]
@script = private constant [34 x i16] [i16 112, i16 114, i16 105, i16 110, i16 116, i16 102, i16 32, i16 111, i16 117, i16 116, i16 59, i16 32, i16 112, i16 114, i16 105, i16 110, i16 116, i16 102, i16 32, i16 101, i16 114, i16 114, i16 32, i16 62, i16 38, i16 50, i16 59, i16 32, i16 101, i16 120, i16 105, i16 116, i16 32, i16 52]

define i32 @$ghul_entry() {
  call void @$ghul_host_process_begin(ptr @shell, i32 7)
  call void @$ghul_host_process_add_argument(ptr @flag, i32 2)
  call void @$ghul_host_process_add_argument(ptr @script, i32 34)
  %status = call i32 @$ghul_host_process_run()
  %out = call ptr @$ghul_host_process_output()
  %err = call ptr @$ghul_host_process_error()
  call void @write_string(ptr %out, i32 1)
  call void @say(ptr @newline, i32 1)
  call void @write_string(ptr %err, i32 1)
  call void @say(ptr @newline, i32 1)
  ret i32 %status
}
