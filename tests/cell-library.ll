; A cell of definitions alone: it has no entry point, and a cell loaded after
; it calls what it defines.

define i32 @add_one(i32 %value) {
  %result = add i32 %value, 1
  ret i32 %result
}
