; Allocates 4096 objects of one size, as a map keyed by objects of one class
; holds them, and checks that their identity hashes spread across 4096
; buckets chosen by the hash's low bits: at least half of the buckets are
; used. Objects of one size sit at a fixed stride, so a hash that kept the
; address's low bits would use a fraction of them. Exits 0 when the spread
; holds, or 1 when it does not.

declare ptr @$ghul_alloc(ptr, i64)
declare i32 @$ghul_identity_hash(ptr)

@descriptor = private constant i64 7

define i32 @$ghul_entry() {
entry:
  %objects = alloca [4096 x ptr]
  %seen = alloca [4096 x i8]
  call void @llvm.memset.p0.i64(ptr %seen, i8 0, i64 4096, i1 false)
  br label %allocate

allocate:
  %i = phi i64 [ 0, %entry ], [ %i.next, %allocate ]
  %object = call ptr @$ghul_alloc(ptr @descriptor, i64 128)
  %slot = getelementptr [4096 x ptr], ptr %objects, i64 0, i64 %i
  store volatile ptr %object, ptr %slot
  %hash = call i32 @$ghul_identity_hash(ptr %object)
  %bucket = and i32 %hash, 4095
  %bucket.wide = zext i32 %bucket to i64
  %mark = getelementptr [4096 x i8], ptr %seen, i64 0, i64 %bucket.wide
  store i8 1, ptr %mark
  %i.next = add i64 %i, 1
  %more = icmp ult i64 %i.next, 4096
  br i1 %more, label %allocate, label %count

count:
  %j = phi i64 [ 0, %allocate ], [ %j.next, %count ]
  %used = phi i32 [ 0, %allocate ], [ %used.next, %count ]
  %flag.at = getelementptr [4096 x i8], ptr %seen, i64 0, i64 %j
  %flag = load i8, ptr %flag.at
  %flag.wide = zext i8 %flag to i32
  %used.next = add i32 %used, %flag.wide
  %j.next = add i64 %j, 1
  %done = icmp eq i64 %j.next, 4096
  br i1 %done, label %check, label %count

check:
  %spread = icmp uge i32 %used.next, 2048
  br i1 %spread, label %pass, label %fail

pass:
  ret i32 0

fail:
  ret i32 1
}

declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)
