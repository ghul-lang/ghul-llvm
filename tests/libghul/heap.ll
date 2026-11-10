; Keeps a 4 MiB block reachable from a global and asks how much of the heap
; is in use. Exits with 0 when both answers, after a full collection and
; without one, count at least the block; with 1 or 2 otherwise.

declare ptr @$ghul_alloc_atomic(i64)
declare double @$ghul_host_heap_bytes(i1)

@kept = global ptr null

define i32 @$ghul_entry() {
entry:
  %block = call ptr @$ghul_alloc_atomic(i64 4194304)
  store ptr %block, ptr @kept

  %collected = call double @$ghul_host_heap_bytes(i1 true)
  %collected_counts = fcmp oge double %collected, 4194304.0
  br i1 %collected_counts, label %uncollected, label %short_after_collecting

uncollected:
  %uncollected_bytes = call double @$ghul_host_heap_bytes(i1 false)
  %uncollected_counts = fcmp oge double %uncollected_bytes, 4194304.0
  br i1 %uncollected_counts, label %pass, label %short_without_collecting

pass:
  ret i32 0

short_after_collecting:
  ret i32 1

short_without_collecting:
  ret i32 2
}
