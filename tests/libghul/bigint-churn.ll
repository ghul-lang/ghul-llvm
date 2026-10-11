; Makes a hundred thousand bigints of 12KB each, over a gigabyte in all,
; keeping none: only digits the collector reclaims let it finish under the
; memory the test allows. Exits with 0 when the last one is still right.

declare ptr @$ghul_host_bigint_from_int(i32)
declare ptr @$ghul_host_bigint_shift_left(ptr, i32)
declare i32 @$ghul_host_bigint_bit_length(ptr)

define i32 @$ghul_entry() {
entry:
  %one = call ptr @$ghul_host_bigint_from_int(i32 1)
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]
  %big = call ptr @$ghul_host_bigint_shift_left(ptr %one, i32 100000)
  %next = add i32 %i, 1
  %more = icmp slt i32 %next, 100000
  br i1 %more, label %loop, label %check

check:
  %bits = call i32 @$ghul_host_bigint_bit_length(ptr %big)
  %right = icmp eq i32 %bits, 100001
  %status = select i1 %right, i32 0, i32 1
  ret i32 %status
}
