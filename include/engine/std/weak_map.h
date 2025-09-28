#ifndef _H_NEO_ENGINE_STD_WEAK_MAP_
#define _H_NEO_ENGINE_STD_WEAK_MAP_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif

NEO_JS_CFUNCTION(neo_js_weak_map_constructor);
NEO_JS_CFUNCTION(neo_js_weak_map_get);
NEO_JS_CFUNCTION(neo_js_weak_map_set);
NEO_JS_CFUNCTION(neo_js_weak_map_has);
NEO_JS_CFUNCTION(neo_js_weak_map_delete);
void neo_js_context_init_std_weak_map(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif