; Keeps 128 MB reachable from a global, collects, then allocates 64 MB of
; garbage and asks for an idle collection. With no argument, standard input
; has nothing to read, so the collection runs to the end: exits 0 when it
; answers true and the heap in use has fallen by more than 32 MB, and when a
; second request, with nothing allocated since, answers true as well. With an
; argument, standard input has something waiting, so the collection gives
; up: exits 0 when it answers false.

declare ptr @$ghul_alloc(i64)
declare i1 @$ghul_host_gc_collect_when_idle()
declare void @$ghul_host_gc_set_free_space_divisor(i32)
declare double @$ghul_host_heap_bytes(i1)
declare i32 @$ghul_host_argument_count()

@node_descriptor = constant i8 1
@garbage_descriptor = constant i8 2

@head = global ptr null

define i32 @$ghul_entry() {
entry:
  ; Collect only once the heap's size has been allocated since the last.
  call void @$ghul_host_gc_set_free_space_divisor(i32 1)
  br label %build

build:
  ; Four million nodes of 32 bytes: { descriptor, next }.
  %i = phi i32 [ 0, %entry ], [ %next_i, %build ]
  %node = call ptr @$ghul_alloc(i64 32)
  store ptr @node_descriptor, ptr %node
  %old = load ptr, ptr @head
  %next_at = getelementptr i8, ptr %node, i64 8
  store ptr %old, ptr %next_at
  store ptr %node, ptr @head
  %next_i = add i32 %i, 1
  %more = icmp slt i32 %next_i, 4194304
  br i1 %more, label %build, label %collected

collected:
  %ignored = call double @$ghul_host_heap_bytes(i1 true)
  br label %churn

churn:
  ; 64 MB of garbage, in objects of a kilobyte.
  %j = phi i32 [ 0, %collected ], [ %next_j, %churn ]
  %garbage = call ptr @$ghul_alloc(i64 1024)
  store ptr @garbage_descriptor, ptr %garbage
  %next_j = add i32 %j, 1
  %more_garbage = icmp slt i32 %next_j, 65536
  br i1 %more_garbage, label %churn, label %idle

idle:
  %before = call double @$ghul_host_heap_bytes(i1 false)
  %finished = call i1 @$ghul_host_gc_collect_when_idle()
  %after = call double @$ghul_host_heap_bytes(i1 false)
  %arguments = call i32 @$ghul_host_argument_count()
  %expect_abandoned = icmp sgt i32 %arguments, 0
  br i1 %expect_abandoned, label %abandoned, label %finished_check

abandoned:
  %abandoned_status = select i1 %finished, i32 4, i32 0
  ret i32 %abandoned_status

finished_check:
  br i1 %finished, label %fell, label %not_finished

not_finished:
  ret i32 2

fell:
  %freed = fsub double %before, %after
  %enough = fcmp ogt double %freed, 33554432.0
  br i1 %enough, label %again, label %not_freed

not_freed:
  ret i32 3

again:
  %skipped = call i1 @$ghul_host_gc_collect_when_idle()
  %again_status = select i1 %skipped, i32 0, i32 5
  ret i32 %again_status
}
