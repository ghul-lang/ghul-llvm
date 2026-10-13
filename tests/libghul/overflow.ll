; Writes a line, then recurses until the stack runs out. The fault is
; reported as a stack overflow, with the frames, after the line is flushed,
; and the process ends with the signal.

declare void @say(ptr, i32)

@before = private constant [8 x i8] c"before\0A\00"

; The frame escapes to an empty asm statement, so the recursion stays a
; recursion rather than becoming a loop.
define internal i32 @Test.deep(i32 %n) noinline {
  %frame = alloca [64 x i8]
  call void asm sideeffect "", "r,~{memory}"(ptr %frame)
  %next = add i32 %n, 1
  %result = call i32 @Test.deep(i32 %next)
  %more = add i32 %result, 1
  ret i32 %more
}

define i32 @$ghul_entry() {
  call void @say(ptr @before, i32 1)
  %result = call i32 @Test.deep(i32 0)
  ret i32 %result
}
