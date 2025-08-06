#ifndef _H_NEO_ENGINE_STD_NUMBER_
#define _H_NEO_ENGINE_STD_NUMBER_
#include "engine/type.h"

#ifdef __cplusplus
extern "C" {
#endif

NEO_JS_CFUNCTION(neo_js_number_is_finite);
NEO_JS_CFUNCTION(neo_js_number_is_integer);
NEO_JS_CFUNCTION(neo_js_number_is_nan);
NEO_JS_CFUNCTION(neo_js_number_is_safe_integer);
NEO_JS_CFUNCTION(neo_js_number_constructor);
NEO_JS_CFUNCTION(neo_js_number_to_exponential);
NEO_JS_CFUNCTION(neo_js_number_to_fixed);
NEO_JS_CFUNCTION(neo_js_number_to_local_string);
NEO_JS_CFUNCTION(neo_js_number_to_precision);
NEO_JS_CFUNCTION(neo_js_number_to_string);
NEO_JS_CFUNCTION(neo_js_number_value_of);

#ifdef __cplusplus
}
#endif
#endif