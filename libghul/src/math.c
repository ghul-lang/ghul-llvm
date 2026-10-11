/* The floating-point functions the core library does not compute itself. */

#include "ghul.h"

#include <math.h>

double $ghul_host_math_sqrt(double value) { return sqrt(value); }
double $ghul_host_math_cbrt(double value) { return cbrt(value); }
double $ghul_host_math_floor(double value) { return floor(value); }
double $ghul_host_math_ceiling(double value) { return ceil(value); }
double $ghul_host_math_truncate(double value) { return trunc(value); }
double $ghul_host_math_sin(double angle) { return sin(angle); }
double $ghul_host_math_cos(double angle) { return cos(angle); }
double $ghul_host_math_tan(double angle) { return tan(angle); }
double $ghul_host_math_asin(double value) { return asin(value); }
double $ghul_host_math_acos(double value) { return acos(value); }
double $ghul_host_math_atan(double value) { return atan(value); }
double $ghul_host_math_atan2(double y, double x) { return atan2(y, x); }
double $ghul_host_math_sinh(double value) { return sinh(value); }
double $ghul_host_math_cosh(double value) { return cosh(value); }
double $ghul_host_math_tanh(double value) { return tanh(value); }
double $ghul_host_math_exp(double value) { return exp(value); }
double $ghul_host_math_log(double value) { return log(value); }
double $ghul_host_math_log10(double value) { return log10(value); }
double $ghul_host_math_log2(double value) { return log2(value); }
double $ghul_host_math_pow(double x, double y) { return pow(x, y); }
