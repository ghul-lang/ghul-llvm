; What libghul tests share, standing in for what a compiled ghūl program
; provides: $ghul_make_string over a string laid out as a descriptor, a
; length and the UTF-16 code units, and helpers for writing text.

%string = type { ptr, i32, [0 x i16] }

@string_descriptor = constant i8 0

declare ptr @$ghul_alloc_atomic(i64)
declare void @$ghul_host_write(ptr, i32, i32)
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)

define ptr @$ghul_make_string(ptr %chars, i32 %length) {
  %count = sext i32 %length to i64
  %bytes = shl i64 %count, 1
  %size = add i64 %bytes, 12
  %string = call ptr @$ghul_alloc_atomic(i64 %size)
  store ptr @string_descriptor, ptr %string
  %length_field = getelementptr %string, ptr %string, i32 0, i32 1
  store i32 %length, ptr %length_field
  %chars_field = getelementptr %string, ptr %string, i32 0, i32 2
  call void @llvm.memcpy.p0.p0.i64(ptr %chars_field, ptr %chars, i64 %bytes, i1 false)
  ret ptr %string
}

; Writes a string built by $ghul_make_string.
define void @write_string(ptr %string, i32 %stream) {
  %length_field = getelementptr %string, ptr %string, i32 0, i32 1
  %length = load i32, ptr %length_field
  %chars = getelementptr %string, ptr %string, i32 0, i32 2
  call void @$ghul_host_write(ptr %chars, i32 %length, i32 %stream)
  ret void
}

; Writes a NUL-terminated ASCII string of at most 256 characters.
define void @say(ptr %text, i32 %stream) {
entry:
  %buffer = alloca [256 x i16]
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %copy ]
  %byte_at = getelementptr i8, ptr %text, i32 %i
  %byte = load i8, ptr %byte_at
  %done = icmp eq i8 %byte, 0
  br i1 %done, label %write, label %copy

copy:
  %unit = zext i8 %byte to i16
  %unit_at = getelementptr [256 x i16], ptr %buffer, i32 0, i32 %i
  store i16 %unit, ptr %unit_at
  %next = add i32 %i, 1
  br label %loop

write:
  call void @$ghul_host_write(ptr %buffer, i32 %i, i32 %stream)
  ret void
}

; Answers whether a string built by $ghul_make_string holds exactly the given
; NUL-terminated ASCII text.
define i1 @string_is(ptr %string, ptr %text) {
entry:
  %length_field = getelementptr %string, ptr %string, i32 0, i32 1
  %length = load i32, ptr %length_field
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %same ]
  %byte_at = getelementptr i8, ptr %text, i32 %i
  %byte = load i8, ptr %byte_at
  %text_done = icmp eq i8 %byte, 0
  %string_done = icmp eq i32 %i, %length
  br i1 %text_done, label %end, label %compare

compare:
  br i1 %string_done, label %different, label %unit

unit:
  %unit_at = getelementptr %string, ptr %string, i32 0, i32 2, i32 %i
  %unit_value = load i16, ptr %unit_at
  %expected = zext i8 %byte to i16
  %equal = icmp eq i16 %unit_value, %expected
  br i1 %equal, label %same, label %different

same:
  %next = add i32 %i, 1
  br label %loop

end:
  ret i1 %string_done

different:
  ret i1 false
}

; A ubyte[] laid out as a descriptor, a length and the bytes, standing in
; for the program's own arrays.
%bytes = type { ptr, i32, [0 x i8] }

@bytes_descriptor = constant i8 0

define ptr @$ghul_make_bytes(ptr %data, i32 %length) {
  %count = sext i32 %length to i64
  %size = add i64 %count, 12
  %array = call ptr @$ghul_alloc_atomic(i64 %size)
  store ptr @bytes_descriptor, ptr %array
  %length_field = getelementptr %bytes, ptr %array, i32 0, i32 1
  store i32 %length, ptr %length_field
  %data_field = getelementptr %bytes, ptr %array, i32 0, i32 2
  call void @llvm.memcpy.p0.p0.i64(ptr %data_field, ptr %data, i64 %count, i1 false)
  ret ptr %array
}

; Writes an array built by $ghul_make_bytes, whose bytes are ASCII, to stream 1.
define void @write_bytes(ptr %array) {
entry:
  %length_field = getelementptr %bytes, ptr %array, i32 0, i32 1
  %length = load i32, ptr %length_field
  %wide = alloca [256 x i16]
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %copy ]
  %done = icmp eq i32 %i, %length
  br i1 %done, label %write, label %copy

copy:
  %byte_at = getelementptr %bytes, ptr %array, i32 0, i32 2, i32 %i
  %byte = load i8, ptr %byte_at
  %unit = zext i8 %byte to i16
  %unit_at = getelementptr [256 x i16], ptr %wide, i32 0, i32 %i
  store i16 %unit, ptr %unit_at
  %next = add i32 %i, 1
  br label %loop

write:
  call void @$ghul_host_write(ptr %wide, i32 %length, i32 1)
  ret void
}
