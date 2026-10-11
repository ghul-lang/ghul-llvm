; Peeks at and reads the first character of standard input, then writes
; everything after it. Exits with 0 when peek and read agree and the input
; is at its end afterwards; with 1 or 2 otherwise.

declare void @write_string(ptr, i32)
declare i32 @ghul_host_stdin_peek()
declare i32 @ghul_host_stdin_read()
declare ptr @ghul_host_stdin_read_to_end()

define i32 @ghul_entry() {
  %peeked = call i32 @ghul_host_stdin_peek()
  %read = call i32 @ghul_host_stdin_read()
  %agree = icmp eq i32 %peeked, %read
  br i1 %agree, label %rest, label %disagree

rest:
  %remainder = call ptr @ghul_host_stdin_read_to_end()
  call void @write_string(ptr %remainder, i32 1)
  %after = call i32 @ghul_host_stdin_read()
  %at_end = icmp eq i32 %after, -1
  br i1 %at_end, label %pass, label %not_at_end

pass:
  ret i32 0

disagree:
  ret i32 1

not_at_end:
  ret i32 2
}
