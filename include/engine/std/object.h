#ifndef _H_NEO_ENGINE_STD_OBJECT_
#define _H_NEO_ENGINE_STD_OBJECT_
#include "engine/basetype/object.h"
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif
NEO_JS_CFUNCTION(neo_js_object_assign);
NEO_JS_CFUNCTION(neo_js_object_create);
NEO_JS_CFUNCTION(neo_js_object_define_properties);
NEO_JS_CFUNCTION(neo_js_object_define_property);
NEO_JS_CFUNCTION(neo_js_object_entries);
NEO_JS_CFUNCTION(neo_js_object_freeze);
NEO_JS_CFUNCTION(neo_js_object_from_entries);
NEO_JS_CFUNCTION(neo_js_object_get_own_property_descriptor);
NEO_JS_CFUNCTION(neo_js_object_get_own_property_descriptors);
NEO_JS_CFUNCTION(neo_js_object_get_own_property_names);
NEO_JS_CFUNCTION(neo_js_object_get_own_property_symbols);
NEO_JS_CFUNCTION(neo_js_object_get_prototype_of);
NEO_JS_CFUNCTION(neo_js_object_group_by);
NEO_JS_CFUNCTION(neo_js_object_has_own);
NEO_JS_CFUNCTION(neo_js_object_is);
NEO_JS_CFUNCTION(neo_js_object_is_extensible);
NEO_JS_CFUNCTION(neo_js_object_is_frozen);
NEO_JS_CFUNCTION(neo_js_object_is_sealed);
NEO_JS_CFUNCTION(neo_js_object_keys);
NEO_JS_CFUNCTION(neo_js_object_prevent_extensions);
NEO_JS_CFUNCTION(neo_js_object_seal);
NEO_JS_CFUNCTION(neo_js_object_set_prototype_of);
NEO_JS_CFUNCTION(neo_js_object_values);

NEO_JS_CFUNCTION(neo_js_object_constructor);
NEO_JS_CFUNCTION(neo_js_object_has_own_property);
NEO_JS_CFUNCTION(neo_js_object_is_prototype_of);
NEO_JS_CFUNCTION(neo_js_object_property_is_enumerable);
NEO_JS_CFUNCTION(neo_js_object_to_local_string);
NEO_JS_CFUNCTION(neo_js_object_to_string);
NEO_JS_CFUNCTION(neo_js_object_value_of);
#ifdef __cplusplus
}
#endif
#endif