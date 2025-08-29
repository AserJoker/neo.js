#ifndef _H_NEO_ENGINE_STD_REFLECT_
#define _H_NEO_ENGINE_STD_REFLECT_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_reflect_apply);
NEO_JS_CFUNCTION(neo_js_reflect_construct);
NEO_JS_CFUNCTION(neo_js_reflect_define_property);
NEO_JS_CFUNCTION(neo_js_reflect_delete_property);
NEO_JS_CFUNCTION(neo_js_reflect_get);
NEO_JS_CFUNCTION(neo_js_reflect_get_own_property_descriptor);
NEO_JS_CFUNCTION(neo_js_reflect_get_prototype_of);
NEO_JS_CFUNCTION(neo_js_reflect_has);
NEO_JS_CFUNCTION(neo_js_reflect_is_extensible);
NEO_JS_CFUNCTION(neo_js_reflect_own_keys);
NEO_JS_CFUNCTION(neo_js_reflect_prevent_extensions);
NEO_JS_CFUNCTION(neo_js_reflect_set);
NEO_JS_CFUNCTION(neo_js_reflect_set_prototype_of);
void neo_js_context_init_std_reflect(neo_js_context_t ctx);
#ifdef __cplusplus
}
#endif
#endif