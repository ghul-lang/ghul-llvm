; Reads through null, as the backend emits them: one guarded by a test
; marked !make.implicit, which LLVM folds into the read at -O1 and above,
; whose null branch throws to a landing pad in the same function; and one
; nothing tests, which faults, and is caught by its caller after a finally
; between them has run.
;
; Writes what each handler sees, and exits with 0.

declare void @say(ptr, i32)
declare void @$ghul_throw(ptr)
declare ptr @$ghul_caught(ptr)
declare i32 @$ghul_personality(...)

%exception = type { ptr, i32 }

@null_reference = constant %exception { ptr null, i32 7 }

; Read rather than written as a constant, so the optimiser cannot see the
; reads are through null.
@nothing = global ptr null
@something = global { ptr, i32 } { ptr null, i32 42 }

@caught_folded = private constant [15 x i8] c"caught folded\0A\00"
@read_value = private constant [12 x i8] c"read value\0A\00"
@finally = private constant [9 x i8] c"finally\0A\00"
@caught_untested = private constant [17 x i8] c"caught untested\0A\00"

define ptr @$ghul_make_null_reference() {
  ret ptr @null_reference
}

; try p.field catch: -1
define i32 @folded(ptr %p) noinline personality ptr @$ghul_personality {
entry:
  %null = icmp eq ptr %p, null
  br i1 %null, label %is_null, label %not_null, !make.implicit !0

is_null:
  %thrown = call ptr @$ghul_make_null_reference()
  invoke void @$ghul_throw(ptr %thrown) to label %impossible unwind label %pad

impossible:
  unreachable

not_null:
  %at = getelementptr i8, ptr %p, i64 8
  %value = load i32, ptr %at
  ret i32 %value

pad:
  %landed = landingpad { ptr, i32 } catch ptr null
  %header = extractvalue { ptr, i32 } %landed, 0
  %caught = call ptr @$ghul_caught(ptr %header)
  %kind_at = getelementptr %exception, ptr %caught, i32 0, i32 1
  %kind = load i32, ptr %kind_at
  %right = icmp eq i32 %kind, 7
  br i1 %right, label %handled, label %wrong

handled:
  call void @say(ptr @caught_folded, i32 1)
  ret i32 -1

wrong:
  ret i32 -2
}

; Volatile, and followed by a call that might throw, so the optimiser neither
; drops the read nor takes the landing pads around the call to it out.
define i32 @untested(ptr %p) noinline {
  %at = getelementptr i8, ptr %p, i64 8
  %value = load volatile i32, ptr %at
  call void @say(ptr @read_value, i32 1)
  ret i32 %value
}

; try untested(p) finally say(...)
define i32 @with_finally(ptr %p) noinline personality ptr @$ghul_personality {
entry:
  %value = invoke i32 @untested(ptr %p) to label %normal unwind label %cleanup

normal:
  ret i32 %value

cleanup:
  %landed = landingpad { ptr, i32 } cleanup
  call void @say(ptr @finally, i32 1)
  %header = extractvalue { ptr, i32 } %landed, 0
  %caught = call ptr @$ghul_caught(ptr %header)
  call void @$ghul_throw(ptr %caught)
  unreachable
}

; try with_finally(p) catch: true when what was caught is the null reference
define i1 @caller(ptr %p) noinline personality ptr @$ghul_personality {
entry:
  %value = invoke i32 @with_finally(ptr %p) to label %nothing unwind label %pad

nothing:
  ret i1 false

pad:
  %landed = landingpad { ptr, i32 } catch ptr null
  %header = extractvalue { ptr, i32 } %landed, 0
  %caught = call ptr @$ghul_caught(ptr %header)
  call void @say(ptr @caught_untested, i32 1)
  %same = icmp eq ptr %caught, @null_reference
  ret i1 %same
}

define i32 @$ghul_entry() {
  %null = load ptr, ptr @nothing

  %value = call i32 @folded(ptr @something)
  %read = icmp eq i32 %value, 42
  br i1 %read, label %present, label %failed

present:
  call void @say(ptr @read_value, i32 1)
  %folded = call i32 @folded(ptr %null)
  %caught_folded = icmp eq i32 %folded, -1
  br i1 %caught_folded, label %next, label %failed

next:
  %caught = call i1 @caller(ptr %null)
  %status = select i1 %caught, i32 0, i32 1
  ret i32 %status

failed:
  ret i32 1
}

define void @$ghul_report_unhandled(ptr %exception) {
  ret void
}

!0 = !{}
