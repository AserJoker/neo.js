#ifndef _H_NEO_ENGINE_STD_SET_
#define _H_NEO_ENGINE_STD_SET_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_set_constructor);
NEO_JS_CFUNCTION(neo_js_set_species);
NEO_JS_CFUNCTION(neo_js_set_add);
NEO_JS_CFUNCTION(neo_js_set_clear);
NEO_JS_CFUNCTION(neo_js_set_delete);
NEO_JS_CFUNCTION(neo_js_set_difference);
NEO_JS_CFUNCTION(neo_js_set_entries);
NEO_JS_CFUNCTION(neo_js_set_for_each);
NEO_JS_CFUNCTION(neo_js_set_has);
NEO_JS_CFUNCTION(neo_js_set_intersection);
NEO_JS_CFUNCTION(neo_js_set_is_disjoin_form);
NEO_JS_CFUNCTION(neo_js_set_is_subset_of);
NEO_JS_CFUNCTION(neo_js_set_is_superset_of);
NEO_JS_CFUNCTION(neo_js_set_keys);
NEO_JS_CFUNCTION(neo_js_set_symmetric_difference);
NEO_JS_CFUNCTION(neo_js_set_union);
NEO_JS_CFUNCTION(neo_js_set_values);
NEO_JS_CFUNCTION(neo_js_set_iterator);
void neo_js_context_init_std_set(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif