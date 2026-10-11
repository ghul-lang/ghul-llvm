; Throws and catches through landing pads as the backend emits them: a catch
; that handles one kind of exception and throws any other on, a finally that
; runs on both the normal and the unwinding path, and two protected regions
; in one function, the inner throwing on to the outer. The exceptions are
; thrown from deep in frames with no landing pads of their own.
;
; Writes what each handler sees, and exits with 0.

declare void @say(ptr, i32)
declare void @$ghul_throw(ptr)
declare ptr @$ghul_caught(ptr)
declare i32 @$ghul_personality(...)

; A stand-in exception: a descriptor and a kind.
%exception = type { ptr, i32 }

@red = constant %exception { ptr null, i32 1 }
@blue = constant %exception { ptr null, i32 2 }

@caught_red = private constant [12 x i8] c"caught red\0A\00"
@finally_normal = private constant [18 x i8] c"finally (normal)\0A\00"
@finally_unwinding = private constant [21 x i8] c"finally (unwinding)\0A\00"
@inner_saw = private constant [14 x i8] c"inner saw it\0A\00"
@outer_caught = private constant [19 x i8] c"outer caught blue\0A\00"

; Throws `exception` from `depth` frames down.
define void @deep(i32 %depth, ptr %exception) {
  %bottom = icmp eq i32 %depth, 0
  br i1 %bottom, label %throw, label %down

throw:
  call void @$ghul_throw(ptr %exception)
  unreachable

down:
  %next = sub i32 %depth, 1
  call void @deep(i32 %next, ptr %exception)
  ret void
}

; try deep() catch red: handles red, throws anything else on.
define void @catch_red(ptr %exception) personality ptr @$ghul_personality {
entry:
  invoke void @deep(i32 50, ptr %exception) to label %done unwind label %pad

done:
  ret void

pad:
  %landed = landingpad { ptr, i32 } catch ptr null
  %header = extractvalue { ptr, i32 } %landed, 0
  %caught = call ptr @$ghul_caught(ptr %header)
  %kind_at = getelementptr %exception, ptr %caught, i32 0, i32 1
  %kind = load i32, ptr %kind_at
  %is_red = icmp eq i32 %kind, 1
  br i1 %is_red, label %handled, label %throw_on

handled:
  call void @say(ptr @caught_red, i32 1)
  ret void

throw_on:
  call void @$ghul_throw(ptr %caught)
  unreachable
}

; try catch_red() finally say(...)
define void @with_finally(ptr %exception) personality ptr @$ghul_personality {
entry:
  invoke void @catch_red(ptr %exception) to label %normal unwind label %cleanup

normal:
  call void @say(ptr @finally_normal, i32 1)
  ret void

cleanup:
  %landed = landingpad { ptr, i32 } cleanup
  call void @say(ptr @finally_unwinding, i32 1)
  %header = extractvalue { ptr, i32 } %landed, 0
  %caught = call ptr @$ghul_caught(ptr %header)
  call void @$ghul_throw(ptr %caught)
  unreachable
}

; try (try with_finally() catch anything: throw on) catch anything
define i1 @nested(ptr %exception) personality ptr @$ghul_personality {
entry:
  invoke void @with_finally(ptr %exception) to label %nothing unwind label %inner

nothing:
  ret i1 false

inner:
  %inner_landed = landingpad { ptr, i32 } catch ptr null
  %inner_header = extractvalue { ptr, i32 } %inner_landed, 0
  %inner_caught = call ptr @$ghul_caught(ptr %inner_header)
  call void @say(ptr @inner_saw, i32 1)
  invoke void @$ghul_throw(ptr %inner_caught) to label %impossible unwind label %outer

impossible:
  unreachable

outer:
  %outer_landed = landingpad { ptr, i32 } catch ptr null
  %outer_header = extractvalue { ptr, i32 } %outer_landed, 0
  %outer_caught = call ptr @$ghul_caught(ptr %outer_header)
  call void @say(ptr @outer_caught, i32 1)
  %same = icmp eq ptr %outer_caught, %exception
  ret i1 %same
}

define i32 @$ghul_entry() {
  ; red is caught by catch_red; the finally runs on the normal path.
  call void @with_finally(ptr @red)

  ; blue passes catch_red and the finally, and reaches both regions of nested.
  %same = call i1 @nested(ptr @blue)
  %status = select i1 %same, i32 0, i32 1
  ret i32 %status
}

@escaped = private constant [17 x i8] c"escaped: a bug!\0A\00"

define void @$ghul_report_unhandled(ptr %exception) {
  call void @say(ptr @escaped, i32 2)
  ret void
}
