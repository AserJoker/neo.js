#ifndef _H_NEO_RUNTIME_FUNCTION_
#define _H_NEO_RUNTIME_FUNCTION_
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
extern neo_js_variable_t neo_js_function_prototype;
extern neo_js_variable_t neo_js_function_class;
void neo_initialize_js_function(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif