; A function for tests/caller.ll to link against, built separately: the way a
; runtime library reaches a program.

define i32 @answer() {
  ret i32 42
}
