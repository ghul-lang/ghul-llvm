; Eight threads at once each build and walk lists on the collected heap, so
; collections happen while every thread is running, and throw and catch
; exceptions, so the unwinder runs on several threads together. Each writes
; one line when its results check out; the main thread joins them all.
;
; Writes "worker ok" eight times and "joined", and exits with 0.

declare void @say(ptr, i32)
declare ptr @$ghul_alloc(i64)
declare void @$ghul_throw(ptr)
declare ptr @$ghul_caught(ptr)
declare i32 @$ghul_personality(...)
declare ptr @$ghul_host_thread_start(ptr, i64)
declare void @$ghul_host_thread_join(ptr)

%node = type { ptr, i64 }
%exception = type { ptr, i32 }

@thrown = constant %exception { ptr null, i32 1 }

@ok = private constant [11 x i8] c"worker ok\0A\00"
@bad = private constant [12 x i8] c"worker bad\0A\00"
@joined = private constant [8 x i8] c"joined\0A\00"
@failed = private constant [17 x i8] c"could not start\0A\00"

define void @throw_one() noinline {
  call void @$ghul_throw(ptr @thrown)
  unreachable
}

; The list 0 .. 19999, newest first.
define ptr @build() noinline {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %next, %loop ]
  %head = phi ptr [ null, %entry ], [ %node, %loop ]
  %node = call ptr @$ghul_alloc(i64 16)
  store ptr %head, ptr %node
  %value_at = getelementptr %node, ptr %node, i32 0, i32 1
  store i64 %i, ptr %value_at
  %next = add i64 %i, 1
  %more = icmp ult i64 %next, 20000
  br i1 %more, label %loop, label %done

done:
  ret ptr %node
}

define i64 @total(ptr %list) noinline {
entry:
  br label %loop

loop:
  %at = phi ptr [ %list, %entry ], [ %rest, %step ]
  %sum = phi i64 [ 0, %entry ], [ %added, %step ]
  %end = icmp eq ptr %at, null
  br i1 %end, label %done, label %step

step:
  %value_at = getelementptr %node, ptr %at, i32 0, i32 1
  %value = load i64, ptr %value_at
  %added = add i64 %sum, %value
  %rest = load ptr, ptr %at
  br label %loop

done:
  ret i64 %sum
}

; Fifty rounds of a list built and summed, then an exception thrown and
; caught.
define void @work(ptr %action) personality ptr @$ghul_personality {
entry:
  br label %round

round:
  %r = phi i32 [ 0, %entry ], [ %r_next, %caught ]
  %list = call ptr @build()
  %sum = call i64 @total(ptr %list)
  %right = icmp eq i64 %sum, 199990000
  br i1 %right, label %throwing, label %wrong

throwing:
  invoke void @throw_one() to label %wrong unwind label %pad

pad:
  %landed = landingpad { ptr, i32 } catch ptr null
  %header = extractvalue { ptr, i32 } %landed, 0
  %exception = call ptr @$ghul_caught(ptr %header)
  %is_thrown = icmp eq ptr %exception, @thrown
  br i1 %is_thrown, label %caught, label %wrong

caught:
  %r_next = add i32 %r, 1
  %again = icmp ult i32 %r_next, 50
  br i1 %again, label %round, label %done

done:
  call void @say(ptr @ok, i32 1)
  ret void

wrong:
  call void @say(ptr @bad, i32 1)
  ret void
}

define i32 @$ghul_entry() {
entry:
  %handles = alloca [8 x ptr]
  br label %start

start:
  %i = phi i64 [ 0, %entry ], [ %i_next, %started ]
  %action = call ptr @$ghul_alloc(i64 8)
  store ptr @work, ptr %action
  %handle = call ptr @$ghul_host_thread_start(ptr %action, i64 0)
  %none = icmp eq ptr %handle, null
  br i1 %none, label %failed, label %started

started:
  %slot = getelementptr [8 x ptr], ptr %handles, i64 0, i64 %i
  store ptr %handle, ptr %slot
  %i_next = add i64 %i, 1
  %more = icmp ult i64 %i_next, 8
  br i1 %more, label %start, label %join

join:
  %j = phi i64 [ 0, %started ], [ %j_next, %join ]
  %joined_slot = getelementptr [8 x ptr], ptr %handles, i64 0, i64 %j
  %joined_handle = load ptr, ptr %joined_slot
  call void @$ghul_host_thread_join(ptr %joined_handle)
  %j_next = add i64 %j, 1
  %more_joins = icmp ult i64 %j_next, 8
  br i1 %more_joins, label %join, label %done

done:
  call void @say(ptr @joined, i32 1)
  ret i32 0

failed:
  call void @say(ptr @failed, i32 1)
  ret i32 1
}

@escaped = private constant [9 x i8] c"escaped\0A\00"

define void @$ghul_report_unhandled(ptr %exception) {
  call void @say(ptr @escaped, i32 2)
  ret void
}
