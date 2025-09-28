#ifndef _H_NEO_ENGINE_STD_WEAK_REF_
#define _H_NEO_ENGINE_STD_WEAK_REF_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif

NEO_JS_CFUNCTION(neo_js_weak_ref_constructor);
NEO_JS_CFUNCTION(neo_js_weak_ref_deref);
void neo_js_context_init_std_weak_ref(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif