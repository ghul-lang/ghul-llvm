; An exception that passes a catch that does not handle it and a finally
; that runs and throws it on, and that nothing else catches: it is still
; reported as unhandled, with status 134, rather than ending the process
; some other way.

declare void @say(ptr, i32)
declare void @$ghul_throw(ptr)
declare ptr @$ghul_caught(ptr)
declare i32 @$ghul_personality(...)

@exception = constant i32 0

@passed_catch = private constant [13 x i8] c"catch: pass\0A\00"
@finally = private constant [9 x i8] c"finally\0A\00"
@description = private constant [44 x i8] c"Unhandled exception. Test.Escaped: got out\0A\00"

define void @$ghul_report_unhandled(ptr %exception) {
  call void @say(ptr @description, i32 2)
  ret void
}

define void @thrower() {
  call void @$ghul_throw(ptr @exception)
  unreachable
}

define void @passes(ptr %unused) personality ptr @$ghul_personality {
entry:
  invoke void @thrower() to label %done unwind label %pad

done:
  ret void

pad:
  %landed = landingpad { ptr, i32 } catch ptr null
  %header = extractvalue { ptr, i32 } %landed, 0
  %caught = call ptr @$ghul_caught(ptr %header)
  call void @say(ptr @passed_catch, i32 1)
  call void @$ghul_throw(ptr %caught)
  unreachable
}

define i32 @$ghul_entry() personality ptr @$ghul_personality {
entry:
  invoke void @passes(ptr null) to label %done unwind label %cleanup

done:
  ret i32 0

cleanup:
  %landed = landingpad { ptr, i32 } cleanup
  call void @say(ptr @finally, i32 1)
  %header = extractvalue { ptr, i32 } %landed, 0
  %caught = call ptr @$ghul_caught(ptr %header)
  call void @$ghul_throw(ptr %caught)
  unreachable
}
