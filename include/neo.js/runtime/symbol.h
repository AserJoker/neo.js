#ifndef _H_NEO_RUNTIME_SYMBOL_
#define _H_NEO_RUNTIME_SYMBOL_
#include "neo.js/engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_symbol_for);
NEO_JS_CFUNCTION(neo_js_symbol_key_for);
NEO_JS_CFUNCTION(neo_js_symbol_constructor);
NEO_JS_CFUNCTION(neo_js_symbol_value_of);
NEO_JS_CFUNCTION(neo_js_symbol_to_string);
NEO_JS_CFUNCTION(neo_js_symbol_symbol_to_primitive);
NEO_JS_CFUNCTION(neo_js_symbol_get_description);
void neo_initialize_js_symbol(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif