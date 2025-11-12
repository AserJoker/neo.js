#ifndef _H_NEO_RUNTIME_ITERATOR_
#define _H_NEO_RUNTIME_ITERATOR_
#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_iterator_constructor);
NEO_JS_CFUNCTION(neo_js_iterator_iterator);
void neo_initialize_js_iterator(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif