; Runs a program that does not exist: the run answers -1, and the failure
; says the program was not found, which becomes the exit status.

declare void @$ghul_host_process_begin(ptr, i32)
declare i32 @$ghul_host_process_run()
declare i32 @$ghul_host_process_failure()

@missing = private constant [20 x i16] [i16 103, i16 104, i16 117, i16 108, i16 45, i16 110, i16 111, i16 45, i16 115, i16 117, i16 99, i16 104, i16 45, i16 112, i16 114, i16 111, i16 103, i16 114, i16 97, i16 109]

define i32 @$ghul_entry() {
  call void @$ghul_host_process_begin(ptr @missing, i32 20)
  %status = call i32 @$ghul_host_process_run()
  %failed = icmp eq i32 %status, -1
  br i1 %failed, label %why, label %ran

why:
  %failure = call i32 @$ghul_host_process_failure()
  ret i32 %failure

ran:
  ret i32 99
}
