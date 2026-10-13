; Registers a finalizer on an object made in a function of its own, so
; nothing reaches it once that function returns, then allocates until a
; collection has run the finalizer, which writes a line and sets a flag.
; Exits with 0 when the finalizer ran, and with 1 when it never did.

declare ptr @$ghul_alloc(i64)
declare void @$ghul_register_finalizer(ptr, ptr)
declare void @say(ptr, i32)

@descriptor = constant i8 1
@finalized = global i1 false
@line = private constant [11 x i8] c"finalized\0A\00"

define internal void @Test.finalize(ptr %object) {
  call void @say(ptr @line, i32 1)
  store i1 true, ptr @finalized
  ret void
}

define internal void @Test.make() noinline {
  %object = call ptr @$ghul_alloc(i64 16)
  store ptr @descriptor, ptr %object
  call void @$ghul_register_finalizer(ptr %object, ptr @Test.finalize)
  ret void
}

define i32 @$ghul_entry() {
entry:
  call void @Test.make()
  br label %churn

churn:
  %i = phi i32 [ 0, %entry ], [ %next, %churn ]
  %garbage = call ptr @$ghul_alloc(i64 1024)
  store ptr @descriptor, ptr %garbage
  %next = add i32 %i, 1
  %done = load volatile i1, ptr @finalized
  %limit = icmp sge i32 %next, 1000000
  %stop = or i1 %done, %limit
  br i1 %stop, label %check, label %churn

check:
  %ran = load volatile i1, ptr @finalized
  %status = select i1 %ran, i32 0, i32 1
  ret i32 %status
}
