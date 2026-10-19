; A cell: what a host loads with dlopen. It defines the entry point the host
; calls and reaches the host's names (puts here) without linking them.

@greeting = private unnamed_addr constant [18 x i8] c"hello from a cell\00"

declare i32 @puts(ptr)

define i32 @"$ghul_entry"() {
  %result = call i32 @puts(ptr @greeting)
  ret i32 7
}
