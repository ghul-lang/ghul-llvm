; A thread that recurses until its stack runs out. The fault is reported as
; a stack overflow on the thread's own signal stack, and the process ends
; with the signal, as it does when the main thread's stack runs out.

declare ptr @$ghul_alloc(i64)
declare ptr @$ghul_host_thread_start(ptr, i64)
declare void @$ghul_host_thread_join(ptr)

define internal i32 @Test.deep(i32 %n) noinline {
  %frame = alloca [64 x i8]
  call void asm sideeffect "", "r,~{memory}"(ptr %frame)
  %next = add i32 %n, 1
  %result = call i32 @Test.deep(i32 %next)
  %more = add i32 %result, 1
  ret i32 %more
}

define void @work(ptr %action) {
  call i32 @Test.deep(i32 0)
  ret void
}

define i32 @$ghul_entry() {
  %action = call ptr @$ghul_alloc(i64 8)
  store ptr @work, ptr %action
  %handle = call ptr @$ghul_host_thread_start(ptr %action, i64 1048576)
  call void @$ghul_host_thread_join(ptr %handle)
  ret i32 0
}
