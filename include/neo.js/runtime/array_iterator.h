#ifndef _H_NEO_RUNTIME_ARRAY_ITERATOR_
#define _H_NEO_RUNTIME_ARRAY_ITERATOR_
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_array_iterator_next);
void neo_initialize_js_array_iterator(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif