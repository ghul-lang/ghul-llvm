; Prints the version of the zlib it is linked against, a shared library of
; the system's, for the dynamic link's smoke test.

declare ptr @zlibVersion()
declare i32 @puts(ptr)

define i32 @main() {
  %version = call ptr @zlibVersion()
  %result = call i32 @puts(ptr %version)
  ret i32 0
}
