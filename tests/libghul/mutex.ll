; Eight threads each add one to a shared count 100000 times, under one
; mutex, so the count comes out exact. Then on the main thread: a mutex is
; not reentrant, only its holder can release it, and try_lock takes only a
; free one.
;
; Writes a line per check, and exits with 0.

declare void @say(ptr, i32)
declare ptr @$ghul_alloc(i64)
declare ptr @$ghul_host_thread_start(ptr, i64)
declare void @$ghul_host_thread_join(ptr)
declare ptr @$ghul_host_mutex_new()
declare i1 @$ghul_host_mutex_lock(ptr)
declare i1 @$ghul_host_mutex_try_lock(ptr)
declare i1 @$ghul_host_mutex_unlock(ptr)

@mutex = global ptr null
@count = global i64 0

@count_right = private constant [13 x i8] c"count right\0A\00"
@count_wrong = private constant [13 x i8] c"count wrong\0A\00"
@relock = private constant [24 x i8] c"second lock refused: 1\0A\00"
@unlock_free = private constant [25 x i8] c"unlocking a free one: 0\0A\00"
@try_free = private constant [23 x i8] c"try_lock when free: 1\0A\00"
@try_held = private constant [23 x i8] c"try_lock when held: 0\0A\00"
@wrong = private constant [7 x i8] c"wrong\0A\00"

define void @work(ptr %action) {
entry:
  %mutex = load ptr, ptr @mutex
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]
  call i1 @$ghul_host_mutex_lock(ptr %mutex)
  %count = load volatile i64, ptr @count
  %added = add i64 %count, 1
  store volatile i64 %added, ptr @count
  call i1 @$ghul_host_mutex_unlock(ptr %mutex)
  %next = add i32 %i, 1
  %more = icmp ult i32 %next, 100000
  br i1 %more, label %loop, label %done

done:
  ret void
}

; Takes the mutex on another thread and holds it until told to let go.
@holding = global i32 0
@release = global i32 0

define void @hold(ptr %action) {
entry:
  %mutex = load ptr, ptr @mutex
  call i1 @$ghul_host_mutex_lock(ptr %mutex)
  store atomic i32 1, ptr @holding seq_cst, align 4
  br label %wait

wait:
  %go = load atomic i32, ptr @release seq_cst, align 4
  %stop = icmp eq i32 %go, 1
  br i1 %stop, label %done, label %wait

done:
  call i1 @$ghul_host_mutex_unlock(ptr %mutex)
  ret void
}

define void @check(i1 %ok, ptr %line) {
  br i1 %ok, label %right, label %bad

right:
  call void @say(ptr %line, i32 1)
  ret void

bad:
  call void @say(ptr @wrong, i32 1)
  ret void
}

define i32 @$ghul_entry() {
entry:
  %mutex = call ptr @$ghul_host_mutex_new()
  store ptr %mutex, ptr @mutex
  %handles = alloca [8 x ptr]
  br label %start

start:
  %i = phi i64 [ 0, %entry ], [ %i_next, %start ]
  %action = call ptr @$ghul_alloc(i64 8)
  store ptr @work, ptr %action
  %handle = call ptr @$ghul_host_thread_start(ptr %action, i64 0)
  %slot = getelementptr [8 x ptr], ptr %handles, i64 0, i64 %i
  store ptr %handle, ptr %slot
  %i_next = add i64 %i, 1
  %more = icmp ult i64 %i_next, 8
  br i1 %more, label %start, label %join

join:
  %j = phi i64 [ 0, %start ], [ %j_next, %join ]
  %joined_slot = getelementptr [8 x ptr], ptr %handles, i64 0, i64 %j
  %joined_handle = load ptr, ptr %joined_slot
  call void @$ghul_host_thread_join(ptr %joined_handle)
  %j_next = add i64 %j, 1
  %more_joins = icmp ult i64 %j_next, 8
  br i1 %more_joins, label %join, label %counted

counted:
  %count = load i64, ptr @count
  %exact = icmp eq i64 %count, 800000
  br i1 %exact, label %right, label %off

right:
  call void @say(ptr @count_right, i32 1)
  br label %main_checks

off:
  call void @say(ptr @count_wrong, i32 1)
  br label %main_checks

main_checks:
  %first = call i1 @$ghul_host_mutex_lock(ptr %mutex)
  %second = call i1 @$ghul_host_mutex_lock(ptr %mutex)
  %refused = xor i1 %second, true
  %both = and i1 %first, %refused
  call void @check(i1 %both, ptr @relock)
  %released = call i1 @$ghul_host_mutex_unlock(ptr %mutex)
  %again = call i1 @$ghul_host_mutex_unlock(ptr %mutex)
  %not_again = xor i1 %again, true
  %unlock_ok = and i1 %released, %not_again
  call void @check(i1 %unlock_ok, ptr @unlock_free)
  %took = call i1 @$ghul_host_mutex_try_lock(ptr %mutex)
  call void @check(i1 %took, ptr @try_free)
  call i1 @$ghul_host_mutex_unlock(ptr %mutex)

  %holder_action = call ptr @$ghul_alloc(i64 8)
  store ptr @hold, ptr %holder_action
  %holder = call ptr @$ghul_host_thread_start(ptr %holder_action, i64 0)
  br label %wait_held

wait_held:
  %held = load atomic i32, ptr @holding seq_cst, align 4
  %is_held = icmp eq i32 %held, 1
  br i1 %is_held, label %try_held, label %wait_held

try_held:
  %took_held = call i1 @$ghul_host_mutex_try_lock(ptr %mutex)
  %refused_held = xor i1 %took_held, true
  call void @check(i1 %refused_held, ptr @try_held)
  store atomic i32 1, ptr @release seq_cst, align 4
  call void @$ghul_host_thread_join(ptr %holder)
  ret i32 0
}
