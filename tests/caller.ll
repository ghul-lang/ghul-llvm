; Exits with the status a separately built function returns.

declare i32 @answer()

define i32 @main() {
  %status = call i32 @answer()
  ret i32 %status
}
