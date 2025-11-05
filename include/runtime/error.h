#ifndef _H_NEO_RUNTIME_ERROR_
#define _H_NEO_RUNTIME_ERROR_
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_error_constructor);
NEO_JS_CFUNCTION(neo_js_error_to_string);
void neo_initialize_js_error(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif