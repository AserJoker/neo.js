#ifndef _H_NEO_ENGINE_STD_MATH_
#define _H_NEO_ENGINE_STD_MATH_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif

NEO_JS_CFUNCTION(neo_js_math_abs);
NEO_JS_CFUNCTION(neo_js_math_acos);
NEO_JS_CFUNCTION(neo_js_math_acosh);
NEO_JS_CFUNCTION(neo_js_math_asin);
NEO_JS_CFUNCTION(neo_js_math_asinh);
NEO_JS_CFUNCTION(neo_js_math_atan);
NEO_JS_CFUNCTION(neo_js_math_atan2);
NEO_JS_CFUNCTION(neo_js_math_atanh);
NEO_JS_CFUNCTION(neo_js_math_cbrt);
NEO_JS_CFUNCTION(neo_js_math_ceil);
NEO_JS_CFUNCTION(neo_js_math_clz32);
NEO_JS_CFUNCTION(neo_js_math_cos);
NEO_JS_CFUNCTION(neo_js_math_cosh);
NEO_JS_CFUNCTION(neo_js_math_exp);
NEO_JS_CFUNCTION(neo_js_math_expm1);
NEO_JS_CFUNCTION(neo_js_math_floor);
NEO_JS_CFUNCTION(neo_js_math_fround);
NEO_JS_CFUNCTION(neo_js_math_hypot);
NEO_JS_CFUNCTION(neo_js_math_imul);
NEO_JS_CFUNCTION(neo_js_math_log);
NEO_JS_CFUNCTION(neo_js_math_log1p);
NEO_JS_CFUNCTION(neo_js_math_log2);
NEO_JS_CFUNCTION(neo_js_math_log10);
NEO_JS_CFUNCTION(neo_js_math_max);
NEO_JS_CFUNCTION(neo_js_math_min);
NEO_JS_CFUNCTION(neo_js_math_pow);
NEO_JS_CFUNCTION(neo_js_math_random);
NEO_JS_CFUNCTION(neo_js_math_round);
NEO_JS_CFUNCTION(neo_js_math_sign);
NEO_JS_CFUNCTION(neo_js_math_sin);
NEO_JS_CFUNCTION(neo_js_math_sinh);
NEO_JS_CFUNCTION(neo_js_math_sqrt);
NEO_JS_CFUNCTION(neo_js_math_tan);
NEO_JS_CFUNCTION(neo_js_math_tanh);
NEO_JS_CFUNCTION(neo_js_math_trunc);
void neo_js_context_init_std_math(neo_js_context_t ctx);

#ifdef __cplusplus
}
#endif
#endif