; Writes, reads, lists and deletes files and directories under files-test/
; in the working directory, writing one line for each answer: a kind, a
; code, a count, a name or a file's content. tests/smoke.sh checks the lines.
;
; Its helpers are named test_*: a definition named like a C library function
; (write, read) would replace that function for libghul too.

declare void @say(ptr, i32)
declare void @write_string(ptr, i32)
declare void @write_bytes(ptr)
declare ptr @$ghul_host_bigint_from_int(i32)
declare ptr @$ghul_host_bigint_to_string(ptr)

declare ptr @$ghul_host_file_current_directory()
declare i32 @$ghul_host_file_kind(ptr, i32)
declare ptr @$ghul_host_file_read(ptr, i32)
declare i32 @$ghul_host_file_last_error()
declare i32 @$ghul_host_file_write(ptr, i32, ptr, i32, i1)
declare i32 @$ghul_host_file_delete(ptr, i32)
declare i32 @$ghul_host_directory_create(ptr, i32)
declare i32 @$ghul_host_file_rename(ptr, i32, ptr, i32)
declare i32 @$ghul_host_directory_set_current(ptr, i32)
declare i32 @$ghul_host_directory_delete(ptr, i32, i1)
declare i32 @$ghul_host_directory_entries(ptr, i32, i1)
declare ptr @$ghul_host_directory_entry(i32)

@newline = private constant [2 x i8] c"\0A\00"
@null_line = private constant [6 x i8] c"null\0A\00"

; Copies NUL-terminated ASCII into `buffer` as UTF-16; answers the length.
define i32 @test_widen(ptr %text, ptr %buffer) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %copy ]
  %byte_at = getelementptr i8, ptr %text, i32 %i
  %byte = load i8, ptr %byte_at
  %done = icmp eq i8 %byte, 0
  br i1 %done, label %end, label %copy

copy:
  %unit = zext i8 %byte to i16
  %unit_at = getelementptr i16, ptr %buffer, i32 %i
  store i16 %unit, ptr %unit_at
  %next = add i32 %i, 1
  br label %loop

end:
  ret i32 %i
}

define i32 @test_length_of(ptr %text) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]
  %byte_at = getelementptr i8, ptr %text, i32 %i
  %byte = load i8, ptr %byte_at
  %next = add i32 %i, 1
  %more = icmp ne i8 %byte, 0
  br i1 %more, label %loop, label %end

end:
  ret i32 %i
}

define void @test_show_int(i32 %value) {
  %big = call ptr @$ghul_host_bigint_from_int(i32 %value)
  %text = call ptr @$ghul_host_bigint_to_string(ptr %big)
  call void @write_string(ptr %text, i32 1)
  call void @say(ptr @newline, i32 1)
  ret void
}

define void @test_kind(ptr %path) {
  %buffer = alloca [256 x i16]
  %length = call i32 @test_widen(ptr %path, ptr %buffer)
  %kind = call i32 @$ghul_host_file_kind(ptr %buffer, i32 %length)
  call void @test_show_int(i32 %kind)
  ret void
}

define void @test_write(ptr %path, ptr %text, i1 %append) {
  %buffer = alloca [256 x i16]
  %length = call i32 @test_widen(ptr %path, ptr %buffer)
  %count = call i32 @test_length_of(ptr %text)
  %code = call i32 @$ghul_host_file_write(ptr %buffer, i32 %length, ptr %text, i32 %count, i1 %append)
  call void @test_show_int(i32 %code)
  ret void
}

; The file's content on a line, or "null" and the code on the next.
define void @test_read(ptr %path) {
entry:
  %buffer = alloca [256 x i16]
  %length = call i32 @test_widen(ptr %path, ptr %buffer)
  %content = call ptr @$ghul_host_file_read(ptr %buffer, i32 %length)
  %absent = icmp eq ptr %content, null
  br i1 %absent, label %failed, label %read

read:
  call void @write_bytes(ptr %content)
  call void @say(ptr @newline, i32 1)
  ret void

failed:
  call void @say(ptr @null_line, i32 1)
  %code = call i32 @$ghul_host_file_last_error()
  call void @test_show_int(i32 %code)
  ret void
}

define void @test_delete(ptr %path) {
  %buffer = alloca [256 x i16]
  %length = call i32 @test_widen(ptr %path, ptr %buffer)
  %code = call i32 @$ghul_host_file_delete(ptr %buffer, i32 %length)
  call void @test_show_int(i32 %code)
  ret void
}

define void @test_rename(ptr %from, ptr %to) {
  %from_buffer = alloca [256 x i16]
  %from_length = call i32 @test_widen(ptr %from, ptr %from_buffer)
  %to_buffer = alloca [256 x i16]
  %to_length = call i32 @test_widen(ptr %to, ptr %to_buffer)
  %code = call i32 @$ghul_host_file_rename(ptr %from_buffer, i32 %from_length, ptr %to_buffer, i32 %to_length)
  call void @test_show_int(i32 %code)
  ret void
}

define void @test_set_current(ptr %path) {
  %buffer = alloca [256 x i16]
  %length = call i32 @test_widen(ptr %path, ptr %buffer)
  %code = call i32 @$ghul_host_directory_set_current(ptr %buffer, i32 %length)
  call void @test_show_int(i32 %code)
  ret void
}

define void @test_make_directory(ptr %path) {
  %buffer = alloca [256 x i16]
  %length = call i32 @test_widen(ptr %path, ptr %buffer)
  %code = call i32 @$ghul_host_directory_create(ptr %buffer, i32 %length)
  call void @test_show_int(i32 %code)
  ret void
}

define void @test_remove_directory(ptr %path, i1 %recursive) {
  %buffer = alloca [256 x i16]
  %length = call i32 @test_widen(ptr %path, ptr %buffer)
  %code = call i32 @$ghul_host_directory_delete(ptr %buffer, i32 %length, i1 %recursive)
  call void @test_show_int(i32 %code)
  ret void
}

; The count on a line, then each name on a line.
define void @test_list(ptr %path, i1 %directories) {
entry:
  %buffer = alloca [256 x i16]
  %length = call i32 @test_widen(ptr %path, ptr %buffer)
  %count = call i32 @$ghul_host_directory_entries(ptr %buffer, i32 %length, i1 %directories)
  call void @test_show_int(i32 %count)
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %name ]
  %more = icmp slt i32 %i, %count
  br i1 %more, label %name, label %end

name:
  %entry_name = call ptr @$ghul_host_directory_entry(i32 %i)
  call void @write_string(ptr %entry_name, i32 1)
  call void @say(ptr @newline, i32 1)
  %next = add i32 %i, 1
  br label %loop

end:
  ret void
}

@root = private constant [11 x i8] c"files-test\00"
@a = private constant [17 x i8] c"files-test/a.txt\00"
@missing = private constant [19 x i8] c"files-test/missing\00"
@deep_missing = private constant [21 x i8] c"files-test/nowhere/x\00"
@nested = private constant [17 x i8] c"files-test/p/q/r\00"
@p = private constant [13 x i8] c"files-test/p\00"
@b = private constant [19 x i8] c"files-test/p/b.txt\00"
@under_file = private constant [19 x i8] c"files-test/a.txt/z\00"
@hello = private constant [6 x i8] c"hello\00"
@world = private constant [7 x i8] c" world\00"
@replaced = private constant [9 x i8] c"replaced\00"
@inner = private constant [6 x i8] c"inner\00"
@moved = private constant [17 x i8] c"files-test/m.txt\00"
@moved_directory = private constant [13 x i8] c"files-test/q\00"
@up = private constant [3 x i8] c"..\00"

define i32 @$ghul_entry() {
  %current = call ptr @$ghul_host_file_current_directory()
  call void @write_string(ptr %current, i32 1)
  call void @say(ptr @newline, i32 1)

  ; kinds: a directory, then nothing
  call void @test_kind(ptr @root)
  call void @test_kind(ptr @a)

  ; write, append, read back, replace
  call void @test_write(ptr @a, ptr @hello, i1 false)
  call void @test_write(ptr @a, ptr @world, i1 true)
  call void @test_kind(ptr @a)
  call void @test_read(ptr @a)
  call void @test_write(ptr @a, ptr @replaced, i1 false)
  call void @test_read(ptr @a)

  ; failures: a missing file, a missing directory, a directory as a file
  call void @test_read(ptr @missing)
  call void @test_read(ptr @deep_missing)
  call void @test_write(ptr @deep_missing, ptr @hello, i1 false)
  call void @test_read(ptr @root)

  ; directories: created along the way, twice, and under a file
  call void @test_make_directory(ptr @nested)
  call void @test_kind(ptr @nested)
  call void @test_make_directory(ptr @nested)
  call void @test_make_directory(ptr @under_file)
  call void @test_write(ptr @b, ptr @inner, i1 false)

  ; listing files and directories, and a directory that is not there
  call void @test_list(ptr @root, i1 false)
  call void @test_list(ptr @root, i1 true)
  call void @test_list(ptr @missing, i1 false)

  ; deleting: not empty, recursively, then not there
  call void @test_remove_directory(ptr @p, i1 false)
  call void @test_remove_directory(ptr @p, i1 true)
  call void @test_kind(ptr @p)
  call void @test_remove_directory(ptr @p, i1 false)

  ; moving: a file, onto a file that is there, a directory, and from nothing
  call void @test_write(ptr @a, ptr @hello, i1 false)
  call void @test_rename(ptr @a, ptr @moved)
  call void @test_read(ptr @moved)
  call void @test_write(ptr @a, ptr @hello, i1 false)
  call void @test_rename(ptr @a, ptr @moved)
  call void @test_make_directory(ptr @p)
  call void @test_rename(ptr @p, ptr @moved_directory)
  call void @test_kind(ptr @moved_directory)
  call void @test_rename(ptr @missing, ptr @p)
  call void @test_remove_directory(ptr @moved_directory, i1 false)
  call void @test_delete(ptr @moved)

  ; deleting files: there, not there, and in a directory that is not there
  call void @test_delete(ptr @a)
  call void @test_kind(ptr @a)
  call void @test_delete(ptr @a)
  call void @test_delete(ptr @deep_missing)

  ; the current directory: into the test's own, a missing one, and back
  call void @test_set_current(ptr @root)
  call void @test_set_current(ptr @missing)
  call void @test_set_current(ptr @up)

  ret i32 0
}
