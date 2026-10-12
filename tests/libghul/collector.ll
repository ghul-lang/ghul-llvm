; Allocates far more than the memory the test allows, keeping only what a
; global reaches: a scanned object holding the only pointer to an unscanned
; one. Exits with 0 when both survive with their contents and descriptors
; intact, so the collector both reclaims garbage and keeps what is reachable.

declare ptr @$ghul_alloc(i64)
declare ptr @$ghul_alloc_atomic(i64)

@root_descriptor = constant i8 1
@leaf_descriptor = constant i8 2
@garbage_descriptor = constant i8 3

@root = global ptr null

define i32 @$ghul_entry() {
entry:
  ; root: { descriptor, pointer to leaf }; leaf: { descriptor, i64 marker }
  %root = call ptr @$ghul_alloc(i64 16)
  store ptr @root_descriptor, ptr %root
  store ptr %root, ptr @root
  %leaf = call ptr @$ghul_alloc_atomic(i64 16)
  store ptr @leaf_descriptor, ptr %leaf
  %marker_at = getelementptr i8, ptr %leaf, i64 8
  store i64 12345678, ptr %marker_at
  %child_at = getelementptr i8, ptr %root, i64 8
  store ptr %leaf, ptr %child_at
  br label %churn

churn:
  ; Four million objects of a kilobyte: four gigabytes in all.
  %i = phi i32 [ 0, %entry ], [ %next, %churn ]
  %garbage = call ptr @$ghul_alloc(i64 1024)
  store ptr @garbage_descriptor, ptr %garbage
  %next = add i32 %i, 1
  %more = icmp slt i32 %next, 4000000
  br i1 %more, label %churn, label %check

check:
  %kept_root = load ptr, ptr @root
  %root_descriptor = load ptr, ptr %kept_root
  %root_ok = icmp eq ptr %root_descriptor, @root_descriptor
  %kept_child_at = getelementptr i8, ptr %kept_root, i64 8
  %kept_leaf = load ptr, ptr %kept_child_at
  %leaf_descriptor = load ptr, ptr %kept_leaf
  %leaf_ok = icmp eq ptr %leaf_descriptor, @leaf_descriptor
  %kept_marker_at = getelementptr i8, ptr %kept_leaf, i64 8
  %marker = load i64, ptr %kept_marker_at
  %marker_ok = icmp eq i64 %marker, 12345678
  %both = and i1 %root_ok, %leaf_ok
  %all = and i1 %both, %marker_ok
  %status = select i1 %all, i32 0, i32 1
  ret i32 %status
}
