; Polls host.stdin_pending until a line is whole, then reads it. Each poll
; reads what has arrived; the line is long, so a poll that looked again at
; everything already decoded would take time quadratic in its length.
; Exits with 0 once the line is read, with 1 where there was none.

declare i1 @$ghul_host_stdin_pending()
declare ptr @$ghul_host_stdin_read_line()

define i32 @$ghul_entry() {
entry:
  br label %poll

poll:
  %pending = call i1 @$ghul_host_stdin_pending()
  br i1 %pending, label %read, label %poll

read:
  %line = call ptr @$ghul_host_stdin_read_line()
  %none = icmp eq ptr %line, null
  br i1 %none, label %missing, label %done

done:
  ret i32 0

missing:
  ret i32 1
}
