#ifndef _H_NEO_RUNTIME_OBJECT_
#define _H_NEO_RUNTIME_OBJECT_
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
extern neo_js_variable_t neo_js_object_prototype;
extern neo_js_variable_t neo_js_object_class;
void neo_initialize_js_object(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif