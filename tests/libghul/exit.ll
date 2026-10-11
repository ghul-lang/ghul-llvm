; Writes a line without ending it, then leaves through the exit host
; operation rather than by returning: what was written must still appear.

declare void @say(ptr, i32)
declare void @$ghul_host_exit(i32)

@text = private constant [17 x i8] c"written, no end.\00"

define i32 @$ghul_entry() {
  call void @say(ptr @text, i32 1)
  call void @$ghul_host_exit(i32 5)
  unreachable
}
