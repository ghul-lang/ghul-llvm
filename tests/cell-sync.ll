; A cell that uses the per-thread slot, a mutex and a condition variable
; through the host, as the core library does for its own state. It sets the
; slot and reads it back, takes and releases a mutex, and waits on a
; condition with a timeout no one signals, answering 7 where all of them
; behave.

declare ptr @"$ghul_alloc"(i64)
declare ptr @"$ghul_host_thread_state"()
declare void @"$ghul_host_thread_set_state"(ptr)
declare ptr @"$ghul_host_mutex_new"()
declare i1 @"$ghul_host_mutex_lock"(ptr)
declare i1 @"$ghul_host_mutex_unlock"(ptr)
declare ptr @"$ghul_host_condition_new"()
declare i32 @"$ghul_host_condition_wait"(ptr, ptr, i64)

define i32 @"$ghul_entry"() {
  %state = call ptr @"$ghul_alloc"(i64 8)
  call void @"$ghul_host_thread_set_state"(ptr %state)
  %read = call ptr @"$ghul_host_thread_state"()
  %slot_ok = icmp eq ptr %read, %state
  %mutex = call ptr @"$ghul_host_mutex_new"()
  %locked = call i1 @"$ghul_host_mutex_lock"(ptr %mutex)
  %condition = call ptr @"$ghul_host_condition_new"()
  %waited = call i32 @"$ghul_host_condition_wait"(ptr %condition, ptr %mutex, i64 5)
  %timed_out = icmp eq i32 %waited, 0
  %unlocked = call i1 @"$ghul_host_mutex_unlock"(ptr %mutex)
  %a = and i1 %slot_ok, %locked
  %b = and i1 %a, %timed_out
  %all = and i1 %b, %unlocked
  %answer = select i1 %all, i32 7, i32 1
  ret i32 %answer
}
