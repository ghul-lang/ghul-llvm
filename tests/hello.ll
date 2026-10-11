; Prints a line through the C library and exits with status 0.

@greeting = private unnamed_addr constant [21 x i8] c"hello from ghul-llvm\00"

declare i32 @puts(ptr)

define i32 @main() {
  %result = call i32 @puts(ptr @greeting)
  ret i32 0
}
