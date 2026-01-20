#ifndef _H_NEO_RUNTIME_BIGINT_
#define _H_NEO_RUNTIME_BIGINT_
#include "neojs/engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_bigint_constructor);
NEO_JS_CFUNCTION(neo_js_bigint_as_int_n);
NEO_JS_CFUNCTION(neo_js_bigint_as_uint_n);
NEO_JS_CFUNCTION(neo_js_bigint_to_locale_string);
NEO_JS_CFUNCTION(neo_js_bigint_to_string);
NEO_JS_CFUNCTION(neo_js_bigint_value_of);
void neo_initialize_js_bigint(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif