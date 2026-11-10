; Lists the directories, then the files, directly in listing-test/ in the
; working directory: the count on a line, then each name on a line.
; tests/smoke.sh lays out the directory, with links among its entries.

declare void @say(ptr, i32)
declare void @write_string(ptr, i32)
declare ptr @$ghul_host_bigint_from_int(i32)
declare ptr @$ghul_host_bigint_to_string(ptr)
declare i32 @$ghul_host_directory_entries(ptr, i32, i1)
declare ptr @$ghul_host_directory_entry(i32)

@newline = private constant [2 x i8] c"\0A\00"
@root = private constant [13 x i8] c"listing-test\00"

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

define void @test_show_int(i32 %value) {
  %big = call ptr @$ghul_host_bigint_from_int(i32 %value)
  %text = call ptr @$ghul_host_bigint_to_string(ptr %big)
  call void @write_string(ptr %text, i32 1)
  call void @say(ptr @newline, i32 1)
  ret void
}

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

define i32 @$ghul_entry() {
  call void @test_list(ptr @root, i1 true)
  call void @test_list(ptr @root, i1 false)
  ret i32 0
}
