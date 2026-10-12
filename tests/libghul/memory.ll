; Checks libghul's memcpy, memmove and memset byte by byte, for every size
; from 0 to 520, which crosses each of their size paths, at every alignment
; of source and destination within eight bytes, and for memmove at every
; overlap within forty bytes in either direction. Every byte of the buffer
; is checked, so a write outside the range is caught as well as a wrong one
; inside it. The buffers are filled and checked through volatile accesses,
; so the optimiser cannot turn those loops into calls of the functions under
; test. Exits 0 when every case holds, or 1 at the first that does not.

declare ptr @memcpy(ptr, ptr, i64)
declare ptr @memmove(ptr, ptr, i64)
declare ptr @memset(ptr, i32, i64)

@a = internal global [1300 x i8] zeroinitializer
@b = internal global [1300 x i8] zeroinitializer

define internal i8 @pattern(i64 %i) {
  %scaled = mul i64 %i, 7
  %offset = add i64 %scaled, 3
  %byte = trunc i64 %offset to i8
  ret i8 %byte
}

; Fills @a with the pattern and @b with 0xEE.
define internal void @fill() {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %value = call i8 @pattern(i64 %i)
  %at.a = getelementptr i8, ptr @a, i64 %i
  store volatile i8 %value, ptr %at.a
  %at.b = getelementptr i8, ptr @b, i64 %i
  store volatile i8 -18, ptr %at.b
  %i.next = add i64 %i, 1
  %done = icmp eq i64 %i.next, 1300
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; One case: kind 0 copies @a+s to @b+d, kind 1 moves @a+s to @a+d, kind 2
; sets @b+d to 0x5A, each for n bytes. Answers whether every byte of the
; buffer written is as it should be.
define internal i1 @trial(i32 %kind, i64 %n, i64 %s, i64 %d) {
entry:
  call void @fill()
  %from = getelementptr i8, ptr @a, i64 %s
  %to.a = getelementptr i8, ptr @a, i64 %d
  %to.b = getelementptr i8, ptr @b, i64 %d
  switch i32 %kind, label %set [ i32 0, label %copy
                                  i32 1, label %move ]

copy:
  call ptr @memcpy(ptr %to.b, ptr %from, i64 %n)
  br label %check

move:
  call ptr @memmove(ptr %to.a, ptr %from, i64 %n)
  br label %check

set:
  call ptr @memset(ptr %to.b, i32 90, i64 %n)
  br label %check

check:
  %is.move = icmp eq i32 %kind, 1
  %buffer = select i1 %is.move, ptr @a, ptr @b
  %is.set = icmp eq i32 %kind, 2
  %end = add i64 %d, %n
  br label %loop

loop:
  %i = phi i64 [ 0, %check ], [ %i.next, %next ]
  %at = getelementptr i8, ptr %buffer, i64 %i
  %actual = load volatile i8, ptr %at
  %at.or.after = icmp uge i64 %i, %d
  %before.end = icmp ult i64 %i, %end
  %inside = and i1 %at.or.after, %before.end
  %relative = sub i64 %i, %d
  %source = add i64 %s, %relative
  %copied = call i8 @pattern(i64 %source)
  %written = select i1 %is.set, i8 90, i8 %copied
  %original.a = call i8 @pattern(i64 %i)
  %original = select i1 %is.move, i8 %original.a, i8 -18
  %expected = select i1 %inside, i8 %written, i8 %original
  %same = icmp eq i8 %actual, %expected
  br i1 %same, label %next, label %wrong

next:
  %i.next = add i64 %i, 1
  %done = icmp eq i64 %i.next, 1300
  br i1 %done, label %right, label %loop

right:
  ret i1 true

wrong:
  ret i1 false
}

define i32 @$ghul_entry() {
entry:
  br label %size

size:
  %n = phi i64 [ 0, %entry ], [ %n.next, %size.next ]
  br label %source

source:
  %s = phi i64 [ 0, %size ], [ %s.next, %source.next ]
  br label %destination

destination:
  %d = phi i64 [ 0, %source ], [ %d.next, %destination.next ]
  %copy.ok = call i1 @trial(i32 0, i64 %n, i64 %s, i64 %d)
  %set.ok = call i1 @trial(i32 2, i64 %n, i64 %s, i64 %d)
  %both = and i1 %copy.ok, %set.ok
  br i1 %both, label %destination.next, label %failed

destination.next:
  %d.next = add i64 %d, 1
  %d.done = icmp eq i64 %d.next, 8
  br i1 %d.done, label %source.next, label %destination

source.next:
  %s.next = add i64 %s, 1
  %s.done = icmp eq i64 %s.next, 8
  br i1 %s.done, label %shift.start, label %source

shift.start:
  br label %shift

shift:
  %shift.i = phi i64 [ 0, %shift.start ], [ %shift.next, %shift.continue ]
  %move.ok = call i1 @trial(i32 1, i64 %n, i64 40, i64 %shift.i)
  br i1 %move.ok, label %shift.continue, label %failed

shift.continue:
  %shift.next = add i64 %shift.i, 1
  %shift.done = icmp eq i64 %shift.next, 81
  br i1 %shift.done, label %size.next, label %shift

size.next:
  %n.next = add i64 %n, 1
  %n.done = icmp eq i64 %n.next, 521
  br i1 %n.done, label %passed, label %size

passed:
  ret i32 0

failed:
  ret i32 1
}
