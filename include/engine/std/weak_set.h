#ifndef _H_NEO_ENGINE_STD_WEAK_SET_
#define _H_NEO_ENGINE_STD_WEAK_SET_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif

NEO_JS_CFUNCTION(neo_js_weak_set_constructor);
NEO_JS_CFUNCTION(neo_js_weak_set_add);
NEO_JS_CFUNCTION(neo_js_weak_set_has);
NEO_JS_CFUNCTION(neo_js_weak_set_delete);
void neo_js_context_init_std_weak_set(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif