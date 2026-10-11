; Clones an object that holds pointers and one that does not, and checks
; each copy: a different object with the same contents, allocated as the
; same kind and at least the same size, whose identity hash differs from
; the original's while the original's stays the same. Exits 0 when every
; check holds, or with the number of the first that fails.

declare ptr @$ghul_alloc(ptr, i64)
declare ptr @$ghul_alloc_atomic(ptr, i64)
declare ptr @$ghul_clone(ptr)
declare i32 @$ghul_identity_hash(ptr)
declare i32 @GC_get_kind_and_size(ptr, ptr)

@descriptor = private constant i64 7

define i32 @check(ptr %original, i32 %base) {
entry:
  %field = getelementptr i64, ptr %original, i64 1
  store i64 12345, ptr %field
  %copy = call ptr @$ghul_clone(ptr %original)
  %same = icmp eq ptr %copy, %original
  br i1 %same, label %fail0, label %contents

contents:
  %copied_field = getelementptr i64, ptr %copy, i64 1
  %value = load i64, ptr %copied_field
  %header = load ptr, ptr %copy
  %value_ok = icmp eq i64 %value, 12345
  %header_ok = icmp eq ptr %header, @descriptor
  %both = and i1 %value_ok, %header_ok
  br i1 %both, label %kinds, label %fail1

kinds:
  %size_a = alloca i64
  %size_b = alloca i64
  %kind_a = call i32 @GC_get_kind_and_size(ptr %original, ptr %size_a)
  %kind_b = call i32 @GC_get_kind_and_size(ptr %copy, ptr %size_b)
  %a = load i64, ptr %size_a
  %b = load i64, ptr %size_b
  %kind_ok = icmp eq i32 %kind_a, %kind_b
  %size_ok = icmp uge i64 %b, %a
  %alike = and i1 %kind_ok, %size_ok
  br i1 %alike, label %hashes, label %fail2

hashes:
  %h1 = call i32 @$ghul_identity_hash(ptr %original)
  %h2 = call i32 @$ghul_identity_hash(ptr %original)
  %h3 = call i32 @$ghul_identity_hash(ptr %copy)
  %stable = icmp eq i32 %h1, %h2
  %distinct = icmp ne i32 %h1, %h3
  %hash_ok = and i1 %stable, %distinct
  br i1 %hash_ok, label %pass, label %fail3

pass:
  ret i32 0

fail0:
  %r0 = add i32 %base, 1
  ret i32 %r0

fail1:
  %r1 = add i32 %base, 2
  ret i32 %r1

fail2:
  %r2 = add i32 %base, 3
  ret i32 %r2

fail3:
  %r3 = add i32 %base, 4
  ret i32 %r3
}

define i32 @$ghul_entry() {
entry:
  %scanned = call ptr @$ghul_alloc(ptr @descriptor, i64 24)
  %first = call i32 @check(ptr %scanned, i32 0)
  %first_ok = icmp eq i32 %first, 0
  br i1 %first_ok, label %atomic, label %done_first

done_first:
  ret i32 %first

atomic:
  %unscanned = call ptr @$ghul_alloc_atomic(ptr @descriptor, i64 24)
  %second = call i32 @check(ptr %unscanned, i32 10)
  ret i32 %second
}
