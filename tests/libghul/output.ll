; Writes to standard output and standard error, including text that is not
; ASCII, and exits with status 3.

declare void @say(ptr, i32)
declare void @$ghul_host_write(ptr, i32, i32)

@first = private constant [8 x i8] c"stdout \00"
@second = private constant [8 x i8] c"stderr\0A\00"
@third = private constant [7 x i8] c" again\00"

; "é€😀" then an unpaired low surrogate, then a newline: U+00E9, U+20AC,
; U+1F600 as a surrogate pair, U+DC00 alone.
@wide = private constant [6 x i16] [i16 u0xE9, i16 u0x20AC, i16 u0xD83D, i16 u0xDE00, i16 u0xDC00, i16 10]

; A surrogate pair split across two writes is still one character.
@high = private constant [1 x i16] [i16 u0xD83D]
@low = private constant [2 x i16] [i16 u0xDE00, i16 10]

define i32 @$ghul_entry() {
  call void @say(ptr @first, i32 1)
  call void @say(ptr @second, i32 2)
  call void @say(ptr @third, i32 1)
  call void @$ghul_host_write(ptr @wide, i32 6, i32 1)
  call void @$ghul_host_write(ptr @high, i32 1, i32 1)
  call void @$ghul_host_write(ptr @low, i32 2, i32 1)
  ret i32 3
}
