#ifndef _H_NEO_RUNTIME_OBJECT_
#define _H_NEO_RUNTIME_OBJECT_
#include "neo.js/engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_object_constructor);
NEO_JS_CFUNCTION(neo_js_object_value_of);
NEO_JS_CFUNCTION(neo_js_object_to_string);
void neo_initialize_js_object(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif