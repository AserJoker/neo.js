#ifndef _H_NEO_RUNTIME_ARRAY_
#define _H_NEO_RUNTIME_ARRAY_
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_array_constructor);
NEO_JS_CFUNCTION(neo_js_array_to_string);
NEO_JS_CFUNCTION(neo_js_array_values);
NEO_JS_CFUNCTION(neo_js_array_push);
void neo_initialize_js_array(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif