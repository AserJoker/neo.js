#ifndef _H_NEO_ENGINE_STD_MAP_
#define _H_NEO_ENGINE_STD_MAP_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif

NEO_JS_CFUNCTION(neo_js_map_group_by);
NEO_JS_CFUNCTION(neo_js_map_constructor);
NEO_JS_CFUNCTION(neo_js_map_clear);
NEO_JS_CFUNCTION(neo_js_map_delete);
NEO_JS_CFUNCTION(neo_js_map_entries);
NEO_JS_CFUNCTION(neo_js_map_for_each);
NEO_JS_CFUNCTION(neo_js_map_get);
NEO_JS_CFUNCTION(neo_js_map_has);
NEO_JS_CFUNCTION(neo_js_map_keys);
NEO_JS_CFUNCTION(neo_js_map_set);
NEO_JS_CFUNCTION(neo_js_map_values);
NEO_JS_CFUNCTION(neo_js_map_species);

#ifdef __cplusplus
}
#endif
#endif