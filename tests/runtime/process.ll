; Writes each command-line argument on a line, then each environment
; variable's name on a line, then the value of GHUL_TEST or "unset".

declare void @say(ptr, i32)
declare void @write_string(ptr, i32)
declare i32 @ghul_host_argument_count()
declare ptr @ghul_host_argument(i32)
declare i32 @ghul_host_environment_count()
declare ptr @ghul_host_environment_name(i32)
declare ptr @ghul_host_environment_lookup(ptr, i32)

@newline = private constant [2 x i8] c"\0A\00"
@unset = private constant [7 x i8] c"unset\0A\00"
@name = private constant [9 x i16] [i16 71, i16 72, i16 85, i16 76, i16 95, i16 84, i16 69, i16 83, i16 84]

define i32 @ghul_entry() {
entry:
  %arguments = call i32 @ghul_host_argument_count()
  br label %argument_loop

argument_loop:
  %i = phi i32 [ 0, %entry ], [ %i_next, %argument ]
  %more_arguments = icmp slt i32 %i, %arguments
  br i1 %more_arguments, label %argument, label %environment

argument:
  %value = call ptr @ghul_host_argument(i32 %i)
  call void @write_string(ptr %value, i32 1)
  call void @say(ptr @newline, i32 1)
  %i_next = add i32 %i, 1
  br label %argument_loop

environment:
  %variables = call i32 @ghul_host_environment_count()
  br label %variable_loop

variable_loop:
  %j = phi i32 [ 0, %environment ], [ %j_next, %variable ]
  %more_variables = icmp slt i32 %j, %variables
  br i1 %more_variables, label %variable, label %lookup

variable:
  %variable_name = call ptr @ghul_host_environment_name(i32 %j)
  call void @write_string(ptr %variable_name, i32 1)
  call void @say(ptr @newline, i32 1)
  %j_next = add i32 %j, 1
  br label %variable_loop

lookup:
  %found = call ptr @ghul_host_environment_lookup(ptr @name, i32 9)
  %absent = icmp eq ptr %found, null
  br i1 %absent, label %not_set, label %set

not_set:
  call void @say(ptr @unset, i32 1)
  ret i32 0

set:
  call void @write_string(ptr %found, i32 1)
  call void @say(ptr @newline, i32 1)
  ret i32 0
}
