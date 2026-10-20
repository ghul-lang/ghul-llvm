; Checks the host operations that answer numbers, exiting with 0 when every
; one answers as it should, and with the number of the first that does not.

declare double @$ghul_host_math_sqrt(double)
declare double @$ghul_host_math_pow(double, double)
declare double @$ghul_host_math_atan2(double, double)
declare double @$ghul_host_math_floor(double)
declare double @$ghul_host_monotonic_milliseconds()
declare void @$ghul_host_sleep(i32)
declare i32 @$ghul_host_random_u32()

@$ghul_abi_4 = external constant i32

define i32 @$ghul_entry() {
  %root = call double @$ghul_host_math_sqrt(double 16.0)
  %root_ok = fcmp oeq double %root, 4.0
  br i1 %root_ok, label %power, label %fail_1

power:
  %raised = call double @$ghul_host_math_pow(double 2.0, double 10.0)
  %raised_ok = fcmp oeq double %raised, 1024.0
  br i1 %raised_ok, label %angle, label %fail_2

angle:
  ; atan2(1, 1) is pi / 4.
  %quarter = call double @$ghul_host_math_atan2(double 1.0, double 1.0)
  %scaled = fmul double %quarter, 4.0
  %floored = call double @$ghul_host_math_floor(double %scaled)
  %angle_ok = fcmp oeq double %floored, 3.0
  br i1 %angle_ok, label %clock, label %fail_3

clock:
  %early = call double @$ghul_host_monotonic_milliseconds()
  %late = call double @$ghul_host_monotonic_milliseconds()
  %forward = fcmp ole double %early, %late
  %started = fcmp oge double %early, 0.0
  %clock_ok = and i1 %forward, %started
  br i1 %clock_ok, label %sleep, label %fail_4

sleep:
  %before = call double @$ghul_host_monotonic_milliseconds()
  call void @$ghul_host_sleep(i32 30)
  %after = call double @$ghul_host_monotonic_milliseconds()
  %waited = fsub double %after, %before
  %sleep_ok = fcmp oge double %waited, 30.0
  br i1 %sleep_ok, label %random, label %fail_7

random:
  ; Two draws of 32 bits agree once in four billion runs.
  %one = call i32 @$ghul_host_random_u32()
  %two = call i32 @$ghul_host_random_u32()
  %differ = icmp ne i32 %one, %two
  br i1 %differ, label %abi, label %fail_5

abi:
  %version = load i32, ptr @$ghul_abi_4
  %abi_ok = icmp eq i32 %version, 4
  br i1 %abi_ok, label %pass, label %fail_6

pass:
  ret i32 0

fail_1:
  ret i32 1
fail_2:
  ret i32 2
fail_3:
  ret i32 3
fail_4:
  ret i32 4
fail_5:
  ret i32 5
fail_6:
  ret i32 6
fail_7:
  ret i32 7
}
