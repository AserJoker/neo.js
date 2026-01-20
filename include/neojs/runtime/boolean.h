#ifndef _H_NEO_RUNTIME_BOOLEAN_
#define _H_NEO_RUNTIME_BOOLEAN_
#include "neojs/engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_boolean_constructor);
NEO_JS_CFUNCTION(neo_js_boolean_to_string);
NEO_JS_CFUNCTION(neo_js_boolean_value_of);
void neo_initialize_js_boolean(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif