#ifndef _H_NEO_RUNTIME_ARRAY_
#define _H_NEO_RUNTIME_ARRAY_
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_array_from);
NEO_JS_CFUNCTION(neo_js_array_from_async);
NEO_JS_CFUNCTION(neo_js_array_is_array);
NEO_JS_CFUNCTION(neo_js_array_of);
NEO_JS_CFUNCTION(neo_js_array_constructor);
NEO_JS_CFUNCTION(neo_js_array_at);
NEO_JS_CFUNCTION(neo_js_array_concat);
NEO_JS_CFUNCTION(neo_js_array_copy_within);
NEO_JS_CFUNCTION(neo_js_array_to_string);
NEO_JS_CFUNCTION(neo_js_array_values);
NEO_JS_CFUNCTION(neo_js_array_push);
NEO_JS_CFUNCTION(neo_js_array_get_symbol_species);
void neo_initialize_js_array(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif