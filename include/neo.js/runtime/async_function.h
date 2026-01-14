#ifndef _H_NEO_RUNTIME_ASYNC_FUNCTION_
#define _H_NEO_RUNTIME_ASYNC_FUNCTION_
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_async_function_constructor);
void neo_initialize_js_async_function(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif