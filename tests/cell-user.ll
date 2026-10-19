; A cell that calls a function the cell before it defines. The call is
; bound when it is first made, by name, in the host's global scope.

declare i32 @add_one(i32)

define i32 @"$ghul_entry"() {
  %result = call i32 @add_one(i32 41)
  ret i32 %result
}
