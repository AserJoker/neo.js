#ifndef _H_NEO_RUNTIME_FUNCTION_
#define _H_NEO_RUNTIME_FUNCTION_
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_function_constructor);
NEO_JS_CFUNCTION(neo_js_function_to_string);
void neo_initialize_js_function(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif