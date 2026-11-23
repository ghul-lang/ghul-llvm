; Condition variables. A worker takes the mutex the main thread is waiting
; with, sets a flag and signals, so waiting released the mutex and the
; signal woke the waiter. Four workers wait at once and one broadcast wakes
; them all. A wait with nobody to signal times out holding the mutex, and a
; wait without the mutex is refused.
;
; Writes a line per check, and exits with 0.

declare void @say(ptr, i32)
declare ptr @$ghul_alloc(i64)
declare ptr @$ghul_host_thread_start(ptr, i64)
declare void @$ghul_host_thread_join(ptr)
declare ptr @$ghul_host_mutex_new()
declare i1 @$ghul_host_mutex_lock(ptr)
declare i1 @$ghul_host_mutex_unlock(ptr)
declare ptr @$ghul_host_condition_new()
declare i32 @$ghul_host_condition_wait(ptr, ptr, i64)
declare void @$ghul_host_condition_signal(ptr)
declare void @$ghul_host_condition_broadcast(ptr)

@mutex = global ptr null
@condition = global ptr null
@flag = global i32 0
@ready = global i32 0
@go = global i32 0
@woken = global i32 0

@signalled = private constant [17 x i8] c"signal woke it\0A\00\00"
@broadcast = private constant [24 x i8] c"broadcast woke all 4\0A\00\00\00"
@timed_out = private constant [32 x i8] c"timed out still holding it\0A\00\00\00\00\00"
@refused = private constant [27 x i8] c"wait without it refused\0A\00\00\00"
@wrong = private constant [7 x i8] c"wrong\0A\00"

define void @check(i1 %ok, ptr %line) {
  br i1 %ok, label %right, label %bad

right:
  call void @say(ptr %line, i32 1)
  ret void

bad:
  call void @say(ptr @wrong, i32 1)
  ret void
}

; Takes the mutex, sets the flag and signals.
define void @setter(ptr %action) {
  %mutex = load ptr, ptr @mutex
  %condition = load ptr, ptr @condition
  call i1 @$ghul_host_mutex_lock(ptr %mutex)
  store volatile i32 1, ptr @flag
  call void @$ghul_host_condition_signal(ptr %condition)
  call i1 @$ghul_host_mutex_unlock(ptr %mutex)
  ret void
}

; Says it is ready, then waits until go is set, and counts itself woken.
define void @waiter(ptr %action) {
entry:
  %mutex = load ptr, ptr @mutex
  %condition = load ptr, ptr @condition
  call i1 @$ghul_host_mutex_lock(ptr %mutex)
  %ready = load volatile i32, ptr @ready
  %ready_next = add i32 %ready, 1
  store volatile i32 %ready_next, ptr @ready
  br label %test

test:
  %go = load volatile i32, ptr @go
  %going = icmp eq i32 %go, 1
  br i1 %going, label %done, label %wait

wait:
  call i32 @$ghul_host_condition_wait(ptr %condition, ptr %mutex, i64 -1)
  br label %test

done:
  %woken = load volatile i32, ptr @woken
  %woken_next = add i32 %woken, 1
  store volatile i32 %woken_next, ptr @woken
  call i1 @$ghul_host_mutex_unlock(ptr %mutex)
  ret void
}

define i32 @$ghul_entry() {
entry:
  %mutex = call ptr @$ghul_host_mutex_new()
  store ptr %mutex, ptr @mutex
  %condition = call ptr @$ghul_host_condition_new()
  store ptr %condition, ptr @condition

  ; signal
  call i1 @$ghul_host_mutex_lock(ptr %mutex)
  %setter_action = call ptr @$ghul_alloc(i64 8)
  store ptr @setter, ptr %setter_action
  %setter = call ptr @$ghul_host_thread_start(ptr %setter_action, i64 0)
  br label %flag_test

flag_test:
  %flag = load volatile i32, ptr @flag
  %set = icmp eq i32 %flag, 1
  br i1 %set, label %flag_set, label %flag_wait

flag_wait:
  call i32 @$ghul_host_condition_wait(ptr %condition, ptr %mutex, i64 -1)
  br label %flag_test

flag_set:
  %unlocked = call i1 @$ghul_host_mutex_unlock(ptr %mutex)
  call void @check(i1 %unlocked, ptr @signalled)
  call void @$ghul_host_thread_join(ptr %setter)

  ; broadcast
  %handles = alloca [4 x ptr]
  br label %start

start:
  %i = phi i64 [ 0, %flag_set ], [ %i_next, %start ]
  %action = call ptr @$ghul_alloc(i64 8)
  store ptr @waiter, ptr %action
  %handle = call ptr @$ghul_host_thread_start(ptr %action, i64 0)
  %slot = getelementptr [4 x ptr], ptr %handles, i64 0, i64 %i
  store ptr %handle, ptr %slot
  %i_next = add i64 %i, 1
  %more = icmp ult i64 %i_next, 4
  br i1 %more, label %start, label %wait_ready

wait_ready:
  call i1 @$ghul_host_mutex_lock(ptr %mutex)
  %ready = load volatile i32, ptr @ready
  %all_ready = icmp eq i32 %ready, 4
  br i1 %all_ready, label %release, label %not_ready

not_ready:
  call i1 @$ghul_host_mutex_unlock(ptr %mutex)
  br label %wait_ready

release:
  store volatile i32 1, ptr @go
  call void @$ghul_host_condition_broadcast(ptr %condition)
  call i1 @$ghul_host_mutex_unlock(ptr %mutex)
  br label %join

join:
  %j = phi i64 [ 0, %release ], [ %j_next, %join ]
  %joined_slot = getelementptr [4 x ptr], ptr %handles, i64 0, i64 %j
  %joined_handle = load ptr, ptr %joined_slot
  call void @$ghul_host_thread_join(ptr %joined_handle)
  %j_next = add i64 %j, 1
  %more_joins = icmp ult i64 %j_next, 4
  br i1 %more_joins, label %join, label %joined

joined:
  %woken = load volatile i32, ptr @woken
  %all_woken = icmp eq i32 %woken, 4
  call void @check(i1 %all_woken, ptr @broadcast)

  ; timeout
  call i1 @$ghul_host_mutex_lock(ptr %mutex)
  %timed = call i32 @$ghul_host_condition_wait(ptr %condition, ptr %mutex, i64 20)
  %was_timeout = icmp eq i32 %timed, 0
  %still_held = call i1 @$ghul_host_mutex_unlock(ptr %mutex)
  %timeout_ok = and i1 %was_timeout, %still_held
  call void @check(i1 %timeout_ok, ptr @timed_out)

  ; not held
  %unheld = call i32 @$ghul_host_condition_wait(ptr %condition, ptr %mutex, i64 -1)
  %was_refused = icmp eq i32 %unheld, -1
  call void @check(i1 %was_refused, ptr @refused)
  ret i32 0
}
