; $ghul_static_init across threads. Eight threads reach one initialiser at
; once; it is slow, and uses its own guard while it runs, as an initialiser
; that uses its own type does. It runs once, and every thread sees what it
; stored once its call returns. Then an initialiser that throws the first
; time: the exception reaches the caller, and the next use runs it again.
;
; Writes "saw 42" eight times, then "ran once", "caught", "ran again", and
; exits with 0.

declare void @say(ptr, i32)
declare ptr @$ghul_alloc(i64)
declare void @$ghul_throw(ptr)
declare ptr @$ghul_caught(ptr)
declare i32 @$ghul_personality(...)
declare ptr @$ghul_host_thread_start(ptr, i64)
declare void @$ghul_host_thread_join(ptr)
declare void @$ghul_host_sleep(i32)
declare void @$ghul_static_init(ptr, ptr)

%exception = type { ptr, i32 }

@thrown = constant %exception { ptr null, i32 1 }

@guard = global i32 0
@value = global i32 0
@runs = global i32 0

@failing_guard = global i32 0
@failing_runs = global i32 0

@saw = private constant [8 x i8] c"saw 42\0A\00"
@saw_wrong = private constant [11 x i8] c"saw wrong\0A\00"
@once = private constant [10 x i8] c"ran once\0A\00"
@not_once = private constant [15 x i8] c"ran too often\0A\00"
@caught = private constant [8 x i8] c"caught\0A\00"
@again = private constant [11 x i8] c"ran again\0A\00"
@wrong = private constant [7 x i8] c"wrong\0A\00"

define void @initialise() {
  atomicrmw add ptr @runs, i32 1 seq_cst
  call void @$ghul_host_sleep(i32 100)
  call void @$ghul_static_init(ptr @guard, ptr @initialise)
  store i32 42, ptr @value
  ret void
}

define void @work(ptr %action) {
  call void @$ghul_static_init(ptr @guard, ptr @initialise)
  %value = load i32, ptr @value
  %right = icmp eq i32 %value, 42
  br i1 %right, label %saw, label %wrong

saw:
  call void @say(ptr @saw, i32 1)
  ret void

wrong:
  call void @say(ptr @saw_wrong, i32 1)
  ret void
}

; Throws on its first run only.
define void @initialise_failing() {
  %before = atomicrmw add ptr @failing_runs, i32 1 seq_cst
  %first = icmp eq i32 %before, 0
  br i1 %first, label %throw, label %done

throw:
  call void @$ghul_throw(ptr @thrown)
  unreachable

done:
  ret void
}

define i32 @$ghul_entry() personality ptr @$ghul_personality {
entry:
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
  %runs = load i32, ptr @runs
  %one = icmp eq i32 %runs, 1
  br i1 %one, label %ran_once, label %ran_more

ran_once:
  call void @say(ptr @once, i32 1)
  br label %failing

ran_more:
  call void @say(ptr @not_once, i32 1)
  br label %failing

failing:
  invoke void @$ghul_static_init(ptr @failing_guard, ptr @initialise_failing)
    to label %no_throw unwind label %pad

no_throw:
  call void @say(ptr @wrong, i32 1)
  ret i32 1

pad:
  %landed = landingpad { ptr, i32 } catch ptr null
  call void @say(ptr @caught, i32 1)
  call void @$ghul_static_init(ptr @failing_guard, ptr @initialise_failing)
  %failing_runs = load i32, ptr @failing_runs
  %guard = load i32, ptr @failing_guard
  %twice = icmp eq i32 %failing_runs, 2
  %done = icmp eq i32 %guard, 2
  %both = and i1 %twice, %done
  br i1 %both, label %ran_again, label %not_again

ran_again:
  call void @say(ptr @again, i32 1)
  ret i32 0

not_again:
  call void @say(ptr @wrong, i32 1)
  ret i32 1
}

@escaped = private constant [9 x i8] c"escaped\0A\00"

define void @$ghul_report_unhandled(ptr %exception) {
  call void @say(ptr @escaped, i32 2)
  ret void
}
